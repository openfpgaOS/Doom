/* i_input.c — map openfpgaOS buttons to chocolate-doom events.
 *
 * Button mapping:
 *   D-pad        -> arrow keys
 *   A            -> ENTER  (menu confirm / fire via key bind)
 *   B            -> hold run; tap/release within 500 ms for use/open
 *   X / Y        -> next / previous weapon
 *   L1 / R1      -> strafe left / right
 *   START        -> ESC
 *   SELECT       -> TAB (map)
 */

#include "of.h"
#include "d_event.h"
#include "doomkeys.h"
#include "i_input.h"
#include "doomtype.h"
#include "doomstat.h"

#include <string.h>

#define B_TAP_USE_MS       500
#define TAP_USE_HOLD_MS    80
#define KEY_TAP_USE        ' '
#define KEY_HOLD_RUN       KEY_RSHIFT
#define KEY_MENU_BACK      KEY_BACKSPACE

static uint32_t prev_buttons;
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

/* Poll the buttons and emit keydown/keyup edges. Called from the main
 * loop (invoked from i_video.c's I_StartTic). */
void I_PollInput(void)
{
    of_input_poll();

    of_input_state_t s;
    of_input_state(0, &s);
    uint32_t curr = s.buttons;
    uint32_t down  = curr & ~prev_buttons;
    uint32_t up    = ~curr &  prev_buttons;

    update_tap_use_release();
    update_b_button(curr);

    for (unsigned i = 0; i < sizeof(BTN_MAP)/sizeof(BTN_MAP[0]); i++) {
        if (down & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 1);
        if (up   & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 0);
    }

    prev_buttons = curr;
}
