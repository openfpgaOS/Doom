/* m_menu.h — openfpgaOS Heretic bridge (not upstream chocolate-doom).
 * Backs the display refresh-mode API the shim (shim/i_video.c) queries;
 * also included by mn_menu.c, d_main.c and of_heretic_compat.c for the
 * shared refresh_mode. */
#ifndef OF_HERETIC_M_MENU_BRIDGE_H
#define OF_HERETIC_M_MENU_BRIDGE_H

#include "doomtype.h"

/* VRR interpolation flag; defined in of_heretic_compat.c, always 0. */
extern int frame_interpolation;

/* Display refresh policy; defined in of_heretic_compat.c, defaults to VRR. */
extern int refresh_mode;

/* Mirror the Doom module's REFRESH_MODE_* values (cdoom/doom/m_menu.h). */
enum
{
    REFRESH_MODE_PAL   = 1,
    REFRESH_MODE_FIXED = 2,
    REFRESH_MODE_NTSC  = 4,
    REFRESH_MODE_VRR   = 6
};

static inline int M_EffectiveRefreshMode(void)
{
    return refresh_mode == REFRESH_MODE_VRR ? REFRESH_MODE_VRR
                                            : REFRESH_MODE_NTSC;
}

static inline boolean M_RefreshModeUsesInterpolation(int mode)
{
    (void) mode;
    return false;
}

#endif /* OF_HERETIC_M_MENU_BRIDGE_H */
