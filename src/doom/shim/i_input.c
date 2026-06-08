/* i_input.c — map openfpgaOS buttons to chocolate-doom events.
 *
 * Button mapping:
 *   D-pad        -> arrow keys
 *   A            -> ENTER  (menu confirm)
 *   B            -> hold run; tap/release within 500 ms for use/open
 *   X / Y        -> next / previous weapon
 *   L1 / R1      -> strafe left / right
 *   L2 / R2      -> use/open / fire
 *   START        -> ESC
 *   SELECT       -> TAB (map)
 *   Left stick   -> move forward/back + strafe left/right
 *   Right stick  -> analog turn
 *
 * Axis response follows the Quake port (in_of.c): movement axes get a
 * 50/50 linear+squared blend on the dock pad and stay linear (1:1) on
 * a SNAC PSX-Analog pad; turn additionally gets a squared curve to
 * keep the centre calm for fine aiming.  The dock turn gain is 0.6x
 * versus 1.0x for SNAC, putting docked full-tilt turn at ~148 deg/s —
 * between Quake's docked look (112) and Doom's keyboard run-turn
 * (246, which SNAC full tilt reaches). */

#include "of.h"
#include "d_event.h"
#include "doomkeys.h"
#include "i_input.h"
#include "i_joystick.h"
#include "m_controls.h"
#include "doomtype.h"
#include "doomstat.h"
#include "m_fixed.h"

#include <string.h>

#define B_TAP_USE_MS       500
#define TAP_USE_HOLD_MS    80
#define KEY_TAP_USE        ' '
#define KEY_POCKET_FIRE    KEY_RCTRL
#define KEY_HOLD_RUN       KEY_RSHIFT
#define KEY_MENU_BACK      KEY_BACKSPACE
#define STICK_DEADZONE     (16 * 256)
#define STICK_FULL_SCALE   32640
#define STICK_INACTIVE     ((int16_t) 0x8000)
#define DOCK_TURN_AXIS_GAIN_NUM 3
#define DOCK_TURN_AXIS_GAIN_DEN 5
#define SNAC_STRAFE_AXIS_GAIN_NUM 5
#define SNAC_STRAFE_AXIS_GAIN_DEN 4
#define STICK_MENU_THRESH  (FRACUNIT / 2)

static uint32_t prev_buttons;
static int pad_analog_seen;
static int b_mode;
static int b_tap_use_allowed;
static unsigned int b_press_ms;
static int tap_use_held;
static unsigned int tap_use_release_ms;

typedef struct { uint32_t mask; int key; } btn_map_t;
static const btn_map_t BTN_MAP[] = {
    { OF_BTN_UP,     KEY_UPARROW    },
    { OF_BTN_DOWN,   KEY_DOWNARROW  },
    { OF_BTN_LEFT,   KEY_LEFTARROW  },
    { OF_BTN_RIGHT,  KEY_RIGHTARROW },
    { OF_BTN_A,      KEY_ENTER      },
    { OF_BTN_X,      ']'            },
    { OF_BTN_Y,      '['            },
    { OF_BTN_L1,     ','            },
    { OF_BTN_R1,     '.'            },
    { OF_BTN_L2,     KEY_TAP_USE    },
    { OF_BTN_R2,     KEY_POCKET_FIRE },
    { OF_BTN_START,  KEY_ESCAPE     },
    { OF_BTN_SELECT, KEY_TAB        },
};

static void post_key(int key, int down)
{
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? ev_keydown : ev_keyup;
    ev.data1 = key;
    if (down && key >= 32 && key < 127) { ev.data2 = key; ev.data3 = key; }
    D_PostEvent(&ev);
}

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

static int normalize_axis(int16_t axis)
{
    int v = axis;
    int sign;
    int mag;
    int out;

    if (v > -STICK_DEADZONE && v < STICK_DEADZONE)
    {
        return 0;
    }

    sign = v < 0 ? -1 : 1;
    mag = abs_int(v);

    if (mag > STICK_FULL_SCALE)
    {
        mag = STICK_FULL_SCALE;
    }

    out = (int) (((int64_t) mag * FRACUNIT + STICK_FULL_SCALE / 2)
               / STICK_FULL_SCALE);

    return sign * out;
}

static int scale_axis(int value, int numerator, int denominator)
{
    int sign;
    int mag;
    int out;

    if (value == 0)
    {
        return 0;
    }

    sign = value < 0 ? -1 : 1;
    mag = abs_int(value);
    out = (mag * numerator + denominator / 2) / denominator;

    if (out > FRACUNIT)
    {
        out = FRACUNIT;
    }

    return sign * out;
}

static int axis_live(int16_t axis)
{
    return axis != 0 && axis != STICK_INACTIVE;
}

/* SNAC / digital-pad guard, same scheme as the Quake port: a digital
 * pad's undriven joy fields reach us pinned at 0x8000 every poll, so
 * axes are zeroed until one of them produces a value only a live,
 * centred-at-rest stick can produce.  Crucially, 0x8000 is ONLY used
 * for latching — once a real stick has been seen it must pass through
 * untouched, because the OS maps the APF's unsigned axis bytes as
 * (raw - 128) * 256, which makes full left/up deflection exactly
 * -32768 (0x8000).  Zeroing it per-poll cut every axis dead at full
 * tilt (e.g. the right stick could strafe right but never left). */
static void filter_inactive_analog_axes(of_input_state_t *s)
{
    if (pad_analog_seen)
    {
        return;
    }

    if (axis_live(s->joy_lx) || axis_live(s->joy_ly) ||
        axis_live(s->joy_rx) || axis_live(s->joy_ry))
    {
        pad_analog_seen = 1;
        return;
    }

    s->joy_lx = 0;
    s->joy_ly = 0;
    s->joy_rx = 0;
    s->joy_ry = 0;
}

static int snac_analog_p1(void)
{
    of_analogizer_state_t st;

    if (!of_analogizer_enabled())
    {
        return 0;
    }

    if (of_analogizer_state(&st) < 0 || !st.enabled)
    {
        return 0;
    }

    if (st.snac_type != 0x12 && st.snac_type != 0x13)
    {
        return 0;
    }

    if (st.snac_assignment == 0x40 || st.snac_assignment == 1)
    {
        return 0;
    }

    return 1;
}

/* Squared response for the turn axis keeps the centre calm for fine
 * aiming.  Applied to both pad types; the per-pad gain below sets the
 * rate. */
static int shape_turn_axis(int value)
{
    int sign;
    int mag;
    int curved;

    if (value == 0)
    {
        return 0;
    }

    sign = value < 0 ? -1 : 1;
    mag = abs_int(value);
    curved = (int)(((int64_t)mag * mag) / FRACUNIT);

    if (curved > FRACUNIT)
    {
        curved = FRACUNIT;
    }

    return sign * curved;
}

/* Softened response for the dock pad's movement axes: a 50/50 blend
 * of linear and squared, out = v*(FRACUNIT+|v|)/(2*FRACUNIT) — the
 * same curve as the Quake port's axis_curved().  Pure squared felt
 * sluggish mid-range; this keeps walking speeds reasonable (half
 * tilt -> ~37%) while still calming the centre, and full deflection
 * reaches full speed.  SNAC DualShock sticks stay linear (1:1). */
static int axis_move_curve(int value)
{
    int sign;
    int mag;
    int out;

    if (value == 0)
    {
        return 0;
    }

    sign = value < 0 ? -1 : 1;
    mag = abs_int(value);
    out = (int)(((int64_t)mag * (FRACUNIT + mag)) / (2 * FRACUNIT));

    if (out > FRACUNIT)
    {
        out = FRACUNIT;
    }

    return sign * out;
}

static uint32_t trigger_button_mask(const of_input_state_t *s)
{
    uint32_t mask = 0;

    if (s->trigger_l != 0)
    {
        mask |= OF_BTN_L2;
    }

    if (s->trigger_r != 0)
    {
        mask |= OF_BTN_R2;
    }

    return mask;
}

static uint32_t joystick_button_mask(int button, int pressed)
{
    if (!pressed || button < 0 || button >= MAX_VIRTUAL_BUTTONS)
    {
        return 0;
    }

    return 1u << button;
}

static unsigned stick_dir(int x, int y)
{
    unsigned dir = JOY_DIR_NONE;

    if (y <= -STICK_MENU_THRESH)
    {
        dir |= JOY_DIR_UP;
    }
    else if (y >= STICK_MENU_THRESH)
    {
        dir |= JOY_DIR_DOWN;
    }

    if (x <= -STICK_MENU_THRESH)
    {
        dir |= JOY_DIR_LEFT;
    }
    else if (x >= STICK_MENU_THRESH)
    {
        dir |= JOY_DIR_RIGHT;
    }

    return dir;
}

static void post_joystick_axes(const of_input_state_t *s, uint32_t buttons)
{
    event_t ev;
    int lx = normalize_axis(s->joy_lx);
    int ly = normalize_axis(s->joy_ly);
    int rx = normalize_axis(s->joy_rx);
    int ry = normalize_axis(s->joy_ry);
    int snac = snac_analog_p1();
    int forward = ly;
    int strafe = lx;
    int turn;

    /* Movement axes: soften the dock pad's response; a SNAC
     * DualShock's pots already feel right at 1:1, except that they
     * rarely reach the byte rails, so strafe gets a 5/4 boost letting
     * ~80% deflection hit full speed (forward is fast enough that the
     * shortfall isn't felt there). */
    if (!snac)
    {
        forward = axis_move_curve(forward);
        strafe = axis_move_curve(strafe);
    }
    else
    {
        strafe = scale_axis(strafe, SNAC_STRAFE_AXIS_GAIN_NUM,
                            SNAC_STRAFE_AXIS_GAIN_DEN);
    }

    /* Turn: squared curve, then the dock pad runs at 0.6x, a SNAC
     * PSX-Analog pad at the full rate. */
    turn = scale_axis(shape_turn_axis(rx),
                      snac ? 1 : DOCK_TURN_AXIS_GAIN_NUM,
                      snac ? 1 : DOCK_TURN_AXIS_GAIN_DEN);

    memset(&ev, 0, sizeof(ev));
    ev.type = ev_joystick;
#if defined(OF_HERETIC) || defined(OF_HEXEN)
    /* Heretic/Hexen: A = fire/confirm; B = back, but only while a menu is up. */
    ev.data1 = joystick_button_mask(joybfire, (buttons & OF_BTN_A) != 0)
             | joystick_button_mask(joybuse,
                                     (buttons & OF_BTN_B) != 0 && menuactive);
#else
    ev.data1 = joystick_button_mask(joybuse, (buttons & OF_BTN_L2) != 0)
             | joystick_button_mask(joybfire, (buttons & OF_BTN_R2) != 0);
#endif
    ev.data2 = turn;
    ev.data3 = forward;
    ev.data4 = strafe;
    ev.data5 = 0;
    ev.data6 = stick_dir(lx, ly) << LSTICK_SHIFT
             | stick_dir(rx, ry) << RSTICK_SHIFT;
    D_PostEvent(&ev);
}

static void update_tap_use_release(void)
{
    if (tap_use_held && (int)(of_time_ms() - tap_use_release_ms) >= 0)
    {
        post_key(KEY_TAP_USE, 0);
        tap_use_held = 0;
    }
}

static void start_tap_use(void)
{
    if (!tap_use_held)
    {
        post_key(KEY_TAP_USE, 1);
        tap_use_held = 1;
    }

    tap_use_release_ms = of_time_ms() + TAP_USE_HOLD_MS;
}

static void cancel_tap_use(void)
{
    if (tap_use_held)
    {
        post_key(KEY_TAP_USE, 0);
        tap_use_held = 0;
    }
}

static void stop_b_mode(int mode, int allow_tap_use)
{
    unsigned int held_ms;

    switch (mode)
    {
        case 1:
            post_key(KEY_HOLD_RUN, 0);
            held_ms = of_time_ms() - b_press_ms;
            if (allow_tap_use && b_tap_use_allowed && held_ms < B_TAP_USE_MS)
            {
                start_tap_use();
            }
            break;

        case 2:
            post_key(KEY_MENU_BACK, 0);
            break;

        default:
            break;
    }
}

static void start_b_mode(int mode)
{
    switch (mode)
    {
        case 1:
            cancel_tap_use();
            b_press_ms = of_time_ms();
            post_key(KEY_HOLD_RUN, 1);
            break;

        case 2:
            cancel_tap_use();
            post_key(KEY_MENU_BACK, 1);
            break;

        default:
            break;
    }
}

static void update_b_button(uint32_t buttons)
{
    int desired_mode = 0;
    int old_mode;

    if (buttons & OF_BTN_B)
    {
        desired_mode = menuactive ? 2 : 1;
    }

    if (desired_mode == b_mode)
    {
        return;
    }

    old_mode = b_mode;
    stop_b_mode(b_mode, desired_mode == 0);
    b_mode = desired_mode;
    b_tap_use_allowed = desired_mode == 1 && old_mode == 0;
    start_b_mode(b_mode);
}

/* Called by the core (i_input.h declares these but has no impl for us). */
void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    (void)x1; (void)y1; (void)x2; (void)y2;
}

void I_StopTextInput(void) { }

void I_BindInputVariables(void) { }

float mouse_acceleration = 2.0f;
int   mouse_threshold    = 10;

void I_ReadMouse(void) { /* openfpgaOS: no mouse */ }

#ifdef OF_HERETIC

/* Heretic Pocket buttons: A fire, B tap use, B hold = modifier (D-pad fly +
 * inventory, Y prev weapon), X use item, Y next weapon, L/R strafe, START menu,
 * SELECT map. A drives fire/confirm via the joystick event (see axes above). */

/* Hold B this long before it acts as the modifier, so a quick tap stays Use. */
#define HER_MOD_HOLD_MS 150

/* Key each held D-pad / Y is emitting, so toggling the modifier mid-hold can
 * swap it (release old, press new). */
static int her_key_up, her_key_down, her_key_left, her_key_right, her_key_y;
static int her_b_down;
static int her_b_chord;
static unsigned int her_b_press_ms;

static void her_emit(int *slot, int key)   /* key 0 = released */
{
    if (key == *slot)
        return;
    if (*slot)
        post_key(*slot, 0);
    if (key)
        post_key(key, 1);
    *slot = key;
}

void I_PollInput(void)
{
    of_input_poll();

    of_input_state_t s;
    of_input_state(0, &s);
    filter_inactive_analog_axes(&s);

    uint32_t curr = s.buttons | trigger_button_mask(&s);
    uint32_t down = curr & ~prev_buttons;
    uint32_t up   = ~curr &  prev_buttons;

    update_tap_use_release();

    int b = (curr & OF_BTN_B) != 0;
    if (b && !her_b_down)
    {
        her_b_down = 1;
        her_b_chord = 0;
        her_b_press_ms = of_time_ms();
    }

    /* Modifier engages only after a short hold, and never while a menu is up. */
    int mod = b && !menuactive
           && (unsigned int)(of_time_ms() - her_b_press_ms) >= HER_MOD_HOLD_MS;

    her_emit(&her_key_up,    (curr & OF_BTN_UP)    ? (mod ? key_flyup     : KEY_UPARROW)    : 0);
    her_emit(&her_key_down,  (curr & OF_BTN_DOWN)  ? (mod ? key_flydown   : KEY_DOWNARROW)  : 0);
    her_emit(&her_key_left,  (curr & OF_BTN_LEFT)  ? (mod ? key_invleft   : KEY_LEFTARROW)  : 0);
    her_emit(&her_key_right, (curr & OF_BTN_RIGHT) ? (mod ? key_invright  : KEY_RIGHTARROW) : 0);
    her_emit(&her_key_y,     (curr & OF_BTN_Y)     ? (mod ? key_prevweapon : key_nextweapon) : 0);

    if (mod && (curr & (OF_BTN_UP | OF_BTN_DOWN | OF_BTN_LEFT | OF_BTN_RIGHT | OF_BTN_Y)))
        her_b_chord = 1;

    if (down & OF_BTN_X)      post_key(key_useartifact, 1);
    if (up   & OF_BTN_X)      post_key(key_useartifact, 0);
    if (down & OF_BTN_L1)     post_key(key_strafeleft, 1);
    if (up   & OF_BTN_L1)     post_key(key_strafeleft, 0);
    if (down & OF_BTN_R1)     post_key(key_straferight, 1);
    if (up   & OF_BTN_R1)     post_key(key_straferight, 0);
    if (down & OF_BTN_START)  post_key(KEY_ESCAPE, 1);
    if (up   & OF_BTN_START)  post_key(KEY_ESCAPE, 0);
    if (down & OF_BTN_SELECT) post_key(KEY_TAB, 1);
    if (up   & OF_BTN_SELECT) post_key(KEY_TAB, 0);

    /* B released as a quick tap that never engaged the modifier -> Use/Open. */
    if (!b && her_b_down)
    {
        her_b_down = 0;
        if (!her_b_chord
         && (unsigned int)(of_time_ms() - her_b_press_ms) < B_TAP_USE_MS)
        {
            start_tap_use();   /* posts key_use (space), auto-released */
        }
    }

    post_joystick_axes(&s, curr);

    prev_buttons = curr;
}

#elif defined(OF_HEXEN)

/* Hexen Pocket buttons: A fire, B tap use, B hold = modifier, X use item,
 * Y next weapon (B+Y prev weapon), L/R jump (B+L / B+R strafe), B+D-pad
 * up/down fly, B+D-pad left/right cycle inventory, START menu, SELECT map.
 * A drives fire/confirm via the joystick event (see axes above). */

/* Hold B this long before it acts as the modifier, so a quick tap stays Use. */
#define HEX_MOD_HOLD_MS 100

/* Key each held D-pad / Y / L / R is emitting, so toggling the modifier
 * mid-hold can swap it (release old, press new). */
static int hex_key_up, hex_key_down, hex_key_left, hex_key_right;
static int hex_key_y, hex_key_l, hex_key_r;
static int hex_b_down;
static int hex_b_chord;
static unsigned int hex_b_press_ms;

static void hex_emit(int *slot, int key)   /* key 0 = released */
{
    if (key == *slot)
        return;
    if (*slot)
        post_key(*slot, 0);
    if (key)
        post_key(key, 1);
    *slot = key;
}

void I_PollInput(void)
{
    of_input_poll();

    of_input_state_t s;
    of_input_state(0, &s);
    filter_inactive_analog_axes(&s);

    uint32_t curr = s.buttons | trigger_button_mask(&s);
    uint32_t down = curr & ~prev_buttons;
    uint32_t up   = ~curr &  prev_buttons;

    update_tap_use_release();

    int b = (curr & OF_BTN_B) != 0;
    if (b && !hex_b_down)
    {
        hex_b_down = 1;
        hex_b_chord = 0;
        hex_b_press_ms = of_time_ms();
    }

    /* Modifier engages only after a short hold, and never while a menu is up. */
    int mod = b && !menuactive
           && (unsigned int)(of_time_ms() - hex_b_press_ms) >= HEX_MOD_HOLD_MS;

    hex_emit(&hex_key_up,    (curr & OF_BTN_UP)    ? (mod ? key_flyup      : KEY_UPARROW)    : 0);
    hex_emit(&hex_key_down,  (curr & OF_BTN_DOWN)  ? (mod ? key_flydown    : KEY_DOWNARROW)  : 0);
    hex_emit(&hex_key_left,  (curr & OF_BTN_LEFT)  ? (mod ? key_strafeleft  : KEY_LEFTARROW)  : 0);
    hex_emit(&hex_key_right, (curr & OF_BTN_RIGHT) ? (mod ? key_straferight : KEY_RIGHTARROW) : 0);
    hex_emit(&hex_key_y,     (curr & OF_BTN_Y)     ? (mod ? key_prevweapon  : key_nextweapon) : 0);
    hex_emit(&hex_key_l,     (curr & OF_BTN_L1)    ? (mod ? key_invleft     : key_jump)       : 0);
    hex_emit(&hex_key_r,     (curr & OF_BTN_R1)    ? (mod ? key_invright    : key_jump)       : 0);

    if (mod && (curr & (OF_BTN_UP | OF_BTN_DOWN | OF_BTN_LEFT | OF_BTN_RIGHT
                      | OF_BTN_Y | OF_BTN_L1 | OF_BTN_R1)))
        hex_b_chord = 1;

    if (down & OF_BTN_X)      post_key(key_useartifact, 1);
    if (up   & OF_BTN_X)      post_key(key_useartifact, 0);
    if (down & OF_BTN_START)  post_key(KEY_ESCAPE, 1);
    if (up   & OF_BTN_START)  post_key(KEY_ESCAPE, 0);
    if (down & OF_BTN_SELECT) post_key(KEY_TAB, 1);
    if (up   & OF_BTN_SELECT) post_key(KEY_TAB, 0);

    /* B released as a quick tap that never engaged the modifier -> Use/Open. */
    if (!b && hex_b_down)
    {
        hex_b_down = 0;
        if (!hex_b_chord
         && (unsigned int)(of_time_ms() - hex_b_press_ms) < B_TAP_USE_MS)
        {
            start_tap_use();   /* posts key_use, auto-released */
        }
    }

    post_joystick_axes(&s, curr);

    prev_buttons = curr;
}

#else

/* Poll the buttons and emit keydown/keyup edges. Called from the main
 * loop (invoked from i_video.c's I_StartTic). */
void I_PollInput(void)
{
    of_input_poll();

    of_input_state_t s;
    of_input_state(0, &s);
    filter_inactive_analog_axes(&s);

    uint32_t curr = s.buttons | trigger_button_mask(&s);
    uint32_t down  = curr & ~prev_buttons;
    uint32_t up    = ~curr &  prev_buttons;

    update_tap_use_release();
    update_b_button(curr);

    for (unsigned i = 0; i < sizeof(BTN_MAP)/sizeof(BTN_MAP[0]); i++) {
        if (down & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 1);
        if (up   & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 0);
    }

    post_joystick_axes(&s, curr);

    prev_buttons = curr;
}

#endif
