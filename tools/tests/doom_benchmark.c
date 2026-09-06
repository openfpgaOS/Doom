/* Headless timedemo probes; linked only by tools/benchmark_doom.py. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "doomstat.h"
#include "i_video.h"
#include "d_player.h"

static uint64_t render_ns, tic_ns, sound_ns;
static uint64_t frames, views, samples[20000];
static uint64_t lazy_updates, lazy_bytes, precache_updates, precache_bytes;
static int rendering, precaching;
static FILE *trace;
static int trace_checked;

static uint64_t clock_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

void __real_R_RenderPlayerView(player_t *player);
void __wrap_R_RenderPlayerView(player_t *player)
{
    uint64_t start = clock_ns();
    rendering = 1;
    __real_R_RenderPlayerView(player);
    rendering = 0;
    uint64_t elapsed = clock_ns() - start;
    render_ns += elapsed;
    if (views < sizeof(samples) / sizeof(samples[0])) samples[views] = elapsed;
    ++views;
}

void __real_R_PrecacheLevel(void);
void __wrap_R_PrecacheLevel(void)
{
    precaching = 1;
    __real_R_PrecacheLevel();
    precaching = 0;
}

void __real_R_GPU_TextureDataUpdated(void *ptr, unsigned int size);
void __wrap_R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    if (rendering) { ++lazy_updates; lazy_bytes += size; }
    if (precaching) { ++precache_updates; precache_bytes += size; }
    __real_R_GPU_TextureDataUpdated(ptr, size);
}

void __real_TryRunTics(void);
void __wrap_TryRunTics(void)
{
    uint64_t start = clock_ns();
    __real_TryRunTics();
    tic_ns += clock_ns() - start;
}

void __real_S_UpdateSounds(mobj_t *listener);
void __wrap_S_UpdateSounds(mobj_t *listener)
{
    uint64_t start = clock_ns();
    __real_S_UpdateSounds(listener);
    sound_ns += clock_ns() - start;
}

/* Timedemos advance exactly one simulation tic per frame. Display waits and
 * SDL compositing are excluded from this CPU renderer benchmark. */
void __wrap_I_StartFrame(void) { }
void __wrap_I_FinishUpdate(void)
{
    ++frames;
    if (!trace_checked) {
        const char *path = getenv("DOOM_BENCH_TRACE");
        if (path) {
            trace = fopen(path, "w");
            if (!trace) abort();
        }
        trace_checked = 1;
    }
    if (trace && I_VideoBuffer) {
        const char *dump_tic = getenv("DOOM_BENCH_DUMP_TIC");
        if (dump_tic && gametic == atoi(dump_tic)) {
            FILE *dump = fopen("frame.raw", "wb");
            if (!dump) abort();
            fwrite(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT, 1, dump);
            fclose(dump);
        }
        uint64_t hash = UINT64_C(14695981039346656037);
        for (size_t i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
            hash = (hash ^ I_VideoBuffer[i]) * UINT64_C(1099511628211);
        player_t *p = &players[consoleplayer];
        fprintf(trace, "%d,%016" PRIx64 ",%d,%d,%d,%u,%d,%d\n",
                gametic, hash, p->mo ? p->mo->x : 0, p->mo ? p->mo->y : 0,
                p->mo ? p->mo->z : 0, p->mo ? p->mo->angle : 0,
                p->health, p->readyweapon);
    }
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

__attribute__((destructor)) static void report(void)
{
    if (trace) fclose(trace);
    size_t n = views < 20000 ? views : 20000;
    qsort(samples, n, sizeof(samples[0]), compare_u64);
    fprintf(stderr, "DOOM_BENCH {\"frames\":%" PRIu64 ",\"views\":%" PRIu64
            ",\"render_ns\":%" PRIu64 ",\"tic_ns\":%" PRIu64
            ",\"sound_ns\":%" PRIu64 ",\"render_p99_ns\":%" PRIu64
            ",\"lazy_texture_updates\":%" PRIu64 ",\"lazy_texture_bytes\":%" PRIu64
            ",\"precache_updates\":%" PRIu64 ",\"precache_bytes\":%" PRIu64 "}\n",
            frames, views, render_ns, tic_ns, sound_ns,
            n ? samples[(n - 1) * 99 / 100] : 0,
            lazy_updates, lazy_bytes, precache_updates, precache_bytes);
}
