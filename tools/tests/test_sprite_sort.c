#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#ifndef DOOM_SORT_SOURCE
#define DOOM_SORT_SOURCE "../../src/doom/cdoom/doom/r_things.c"
#endif
#include DOOM_SORT_SOURCE

static unsigned seed = 1;
static unsigned random_u32(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

static void fill(int n, int pattern)
{
    vissprite_p = vissprites + n;
    for (int i = 0; i < n; ++i) {
        switch (pattern) {
        case 0: vissprites[i].scale = (int)random_u32(); break;
        case 1: vissprites[i].scale = (int)(random_u32() % 8); break;
        case 2: vissprites[i].scale = i; break;
        case 3: vissprites[i].scale = n - i; break;
        default: vissprites[i].scale = INT_MAX; break;
        }
    }
}

static void check(int n)
{
    unsigned char seen[MAXVISSPRITES] = {0};
    vissprite_t *prev = &vsprsortedhead, *cur = prev->next;
    for (int i = 0; i < n; ++i) {
        assert(cur >= vissprites && cur < vissprites + n);
        assert(!seen[cur - vissprites]++);
        assert(cur->prev == prev);
        if (i) {
            assert(prev->scale <= cur->scale);
            if (prev->scale == cur->scale) assert(prev < cur);
        }
        prev = cur;
        cur = cur->next;
    }
    assert(cur == &vsprsortedhead && cur->prev == prev);
}

int main(void)
{
    unsigned cases = 0;
    for (int n = 0; n <= MAXVISSPRITES; ++n)
        for (int pattern = 0; pattern < 5; ++pattern) {
            fill(n, pattern);
            R_SortVisSprites();
            check(n);
            ++cases;
        }
    printf("PASS: %u stable sprite sort cases (0..%d sprites)\n", cases, MAXVISSPRITES);
    for (int n = 16; n <= MAXVISSPRITES; n *= 4) {
        fill(n, 0);
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < 20000; ++i) R_SortVisSprites();
        clock_gettime(CLOCK_MONOTONIC, &end);
        uint64_t ns = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000u
                    + end.tv_nsec - start.tv_nsec;
        printf("SORT_BENCH %d %" PRIu64 "\n", n, ns / 20000);
    }
    return 0;
}
