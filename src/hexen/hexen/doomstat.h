/* doomstat.h — openfpgaOS Hexen bridge (not upstream chocolate-doom).
 *
 * The shared FPGA input shim (shim/i_input.c) reads `menuactive` — a Doom
 * global — to decide its input mode. Hexen's menu exposes the same state
 * as `MenuActive` (hexen/mn_menu.c), so map the Doom name onto Hexen's
 * symbol. Only the shared/shim layer includes this header when building the
 * Hexen core; Hexen's own code uses MenuActive directly. */
#ifndef OF_HEXEN_DOOMSTAT_BRIDGE_H
#define OF_HEXEN_DOOMSTAT_BRIDGE_H

#include "doomtype.h"

extern boolean MenuActive;
#define menuactive MenuActive

#endif /* OF_HEXEN_DOOMSTAT_BRIDGE_H */
