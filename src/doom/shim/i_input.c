/* i_input.c — map openfpgaOS buttons to chocolate-doom events.
 *
 * Button mapping (gamepad-friendly):
 *   D-pad        -> arrow keys
 *   A            -> CTRL   (fire)
 *   B            -> SPACE  (use)
 *   X            -> SHIFT  (run)
 *   Y            -> ALT    (strafe)
 *   L1 / R1      -> weapon prev / next (',' / '.')
 *   START        -> ESC
 *   SELECT       -> TAB (map)
 */

#include "of.h"
#include "d_event.h"
#include "doomkeys.h"
#include "i_input.h"
#include "doomtype.h"

#include <string.h>

static uint32_t prev_buttons;

typedef struct { uint32_t mask; int key; } btn_map_t;
static const btn_map_t BTN_MAP[] = {
    { OF_BTN_UP,     KEY_UPARROW    },
    { OF_BTN_DOWN,   KEY_DOWNARROW  },
    { OF_BTN_LEFT,   KEY_LEFTARROW  },
    { OF_BTN_RIGHT,  KEY_RIGHTARROW },
    { OF_BTN_A,      KEY_RCTRL      },
    { OF_BTN_B,      ' '            },
    { OF_BTN_X,      KEY_RSHIFT     },
    { OF_BTN_Y,      KEY_RALT       },
    { OF_BTN_L1,     ','            },
    { OF_BTN_R1,     '.'            },
    { OF_BTN_START,  KEY_ESCAPE     },
    { OF_BTN_SELECT, KEY_TAB        },
    { OF_BTN_L2,     KEY_ENTER      },
    { OF_BTN_R2,     'y'            },
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

    for (unsigned i = 0; i < sizeof(BTN_MAP)/sizeof(BTN_MAP[0]); i++) {
        if (down & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 1);
        if (up   & BTN_MAP[i].mask) post_key(BTN_MAP[i].key, 0);
    }

    prev_buttons = curr;
}
