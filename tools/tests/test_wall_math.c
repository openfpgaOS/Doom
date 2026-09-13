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
#ifdef TEST_WALL_VIEW_CACHE
int bsp_view_validcount = 1;
#endif
static uint32_t seed = 1;
static uint32_t random_u32(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
static int32_t coord(void) { return (int32_t)(random_u32() % 2000000001u) - 1000000000; }

static void check(rendersegcache_t *cache)
{
    __int128 dx = ((int64_t)cache->v2->x - cache->v1->x) >> 1;
    __int128 dy = ((int64_t)cache->v2->y - cache->v1->y) >> 1;
    __int128 dx1 = ((int64_t)viewx - cache->v1->x) >> 1;
    __int128 dy1 = ((int64_t)viewy - cache->v1->y) >> 1;
    __int128 d = ((dy * dx1 - dx * dy1) / cache->length_half) * 2;
    __int128 o = ((dx * dx1 + dy * dy1) / cache->length_half) * 2;
    fixed_t actual_d, actual_o;
    cache->length_reciprocal = R_WallReciprocal(cache->length_half);
    R_ExactDistOffset(cache, &actual_d, &actual_o);
    assert(actual_d == (d < 0 ? 0 : d > INT32_MAX ? INT32_MAX : (fixed_t)d));
    assert(actual_o == (fixed_t)o);
#ifdef TEST_WALL_VIEW_CACHE
    ++bsp_view_validcount;
    R_CachedDistOffset(cache, &actual_d, &actual_o);
    assert(actual_d == (d < 0 ? 0 : d > INT32_MAX ? INT32_MAX : (fixed_t)d));
    assert(actual_o == (fixed_t)o);
    R_CachedDistOffset(cache, &actual_d, &actual_o);
    assert(actual_d == (d < 0 ? 0 : d > INT32_MAX ? INT32_MAX : (fixed_t)d));
    assert(actual_o == (fixed_t)o);
#endif
}

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
        uint32_t len = dx == 0 ? (uint32_t)llabs(dy)
                     : dy == 0 ? (uint32_t)llabs(dx) : random_u32();
        if ((i & 7) == 0) ++len; /* Near-axis lengths must use the exact fallback. */
        if (!len) len = 1;
        cache.length_half = len;
        check(&cache);
    }
    static const int32_t edge[] = {INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX};
    static const uint32_t length[] = {1, 2, 65536, UINT32_MAX};
    for (unsigned i = 0; i < 7; ++i)
        for (unsigned j = 0; j < 7; ++j)
            for (unsigned k = 0; k < 7; ++k)
                for (unsigned n = 0; n < 4; ++n) {
                    a.x = a.y = edge[i]; b.x = b.y = edge[j];
                    viewx = viewy = edge[k]; cache.length_half = length[n];
                    check(&cache);
                    b.y = a.y;
                    check(&cache);
                    b.y = edge[j]; b.x = a.x;
                    check(&cache);
                }
    puts("PASS: 1000000 random and 4116 boundary wall distance/offset cases");
    return 0;
}
