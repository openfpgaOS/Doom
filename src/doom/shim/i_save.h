#ifndef I_SAVE_H
#define I_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "doomtype.h"

void I_SetOpenFPGASaveIdentity(const char *iwad, const char *pwad);
uint32_t I_OpenFPGASaveGameID(void);
boolean I_OpenFPGASaveRead(const char *name, byte *buffer,
                           size_t capacity, size_t *length);
boolean I_OpenFPGASaveReadHeader(const char *name, byte *buffer,
                                 size_t length);
boolean I_OpenFPGASaveWrite(const char *name, const byte *buffer,
                            size_t length);
void I_MigratePocketDoomSaves(void);

#endif
