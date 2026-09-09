/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact division by a nonzero wall length computed at level load. */
#ifndef R_WALLDIV_H
#define R_WALLDIV_H

#include <stdint.h>

static inline uint64_t R_WallReciprocal(uint32_t divisor)
{
    if (divisor <= 1)
        return 0;
    return UINT64_MAX / divisor + ((divisor & (divisor - 1)) == 0);
}

static inline uint64_t R_MultiplyHigh64(uint64_t a, uint64_t b)
{
    uint32_t al = (uint32_t)a, ah = a >> 32;
    uint32_t bl = (uint32_t)b, bh = b >> 32;
    uint64_t low = (uint64_t)al * bl;
    uint64_t middle = (uint64_t)ah * bl + (low >> 32);
    uint32_t carry = middle >> 32;

    middle = (uint64_t)al * bh + (uint32_t)middle;
    return (uint64_t)ah * bh + carry + (middle >> 32);
}

static inline int64_t R_DivideWall(int64_t value, uint32_t divisor,
                                  uint64_t reciprocal)
{
    uint64_t magnitude, quotient;

    if (divisor == 1)
        return value;

    magnitude = value < 0 ? -(uint64_t)value : (uint64_t)value;
    quotient = R_MultiplyHigh64(magnitude, reciprocal);
    /* floor(2^64 / divisor) underestimates the quotient by at most one. */
    quotient += magnitude - quotient * divisor >= divisor;
    return value < 0 ? -(int64_t)quotient : (int64_t)quotient;
}

static inline int32_t R_HalfDifference(int32_t b, int32_t a)
{
    return (b >> 1) - (a >> 1) - ((a & ~b) & 1);
}

#endif
