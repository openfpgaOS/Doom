/* m_menu.h — openfpgaOS Heretic bridge (not upstream chocolate-doom).
 * Backs the display refresh-mode API the shim (shim/i_video.c) queries;
 * also included by mn_menu.c, d_main.c and of_heretic_compat.c for the
 * shared refresh_mode.
 *
 * Mirrors the Doom module's semantics (cdoom/doom/m_menu.c): VRR and
 * FIXED are display-paced interpolating modes; analog video out forces
 * PAL/NTSC (fixed broadcast cadence, no interpolation).  The old bridge
 * hard-coded "never interpolates" from the software-renderer era — that
 * silently disabled the uncapped pipeline for this game. */
#ifndef OF_HERETIC_M_MENU_BRIDGE_H
#define OF_HERETIC_M_MENU_BRIDGE_H

#include "doomtype.h"
#ifndef OF_PC
#include "of_analogizer.h"
#endif

/* VRR interpolation flag the shim updates each frame; defined in
 * of_heretic_compat.c. */
extern int frame_interpolation;

/* Display refresh policy; defined in of_heretic_compat.c, defaults to
 * VRR.  Toggled in the Options menu. */
extern int refresh_mode;

/* Mirror the Doom module's REFRESH_MODE_* values (cdoom/doom/m_menu.h). */
enum
{
    REFRESH_MODE_PAL   = 1,
    REFRESH_MODE_FIXED = 2,
    REFRESH_MODE_NTSC  = 4,
    REFRESH_MODE_VRR   = 6
};

/* Pocket button layout; defined in of_heretic_compat.c, defaults to DEFAULT.
 * Read by the shim (shim/i_input.c), toggled in the Options menu.  DEFAULT is
 * the unified Doom/Quake/Duke3D map; DISCO is the B-modifier map. */
extern int control_scheme;

enum
{
    CONTROL_SCHEME_DEFAULT = 0,
    CONTROL_SCHEME_DISCO   = 1
};

#define HERETIC_ANLG_VIDEO_YC_PAL      0x4u
#define HERETIC_ANLG_VIDEO_POCKET_OFF  0x8u

static inline int M_NormalizeRefreshMode(int mode)
{
    return mode == REFRESH_MODE_VRR ? REFRESH_MODE_VRR : REFRESH_MODE_FIXED;
}

static inline int M_AnalogizerRefreshMode(void)
{
#ifndef OF_PC
    of_analogizer_state_t state;

    if (of_analogizer_state(&state) < 0 || !state.enabled)
        return -1;

    if ((state.video_mode & ~HERETIC_ANLG_VIDEO_POCKET_OFF)
        == HERETIC_ANLG_VIDEO_YC_PAL)
    {
        return REFRESH_MODE_PAL;
    }

    return REFRESH_MODE_NTSC;
#else
    return -1;
#endif
}

static inline int M_EffectiveRefreshMode(void)
{
    int analogizer_mode = M_AnalogizerRefreshMode();

    if (analogizer_mode >= 0)
        return analogizer_mode;

    return M_NormalizeRefreshMode(refresh_mode);
}

static inline boolean M_RefreshModeUsesInterpolation(int mode)
{
    switch (mode)
    {
        case REFRESH_MODE_FIXED:
        case REFRESH_MODE_VRR:
            return true;
        case REFRESH_MODE_PAL:
        case REFRESH_MODE_NTSC:
            return false;
        default:
            return M_NormalizeRefreshMode(mode) == REFRESH_MODE_FIXED
                || M_NormalizeRefreshMode(mode) == REFRESH_MODE_VRR;
    }
}

#endif /* OF_HERETIC_M_MENU_BRIDGE_H */
