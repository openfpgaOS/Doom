//
// Doom renderer performance counters for openfpgaOS.
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "of_timer.h"
#include "r_perf.h"

#define R_PERF_REPORT_US 5000000u

typedef struct
{
    uint64_t stage_us[R_PERF_STAGE_COUNT];
    uint64_t frame_us;
    uint32_t max_frame_us;
    uint32_t frames;
    uint32_t views;
    uint32_t direct_frames;
    uint32_t blit_frames;

    uint64_t gpu_columns;
    uint64_t gpu_column_pixels;
    uint64_t gpu_spans;
    uint64_t gpu_span_pixels;
    uint64_t cpu_columns;
    uint64_t cpu_column_pixels;
    uint64_t cpu_spans;
    uint64_t cpu_span_pixels;

    uint32_t gpu_finishes;
    uint32_t prepare_cpu_calls;
    uint64_t dma_waits;
    uint64_t dma_spin_iters;
    uint64_t ring_waits;
    uint64_t ring_spin_iters;
    uint32_t min_ring_free;
    uint32_t ring_free;
    uint32_t gpu_status;

    unsigned int interval_start_us;
    unsigned int frame_start_us;
    int frame_active;
} r_perf_counters_t;

static r_perf_counters_t perf;

static void R_Perf_Reset(unsigned int now_us)
{
    memset(&perf, 0, sizeof(perf));
    perf.interval_start_us = now_us;
    perf.min_ring_free = 0xffffffffu;
}

unsigned int R_Perf_NowUS(void)
{
    return of_time_us();
}

unsigned int R_Perf_BeginStage(void)
{
    return R_Perf_NowUS();
}

void R_Perf_AddStageUS(r_perf_stage_t stage, unsigned int elapsed_us)
{
    if ((unsigned int)stage >= R_PERF_STAGE_COUNT)
        return;

    perf.stage_us[stage] += elapsed_us;
}

void R_Perf_EndStage(r_perf_stage_t stage, unsigned int start_us)
{
    R_Perf_AddStageUS(stage, R_Perf_NowUS() - start_us);
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

    R_Perf_Reset(now_us);
}

void R_Perf_FrameStart(void)
{
    unsigned int now = R_Perf_NowUS();

    if (perf.interval_start_us == 0)
        R_Perf_Reset(now);

    perf.frame_start_us = now;
    perf.frame_active = 1;
    perf.frames++;
}

void R_Perf_FrameCancel(void)
{
    if (!perf.frame_active)
        return;

    if (perf.frames > 0)
        perf.frames--;
    perf.frame_active = 0;
}

void R_Perf_FrameEnd(void)
{
    unsigned int now = R_Perf_NowUS();

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
    perf.views++;
}

void R_Perf_CountPresentedFrame(int direct_gpu)
{
    if (direct_gpu)
        perf.direct_frames++;
    else
        perf.blit_frames++;
}

void R_Perf_CountGpuColumn(unsigned int pixels)
{
    perf.gpu_columns++;
    perf.gpu_column_pixels += pixels;
}

void R_Perf_CountGpuSpan(unsigned int pixels)
{
    perf.gpu_spans++;
    perf.gpu_span_pixels += pixels;
}

void R_Perf_CountCpuColumn(unsigned int pixels)
{
    perf.cpu_columns++;
    perf.cpu_column_pixels += pixels;
}

void R_Perf_CountCpuSpan(unsigned int pixels)
{
    perf.cpu_spans++;
    perf.cpu_span_pixels += pixels;
}

void R_Perf_CountGpuFinish(void)
{
    perf.gpu_finishes++;
}

void R_Perf_CountPrepareCPU(void)
{
    perf.prepare_cpu_calls++;
}

void R_Perf_AddGpuDebug(uint32_t dma_waits, uint32_t dma_spin_iters,
                        uint32_t ring_waits, uint32_t ring_spin_iters,
                        uint32_t min_ring_free, uint32_t ring_free,
                        uint32_t status)
{
    perf.dma_waits += dma_waits;
    perf.dma_spin_iters += dma_spin_iters;
    perf.ring_waits += ring_waits;
    perf.ring_spin_iters += ring_spin_iters;
    if (min_ring_free < perf.min_ring_free)
        perf.min_ring_free = min_ring_free;
    perf.ring_free = ring_free;
    perf.gpu_status = status;
}
