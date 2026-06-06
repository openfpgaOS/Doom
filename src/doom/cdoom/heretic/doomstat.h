/* doomstat.h — openfpgaOS Heretic bridge (not upstream chocolate-doom).
 *
 * The shared FPGA input shim (shim/i_input.c) reads `menuactive` — a Doom
 * global — to decide its input mode. Heretic's menu exposes the same state
 * as `MenuActive` (heretic/doomdef.h), so map the Doom name onto Heretic's
 * symbol. Only the shared/shim layer includes this header when building the
 * Heretic core; Heretic's own code uses MenuActive directly. */
#ifndef OF_HERETIC_DOOMSTAT_BRIDGE_H
#define OF_HERETIC_DOOMSTAT_BRIDGE_H

#include "doomtype.h"

extern boolean MenuActive;
#define menuactive MenuActive

#endif /* OF_HERETIC_DOOMSTAT_BRIDGE_H */
