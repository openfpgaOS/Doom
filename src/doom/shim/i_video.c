/* i_video.c — chocolate-doom video backend over openfpgaOS SDK.
 *
 * Doom renders into a 320x200 indexed buffer (I_VideoBuffer).  With GPU
 * flip enabled, the GPU layer retargets that pointer at the rotating
 * 320x200 hardware framebuffer.  The static buffer below remains the CPU
 * fallback when GPU flip is disabled.
 */

#include "config.h"
#include "of.h"
#include "i_system.h"
#include "i_video.h"
#include "i_timer.h"
#include "m_menu.h"
#include "v_video.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "r_gpu.h"
#include "r_perf.h"
#include "z_zone.h"
#include "m_controls.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Public globals expected by chocolate-doom core. */
char *video_driver    = "";
boolean screenvisible = true;

int     vanilla_keyboard_mapping = 1;
boolean screensaver_mode         = false;
int     usegamma                 = 0;
pixel_t *I_VideoBuffer           = NULL;

int     screen_width             = SCREENWIDTH;
int     screen_height            = SCREENHEIGHT;
int     fullscreen               = 1;
int     aspect_ratio_correct     = 0;
int     integer_scaling          = 1;
int     smooth_pixel_scaling     = 0;
int     vga_porch_flash          = 0;
int     force_software_renderer  = 0;
int     png_screenshots          = 0;
char   *window_position          = "center";
unsigned int joywait             = 0;
int     usemouse                 = 0;

/* ---- Gamma (chocolate-doom's 5-level table) -------------------------- */
static const byte gammatable[5][256] = {
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
     33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
     65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,
     97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
     121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
     145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,
     169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,
     193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,
     217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,
     241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,255}
};

/* Palette storage (doom hands us 256 × RGB 0-255 triplets). */
static uint32_t palette32[256];

/* Stable fallback backbuffer used when GPU flip is disabled/unavailable. */
static pixel_t video_buf[SCREENWIDTH * SCREENHEIGHT];

/* Grab-mouse callback (ignored on this backend). */
static grabmouse_callback_t grabmouse_cb;

#define GPU_PACE_SAMPLES 32
#define GPU_PACE_WARMUP_SAMPLES 8
#define DOOM_CAPPED_VTOTAL OF_VIDEO_VTOTAL_61_25HZ
#define GPU_PACE_MIN_US 16667u
#define GPU_PACE_MAX_US 23800u
#define GPU_PACE_MARGIN_US 1000u
#define GPU_PACE_WAIT_CAP_US 1000u
#define GPU_PACE_SPIKE_US 50000u
#define GPU_PACE_ENABLE_MAX_TARGET_US GPU_PACE_MAX_US
#define GPU_PACE_VTOTAL_MIN OF_VIDEO_VTOTAL_60HZ
#define GPU_PACE_VTOTAL_MAX OF_VIDEO_VTOTAL_42HZ
#define GPU_PACE_VTOTAL_HYSTERESIS 5u
#define GPU_PACE_VTOTAL_RAISE_STEP 24u
#define GPU_PACE_VTOTAL_LOWER_STEP 24u
#define GPU_PACE_VTOTAL_UPDATE_US 500000u
#define GPU_PACE_POCKET_VTOTAL_UPDATE_US 100000u
#define GPU_PACE_POCKET_VTOTAL_STEP 12u

typedef struct
{
    unsigned int samples[GPU_PACE_SAMPLES];
    unsigned int sample_count;
    unsigned int sample_pos;
    unsigned int target_us;
    unsigned int vrr_target_us;
    unsigned int last_queue_us;
    unsigned int current_vtotal;
    unsigned int last_vtotal_update_us;
    uint32_t timing_vblank_count;
    uint64_t timing_vblank_us;
    unsigned int refresh_period_us;
    int options_checked;
    int enabled;
    int fine_vrr_enabled;
    int pocket_vrr_enabled;
    int pocket_pacing_enabled;
} gpu_adaptive_pace_t;

static gpu_adaptive_pace_t gpu_pace;

static void I_ApplyCappedRefresh(void);

static void I_CheckAdaptivePacingOptions(void)
{
    if (gpu_pace.options_checked)
        return;

    gpu_pace.enabled = M_CheckParm("-noadaptivepacing") <= 0;
    gpu_pace.pocket_pacing_enabled = M_CheckParm("-nopocketpacing") <= 0
                                   && M_CheckParm("-fixed60") <= 0;
    /* Pocket pacing implies pocket VRR (sample comes from I_StartFrame
     * instead of I_AdaptiveGpuPaceBeforeFlip; the two are coupled). */
    gpu_pace.pocket_vrr_enabled = (gpu_pace.enabled
                                   && M_CheckParm("-pocketvrr") > 0
                                   && M_CheckParm("-fixed60") <= 0)
                                || gpu_pace.pocket_pacing_enabled;
    gpu_pace.fine_vrr_enabled = gpu_pace.enabled
                              && !gpu_pace.pocket_vrr_enabled
                              && M_CheckParm("-nofinevrr") <= 0
                              && M_CheckParm("-fixed60") <= 0;
    I_SetPocketPacing(gpu_pace.pocket_pacing_enabled);
    gpu_pace.options_checked = 1;
}

static unsigned int I_AdaptivePacePercentile(void)
{
    unsigned int sorted[GPU_PACE_SAMPLES];
    unsigned int n = gpu_pace.sample_count;
    unsigned int idx;

    if (n == 0)
        return GPU_PACE_MIN_US;

    for (unsigned int i = 0; i < n; i++)
    {
        unsigned int value = gpu_pace.samples[i];
        unsigned int j = i;

        while (j > 0 && sorted[j - 1] > value)
        {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }

    idx = (n * 7u) / 8u;
    if (idx >= n)
        idx = n - 1u;

    return sorted[idx];
}

static void I_UpdateMeasuredRefreshPeriod(void)
{
    of_video_timing_t timing;

    of_video_get_timing(&timing);
    if (timing.vblank_count == 0 || timing.last_vblank_us == 0)
        return;

    if (gpu_pace.timing_vblank_count != 0 &&
        timing.vblank_count != gpu_pace.timing_vblank_count &&
        timing.last_vblank_us > gpu_pace.timing_vblank_us)
    {
        uint32_t delta_vblanks =
            timing.vblank_count - gpu_pace.timing_vblank_count;
        uint64_t delta_us =
            timing.last_vblank_us - gpu_pace.timing_vblank_us;

        if (delta_vblanks != 0)
        {
            unsigned int period_us =
                (unsigned int)(delta_us / delta_vblanks);

            if (period_us >= 10000u && period_us <= 25000u)
                gpu_pace.refresh_period_us = period_us;
        }
    }

    gpu_pace.timing_vblank_count = timing.vblank_count;
    gpu_pace.timing_vblank_us = timing.last_vblank_us;
}

static unsigned int I_PaceTargetToVTotal(unsigned int target_us)
{
    uint64_t vtotal;
    unsigned int current_vtotal;
    unsigned int current_period_us;

    current_vtotal = gpu_pace.current_vtotal != 0
                   ? gpu_pace.current_vtotal
                   : OF_VIDEO_VTOTAL_60HZ;
    current_period_us = gpu_pace.refresh_period_us != 0
                      ? gpu_pace.refresh_period_us
                      : GPU_PACE_MIN_US;

    vtotal = ((uint64_t)current_vtotal * target_us
              + (current_period_us / 2u)) / current_period_us;

    if (vtotal < GPU_PACE_VTOTAL_MIN)
        vtotal = GPU_PACE_VTOTAL_MIN;
    if (vtotal > GPU_PACE_VTOTAL_MAX)
        vtotal = GPU_PACE_VTOTAL_MAX;

    return (unsigned int)vtotal;
}

/* Hybrid VRR control law:
 *   - sample is directly measured renderer prep time (Doom's signal)
 *   - asymmetric EMA: quick attack on heavier samples, alpha=1/64 decay
 *     when load eases.  Do not let a single door/open-area spike yank
 *     the panel refresh all the way down.
 *   - desired = render + 10% headroom
 *   - vtotal comes from the measured period ratio, clamped to [60Hz, 42Hz]
 *   - drops samples > 50ms (level loads, pauses) before they pollute
 *     the EMA */
static void I_UpdatePocketVRR(unsigned int sample_us)
{
    unsigned int now_us;
    unsigned int current_vtotal;
    unsigned int desired_us;
    unsigned int desired_vtotal;
    unsigned int delta;
    unsigned int next_vtotal;

    I_UpdateMeasuredRefreshPeriod();

    if (sample_us == 0 || sample_us > GPU_PACE_SPIKE_US)
        return;

    if (gpu_pace.vrr_target_us == 0)
        gpu_pace.vrr_target_us = sample_us;
    else if (sample_us > gpu_pace.vrr_target_us)
        gpu_pace.vrr_target_us =
            (gpu_pace.vrr_target_us * 3u + sample_us + 2u) / 4u;
    else
        gpu_pace.vrr_target_us +=
            ((int)sample_us - (int)gpu_pace.vrr_target_us) / 64;

    now_us = of_time_us();
    if (gpu_pace.last_vtotal_update_us != 0 &&
        now_us - gpu_pace.last_vtotal_update_us < GPU_PACE_POCKET_VTOTAL_UPDATE_US)
        return;

    current_vtotal = gpu_pace.current_vtotal != 0
                   ? gpu_pace.current_vtotal
                   : OF_VIDEO_VTOTAL_60HZ;
    if (gpu_pace.refresh_period_us == 0)
        return;

    desired_us = gpu_pace.vrr_target_us + gpu_pace.vrr_target_us / 10;
    desired_vtotal = I_PaceTargetToVTotal(desired_us);
    delta = desired_vtotal > current_vtotal
          ? desired_vtotal - current_vtotal
          : current_vtotal - desired_vtotal;
    if (delta < GPU_PACE_VTOTAL_HYSTERESIS)
        return;

    next_vtotal = desired_vtotal;
    if (desired_vtotal > current_vtotal &&
        desired_vtotal - current_vtotal > GPU_PACE_POCKET_VTOTAL_STEP)
    {
        next_vtotal = current_vtotal + GPU_PACE_POCKET_VTOTAL_STEP;
    }
    else if (current_vtotal > desired_vtotal &&
             current_vtotal - desired_vtotal > GPU_PACE_POCKET_VTOTAL_STEP)
    {
        next_vtotal = current_vtotal - GPU_PACE_POCKET_VTOTAL_STEP;
    }

    of_video_set_refresh_vtotal(next_vtotal);
    gpu_pace.current_vtotal = next_vtotal;
    gpu_pace.last_vtotal_update_us = now_us;
    R_Perf_PacingSetVTotal(next_vtotal);

    /* Push the implied next-vblank period into the predictor so the
     * interpolation phase in d_main.c uses the new V_TOTAL on the very
     * next frame, instead of waiting one frame for the measured
     * vblank-interval average to catch up. */
    I_SetPredictedVblankPeriodUS(((uint64_t)gpu_pace.refresh_period_us
                                  * next_vtotal) / current_vtotal);
}

static void I_UpdateFineVRR(unsigned int target_us)
{
    unsigned int now_us;
    unsigned int desired_vtotal;
    unsigned int current_vtotal;
    unsigned int delta;
    unsigned int next_vtotal;

    I_UpdateMeasuredRefreshPeriod();

    if (!gpu_pace.fine_vrr_enabled ||
        gpu_pace.sample_count < GPU_PACE_WARMUP_SAMPLES)
        return;

    if (gpu_pace.vrr_target_us == 0)
    {
        gpu_pace.vrr_target_us = target_us;
    }
    else
    {
        gpu_pace.vrr_target_us =
            (gpu_pace.vrr_target_us * 7u + target_us + 4u) / 8u;
    }

    now_us = of_time_us();
    if (gpu_pace.last_vtotal_update_us != 0 &&
        now_us - gpu_pace.last_vtotal_update_us < GPU_PACE_VTOTAL_UPDATE_US)
        return;

    current_vtotal = gpu_pace.current_vtotal != 0
                   ? gpu_pace.current_vtotal
                   : OF_VIDEO_VTOTAL_60HZ;
    desired_vtotal = I_PaceTargetToVTotal(gpu_pace.vrr_target_us);
    delta = desired_vtotal > current_vtotal
          ? desired_vtotal - current_vtotal
          : current_vtotal - desired_vtotal;

    if (delta < GPU_PACE_VTOTAL_HYSTERESIS)
        return;

    next_vtotal = desired_vtotal;
    if (desired_vtotal > current_vtotal &&
        desired_vtotal - current_vtotal > GPU_PACE_VTOTAL_RAISE_STEP)
    {
        next_vtotal = current_vtotal + GPU_PACE_VTOTAL_RAISE_STEP;
    }
    else if (current_vtotal > desired_vtotal &&
             current_vtotal - desired_vtotal > GPU_PACE_VTOTAL_LOWER_STEP)
    {
        next_vtotal = current_vtotal - GPU_PACE_VTOTAL_LOWER_STEP;
    }

    of_video_set_refresh_vtotal(next_vtotal);
    gpu_pace.current_vtotal = next_vtotal;
    gpu_pace.last_vtotal_update_us = now_us;
    R_Perf_PacingSetVTotal(next_vtotal);
}

static unsigned int I_UpdateAdaptivePaceTarget(unsigned int sample_us)
{
    unsigned int desired_us;

    if (sample_us == 0)
        sample_us = GPU_PACE_MIN_US;

    if (sample_us > GPU_PACE_SPIKE_US)
        return gpu_pace.target_us ? gpu_pace.target_us : GPU_PACE_MIN_US;

    gpu_pace.samples[gpu_pace.sample_pos] = sample_us;
    gpu_pace.sample_pos = (gpu_pace.sample_pos + 1u) % GPU_PACE_SAMPLES;
    if (gpu_pace.sample_count < GPU_PACE_SAMPLES)
        gpu_pace.sample_count++;

    desired_us = I_AdaptivePacePercentile() + GPU_PACE_MARGIN_US;
    if (desired_us < GPU_PACE_MIN_US)
        desired_us = GPU_PACE_MIN_US;
    if (desired_us > GPU_PACE_MAX_US)
        desired_us = GPU_PACE_MAX_US;

    if (gpu_pace.target_us == 0)
    {
        gpu_pace.target_us = desired_us;
    }
    else if (desired_us > gpu_pace.target_us)
    {
        gpu_pace.target_us = (gpu_pace.target_us + desired_us + 1u) / 2u;
    }
    else
    {
        gpu_pace.target_us = (gpu_pace.target_us * 3u + desired_us) / 4u;
    }

    R_Perf_PacingSetTargetUS(gpu_pace.target_us);
    if (frame_interpolation && !gpu_pace.pocket_vrr_enabled)
        I_UpdateFineVRR(gpu_pace.target_us);
    return gpu_pace.target_us;
}

static void I_AdaptiveGpuPaceBeforeFlip(void)
{
    unsigned int now_us;
    unsigned int target_us;
    unsigned int prepare_us;
    unsigned int elapsed_us;
    unsigned int sample_us;
    unsigned int wait_us;
    unsigned int wait_start_us;
    unsigned int wait_deadline_us;
    unsigned int waited_us;

    I_CheckAdaptivePacingOptions();

    if (!gpu_pace.enabled)
        return;

    if (!frame_interpolation)
    {
        I_ApplyCappedRefresh();
        return;
    }

    now_us = of_time_us();
    prepare_us = R_Perf_PacingCurrentPrepareUS();
    elapsed_us = gpu_pace.last_queue_us != 0
               ? now_us - gpu_pace.last_queue_us
               : 0;
    /* Learn from renderer cost, not from the current scanout interval.
     * Otherwise, once VRR stretches to a slower vtotal, that longer
     * present interval feeds back into the target and can lock us at the
     * slowest refresh even after the renderer has recovered. */
    sample_us = prepare_us;

    if (gpu_pace.pocket_vrr_enabled)
        I_UpdatePocketVRR(sample_us);

    target_us = I_UpdateAdaptivePaceTarget(sample_us);

    if (gpu_pace.last_queue_us == 0 ||
        gpu_pace.sample_count < GPU_PACE_WARMUP_SAMPLES ||
        (!gpu_pace.fine_vrr_enabled &&
         target_us <= GPU_PACE_MIN_US + GPU_PACE_MARGIN_US) ||
        target_us > GPU_PACE_ENABLE_MAX_TARGET_US)
        return;

    if (elapsed_us >= target_us)
        return;

    wait_us = target_us - elapsed_us;
    if (wait_us > GPU_PACE_WAIT_CAP_US)
        wait_us = GPU_PACE_WAIT_CAP_US;

    wait_start_us = of_time_us();
    wait_deadline_us = wait_start_us + wait_us;
    while ((int)(of_time_us() - wait_deadline_us) < 0)
        __asm__ volatile("" ::: "memory");
    waited_us = of_time_us() - wait_start_us;
    R_Perf_PacingAddWait(waited_us);
}

static void I_AdaptiveGpuPaceQueued(void)
{
    I_CheckAdaptivePacingOptions();

    if (!gpu_pace.enabled)
        return;

    gpu_pace.last_queue_us = of_time_us();
}

static void I_AdaptiveGpuPaceReset(void)
{
    gpu_pace.last_queue_us = 0;
    /* Clear EMA state so re-entry into VRR is clean. */
    gpu_pace.vrr_target_us = 0;
}

/* ---- Init / shutdown ------------------------------------------------- */

static void I_SetDoomVideoMode(void)
{
    of_video_mode_t mode = {
        SCREENWIDTH,
        SCREENHEIGHT,
        SCREENWIDTH,
        OF_VIDEO_MODE_8BIT,
        0
    };
    of_video_mode_t actual;

    if (of_video_set_mode(&mode) < 0)
        I_Error("Failed to set openfpgaOS 320x200 video mode");

    of_video_get_mode(&actual);
    if (actual.width != SCREENWIDTH ||
        actual.height != SCREENHEIGHT ||
        actual.stride != SCREENWIDTH ||
        actual.color_mode != OF_VIDEO_MODE_8BIT)
    {
        I_Error("Unexpected openfpgaOS video mode %ux%u stride %u color %u",
                (unsigned int)actual.width,
                (unsigned int)actual.height,
                (unsigned int)actual.stride,
                (unsigned int)actual.color_mode);
    }
}

static void I_ApplyCappedRefresh(void)
{
    of_video_set_refresh_vtotal(DOOM_CAPPED_VTOTAL);
    gpu_pace.current_vtotal = DOOM_CAPPED_VTOTAL;
    R_Perf_PacingSetVTotal(DOOM_CAPPED_VTOTAL);
}

void I_InitGraphics(void)
{
    of_video_init();
    I_SetDoomVideoMode();

    /* Parse pacing flags before the first frame so pocket_pacing /
     * pocket_vrr_enabled are set when I_StartFrame and I_FinishUpdate
     * first run.  Without this, the pocket short-circuit fires with
     * pocket_vrr_enabled=0 on every frame and VRR never engages. */
    I_CheckAdaptivePacingOptions();
    gpu_pace.last_vtotal_update_us = of_time_us();
    gpu_pace.refresh_period_us = GPU_PACE_MIN_US;
    gpu_pace.vrr_target_us = 0;
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    I_ApplyCappedRefresh();

    /* Clear all three 320x200 back buffers once. */
    for (int buf = 0; buf < 3; buf++) {
        uint8_t *fb = of_video_surface();
        memset(fb, 0, SCREENWIDTH * SCREENHEIGHT);
        of_video_flip();
        of_video_wait_flip();
    }

    I_VideoBuffer = video_buf;
    V_RestoreBuffer();
    R_GPU_Init();
    screenvisible = true;

    key_fire = KEY_ENTER;
    key_use  = ' ';
    key_speed = KEY_RSHIFT;
    key_strafeleft = ',';
    key_straferight = '.';
    key_prevweapon = '[';
    key_nextweapon = ']';
    key_message_refresh = 0;
}

void I_GraphicsCheckCommandLine(void) { }
void I_ShutdownGraphics(void)          { R_GPU_Shutdown(); I_VideoBuffer = NULL; }

void I_SetWindowTitle(const char *t)   { (void)t; }
void I_InitWindowTitle(void)           { }
void I_RegisterWindowIcon(const unsigned int *icon, int w, int h) { (void)icon; (void)w; (void)h; }
void I_InitWindowIcon(void)            { }
void I_CheckIsScreensaver(void)        { }
void I_SetGrabMouseCallback(grabmouse_callback_t func) { grabmouse_cb = func; (void)grabmouse_cb; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void)xoffs; (void)yoffs; }
void I_BeginRead(void)                 { }

void I_StartFrame(void)
{
    static uint32_t last_seen_vblank = 0;
    static int last_frame_interpolation = -1;
    of_video_timing_t timing;

    /* VRR off (capped): request ~61.25Hz and let the wall-clock 35Hz tic
     * clock run as-is.  61.25/35 = 7/4, giving a stable 2/2/2/1 refresh
     * hold cadence instead of the 60Hz 12/7 beat. */
    if (!frame_interpolation)
    {
        I_ApplyCappedRefresh();
        last_frame_interpolation = 0;
        if (!I_PocketPacingActive())
            return;
    }
    else if (!I_PocketPacingActive())
    {
        return;
    }

    /* Wait for the next vsync at frame start so the renderer has the
     * full vsync period as its budget.  Poll on vblank_count rather
     * than of_video_wait_flip() because the GPU triggered-flip path
     * queues CMD_FLIPs that of_video_wait_flip() does not track. */
    for (;;) {
        of_video_get_timing(&timing);
        if (timing.vblank_count != last_seen_vblank)
            break;
    }
    last_seen_vblank = timing.vblank_count;

    if (frame_interpolation
               && last_frame_interpolation == 0
               && gpu_pace.current_vtotal != OF_VIDEO_VTOTAL_60HZ) {
        of_video_set_refresh_vtotal(OF_VIDEO_VTOTAL_60HZ);
        gpu_pace.current_vtotal = OF_VIDEO_VTOTAL_60HZ;
        gpu_pace.refresh_period_us = GPU_PACE_MIN_US;
        R_Perf_PacingSetVTotal(OF_VIDEO_VTOTAL_60HZ);
    }
    last_frame_interpolation = frame_interpolation ? 1 : 0;

    /* VRR on: one gametic per rendered frame, advanced right at the
     * start of this frame so TryRunTics sees the new tic value. */
    I_PocketAdvanceFrameTic();
}
extern void I_PollInput(void);
void I_StartTic(void)                  { I_PollInput(); }

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void)w; (void)h;
    if (x) *x = 0;
    if (y) *y = 0;
}

void I_BindVideoVariables(void)
{
    M_BindIntVariable("use_mouse",               &usemouse);
    M_BindIntVariable("fullscreen",              &fullscreen);
    M_BindIntVariable("aspect_ratio_correct",    &aspect_ratio_correct);
    M_BindIntVariable("integer_scaling",         &integer_scaling);
    M_BindIntVariable("vga_porch_flash",         &vga_porch_flash);
    M_BindIntVariable("smooth_pixel_scaling",    &smooth_pixel_scaling);
    M_BindIntVariable("startup_delay",           (int[]){0});
    M_BindIntVariable("fullscreen_width",        &screen_width);
    M_BindIntVariable("fullscreen_height",       &screen_height);
    M_BindIntVariable("force_software_renderer", &force_software_renderer);
    M_BindIntVariable("max_scaling_buffer_pixels", (int[]){0});
    M_BindIntVariable("window_width",            &screen_width);
    M_BindIntVariable("window_height",           &screen_height);
    M_BindIntVariable("grabmouse",               (int[]){0});
    M_BindIntVariable("usegamma",                &usegamma);
    M_BindIntVariable("png_screenshots",         &png_screenshots);
    M_BindIntVariable("vanilla_keyboard_mapping",&vanilla_keyboard_mapping);
    M_BindStringVariable("video_driver",         &video_driver);
    M_BindStringVariable("window_position",      &window_position);
}

/* ---- Palette --------------------------------------------------------- */

void I_SetPalette(byte *doompal)
{
    const byte *gamma = gammatable[usegamma % 5];
    for (int i = 0; i < 256; i++) {
        uint32_t r = gamma[*doompal++];
        uint32_t g = gamma[*doompal++];
        uint32_t b = gamma[*doompal++];
        palette32[i] = (r << 16) | (g << 8) | b;
    }
    of_video_palette_bulk(palette32, 256);
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = 0x7FFFFFFF;
    for (int i = 0; i < 256; i++) {
        int dr = r - ((palette32[i] >> 16) & 0xFF);
        int dg = g - ((palette32[i] >>  8) & 0xFF);
        int db = b - ((palette32[i])       & 0xFF);
        int d  = dr*dr + dg*dg + db*db;
        if (d < best_diff) { best_diff = d; best = i; if (d == 0) break; }
    }
    return best;
}

/* ---- Frame update ---------------------------------------------------- */

void I_UpdateNoBlit(void) { }

void I_FinishUpdate(void)
{
    /* Pace presentation to ~60 Hz.
     *
     * Before uncapped interpolation landed, TryRunTics() blocked ~28 ms
     * per iteration waiting for the next 35 Hz tic — that was the only
     * thing keeping the loop from running at CPU speed, because
     * of_video_flip() is non-blocking.
     *
     * With the uncapped path, TryRunTics() returns immediately when no
     * new tic is ready. Without a wait here the main loop busy-spins at
     * CPU speed, which starves audio/IRQ handling enough to appear as a
     * freeze with a stuck buzzing sound effect when the menu sfx fires.
     *
     * Direct GPU flip is paced by CMD_FLIP backpressure instead of an
     * explicit post-present wait.  That gives the renderer a little room:
     * the CPU can build the next frame while the previous swap is still
     * pending, and the next CMD_FLIP naturally stalls until the single
     * pending swap slot clears. */
    static unsigned int last_flip_us = 0;
    const unsigned int target_us = 16667;  /* ~60 Hz */

    /* Pocket pacing path: I_StartFrame already waited for the previous
     * flip and advanced the tic clock.  Skip the percentile pacing,
     * skip the explicit busy-wait in I_AdaptiveGpuPaceBeforeFlip, skip
     * the usleep in the CPU-fb branch — just submit the present and
     * record perf state.  CMD_FLIP backpressure / the next frame's
     * of_video_wait_flip() provide the only stall.
     *
     * Sample the renderer prep time before PacingFrameQueued() clears
     * frame_active — that's the directly-measured render cost we feed
     * into VRR. */
    if (I_PocketPacingActive()) {
        last_flip_us = 0;
        /* Dynamic VRR only when interpolating; capped mode holds the
         * fixed 61.25Hz target in I_StartFrame. */
        if (gpu_pace.pocket_vrr_enabled && frame_interpolation) {
            unsigned int prepare_us = R_Perf_PacingCurrentPrepareUS();
            if (prepare_us > 0)
                I_UpdatePocketVRR(prepare_us);
        }
        if (R_GPU_UsingDirectFramebuffer()) {
            if (R_GPU_PresentFrame()) {
                R_Perf_CountPresentedFrame(1);
                R_Perf_PacingFrameQueued();
                R_Perf_FrameEnd();
                return;
            }
            R_GPU_EndFrame();
        } else {
            R_GPU_EndFrame();
        }
        uint8_t *fb = of_video_surface();
        memcpy(fb, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
        of_video_flip();
        R_Perf_CountPresentedFrame(0);
        R_Perf_PacingFrameQueued();
        R_Perf_FrameEnd();
        return;
    }

    if (R_GPU_UsingDirectFramebuffer()) {
        last_flip_us = 0;
        I_AdaptiveGpuPaceBeforeFlip();
        if (R_GPU_PresentFrame())
        {
            I_AdaptiveGpuPaceQueued();
            R_Perf_CountPresentedFrame(1);
            R_Perf_PacingFrameQueued();
            R_Perf_FrameEnd();
            return;
        }

        I_AdaptiveGpuPaceReset();
        R_GPU_EndFrame();
    } else {
        I_AdaptiveGpuPaceReset();
        R_GPU_EndFrame();

        unsigned int now = of_time_us();
        unsigned int dt  = now - last_flip_us;
        if (last_flip_us && dt < target_us) {
            unsigned int wait_start = R_Perf_BeginStage();
            usleep(target_us - dt);
            R_Perf_EndStage(R_PERF_STAGE_VSYNC_WAIT, wait_start);
        }
        last_flip_us = of_time_us();
    }

    unsigned int blit_start = R_Perf_BeginStage();
    uint8_t *fb = of_video_surface();
    memcpy(fb, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
    of_video_flip();
    R_Perf_EndStage(R_PERF_STAGE_BLIT, blit_start);
    R_Perf_CountPresentedFrame(0);
    R_Perf_PacingFrameQueued();
    R_Perf_FrameEnd();
}

void I_ReadScreen(pixel_t *scr)
{
    R_GPU_PrepareForCPUAccess();
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}
