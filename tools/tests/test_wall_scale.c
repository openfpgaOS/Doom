/* Compare production wall scale division with independent wide division. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#ifndef DOOM_SCALE_SOURCE
#define DOOM_SCALE_SOURCE "../../src/doom/cdoom/doom/r_main.c"
#endif
#include DOOM_SCALE_SOURCE

static uint32_t state = 1;
static uint64_t cases;

static uint32_t random32(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void check(uint32_t num, uint32_t den)
{
    uint64_t reference = ((uint64_t)num << 16) / den;
    if (reference > 64u * 65536u) reference = 64u * 65536u;
    uint32_t actual = R_PositiveScaleDiv(num, den);
    if (actual != reference) {
        fprintf(stderr, "num=%" PRIu32 " den=%" PRIu32 " actual=%" PRIu32
                " expected=%" PRIu64 "\n", num, den, actual, reference);
        assert(0);
    }
    ++cases;
}

static void neighbors(uint64_t num, uint32_t den)
{
    for (int delta = -2; delta <= 2; ++delta) {
        uint64_t candidate = num + delta;
        if (candidate <= UINT32_MAX) check(candidate, den);
    }
}

int main(void)
{
    for (unsigned den = 1; den < 512; ++den)
        for (unsigned num = 0; num < 512; ++num)
            check(num, den);

    for (unsigned i = 0; i < 10000000; ++i) {
        uint32_t den = random32() & INT32_MAX;
        if (!den) den = 1;
        check(random32(), den);
        check(random32() % (160 * 65536), den);
        unsigned shift = random32() % 31;
        den = (random32() & INT32_MAX) >> shift;
        if (!den) den = 1;
        check(random32(), den);
        neighbors((uint64_t)den * 64, den);
    }

    /* Seed-bin boundaries at every normalization, and quotient boundaries
     * around the renderer's minimum scale, integer scales and saturation. */
    static const uint32_t quotients[] = {
        0, 1, 255, 256, 257, 65535, 65536, 65537, 4194303, 4194304
    };
    for (unsigned shift = 0; shift < 32; ++shift)
        for (uint64_t bin = 32; bin <= 64; ++bin)
            for (int delta = -2; delta <= 2; ++delta) {
                uint64_t den = ((bin << 26) >> shift) + delta;
                if (!den || den > INT32_MAX) continue;
                check(0, den);
                check(1, den);
                check(UINT32_MAX, den);
                for (unsigned q = 0; q < sizeof(quotients) / sizeof(quotients[0]); ++q)
                    neighbors(((uint64_t)quotients[q] * den) >> 16, den);
                for (unsigned whole = 0; whole <= 64; ++whole)
                    neighbors((uint64_t)whole * den, den);
            }
    printf("PASS: %" PRIu64 " exact scale comparisons\n", cases);
    return 0;
}
