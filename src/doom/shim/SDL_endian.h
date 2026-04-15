/* SDL_endian.h shim — RISC-V target is little-endian.
 *
 * Only the macros actually referenced by chocolate-doom are defined
 * (SDL_SwapLE16/32, SDL_SwapBE16/32, SDL_BYTEORDER, SDL_LIL_ENDIAN,
 * SDL_BIG_ENDIAN). When building the desktop app_pc target, the real
 * SDL2 headers are on the include path and this file is ignored.
 */

#ifndef SDL_endian_h_
#define SDL_endian_h_

#include <stdint.h>

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN

static inline uint16_t SDL_Swap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t SDL_Swap32(uint32_t x) { return __builtin_bswap32(x); }

#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)

#endif
