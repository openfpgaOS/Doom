/* of_hexen_compat.c — backing definitions for the shim globals the
 * openfpgaOS port added to Doom. Hexen reuses the Doom-targeted shim
 * (shim/i_video.c, shim/i_input.c), so it must define them. Not upstream. */

#include "m_menu.h"

/* VRR interpolation flag read by the shim. Hexen never interpolates. */
int frame_interpolation = 0;

/* Display refresh policy; toggled in Options. Defaults to VRR like Doom. */
int refresh_mode = REFRESH_MODE_VRR;

/* Pacing stubs the shim links against. Hexen uses the stock software
 * renderer, so report no over-budget time and ignore frame events. */
unsigned int R_Perf_PacingCurrentPrepareUS(void) { return 0; }
void R_Perf_PacingFrameStart(void)               { }
void R_Perf_PacingFrameCancel(void)              { }
void R_Perf_PacingAddWait(unsigned int wait_us)  { (void) wait_us; }
void R_Perf_PacingFrameQueued(void)              { }
