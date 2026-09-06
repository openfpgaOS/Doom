/* Compare the real wall setup against the original integer equations. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#ifndef DOOM_WALL_SOURCE
#define DOOM_WALL_SOURCE "../../src/doom/cdoom/doom/r_segs.c"
#endif
#include DOOM_WALL_SOURCE

fixed_t viewx, viewy;
static uint32_t seed = 1;
static uint32_t random_u32(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
static int32_t coord(void) { return (int32_t)(random_u32() % 2000000001u) - 1000000000; }

int main(void)
{
    vertex_t a = {0}, b = {0};
    rendersegcache_t cache = {0};
    cache.v1 = &a; cache.v2 = &b;
    for (unsigned i = 0; i < 1000000; ++i) {
        a.x = coord(); a.y = coord(); b.x = coord(); b.y = coord();
        viewx = coord(); viewy = coord();
        if (i % 3 == 0) b.x = a.x;
        if (i % 3 == 1) b.y = a.y;
        int64_t dx = ((int64_t)b.x - a.x) >> 1;
        int64_t dy = ((int64_t)b.y - a.y) >> 1;
        int64_t dx1 = ((int64_t)viewx - a.x) >> 1;
        int64_t dy1 = ((int64_t)viewy - a.y) >> 1;
        uint32_t len = dx == 0 ? (uint32_t)llabs(dy)
                     : dy == 0 ? (uint32_t)llabs(dx) : random_u32();
        if ((i & 7) == 0) ++len; /* Near-axis lengths must use the exact fallback. */
        if (!len) len = 1;
        cache.length_half = len;
        int64_t d = ((dy * dx1 - dx * dy1) / len) * 2;
        int64_t o = ((dx * dx1 + dy * dy1) / len) * 2;
        fixed_t actual_d, actual_o;
        R_ExactDistOffset(&cache, &actual_d, &actual_o);
        assert(actual_d == (d < 0 ? 0 : d > INT32_MAX ? INT32_MAX : (fixed_t)d));
        assert(actual_o == (fixed_t)o);
    }
    puts("PASS: 1000000 wall distance/offset cases, including axis directions and fallback");
    return 0;
}
