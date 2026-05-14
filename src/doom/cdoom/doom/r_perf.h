//
// Doom renderer performance counters for openfpgaOS.
//

#ifndef __R_PERF__
#define __R_PERF__

#include <stdint.h>

typedef enum
{
    R_PERF_STAGE_DISPLAY,
    R_PERF_STAGE_VIEW,
    R_PERF_STAGE_BSP,
    R_PERF_STAGE_PLANES,
    R_PERF_STAGE_MASKED,
    R_PERF_STAGE_PRESENT,
    R_PERF_STAGE_GPU_WAIT,
    R_PERF_STAGE_VSYNC_WAIT,
    R_PERF_STAGE_GPU_FLIP,
    R_PERF_STAGE_CACHE,
    R_PERF_STAGE_BLIT,
    R_PERF_STAGE_COUNT
} r_perf_stage_t;

unsigned int R_Perf_NowUS(void);
unsigned int R_Perf_BeginStage(void);
void R_Perf_EndStage(r_perf_stage_t stage, unsigned int start_us);
void R_Perf_AddStageUS(r_perf_stage_t stage, unsigned int elapsed_us);

void R_Perf_FrameStart(void);
void R_Perf_FrameCancel(void);
void R_Perf_FrameEnd(void);
void R_Perf_CountRenderedView(void);
void R_Perf_CountPresentedFrame(int direct_gpu);

void R_Perf_CountGpuColumn(unsigned int pixels);
void R_Perf_CountGpuColumnBatch(unsigned int lanes);
void R_Perf_CountGpuSpan(unsigned int pixels);
void R_Perf_CountCpuColumn(unsigned int pixels);
void R_Perf_CountCpuSpan(unsigned int pixels);
void R_Perf_CountGpuFinish(void);
void R_Perf_CountPrepareCPU(void);
void R_Perf_AddGpuDebug(uint32_t dma_waits, uint32_t dma_spin_iters,
                        uint32_t ring_waits, uint32_t ring_spin_iters,
                        uint32_t min_ring_free, uint32_t ring_free,
                        uint32_t status);

#endif
