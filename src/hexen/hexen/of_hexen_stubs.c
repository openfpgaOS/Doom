/* of_hexen_stubs.c — link stubs for Hexen subsystems the openfpgaOS port drops
 * (not upstream chocolate-doom). The shim's i_net_stub.c already stubs most of
 * i_cdmus / i_videohr but against an older API, so a few entry points are
 * missing; Hexen also ships no dehacked, so the shared deh_main.c needs empty
 * section/signature tables. CD audio and the hi-res startup screen are inert at
 * runtime (I_CDMusInit fails, I_SetVideoModeHR returns false), so these no-op. */

#include <stddef.h>

#include "doomtype.h"
#include "deh_defs.h"

/* No dehacked support in Hexen — empty NULL-terminated tables. */
deh_section_t *deh_section_types[] = { NULL };
const char *deh_signatures[] = { NULL };

/* CD audio entry points missing from i_net_stub.c's older-API set. */
void I_CDMusPrintStartup(void)     { }
int  I_CDMusFirstTrack(void)       { return -1; }
int  I_CDMusLastTrack(void)        { return -1; }
int  I_CDMusTrackLength(int track) { (void) track; return -1; }

/* HR palette helpers missing from i_net_stub.c's i_videohr stubs. */
void I_FadeToPaletteHR(const byte *palette) { (void) palette; }
void I_BlackPaletteHR(void)                 { }
