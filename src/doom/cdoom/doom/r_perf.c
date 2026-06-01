//
// Doom renderer performance counters for openfpgaOS.
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "m_argv.h"
#include "of_timer.h"
#include "of_video.h"
#define R_PERF_IMPLEMENTATION
#include "r_perf.h"

#if R_RENDER_PERF

#define R_PERF_REPORT_US 5000000u

typedef struct
{
    uint64_t stage_us[R_PERF_STAGE_COUNT];
    uint64_t current_stage_us[R_PERF_STAGE_COUNT];
    uint64_t detail_us[R_PERF_DETAIL_COUNT];
    uint64_t frame_us;
    uint32_t max_frame_us;
    uint32_t frames;
    uint32_t views;
    uint32_t direct_frames;
    uint32_t blit_frames;

    uint32_t gpu_columns;
    uint32_t gpu_column_pixels;
    uint32_t gpu_column_batches;
    uint32_t gpu_column_batch_lanes;
    uint32_t gpu_spans;
    uint32_t gpu_span_pixels;
    uint32_t cpu_columns;
    uint32_t cpu_column_pixels;
    uint32_t cpu_spans;
    uint32_t cpu_span_pixels;

    uint32_t gpu_finishes;
    uint32_t prepare_cpu_calls;
    uint64_t dma_waits;
    uint64_t dma_spin_iters;
    uint64_t dma_wait_us;
    uint64_t ring_waits;
    uint64_t ring_spin_iters;
    uint64_t ring_wait_us;
    uint64_t cmd_flushes;
    uint64_t cmd_flush_us;
    uint64_t cmd_flush_words;
    uint64_t cmd_words;
    uint32_t min_ring_free;
    uint32_t ring_free;
    uint32_t gpu_status;

    unsigned int interval_start_us;
    unsigned int frame_start_us;
    int frame_active;
} r_perf_counters_t;

static r_perf_counters_t perf;
static int r_perf_enabled;
int r_perf_summary_enabled;
int r_perf_detail_enabled;
uint32_t r_perf_detail_count[R_PERF_DETAIL_COUNT];
static int r_perf_options_checked;

static void R_Perf_CheckOptions(void)
{
    int detail_requested;
    int disabled;

    if (r_perf_options_checked)
        return;

    disabled = M_CheckParm("-noperf") > 0
            || M_CheckParm("-norenderperf") > 0
            || M_CheckParm("-noperfsummary") > 0
            || M_CheckParm("-norenderperfsummary") > 0;

    detail_requested = M_CheckParm("-renderperfdetail") > 0
                    || M_CheckParm("-perfdetail") > 0;

    if (disabled)
        detail_requested = 0;

    r_perf_detail_enabled =
#if R_RENDER_PERF_DETAIL_TIMING
                          detail_requested;
#else
                          0;
#endif

    r_perf_summary_enabled = !disabled
                          && (detail_requested
                           || M_CheckParm("-renderperfsummary") > 0
                           || M_CheckParm("-perfsummary") > 0
                           || M_CheckParm("-renderperf") > 0
                           || M_CheckParm("-perf") > 0
                           || R_RENDER_PERF);
    r_perf_enabled = r_perf_summary_enabled;

    r_perf_options_checked = 1;
}

static void R_Perf_Reset(unsigned int now_us)
{
    memset(&perf, 0, sizeof(perf));
    memset(r_perf_detail_count, 0, sizeof(r_perf_detail_count));
    perf.interval_start_us = now_us;
    perf.min_ring_free = 0xffffffffu;
}

unsigned int R_Perf_NowUS(void)
{
    return of_time_us();
}

unsigned int R_Perf_BeginStage(void)
{
    R_Perf_CheckOptions();
    if (!r_perf_enabled)
        return 0;

    return R_Perf_NowUS();
}

void R_Perf_AddStageUS(r_perf_stage_t stage, unsigned int elapsed_us)
{
    if (!r_perf_enabled)
        return;
    if ((unsigned int)stage >= R_PERF_STAGE_COUNT)
        return;

    perf.stage_us[stage] += elapsed_us;
    if (perf.frame_active)
        perf.current_stage_us[stage] += elapsed_us;
}

void R_Perf_EndStage(r_perf_stage_t stage, unsigned int start_us)
{
    if (start_us == 0)
        return;

    R_Perf_AddStageUS(stage, R_Perf_NowUS() - start_us);
}

void R_Perf_CountDetail(r_perf_detail_t detail)
{
    if (!r_perf_summary_enabled && !r_perf_detail_enabled)
        return;
    if ((unsigned int)detail >= R_PERF_DETAIL_COUNT)
        return;

    r_perf_detail_count[detail]++;
}

void R_Perf_EndDetail(r_perf_detail_t detail, unsigned int start_us)
{
    if ((unsigned int)detail >= R_PERF_DETAIL_COUNT)
        return;

    r_perf_detail_count[detail]++;
    perf.detail_us[detail] += R_Perf_NowUS() - start_us;
}

static void R_Perf_PrintAvg(const char *name, uint64_t total_us,
                            uint32_t frames)
{
    uint64_t avg;

    if (frames == 0)
        frames = 1;

    avg = total_us / frames;
    printf(" %s=%llu.%03llu", name,
           (unsigned long long)(avg / 1000u),
           (unsigned long long)(avg % 1000u));
}

static void R_Perf_PrintRate(const char *name, uint64_t count,
                             unsigned int elapsed_us)
{
    uint64_t per_sec;

    if (elapsed_us == 0)
        elapsed_us = 1;

    per_sec = (count * 1000000ull) / elapsed_us;
    printf(" %s/s=%llu", name, (unsigned long long)per_sec);
}

static void R_Perf_PrintTotalMS(const char *name, uint64_t total_us)
{
    printf(" %s_ms=%llu.%03llu", name,
           (unsigned long long)(total_us / 1000u),
           (unsigned long long)(total_us % 1000u));
}

static void R_Perf_PrintAvgCount(const char *name, uint64_t count,
                                 uint32_t divisor)
{
    uint64_t avg10;

    if (divisor == 0)
        divisor = 1;

    avg10 = (count * 10ull) / divisor;
    printf(" %s=%llu.%llu", name,
           (unsigned long long)(avg10 / 10u),
           (unsigned long long)(avg10 % 10u));
}

static void R_Perf_PrintAndReset(unsigned int now_us)
{
    unsigned int elapsed_us = now_us - perf.interval_start_us;
    unsigned int elapsed_ms = elapsed_us / 1000u;
    uint64_t fps10;

    if (perf.frames == 0)
    {
        R_Perf_Reset(now_us);
        return;
    }

    if (elapsed_us == 0)
        elapsed_us = 1;

    fps10 = ((uint64_t)perf.frames * 10000000ull) / elapsed_us;

    printf("[render_perf] window=%ums frames=%u fps=%llu.%llu views=%u direct=%u blit=%u max_frame=%u.%03u\n",
           elapsed_ms,
           perf.frames,
           (unsigned long long)(fps10 / 10u),
           (unsigned long long)(fps10 % 10u),
           perf.views,
           perf.direct_frames,
           perf.blit_frames,
           perf.max_frame_us / 1000u,
           perf.max_frame_us % 1000u);

    printf("[render_perf] avg_ms:");
    R_Perf_PrintAvg("frame", perf.frame_us, perf.frames);
    R_Perf_PrintAvg("display", perf.stage_us[R_PERF_STAGE_DISPLAY], perf.frames);
    R_Perf_PrintAvg("view", perf.stage_us[R_PERF_STAGE_VIEW], perf.views);
    R_Perf_PrintAvg("bsp", perf.stage_us[R_PERF_STAGE_BSP], perf.views);
    R_Perf_PrintAvg("planes", perf.stage_us[R_PERF_STAGE_PLANES], perf.views);
    R_Perf_PrintAvg("masked", perf.stage_us[R_PERF_STAGE_MASKED], perf.views);
    R_Perf_PrintAvg("present", perf.stage_us[R_PERF_STAGE_PRESENT], perf.frames);
    R_Perf_PrintAvg("gpu_wait", perf.stage_us[R_PERF_STAGE_GPU_WAIT], perf.frames);
    R_Perf_PrintAvg("vsync", perf.stage_us[R_PERF_STAGE_VSYNC_WAIT], perf.frames);
    R_Perf_PrintAvg("flip", perf.stage_us[R_PERF_STAGE_GPU_FLIP], perf.frames);
    R_Perf_PrintAvg("cache", perf.stage_us[R_PERF_STAGE_CACHE], perf.frames);
    R_Perf_PrintAvg("blit", perf.stage_us[R_PERF_STAGE_BLIT], perf.frames);
    printf("\n");

    printf("[render_perf] draws:");
    R_Perf_PrintRate("gpu_col", perf.gpu_columns, elapsed_us);
    R_Perf_PrintRate("gpu_col_px", perf.gpu_column_pixels, elapsed_us);
    R_Perf_PrintRate("gpu_colgrp", perf.gpu_column_batches, elapsed_us);
    if (perf.gpu_column_batches != 0)
    {
        uint64_t lane_avg10 =
            (perf.gpu_column_batch_lanes * 10ull) / perf.gpu_column_batches;
        printf(" gpu_colgrp_lanes=%llu.%llu",
               (unsigned long long)(lane_avg10 / 10u),
               (unsigned long long)(lane_avg10 % 10u));
    }
    R_Perf_PrintRate("gpu_span", perf.gpu_spans, elapsed_us);
    R_Perf_PrintRate("gpu_span_px", perf.gpu_span_pixels, elapsed_us);
    R_Perf_PrintRate("cpu_col", perf.cpu_columns, elapsed_us);
    R_Perf_PrintRate("cpu_col_px", perf.cpu_column_pixels, elapsed_us);
    R_Perf_PrintRate("cpu_span", perf.cpu_spans, elapsed_us);
    R_Perf_PrintRate("cpu_span_px", perf.cpu_span_pixels, elapsed_us);
    printf("\n");

    printf("[render_perf] gpu: finish=%u prepare_cpu=%u dma_wait=%llu dma_spin=%llu ring_wait=%llu ring_spin=%llu min_ring=%u ring_free=%u status=%08x\n",
           perf.gpu_finishes,
           perf.prepare_cpu_calls,
           (unsigned long long)perf.dma_waits,
           (unsigned long long)perf.dma_spin_iters,
           (unsigned long long)perf.ring_waits,
           (unsigned long long)perf.ring_spin_iters,
           perf.min_ring_free == 0xffffffffu ? 0 : perf.min_ring_free,
           perf.ring_free,
           perf.gpu_status);

    printf("[render_perf] gpu_time:");
    R_Perf_PrintTotalMS("dma_wait", perf.dma_wait_us);
    R_Perf_PrintTotalMS("ring_wait", perf.ring_wait_us);
    R_Perf_PrintTotalMS("cmd_flush", perf.cmd_flush_us);
    printf(" cmd_flushes=%llu",
           (unsigned long long)perf.cmd_flushes);
    R_Perf_PrintRate("cmd_words", perf.cmd_words, elapsed_us);
    R_Perf_PrintRate("cmd_flush_words", perf.cmd_flush_words, elapsed_us);
    printf("\n");

#if R_RENDER_PERF_DETAIL
    if (r_perf_summary_enabled)
    {
        printf("[render_perf] scene_work:");
        R_Perf_PrintAvgCount("nodes/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_NODE],
                             perf.views);
        R_Perf_PrintAvgCount("subsec/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_SUBSECTOR],
                             perf.views);
        R_Perf_PrintAvgCount("bbox/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_CHECK_BBOX],
                             perf.views);
        R_Perf_PrintAvgCount("addline/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_ADD_LINE],
                             perf.views);
        R_Perf_PrintAvgCount("storewall/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_STORE_WALL],
                             perf.views);
        R_Perf_PrintAvgCount("segloop/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_SEG_LOOP],
                             perf.views);
        R_Perf_PrintAvgCount("mapplane/view",
                             r_perf_detail_count[R_PERF_DETAIL_PLANE_MAP],
                             perf.views);
        printf("\n");

        printf("[render_perf] scene_mix:");
        R_Perf_PrintAvgCount("front_bbox/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_FRONT_BBOX],
                             perf.views);
        R_Perf_PrintAvgCount("front_culled/view",
                             r_perf_detail_count[R_PERF_DETAIL_BSP_FRONT_CULLED],
                             perf.views);
        R_Perf_PrintAvgCount("wall_tex/view",
                             r_perf_detail_count[R_PERF_DETAIL_WALL_TEXTURED],
                             perf.views);
        R_Perf_PrintAvgCount("wall_plane/view",
                             r_perf_detail_count[R_PERF_DETAIL_WALL_PLANE],
                             perf.views);
        R_Perf_PrintAvgCount("wall_sil/view",
                             r_perf_detail_count[R_PERF_DETAIL_WALL_SILHOUETTE],
                             perf.views);
        R_Perf_PrintAvgCount("wall_skip/view",
                             r_perf_detail_count[R_PERF_DETAIL_WALL_SKIPPED],
                             perf.views);
        R_Perf_PrintAvgCount("span_px/span",
                             perf.gpu_span_pixels,
                             perf.gpu_spans);
        R_Perf_PrintAvgCount("col_px/col",
                             perf.gpu_column_pixels,
                             perf.gpu_columns);
        printf("\n");
    }

    if (r_perf_detail_enabled)
    {
        printf("[render_perf] bsp_detail_avg_ms:");
        R_Perf_PrintAvg("bbox", perf.detail_us[R_PERF_DETAIL_BSP_CHECK_BBOX], perf.views);
        R_Perf_PrintAvg("subsec", perf.detail_us[R_PERF_DETAIL_BSP_SUBSECTOR], perf.views);
        R_Perf_PrintAvg("addline", perf.detail_us[R_PERF_DETAIL_BSP_ADD_LINE], perf.views);
        R_Perf_PrintAvg("storewall", perf.detail_us[R_PERF_DETAIL_BSP_STORE_WALL], perf.views);
        R_Perf_PrintAvg("segloop", perf.detail_us[R_PERF_DETAIL_BSP_SEG_LOOP], perf.views);
        R_Perf_PrintAvg("findplane", perf.detail_us[R_PERF_DETAIL_BSP_FIND_PLANE], perf.views);
        R_Perf_PrintAvg("checkplane", perf.detail_us[R_PERF_DETAIL_BSP_CHECK_PLANE], perf.views);
        R_Perf_PrintAvg("addsprites", perf.detail_us[R_PERF_DETAIL_BSP_ADD_SPRITES], perf.views);
        R_Perf_PrintAvg("mapplane", perf.detail_us[R_PERF_DETAIL_PLANE_MAP], perf.views);
        printf("\n");

        printf("[render_perf] bsp_detail_counts:");
        R_Perf_PrintRate("nodes", r_perf_detail_count[R_PERF_DETAIL_BSP_NODE], elapsed_us);
        R_Perf_PrintRate("subsec", r_perf_detail_count[R_PERF_DETAIL_BSP_SUBSECTOR], elapsed_us);
        R_Perf_PrintRate("bbox", r_perf_detail_count[R_PERF_DETAIL_BSP_CHECK_BBOX], elapsed_us);
        R_Perf_PrintRate("addline", r_perf_detail_count[R_PERF_DETAIL_BSP_ADD_LINE], elapsed_us);
        R_Perf_PrintRate("storewall", r_perf_detail_count[R_PERF_DETAIL_BSP_STORE_WALL], elapsed_us);
        R_Perf_PrintRate("segloop", r_perf_detail_count[R_PERF_DETAIL_BSP_SEG_LOOP], elapsed_us);
        R_Perf_PrintRate("findplane", r_perf_detail_count[R_PERF_DETAIL_BSP_FIND_PLANE], elapsed_us);
        R_Perf_PrintRate("checkplane", r_perf_detail_count[R_PERF_DETAIL_BSP_CHECK_PLANE], elapsed_us);
        R_Perf_PrintRate("addsprites", r_perf_detail_count[R_PERF_DETAIL_BSP_ADD_SPRITES], elapsed_us);
        R_Perf_PrintRate("mapplane", r_perf_detail_count[R_PERF_DETAIL_PLANE_MAP], elapsed_us);
        printf("\n");
    }
#endif

    R_Perf_Reset(now_us);
}

void R_Perf_FrameStart(void)
{
    unsigned int now = R_Perf_NowUS();

    R_Perf_CheckOptions();
    if (!r_perf_enabled)
        return;

    if (perf.interval_start_us == 0)
        R_Perf_Reset(now);

    perf.frame_start_us = now;
    memset(perf.current_stage_us, 0, sizeof(perf.current_stage_us));
    perf.frame_active = 1;
    perf.frames++;
}

void R_Perf_FrameCancel(void)
{
    if (!r_perf_enabled)
        return;
    if (!perf.frame_active)
        return;

    if (perf.frames > 0)
        perf.frames--;
    perf.frame_active = 0;
}

void R_Perf_FrameEnd(void)
{
    unsigned int now = R_Perf_NowUS();

    if (!r_perf_enabled)
        return;

    if (perf.frame_active)
    {
        unsigned int frame_us = now - perf.frame_start_us;
        perf.frame_us += frame_us;
        if (frame_us > perf.max_frame_us)
            perf.max_frame_us = frame_us;
        perf.frame_active = 0;
    }

    if ((unsigned int)(now - perf.interval_start_us) >= R_PERF_REPORT_US)
        R_Perf_PrintAndReset(now);
}

void R_Perf_CountRenderedView(void)
{
    if (!r_perf_enabled)
        return;

    perf.views++;
}

void R_Perf_CountPresentedFrame(int direct_gpu)
{
    if (!r_perf_enabled)
        return;

    if (direct_gpu)
        perf.direct_frames++;
    else
        perf.blit_frames++;
}

uint64_t R_Perf_CurrentStageUS(r_perf_stage_t stage)
{
    if (!r_perf_enabled || (unsigned int)stage >= R_PERF_STAGE_COUNT)
        return 0;

    return perf.current_stage_us[stage];
}

void R_Perf_CountGpuColumn(unsigned int pixels)
{
    if (!r_perf_enabled)
        return;

    perf.gpu_columns++;
    perf.gpu_column_pixels += pixels;
}

void R_Perf_CountGpuColumns(unsigned int columns, unsigned int pixels)
{
    if (!r_perf_enabled)
        return;

    perf.gpu_columns += columns;
    perf.gpu_column_pixels += pixels;
}

void R_Perf_CountGpuColumnBatch(unsigned int lanes)
{
    if (!r_perf_enabled)
        return;

    perf.gpu_column_batches++;
    perf.gpu_column_batch_lanes += lanes;
}

void R_Perf_CountGpuSpan(unsigned int pixels)
{
    if (!r_perf_enabled)
        return;

    perf.gpu_spans++;
    perf.gpu_span_pixels += pixels;
}

void R_Perf_CountCpuColumn(unsigned int pixels)
{
    if (!r_perf_enabled)
        return;

    perf.cpu_columns++;
    perf.cpu_column_pixels += pixels;
}

void R_Perf_CountCpuSpan(unsigned int pixels)
{
    if (!r_perf_enabled)
        return;

    perf.cpu_spans++;
    perf.cpu_span_pixels += pixels;
}

void R_Perf_CountGpuFinish(void)
{
    if (!r_perf_enabled)
        return;

    perf.gpu_finishes++;
}

void R_Perf_CountPrepareCPU(void)
{
    if (!r_perf_enabled)
        return;

    perf.prepare_cpu_calls++;
}

void R_Perf_AddGpuDebug(uint32_t dma_waits, uint32_t dma_spin_iters,
                        uint32_t dma_wait_us,
                        uint32_t ring_waits, uint32_t ring_spin_iters,
                        uint32_t ring_wait_us,
                        uint32_t cmd_flushes, uint32_t cmd_flush_us,
                        uint32_t cmd_flush_words, uint32_t cmd_words,
                        uint32_t min_ring_free, uint32_t ring_free,
                        uint32_t status)
{
    if (!r_perf_enabled)
        return;

    perf.dma_waits += dma_waits;
    perf.dma_spin_iters += dma_spin_iters;
    perf.dma_wait_us += dma_wait_us;
    perf.ring_waits += ring_waits;
    perf.ring_spin_iters += ring_spin_iters;
    perf.ring_wait_us += ring_wait_us;
    perf.cmd_flushes += cmd_flushes;
    perf.cmd_flush_us += cmd_flush_us;
    perf.cmd_flush_words += cmd_flush_words;
    perf.cmd_words += cmd_words;
    if (min_ring_free < perf.min_ring_free)
        perf.min_ring_free = min_ring_free;
    perf.ring_free = ring_free;
    perf.gpu_status = status;
}

#endif

#if !R_RENDER_PERF

#define R_SLOW_REPORT_US 10000000u

typedef struct
{
    uint64_t current_stage_us[R_PERF_STAGE_COUNT];
    uint64_t worst_stage_us[R_PERF_STAGE_COUNT];
    unsigned int interval_start_us;
    unsigned int frame_start_us;
    uint32_t frames;
    uint32_t views;
    uint32_t direct_frames;
    uint32_t blit_frames;
    uint32_t worst_frame_us;
    uint32_t worst_views;
    int frame_active;
    int options_checked;
    int enabled;
    int spill_enabled;
} r_slow_counters_t;

static r_slow_counters_t slow;

static void R_Slow_CheckOptions(void)
{
    if (slow.options_checked)
        return;

#if R_RUNTIME_TRACES
    slow.enabled = M_CheckParm("-slowframetrace") > 0
                && M_CheckParm("-noslowframetrace") <= 0;
#else
    slow.enabled = 0;
#endif
    slow.spill_enabled = M_CheckParm("-spilltrace") > 0
                       && M_CheckParm("-nospilltrace") <= 0;
    slow.options_checked = 1;
}

static int R_Slow_StageTimingEnabled(void)
{
    R_Slow_CheckOptions();
    return slow.enabled || slow.spill_enabled;
}

static void R_Slow_Reset(unsigned int now_us)
{
    int options_checked = slow.options_checked;
    int enabled = slow.enabled;
    int spill_enabled = slow.spill_enabled;

    memset(&slow, 0, sizeof(slow));
    slow.interval_start_us = now_us;
    slow.options_checked = options_checked;
    slow.enabled = enabled;
    slow.spill_enabled = spill_enabled;
}

static void R_Slow_PrintMS(const char *name, uint64_t us)
{
    printf(" %s=%llu.%03llums", name,
           (unsigned long long)(us / 1000u),
           (unsigned long long)(us % 1000u));
}

static void R_Slow_PrintAndReset(unsigned int now_us)
{
    uint64_t hud_us = 0;

    if (slow.worst_frame_us != 0)
    {
        uint64_t display_us = slow.worst_stage_us[R_PERF_STAGE_DISPLAY];
        uint64_t view_us = slow.worst_stage_us[R_PERF_STAGE_VIEW];

        if (display_us > view_us)
            hud_us = display_us - view_us;

        printf("[slow] max=%u.%03ums frames=%u views=%u direct=%u blit=%u",
               slow.worst_frame_us / 1000u,
               slow.worst_frame_us % 1000u,
               slow.frames,
               slow.worst_views,
               slow.direct_frames,
               slow.blit_frames);
        R_Slow_PrintMS("disp", display_us);
        R_Slow_PrintMS("hud", hud_us);
        R_Slow_PrintMS("view", view_us);
        R_Slow_PrintMS("bsp", slow.worst_stage_us[R_PERF_STAGE_BSP]);
        R_Slow_PrintMS("pl", slow.worst_stage_us[R_PERF_STAGE_PLANES]);
        R_Slow_PrintMS("mask", slow.worst_stage_us[R_PERF_STAGE_MASKED]);
        R_Slow_PrintMS("pres", slow.worst_stage_us[R_PERF_STAGE_PRESENT]);
        R_Slow_PrintMS("gpuw", slow.worst_stage_us[R_PERF_STAGE_GPU_WAIT]);
        R_Slow_PrintMS("pace", slow.worst_stage_us[R_PERF_STAGE_VSYNC_WAIT]);
        R_Slow_PrintMS("flip", slow.worst_stage_us[R_PERF_STAGE_GPU_FLIP]);
        R_Slow_PrintMS("cache", slow.worst_stage_us[R_PERF_STAGE_CACHE]);
        R_Slow_PrintMS("blit", slow.worst_stage_us[R_PERF_STAGE_BLIT]);
        printf("\n");
    }

    R_Slow_Reset(now_us);
}

unsigned int R_Perf_NowUS(void)
{
    return of_time_us();
}

unsigned int R_Perf_BeginStage(void)
{
    if (!R_Slow_StageTimingEnabled())
        return 0;

    return of_time_us();
}

void R_Perf_AddStageUS(r_perf_stage_t stage, unsigned int elapsed_us)
{
    if (!R_Slow_StageTimingEnabled() || !slow.frame_active ||
        (unsigned int)stage >= R_PERF_STAGE_COUNT)
        return;

    slow.current_stage_us[stage] += elapsed_us;
}

void R_Perf_EndStage(r_perf_stage_t stage, unsigned int start_us)
{
    if (start_us == 0)
        return;

    R_Perf_AddStageUS(stage, of_time_us() - start_us);
}

void R_Perf_FrameStart(void)
{
    unsigned int now_us;

    if (!R_Slow_StageTimingEnabled())
        return;

    now_us = of_time_us();
    if (slow.interval_start_us == 0)
        R_Slow_Reset(now_us);

    memset(slow.current_stage_us, 0, sizeof(slow.current_stage_us));
    slow.frame_start_us = now_us;
    slow.views = 0;
    slow.frame_active = 1;
    if (slow.enabled)
        slow.frames++;
}

void R_Perf_FrameCancel(void)
{
    if (!R_Slow_StageTimingEnabled() || !slow.frame_active)
        return;

    if (slow.enabled && slow.frames > 0)
        slow.frames--;
    slow.frame_active = 0;
}

void R_Perf_FrameEnd(void)
{
    unsigned int now_us;

    if (!R_Slow_StageTimingEnabled())
        return;

    now_us = of_time_us();

    if (slow.enabled && slow.frame_active)
    {
        unsigned int frame_us = now_us - slow.frame_start_us;

        if (frame_us > slow.worst_frame_us)
        {
            slow.worst_frame_us = frame_us;
            slow.worst_views = slow.views;
            memcpy(slow.worst_stage_us, slow.current_stage_us,
                   sizeof(slow.worst_stage_us));
        }

        slow.frame_active = 0;
    }
    else if (slow.frame_active)
    {
        slow.frame_active = 0;
    }

    if (slow.enabled &&
        (unsigned int)(now_us - slow.interval_start_us) >=
        R_SLOW_REPORT_US)
        R_Slow_PrintAndReset(now_us);
}

void R_Perf_CountRenderedView(void)
{
    if (R_Slow_StageTimingEnabled() && slow.frame_active)
        slow.views++;
}

void R_Perf_CountPresentedFrame(int direct_gpu)
{
    if (!slow.enabled)
        return;

    if (direct_gpu)
        slow.direct_frames++;
    else
        slow.blit_frames++;
}

uint64_t R_Perf_CurrentStageUS(r_perf_stage_t stage)
{
    if (!R_Slow_StageTimingEnabled() || !slow.frame_active ||
        (unsigned int)stage >= R_PERF_STAGE_COUNT)
        return 0;

    return slow.current_stage_us[stage];
}

#endif

#define R_PACING_REPORT_US 10000000u
#define R_PACING_DEFAULT_PERIOD_US 16667u

typedef struct
{
    uint64_t mask_us;
    uint64_t emit_us;
    uint64_t span_submit_us;
    uint32_t sprites;
    uint32_t spans;
    uint32_t span_pixels;
    uint32_t cpu_columns;
    uint32_t cpu_pixels;
    uint32_t mask_pixels;
    uint32_t columns;
    uint32_t posts;
    uint32_t rows;
    uint32_t max_mask_us;
    uint32_t max_emit_us;
    uint32_t max_span_submit_us;
} r_fuzz_counters_t;

static r_fuzz_counters_t fuzz;
static int fuzz_options_checked;
static int fuzz_timing_enabled;

typedef struct
{
    uint64_t prepare_us;
    uint64_t pace_wait_us;
    uint64_t last_vblank_us;
    unsigned int interval_start_us;
    unsigned int frame_start_us;
    unsigned int current_frame_wait_us;
    uint32_t frames;
    uint32_t paced_frames;
    uint32_t over_budget_frames;
    uint32_t over_budget_vsyncs;
    uint32_t max_prepare_us;
    uint32_t max_wait_us;
    uint32_t max_over_budget_vsyncs;
    uint32_t refresh_period_us;
    uint32_t adaptive_target_us;
    uint32_t requested_vtotal;
    uint32_t vblank_count;
    uint32_t window_vblank_start;
    uint32_t window_present_start;
    int frame_active;
    int window_has_timing;
    int options_checked;
    int enabled;
} r_pacing_counters_t;

static r_pacing_counters_t pacing;

int R_Perf_FuzzTimingEnabled(void)
{
#if R_RUNTIME_TRACES
    if (!fuzz_options_checked)
    {
        fuzz_timing_enabled = M_CheckParm("-fuzztiming") > 0
                           || M_CheckParm("-fuzztrace") > 0;
        fuzz_options_checked = 1;
    }

    return fuzz_timing_enabled;
#else
    return 0;
#endif
}

static void R_Fuzz_Reset(void)
{
    memset(&fuzz, 0, sizeof(fuzz));
}

static void R_Fuzz_Print(void)
{
    uint64_t row_us;
    uint64_t avg_mask_us;
    uint64_t avg_row_us;
    uint64_t avg_submit_us;
    uint64_t avg_total_us;
    uint64_t spans_per_sprite10;
    uint64_t pixels_per_span10;

    if (!R_Perf_FuzzTimingEnabled() ||
        (fuzz.sprites == 0 && fuzz.spans == 0 && fuzz.cpu_columns == 0))
        return;

    row_us = fuzz.emit_us > fuzz.span_submit_us
           ? fuzz.emit_us - fuzz.span_submit_us
           : 0;

    avg_mask_us = fuzz.sprites ? fuzz.mask_us / fuzz.sprites : 0;
    avg_row_us = fuzz.sprites ? row_us / fuzz.sprites : 0;
    avg_submit_us = fuzz.sprites ? fuzz.span_submit_us / fuzz.sprites : 0;
    avg_total_us = fuzz.sprites
                 ? (fuzz.mask_us + fuzz.emit_us) / fuzz.sprites
                 : 0;
    spans_per_sprite10 = fuzz.sprites
                       ? ((uint64_t)fuzz.spans * 10u) / fuzz.sprites
                       : 0;
    pixels_per_span10 = fuzz.spans
                      ? ((uint64_t)fuzz.span_pixels * 10u) / fuzz.spans
                      : 0;

    printf("[fuzz] sprites=%u spans=%u sp/spr=%llu.%llu px/span=%llu.%llu cpu_col=%u cpu_px=%u mask_px=%u cols=%u posts=%u rows=%u avg_ms mask=%llu.%03llu row=%llu.%03llu submit=%llu.%03llu total=%llu.%03llu max_ms mask=%u.%03u emit=%u.%03u span=%u.%03u\n",
           fuzz.sprites,
           fuzz.spans,
           (unsigned long long)(spans_per_sprite10 / 10u),
           (unsigned long long)(spans_per_sprite10 % 10u),
           (unsigned long long)(pixels_per_span10 / 10u),
           (unsigned long long)(pixels_per_span10 % 10u),
           fuzz.cpu_columns,
           fuzz.cpu_pixels,
           fuzz.mask_pixels,
           fuzz.columns,
           fuzz.posts,
           fuzz.rows,
           (unsigned long long)(avg_mask_us / 1000u),
           (unsigned long long)(avg_mask_us % 1000u),
           (unsigned long long)(avg_row_us / 1000u),
           (unsigned long long)(avg_row_us % 1000u),
           (unsigned long long)(avg_submit_us / 1000u),
           (unsigned long long)(avg_submit_us % 1000u),
           (unsigned long long)(avg_total_us / 1000u),
           (unsigned long long)(avg_total_us % 1000u),
           fuzz.max_mask_us / 1000u,
           fuzz.max_mask_us % 1000u,
           fuzz.max_emit_us / 1000u,
           fuzz.max_emit_us % 1000u,
           fuzz.max_span_submit_us / 1000u,
           fuzz.max_span_submit_us % 1000u);
}

static void R_Pacing_CheckOptions(void)
{
    if (pacing.options_checked)
        return;

#if R_RUNTIME_TRACES
    pacing.enabled = M_CheckParm("-pacingtrace") > 0
                  && M_CheckParm("-nopacingtrace") <= 0;
#else
    pacing.enabled = 0;
#endif
    pacing.refresh_period_us = R_PACING_DEFAULT_PERIOD_US;
    pacing.options_checked = 1;
}

static void R_Pacing_UpdateRefreshPeriod(const of_video_timing_t *timing)
{
    if (timing->vblank_count == 0 || timing->last_vblank_us == 0)
        return;

    if (pacing.vblank_count != 0 &&
        timing->vblank_count != pacing.vblank_count &&
        timing->last_vblank_us > pacing.last_vblank_us)
    {
        uint32_t delta_vblanks = timing->vblank_count - pacing.vblank_count;
        uint64_t delta_us = timing->last_vblank_us - pacing.last_vblank_us;

        if (delta_vblanks != 0)
        {
            uint32_t period_us = (uint32_t)(delta_us / delta_vblanks);

            if (period_us >= 10000u && period_us <= 25000u)
                pacing.refresh_period_us = period_us;
        }
    }

    pacing.vblank_count = timing->vblank_count;
    pacing.last_vblank_us = timing->last_vblank_us;
}

static void R_Pacing_Reset(unsigned int now_us)
{
    of_video_timing_t timing;
    uint32_t refresh_period_us = pacing.refresh_period_us;
    uint32_t adaptive_target_us = pacing.adaptive_target_us;
    uint32_t requested_vtotal = pacing.requested_vtotal;
    int options_checked = pacing.options_checked;
    int enabled = pacing.enabled;

    of_video_get_timing(&timing);

    memset(&pacing, 0, sizeof(pacing));
    R_Fuzz_Reset();
    pacing.interval_start_us = now_us;
    pacing.refresh_period_us =
        refresh_period_us ? refresh_period_us : R_PACING_DEFAULT_PERIOD_US;
    pacing.adaptive_target_us = adaptive_target_us;
    pacing.requested_vtotal = requested_vtotal;
    pacing.vblank_count = timing.vblank_count;
    pacing.last_vblank_us = timing.last_vblank_us;
    pacing.window_vblank_start = timing.vblank_count;
    pacing.window_present_start = timing.present_count;
    pacing.window_has_timing =
        timing.vblank_count != 0 || timing.present_count != 0;
    pacing.options_checked = options_checked;
    pacing.enabled = enabled;
}

static void R_Pacing_PrintAndReset(unsigned int now_us)
{
    of_video_timing_t timing;
    unsigned int elapsed_us = now_us - pacing.interval_start_us;
    uint32_t frames = pacing.frames;
    uint32_t period_us = pacing.refresh_period_us;
    uint32_t vblank_slots = 0;
    uint32_t queue_gap_vsyncs = 0;
    uint64_t fps10;
    uint64_t avg_prepare_us;
    uint64_t avg_wait_us = 0;
    uint32_t refresh_hz100;
    uint32_t over_pct10;

    if (elapsed_us == 0)
        elapsed_us = 1;

    if (frames == 0)
    {
        R_Pacing_Reset(now_us);
        return;
    }

    if (period_us == 0)
        period_us = R_PACING_DEFAULT_PERIOD_US;

    of_video_get_timing(&timing);
    R_Pacing_UpdateRefreshPeriod(&timing);

    if (pacing.window_has_timing)
    {
        vblank_slots = timing.vblank_count - pacing.window_vblank_start;
        if (vblank_slots > frames)
            queue_gap_vsyncs = vblank_slots - frames;
    }

    if (vblank_slots != 0)
    {
        period_us = (uint32_t)(((uint64_t)elapsed_us +
                                (vblank_slots / 2u)) / vblank_slots);
        refresh_hz100 =
            (uint32_t)(((uint64_t)vblank_slots * 100000000ull) /
                       elapsed_us);
    }
    else
    {
        refresh_hz100 = (uint32_t)(100000000ull / period_us);
    }

    fps10 = ((uint64_t)frames * 10000000ull) / elapsed_us;
    avg_prepare_us = pacing.prepare_us / frames;
    if (pacing.paced_frames != 0)
        avg_wait_us = pacing.pace_wait_us / pacing.paced_frames;
    over_pct10 = (pacing.over_budget_frames * 1000u) / frames;

    printf("[pacing] q=%u fps=%llu.%llu vb=%u gap=%u hz=%u.%02u vt=%u prep=%llu.%03llums max=%u.%03ums tgt=%u.%03ums wait=%llu.%03llums/%u.%03ums over=%u.%u%%\n",
           frames,
           (unsigned long long)(fps10 / 10u),
           (unsigned long long)(fps10 % 10u),
           vblank_slots,
           queue_gap_vsyncs,
           refresh_hz100 / 100u,
           refresh_hz100 % 100u,
           pacing.requested_vtotal,
           (unsigned long long)(avg_prepare_us / 1000u),
           (unsigned long long)(avg_prepare_us % 1000u),
           pacing.max_prepare_us / 1000u,
           pacing.max_prepare_us % 1000u,
           pacing.adaptive_target_us / 1000u,
           pacing.adaptive_target_us % 1000u,
           (unsigned long long)(avg_wait_us / 1000u),
           (unsigned long long)(avg_wait_us % 1000u),
           pacing.max_wait_us / 1000u,
           pacing.max_wait_us % 1000u,
           over_pct10 / 10u,
           over_pct10 % 10u);

    R_Fuzz_Print();
    R_Pacing_Reset(now_us);
}

void R_Perf_PacingFrameStart(void)
{
    of_video_timing_t timing;
    unsigned int now_us;

    R_Pacing_CheckOptions();

    now_us = of_time_us();

    if (!pacing.enabled)
    {
        pacing.frame_start_us = now_us;
        pacing.current_frame_wait_us = 0;
        pacing.frame_active = 1;
        return;
    }

    if (pacing.interval_start_us == 0)
        R_Pacing_Reset(now_us);

    of_video_get_timing(&timing);
    R_Pacing_UpdateRefreshPeriod(&timing);

    pacing.frame_start_us = now_us;
    pacing.current_frame_wait_us = 0;
    pacing.frame_active = 1;
}

void R_Perf_PacingFrameCancel(void)
{
    pacing.frame_active = 0;
    pacing.current_frame_wait_us = 0;
}

unsigned int R_Perf_PacingCurrentPrepareUS(void)
{
    unsigned int elapsed_us;

    if (!pacing.frame_active)
        return 0;

    elapsed_us = of_time_us() - pacing.frame_start_us;
    if (elapsed_us <= pacing.current_frame_wait_us)
        return 0;

    return elapsed_us - pacing.current_frame_wait_us;
}

void R_Perf_PacingAddWait(unsigned int wait_us)
{
    if (!pacing.frame_active || wait_us == 0)
        return;

    R_Perf_AddStageUS(R_PERF_STAGE_VSYNC_WAIT, wait_us);
    pacing.current_frame_wait_us += wait_us;

    if (!pacing.enabled)
        return;

    pacing.paced_frames++;
    pacing.pace_wait_us += wait_us;
    if (wait_us > pacing.max_wait_us)
        pacing.max_wait_us = wait_us;
}

void R_Perf_PacingSetTargetUS(unsigned int target_us)
{
    R_Pacing_CheckOptions();
    if (!pacing.enabled)
        return;

    pacing.adaptive_target_us = target_us;
}

void R_Perf_PacingSetVTotal(unsigned int vtotal)
{
    R_Pacing_CheckOptions();
    if (!pacing.enabled)
        return;

    pacing.requested_vtotal = vtotal;
}

void R_Perf_PacingFrameQueued(void)
{
    of_video_timing_t timing;
    unsigned int now_us;
    unsigned int prepare_us;
    uint32_t period_us;
    uint32_t over_budget_vsyncs = 0;

    if (!pacing.frame_active)
        return;

    now_us = of_time_us();
    prepare_us = now_us - pacing.frame_start_us;
    if (prepare_us > pacing.current_frame_wait_us)
        prepare_us -= pacing.current_frame_wait_us;
    else
        prepare_us = 0;

    if (!pacing.enabled)
    {
        pacing.frame_active = 0;
        pacing.current_frame_wait_us = 0;
        return;
    }

    of_video_get_timing(&timing);
    R_Pacing_UpdateRefreshPeriod(&timing);

    period_us = pacing.refresh_period_us;
    if (period_us == 0)
        period_us = R_PACING_DEFAULT_PERIOD_US;

    if (prepare_us > period_us)
        over_budget_vsyncs = ((prepare_us + period_us - 1u) / period_us) - 1u;

    pacing.prepare_us += prepare_us;
    pacing.frames++;

    if (prepare_us > pacing.max_prepare_us)
        pacing.max_prepare_us = prepare_us;

    if (over_budget_vsyncs != 0)
    {
        pacing.over_budget_frames++;
        pacing.over_budget_vsyncs += over_budget_vsyncs;
        if (over_budget_vsyncs > pacing.max_over_budget_vsyncs)
            pacing.max_over_budget_vsyncs = over_budget_vsyncs;
    }

    pacing.frame_active = 0;
    pacing.current_frame_wait_us = 0;

    if ((unsigned int)(now_us - pacing.interval_start_us) >=
        R_PACING_REPORT_US)
        R_Pacing_PrintAndReset(now_us);
}

void R_Perf_CountFuzzSprite(unsigned int mask_us,
                            unsigned int emit_us,
                            unsigned int mask_pixels,
                            unsigned int columns,
                            unsigned int posts,
                            unsigned int rows)
{
    if (!R_Perf_FuzzTimingEnabled())
        return;

    fuzz.sprites++;
    fuzz.mask_us += mask_us;
    fuzz.emit_us += emit_us;
    fuzz.mask_pixels += mask_pixels;
    fuzz.columns += columns;
    fuzz.posts += posts;
    fuzz.rows += rows;

    if (mask_us > fuzz.max_mask_us)
        fuzz.max_mask_us = mask_us;
    if (emit_us > fuzz.max_emit_us)
        fuzz.max_emit_us = emit_us;
}

void R_Perf_CountFuzzSpan(unsigned int submit_us, unsigned int pixels)
{
    if (!R_Perf_FuzzTimingEnabled())
        return;

    fuzz.spans++;
    fuzz.span_pixels += pixels;
    fuzz.span_submit_us += submit_us;

    if (submit_us > fuzz.max_span_submit_us)
        fuzz.max_span_submit_us = submit_us;
}

void R_Perf_CountFuzzCpuColumn(unsigned int pixels)
{
    if (!R_Perf_FuzzTimingEnabled())
        return;

    fuzz.cpu_columns++;
    fuzz.cpu_pixels += pixels;
}
