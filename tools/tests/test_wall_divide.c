/* Check wall division and coordinate subtraction against wide arithmetic. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "r_walldiv.h"

static uint32_t seed = 1;
static uint32_t random_u32(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

static void check(int64_t n, uint32_t d)
{
    uint64_t reciprocal = R_WallReciprocal(d);
    assert(R_DivideWall(n, d, reciprocal) == n / (int64_t)d);
}

int main(void)
{
    static const int64_t nums[] = {
        INT64_MIN, INT64_MIN + 1, -INT64_C(0xffffffff), INT32_MIN,
        -65536, -2, -1, 0, 1, 2, 65536, INT32_MAX, UINT32_MAX,
        INT64_MAX - 1, INT64_MAX
    };
    static const int32_t coords[] = {
        INT32_MIN, INT32_MIN + 1, -65536, -3, -2, -1,
        0, 1, 2, 3, 65536, INT32_MAX - 1, INT32_MAX
    };
    for (uint64_t p = 1; p <= UINT32_MAX; p <<= 1)
    {
        for (int delta = -1; delta <= 1; ++delta)
        {
            uint64_t d = p + delta;
            if (!d || d > UINT32_MAX) continue;
            for (unsigned i = 0; i < sizeof(nums) / sizeof(nums[0]); ++i)
                check(nums[i], d);
            check((int64_t)d * 12345 - 1, d);
            check((int64_t)d * 12345, d);
            check((int64_t)d * 12345 + 1, d);
        }
    }
    for (unsigned i = 0; i < sizeof(coords) / sizeof(coords[0]); ++i)
        for (unsigned j = 0; j < sizeof(coords) / sizeof(coords[0]); ++j)
            assert(R_HalfDifference(coords[i], coords[j]) ==
                   ((int64_t)coords[i] - coords[j]) >> 1);

    for (unsigned i = 0; i < 2000000; ++i)
    {
        uint64_t a = ((uint64_t)random_u32() << 32) | random_u32();
        uint64_t b = ((uint64_t)random_u32() << 32) | random_u32();
        uint32_t d = random_u32();
        if (!d) d = 1;
        assert(R_MultiplyHigh64(a, b) == (uint64_t)(((__uint128_t)a * b) >> 64));
        check((int64_t)a, d);
        assert(R_HalfDifference((int32_t)a, (int32_t)b) ==
               ((int64_t)(int32_t)a - (int32_t)b) >> 1);
    }
    for (unsigned i = 0; i < sizeof(nums) / sizeof(nums[0]); ++i)
        check(nums[i], UINT32_MAX);
    puts("PASS: 2000000 exact divides/products/differences and boundary cases");
    return 0;
}
