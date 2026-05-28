//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Endianess handling, swapping 16bit and 32bit.
//


#ifndef __I_SWAP__
#define __I_SWAP__

#include <stdint.h>
#include "SDL_endian.h"

#ifndef SDL_Swap16
static inline uint16_t I_Swap16Fallback(uint16_t x)
{
    return __builtin_bswap16(x);
}
#define SDL_Swap16(x) I_Swap16Fallback(x)
#endif

#ifndef SDL_Swap32
static inline uint32_t I_Swap32Fallback(uint32_t x)
{
    return __builtin_bswap32(x);
}
#define SDL_Swap32(x) I_Swap32Fallback(x)
#endif

#ifndef SDL_SwapLE16
#define SDL_SwapLE16(x) ((uint16_t)(x))
#endif

#ifndef SDL_SwapLE32
#define SDL_SwapLE32(x) ((uint32_t)(x))
#endif

#ifndef SDL_SwapBE16
#define SDL_SwapBE16(x) SDL_Swap16(x)
#endif

#ifndef SDL_SwapBE32
#define SDL_SwapBE32(x) SDL_Swap32(x)
#endif

// Endianess handling.
// WAD files are stored little endian.

// Just use SDL's endianness swapping functions.

// These are deliberately cast to signed values; this is the behaviour
// of the macros in the original source and some code relies on it.

#define SHORT(x)  ((signed short) SDL_SwapLE16(x))
#define LONG(x)   ((signed int) SDL_SwapLE32(x))

// Defines for checking the endianness of the system.

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SYS_BIG_ENDIAN
#endif

#endif
