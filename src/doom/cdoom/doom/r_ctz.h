/* r_ctz.h — inline count of trailing zero bits (not upstream) */
#ifndef R_CTZ_H
#define R_CTZ_H

#include <stdint.h>

/* Index of the lowest set bit of a nonzero word.  RV32IM has no such
 * instruction, so __builtin_ctz is a libgcc call; this is a de Bruijn
 * multiply and one table load. */
static inline __attribute__((always_inline)) unsigned int R_Ctz32(uint32_t v)
{
    static const uint8_t debruijn[32] = {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9,
    };

    return debruijn[((v & -v) * 0x077CB531u) >> 27];
}

#endif
