//
// Doom renderer performance counters for openfpgaOS.
//

#ifndef __R_PERF__
#define __R_PERF__

#include <stdint.h>

#ifndef R_RENDER_PERF
#define R_RENDER_PERF 0
#endif

#ifndef R_RUNTIME_TRACES
#define R_RUNTIME_TRACES 0
#endif

#ifndef R_RENDER_PERF_DETAIL
#define R_RENDER_PERF_DETAIL 0
#endif

#ifndef R_RENDER_PERF_DETAIL_TIMING
#define R_RENDER_PERF_DETAIL_TIMING 0
#endif

/*
 * Instrumentation modes:
 *
 * R_RENDER_PERF enables the 5s render pipeline summary by default; use
 * -noperf to suppress it in a diagnostic build.
 * R_RENDER_PERF_DETAIL additionally enables hot-loop BSP/plane/masked work
 * counters.  R_RENDER_PERF_DETAIL_TIMING adds microsecond timing around
 * those hot probes; keep that separate because these probes live in
 * OF_FASTTEXT render functions and can pressure the small BRAM budget.
 *
 * R_RUNTIME_TRACES enables optional UART-oriented trace summaries selected
 * at runtime with -pacingtrace, -fuzztrace/-fuzztiming, or -slowframetrace.
 *
 * Default builds keep both off.  The pacing frame-time helpers below are
 * still present because adaptive presentation uses them as state, not as
 * logging.
 */

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

typedef enum
{
    R_PERF_DETAIL_BSP_NODE,
    R_PERF_DETAIL_BSP_SUBSECTOR,
    R_PERF_DETAIL_BSP_CHECK_BBOX,
    R_PERF_DETAIL_BSP_ADD_LINE,
    R_PERF_DETAIL_BSP_STORE_WALL,
    R_PERF_DETAIL_BSP_SEG_LOOP,
    R_PERF_DETAIL_BSP_FIND_PLANE,
    R_PERF_DETAIL_BSP_CHECK_PLANE,
    R_PERF_DETAIL_BSP_ADD_SPRITES,
    R_PERF_DETAIL_PLANE_MAP,
    R_PERF_DETAIL_BSP_FRONT_BBOX,
    R_PERF_DETAIL_BSP_FRONT_CULLED,
    R_PERF_DETAIL_WALL_TEXTURED,
    R_PERF_DETAIL_WALL_PLANE,
    R_PERF_DETAIL_WALL_SILHOUETTE,
    R_PERF_DETAIL_WALL_SKIPPED,
    R_PERF_DETAIL_COUNT
} r_perf_detail_t;

#if R_RENDER_PERF || R_RUNTIME_TRACES || defined(R_PERF_IMPLEMENTATION)
unsigned int R_Perf_NowUS(void);
unsigned int R_Perf_BeginStage(void);
void R_Perf_EndStage(r_perf_stage_t stage, unsigned int start_us);
void R_Perf_AddStageUS(r_perf_stage_t stage, unsigned int elapsed_us);
#else
static inline unsigned int R_Perf_NowUS(void)
{
    return 0;
}

static inline unsigned int R_Perf_BeginStage(void)
{
    return 0;
}

static inline void R_Perf_EndStage(r_perf_stage_t stage,
                                   unsigned int start_us)
{
    (void)stage;
    (void)start_us;
}

static inline void R_Perf_AddStageUS(r_perf_stage_t stage,
                                     unsigned int elapsed_us)
{
    (void)stage;
    (void)elapsed_us;
}
#endif

#if R_RENDER_PERF && R_RENDER_PERF_DETAIL

void R_Perf_CountDetail(r_perf_detail_t detail);
void R_Perf_EndDetail(r_perf_detail_t detail, unsigned int start_us);

extern int r_perf_summary_enabled;
extern int r_perf_detail_enabled;
extern uint32_t r_perf_detail_count[R_PERF_DETAIL_COUNT];

#if R_RENDER_PERF_DETAIL_TIMING
#define R_PERF_DETAIL_BEGIN() \
    (r_perf_detail_enabled ? R_Perf_BeginStage() : 0u)

#define R_PERF_DETAIL_END(detail, start_us) \
    do { \
        if (r_perf_detail_enabled) \
            R_Perf_EndDetail((detail), (start_us)); \
        else if (r_perf_summary_enabled) \
            R_Perf_CountDetail((detail)); \
    } while (0)
#else
#define R_PERF_DETAIL_BEGIN() 0u

#define R_PERF_DETAIL_END(detail, start_us) \
    do { \
        (void)(start_us); \
        r_perf_detail_count[(detail)]++; \
    } while (0)
#endif

#define R_PERF_DETAIL_COUNT(detail) \
    do { \
        r_perf_detail_count[(detail)]++; \
    } while (0)

#else

#define R_PERF_DETAIL_BEGIN() 0u
#define R_PERF_DETAIL_END(detail, start_us) \
    do { (void)(detail); (void)(start_us); } while (0)
#define R_PERF_DETAIL_COUNT(detail) \
    do { (void)(detail); } while (0)

#endif

#if R_RENDER_PERF

void R_Perf_CountGpuColumn(unsigned int pixels);
void R_Perf_CountGpuColumns(unsigned int columns, unsigned int pixels);
void R_Perf_CountGpuColumnBatch(unsigned int lanes);
void R_Perf_CountGpuSpan(unsigned int pixels);
void R_Perf_CountCpuColumn(unsigned int pixels);
void R_Perf_CountCpuSpan(unsigned int pixels);
void R_Perf_CountGpuFinish(void);
void R_Perf_CountPrepareCPU(void);
void R_Perf_AddGpuDebug(uint32_t dma_waits, uint32_t dma_spin_iters,
                        uint32_t dma_wait_us,
                        uint32_t ring_waits, uint32_t ring_spin_iters,
                        uint32_t ring_wait_us,
                        uint32_t cmd_flushes, uint32_t cmd_flush_us,
                        uint32_t cmd_flush_words, uint32_t cmd_words,
                        uint32_t min_ring_free, uint32_t ring_free,
                        uint32_t status);

#else

static inline void R_Perf_CountDetail(r_perf_detail_t detail)
{
    (void)detail;
}
static inline void R_Perf_EndDetail(r_perf_detail_t detail,
                                    unsigned int start_us)
{
    (void)detail;
    (void)start_us;
}
static inline void R_Perf_CountGpuColumn(unsigned int pixels)
{
    (void)pixels;
}
static inline void R_Perf_CountGpuColumns(unsigned int columns,
                                          unsigned int pixels)
{
    (void)columns;
    (void)pixels;
}
static inline void R_Perf_CountGpuColumnBatch(unsigned int lanes)
{
    (void)lanes;
}
static inline void R_Perf_CountGpuSpan(unsigned int pixels)
{
    (void)pixels;
}
static inline void R_Perf_CountCpuColumn(unsigned int pixels)
{
    (void)pixels;
}
static inline void R_Perf_CountCpuSpan(unsigned int pixels)
{
    (void)pixels;
}
static inline void R_Perf_CountGpuFinish(void) {}
static inline void R_Perf_CountPrepareCPU(void) {}
static inline void R_Perf_AddGpuDebug(uint32_t dma_waits,
                                      uint32_t dma_spin_iters,
                                      uint32_t dma_wait_us,
                                      uint32_t ring_waits,
                                      uint32_t ring_spin_iters,
                                      uint32_t ring_wait_us,
                                      uint32_t cmd_flushes,
                                      uint32_t cmd_flush_us,
                                      uint32_t cmd_flush_words,
                                      uint32_t cmd_words,
                                      uint32_t min_ring_free,
                                      uint32_t ring_free,
                                      uint32_t status)
{
    (void)dma_waits;
    (void)dma_spin_iters;
    (void)dma_wait_us;
    (void)ring_waits;
    (void)ring_spin_iters;
    (void)ring_wait_us;
    (void)cmd_flushes;
    (void)cmd_flush_us;
    (void)cmd_flush_words;
    (void)cmd_words;
    (void)min_ring_free;
    (void)ring_free;
    (void)status;
}

#endif

#if R_RENDER_PERF || R_RUNTIME_TRACES || defined(R_PERF_IMPLEMENTATION)
void R_Perf_FrameStart(void);
void R_Perf_FrameCancel(void);
void R_Perf_FrameEnd(void);
void R_Perf_CountRenderedView(void);
void R_Perf_CountPresentedFrame(int direct_gpu);
#else
static inline void R_Perf_FrameStart(void) {}
static inline void R_Perf_FrameCancel(void) {}
static inline void R_Perf_FrameEnd(void) {}
static inline void R_Perf_CountRenderedView(void) {}
static inline void R_Perf_CountPresentedFrame(int direct_gpu)
{
    (void)direct_gpu;
}
#endif

void R_Perf_PacingFrameStart(void);
void R_Perf_PacingFrameCancel(void);
unsigned int R_Perf_PacingCurrentPrepareUS(void);
void R_Perf_PacingAddWait(unsigned int wait_us);

#if R_RUNTIME_TRACES || defined(R_PERF_IMPLEMENTATION)
void R_Perf_PacingSetTargetUS(unsigned int target_us);
void R_Perf_PacingSetVTotal(unsigned int vtotal);
#else
static inline void R_Perf_PacingSetTargetUS(unsigned int target_us)
{
    (void)target_us;
}

static inline void R_Perf_PacingSetVTotal(unsigned int vtotal)
{
    (void)vtotal;
}
#endif

void R_Perf_PacingFrameQueued(void);

#if R_RUNTIME_TRACES || defined(R_PERF_IMPLEMENTATION)
void R_Perf_CountFuzzSprite(unsigned int mask_us,
                            unsigned int emit_us,
                            unsigned int mask_pixels,
                            unsigned int columns,
                            unsigned int posts,
                            unsigned int rows);
int R_Perf_FuzzTimingEnabled(void);
void R_Perf_CountFuzzSpan(unsigned int submit_us, unsigned int pixels);
void R_Perf_CountFuzzCpuColumn(unsigned int pixels);
#else
static inline void R_Perf_CountFuzzSprite(unsigned int mask_us,
                                          unsigned int emit_us,
                                          unsigned int mask_pixels,
                                          unsigned int columns,
                                          unsigned int posts,
                                          unsigned int rows)
{
    (void)mask_us;
    (void)emit_us;
    (void)mask_pixels;
    (void)columns;
    (void)posts;
    (void)rows;
}

static inline int R_Perf_FuzzTimingEnabled(void)
{
    return 0;
}

static inline void R_Perf_CountFuzzSpan(unsigned int submit_us,
                                        unsigned int pixels)
{
    (void)submit_us;
    (void)pixels;
}

static inline void R_Perf_CountFuzzCpuColumn(unsigned int pixels)
{
    (void)pixels;
}
#endif

#endif
