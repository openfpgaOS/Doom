/* Exercise the production video backend with a deterministic display clock. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DOOM_VIDEO_SOURCE
#define DOOM_VIDEO_SOURCE "../../src/doom/shim/i_video.c"
#endif
#include DOOM_VIDEO_SOURCE

static uint32_t now_us = 1000000;
static const uint32_t period_us = 16667;
static int requested_mode = REFRESH_MODE_FIXED;
static int direct_fb;
static uint32_t pending_until;
static unsigned int wait_calls, sleep_calls;
static struct of_capabilities caps;
static uint8_t fake_fb[SCREENWIDTH * SCREENHEIGHT];
boolean singletics;
int frame_interpolation = 1;
static int *bound_startup, *bound_scaling, *bound_grab;

void M_BindIntVariable(const char *name, int *location)
{
    if (!strcmp(name, "startup_delay")) bound_startup = location;
    if (!strcmp(name, "max_scaling_buffer_pixels")) bound_scaling = location;
    if (!strcmp(name, "grabmouse")) bound_grab = location;
}
void M_BindStringVariable(const char *name, char **location)
{ (void)name; (void)location; }

unsigned int of_time_us(void) { return now_us++; }
const struct of_capabilities *of_get_caps(void) { return &caps; }
void of_video_get_timing(of_video_timing_t *t)
{
    memset(t, 0, sizeof(*t));
    t->vblank_count = now_us / period_us;
    t->last_vblank_us = (uint64_t)t->vblank_count * period_us;
}
void of_video_wait_flip(void)
{
    ++wait_calls;
    if (now_us < pending_until) now_us = pending_until;
    pending_until = 0;
}
uint8_t *of_video_surface(void) { return fake_fb; }
void of_video_flip(void) { }
void of_video_palette_bulk(const uint32_t *colors, int count)
{ assert(colors && count == 256); }
int usleep(useconds_t us) { ++sleep_calls; now_us += us; return 0; }
int M_CheckParm(const char *s) { (void)s; return 0; }
int M_EffectiveRefreshMode(void) { return requested_mode; }
boolean M_RefreshModeUsesInterpolation(int mode) { (void)mode; return true; }
boolean R_GPU_UsingDirectFramebuffer(void) { return direct_fb; }
boolean R_GPU_PresentFrame(void)
{
    pending_until = (now_us / period_us + 1) * period_us;
    return true;
}
void R_GPU_EndFrame(void) { }
void I_SetPocketPacing(int enabled) { (void)enabled; }
void I_SetPredictedVblankPeriodUS(uint64_t us) { (void)us; }
void I_SetDisplayFrameSampleUS(uint64_t us) { (void)us; }
void I_SetDisplayFrameSampleNow(void) { }
unsigned int R_Perf_PacingCurrentPrepareUS(void) { return 18000; }
void R_Perf_PacingAddWait(unsigned int us) { (void)us; }
void R_Perf_PacingFrameQueued(void) { }

int main(int argc, char **argv)
{
    assert(argc == 2);
    I_VideoBuffer = fake_fb;
    if (!strcmp(argv[1], "gamma")) {
        byte palette[256 * 3];
        for (int i = 0; i < 256; ++i)
            palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = i;
        const int black[] = {1, 2, 4, 8, 16};
        for (usegamma = 0; usegamma < 5; ++usegamma) {
            I_SetPalette(palette);
            assert(palette32[0] == (uint32_t)black[usegamma] * 0x010101);
            assert(palette32[255] == 0xffffff);
            for (int i = 1; i < 256; ++i) assert(palette32[i] >= palette32[i - 1]);
        }
        usegamma = -1;
        I_SetPalette(palette);
        assert(palette32[0] == 0x010101);
    } else if (!strcmp(argv[1], "config-lifetime")) {
        I_BindVideoVariables();
        assert(bound_startup && bound_scaling && bound_grab);
        *bound_startup = 11; *bound_scaling = 22; *bound_grab = 33;
        assert(*bound_startup == 11 && *bound_scaling == 22 && *bound_grab == 33);
    } else if (!strcmp(argv[1], "mister-period")) {
        caps.platform_id = OF_PLATFORM_MISTER;
        assert(I_RefreshModeVTotal(REFRESH_MODE_FIXED) == DOOM_VIDEO_VTOTAL_60HZ);
        caps.platform_id = OF_PLATFORM_POCKET;
        assert(I_RefreshModeVTotal(REFRESH_MODE_FIXED) == DOOM_VIDEO_VTOTAL_42HZ);
        assert(I_RefreshModeVTotal(REFRESH_MODE_PAL) == DOOM_VIDEO_VTOTAL_50HZ);
    } else if (!strcmp(argv[1], "late-frame")) {
        now_us = 20000;
        uint32_t last = 0, elapsed = 0;
        of_video_timing_t t;
        assert(I_WaitForNextVBlank(&last, 100000, &elapsed, &t));
        assert(now_us < 20100 && last == 1 && elapsed == 1);
        now_us = 70000;
        assert(I_WaitForNextVBlank(&last, 100000, &elapsed, &t));
        assert(now_us < 70100 && last == 4 && elapsed == 3);
    } else if (!strcmp(argv[1], "fresh-frame")) {
        now_us = 20000;
        uint32_t last = 1, elapsed = 0;
        of_video_timing_t t;
        assert(I_WaitForNextVBlank(&last, 100000, &elapsed, &t));
        assert(now_us >= 33334 && now_us < 33400 && elapsed == 1);
    } else if (!strcmp(argv[1], "timedemo")) {
        singletics = true;
        uint32_t start = now_us;
        I_StartFrame();
        I_FinishUpdate();
        I_StartFrame();
        I_FinishUpdate();
        assert(now_us - start < 100 && sleep_calls == 0);
    } else if (!strcmp(argv[1], "overload")) {
        direct_fb = 1;
        unsigned int start_waits = 0;
        uint32_t longest = 0, start = now_us;
        for (int i = 0; i < 24; ++i) {
            unsigned int waits_before = wait_calls;
            uint32_t before = now_us;
            I_StartFrame();
            start_waits += wait_calls - waits_before;
            now_us += 18000;
            I_FinishUpdate();
            if (now_us - before > longest) longest = now_us - before;
        }
        printf("24 frames: %u us; longest: %u us; forced frame-start waits: %u\n",
               now_us - start, longest, start_waits);
        assert(start_waits == 0 && longest < 18100);
    } else {
        abort();
    }
    printf("PASS: %s\n", argv[1]);
    return 0;
}
