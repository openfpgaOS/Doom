/* p_saveg.h — openfpgaOS Heretic bridge (not upstream chocolate-doom).
 * The shim includes "p_saveg.h" for the Pocket-save entry points
 * P_SetOpenFPGASavePrefix and P_SaveGameFile; Heretic defines them in
 * p_saveg.c. */
#ifndef OF_HERETIC_P_SAVEG_BRIDGE_H
#define OF_HERETIC_P_SAVEG_BRIDGE_H

/* The shim reads SAVESTRINGSIZE from here; Heretic's real value lives in
 * doomdef.h, mirrored so the shim needn't include it. */
#ifndef SAVESTRINGSIZE
#define SAVESTRINGSIZE 24
#endif

void P_SetOpenFPGASavePrefix(const char *prefix);
char *P_SaveGameFile(int slot);

#endif /* OF_HERETIC_P_SAVEG_BRIDGE_H */
