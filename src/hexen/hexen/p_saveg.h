/* p_saveg.h — openfpgaOS Hexen save bridge (not upstream chocolate-doom).
 * Declares the shim's Pocket-save entry points (P_SetOpenFPGASavePrefix,
 * P_SaveGameFile) and the OFX_* in-RAM-VFS + LZSS slot layer in of_hexen_save.c
 * that sv_save.c and mn_menu.c use under #ifndef OF_PC. */
#ifndef OF_HEXEN_P_SAVEG_BRIDGE_H
#define OF_HEXEN_P_SAVEG_BRIDGE_H

#include <stddef.h>

#include "doomtype.h"

/* The shim reads SAVESTRINGSIZE; Hexen's real value is HXS_DESCRIPTION_LENGTH
 * (24) in h2def.h, mirrored so the shim needn't include it. */
#ifndef SAVESTRINGSIZE
#define SAVESTRINGSIZE 24
#endif

/* Shim entry points. */
void P_SetOpenFPGASavePrefix(const char *prefix);
char *P_SaveGameFile(int slot);

/* In-RAM VFS + NVRAM slot persistence (of_hexen_save.c), used by sv_save.c
 * and mn_menu.c under #ifndef OF_PC. */
void *OFX_OpenWrite(const char *name);   /* returns FILE* over a RAM buffer  */
void *OFX_OpenRead(const char *name);    /* returns FILE* or NULL            */
void OFX_BeforeClose(void *fp);          /* commit a pending write to the VFS */
boolean OFX_Exists(const char *name);
void OFX_Remove(const char *name);
void OFX_Copy(const char *src, const char *dst);
void OFX_FlushSlot(int slot);            /* VFS hex<slot>* -> NVRAM .sav      */
void OFX_EnsureSlot(int slot);           /* NVRAM .sav -> VFS hex<slot>*      */
boolean OFX_SlotDescription(int slot, char *desc); /* desc[SAVESTRINGSIZE]    */

/* LZSS codec (exposed for a desktop round-trip/ratio unit test). Returns the
 * compressed/decompressed byte count, or -1 on overflow of the destination. */
int OFX_LzssCompress(const unsigned char *src, int srclen,
                     unsigned char *dst, int dstcap);
int OFX_LzssDecompress(const unsigned char *src, int srclen,
                       unsigned char *dst, int dstcap);

#endif /* OF_HEXEN_P_SAVEG_BRIDGE_H */
