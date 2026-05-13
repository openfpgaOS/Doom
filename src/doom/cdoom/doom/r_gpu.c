//
// Doom renderer hooks for the openfpgaOS span GPU.
//
// The SDK GPU header owns static mutable ring state, so this is the only
// Doom translation unit that includes of_gpu.h.
//

#include "config.h"

#include "doomdef.h"
#include "i_video.h"
#include "m_argv.h"
#include "r_gpu.h"
#include "r_local.h"
#include "r_state.h"
#include "w_wad.h"

#include <stdint.h>
#include <stdio.h>

#ifdef OF_PC

int r_gpu_enabled = 0;

void R_GPU_Init(void) { }
void R_GPU_Shutdown(void) { }
void R_GPU_BeginDisplayFrame(void) { }
void R_GPU_BeginFrame(void) { }
void R_GPU_EndFrame(void) { }
void R_GPU_PrepareForCPUAccess(void) { }
void R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    (void)ptr;
    (void)size;
}
boolean R_GPU_PresentFrame(void) { return false; }
boolean R_GPU_UsingDirectFramebuffer(void) { return false; }
int R_GPU_CurrentDrawSlot(void) { return -1; }
boolean R_GPU_DrawColumn(void) { return false; }
boolean R_GPU_DrawSpan(void) { return false; }
boolean R_GPU_DeferLumpRelease(int lumpnum)
{
    (void)lumpnum;
    return false;
}

#else

#include "of_cache.h"
#include "of_caps.h"
#include "of_gpu.h"
#include "of_video.h"
#include "v_video.h"

int r_gpu_enabled = 1;

static int gpu_present;
static int gpu_frame_active;
static int gpu_pending;
static int gpu_cpu_dirty;
static int gpu_colormap_rows;
static int gpu_flip_enabled;
static int gpu_draw_idx;
static int gpu_display_frame_active;
static uint8_t *gpu_draw_fb;
static uint8_t *gpu_draw_render_base;
static uintptr_t gpu_framebuffer_delta;

#define GPU_DEFERRED_LUMPS 64
#define GPU_LETTERBOX_Y ((OF_SCREEN_H - SCREENHEIGHT) / 2)
static int gpu_deferred_lumps[GPU_DEFERRED_LUMPS];
static int gpu_deferred_lump_count;

static void gpu_release_deferred_lumps(void)
{
    for (int i = 0; i < gpu_deferred_lump_count; i++)
        W_ReleaseLumpNum(gpu_deferred_lumps[i]);
    gpu_deferred_lump_count = 0;
}

static void gpu_finish_pending(void)
{
    if (!gpu_pending)
        return;

    of_gpu_finish();
    gpu_pending = 0;

    /* GPU writes bypass the CPU cache.  Flush+invalidate gives any dirty
     * CPU-only lines a chance to land and drops stale lines for GPU-written
     * pixels before the CPU renders overlays or copies the screen out. */
    if (I_VideoBuffer != NULL && !gpu_flip_enabled)
        of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);

    gpu_release_deferred_lumps();
}

static void gpu_prepare_for_gpu_write(void)
{
    if (!gpu_frame_active || I_VideoBuffer == NULL)
        return;

    if (gpu_cpu_dirty)
    {
        if (!gpu_flip_enabled)
            of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
        gpu_cpu_dirty = 0;
    }
}

static int gpu_colormap_row(const lighttable_t *map)
{
    if (map == NULL || colormaps == NULL || gpu_colormap_rows <= 0)
        return -1;

    uintptr_t base = (uintptr_t)colormaps;
    uintptr_t p = (uintptr_t)map;
    uintptr_t end = base + (uintptr_t)gpu_colormap_rows * 256u;

    if (p < base || p >= end)
        return -1;

    uintptr_t off = p - base;
    if ((off & 0xffu) != 0)
        return -1;

    return (int)(off >> 8);
}

static uint32_t gpu_framebuffer_addr(pixel_t *ptr)
{
    if (gpu_flip_enabled && gpu_draw_render_base != NULL)
        return (uint32_t)((uintptr_t)ptr + gpu_framebuffer_delta);

    return (uint32_t)(uintptr_t)ptr;
}

static int32_t gpu_column_t_start(void)
{
    uint32_t y = (uint32_t)(dc_yl - centery);
    return (int32_t)((uint32_t)dc_texturemid + y * (uint32_t)dc_iscale);
}

void R_GPU_Init(void)
{
    gpu_present = 0;
    gpu_frame_active = 0;
    gpu_pending = 0;
    gpu_cpu_dirty = 0;
    gpu_colormap_rows = 0;
    gpu_flip_enabled = 0;
    gpu_draw_idx = -1;
    gpu_display_frame_active = 0;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;
    gpu_framebuffer_delta = 0;
    gpu_deferred_lump_count = 0;

    if (!r_gpu_enabled || M_CheckParm("-nogpu") > 0)
        return;

    const struct of_capabilities *caps = of_get_caps();
    if (caps == NULL || caps->gpu_base == 0 ||
        (caps->hw_features & OF_HW_GPU_SPAN) == 0)
    {
        printf("[doom_gpu] no span GPU advertised; using CPU renderer\n");
        return;
    }

    of_gpu_init();

    lumpindex_t lump = W_GetNumForName("COLORMAP");
    int cmap_size = W_LumpLength(lump);
    if (cmap_size > 64 * 256)
        cmap_size = 64 * 256;

    gpu_colormap_rows = cmap_size / 256;

    /* Make all already-loaded WAD/cache data visible to the GPU, then copy
     * Doom's palette remap rows into the fabric palookup table. */
    of_cache_flush();
    of_gpu_palookup_upload(0, colormaps, (uint32_t)cmap_size);
    GPU_TEX_FLUSH = 1;

    gpu_present = 1;

    if (M_CheckParm("-nogpuflip") <= 0)
    {
        gpu_draw_idx = of_video_acquire_next(-1, 0);
        gpu_flip_enabled = gpu_draw_idx >= 0;
    }

    printf("[doom_gpu] span renderer enabled (%d colormap rows%s)\n",
           gpu_colormap_rows,
           gpu_flip_enabled ? ", GPU_FLIP" : "");
}

void R_GPU_Shutdown(void)
{
    if (!gpu_present)
        return;

    if (gpu_flip_enabled)
        of_video_wait_flip();
    gpu_finish_pending();
    of_gpu_shutdown();
    gpu_flip_enabled = 0;
    gpu_draw_idx = -1;
    gpu_display_frame_active = 0;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;
    gpu_framebuffer_delta = 0;
    gpu_present = 0;
}

boolean R_GPU_UsingDirectFramebuffer(void)
{
    return gpu_present && gpu_flip_enabled;
}

int R_GPU_CurrentDrawSlot(void)
{
    if (!gpu_present || !gpu_flip_enabled || !gpu_display_frame_active)
        return -1;

    return gpu_draw_idx;
}

void R_GPU_BeginDisplayFrame(void)
{
    if (!gpu_present || !gpu_flip_enabled || gpu_draw_idx < 0)
        return;
    if (gpu_display_frame_active)
        return;

    gpu_finish_pending();

    gpu_draw_fb = of_video_buffer_addr(gpu_draw_idx);
    if (gpu_draw_fb == NULL)
    {
        gpu_flip_enabled = 0;
        return;
    }

    gpu_draw_render_base = gpu_draw_fb + GPU_LETTERBOX_Y * OF_SCREEN_W;
    pixel_t *video_buffer = (pixel_t *)of_uncached(gpu_draw_render_base);
    gpu_framebuffer_delta = (uintptr_t)gpu_draw_render_base
                          - (uintptr_t)video_buffer;

    if (I_VideoBuffer != video_buffer)
    {
        I_VideoBuffer = video_buffer;
        R_RetargetBuffer();
    }

    V_RestoreBuffer();

    gpu_display_frame_active = 1;

    /* All three hardware buffers are cleared during video init.  Doom only
     * renders into the centered 320x200 window, so the bars stay black and
     * do not need per-frame GPU clears. */
}

void R_GPU_BeginFrame(void)
{
    if (!gpu_present || I_VideoBuffer == NULL)
        return;

    gpu_finish_pending();
    gpu_frame_active = 1;
    gpu_cpu_dirty = 0;

    /* In direct-FB mode CPU drawing uses the uncached alias of the
     * hardware draw buffer.  Otherwise, Doom is rendering into the stable
     * fallback buffer and GPU reads/writes need cache handoff. */
    if (!gpu_flip_enabled)
        of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
    of_gpu_set_framebuffer((uint32_t)(uintptr_t)
                           (gpu_flip_enabled && gpu_draw_render_base != NULL
                            ? gpu_draw_render_base
                            : (uint8_t *)I_VideoBuffer),
                           SCREENWIDTH);
}

void R_GPU_EndFrame(void)
{
    if (!gpu_present || !gpu_frame_active)
        return;

    gpu_finish_pending();
    gpu_frame_active = 0;
    gpu_cpu_dirty = 0;
    gpu_release_deferred_lumps();
}

void R_GPU_PrepareForCPUAccess(void)
{
    if (!gpu_present || !gpu_frame_active)
        return;

    gpu_finish_pending();
    gpu_cpu_dirty = 1;
}

void R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    if (!gpu_present || ptr == NULL || size == 0)
        return;

    /* Texture-cache invalidation is only safe while the GPU is idle.  This
     * path is cold: it runs when Doom loads a lump or builds a composite
     * texture, not for every already-cached texel fetch. */
    gpu_finish_pending();
    of_cache_flush_range(ptr, size);
    GPU_TEX_FLUSH = 1;
}

boolean R_GPU_PresentFrame(void)
{
    if (!gpu_present || !gpu_flip_enabled || I_VideoBuffer == NULL ||
        gpu_draw_idx < 0 || !gpu_display_frame_active)
        return false;

    /* The previous display swap and current GPU spans are independent.
     * Wait for scanout availability first so current-frame GPU work can
     * overlap that wait, then drain before queuing this frame's flip. */
    of_video_wait_flip();
    gpu_finish_pending();

    uint32_t token = of_gpu_flip_to(gpu_draw_idx);
    of_gpu_kick();

    gpu_draw_idx = of_video_acquire_next(gpu_draw_idx, token);
    if (gpu_draw_idx < 0)
        gpu_flip_enabled = 0;

    gpu_display_frame_active = 0;
    gpu_frame_active = 0;
    gpu_cpu_dirty = 0;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;

    return true;
}

boolean R_GPU_DrawColumn(void)
{
    int count = dc_yh - dc_yl + 1;
    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (count <= 0 || count > 4095 || dc_source == NULL)
        return false;

    int light = gpu_colormap_row(dc_colormap);
    if (light < 0 || light > 63)
        return false;

    gpu_prepare_for_gpu_write();

    of_gpu_span_t span;
    span.fb_addr = gpu_framebuffer_addr(ylookup[dc_yl] + columnofs[dc_x]);
    span.tex_addr = (uint32_t)(uintptr_t)dc_source;
    span.s = 0;
    span.t = gpu_column_t_start();
    span.sstep = 0;
    span.tstep = dc_iscale;
    span.count = (uint16_t)count;
    span.light = (uint8_t)light;
    span.flags = OF_GPU_SPAN_COLORMAP;
    span.colormap_id = 0;
    span.fb_stride = SCREENWIDTH;
    span.tex_width = 1;
    span.tex_w_mask = 0;
    span.tex_h_mask = 127;
    span.sdivz = span.tdivz = span.zi_persp = 0;
    span.sdivz_step = span.tdivz_step = span.zi_step = 0;

    of_gpu_draw_span(&span);
    gpu_pending = 1;
    return true;
}

boolean R_GPU_DrawSpan(void)
{
    int count = ds_x2 - ds_x1 + 1;
    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (count <= 0 || count > 4095 || ds_source == NULL)
        return false;

    int light = gpu_colormap_row(ds_colormap);
    if (light < 0 || light > 63)
        return false;

    gpu_prepare_for_gpu_write();

    of_gpu_span_t span;
    span.fb_addr = gpu_framebuffer_addr(ylookup[ds_y] + columnofs[ds_x1]);
    span.tex_addr = (uint32_t)(uintptr_t)ds_source;
    span.s = ds_xfrac;
    span.t = ds_yfrac;
    span.sstep = ds_xstep;
    span.tstep = ds_ystep;
    span.count = (uint16_t)count;
    span.light = (uint8_t)light;
    span.flags = OF_GPU_SPAN_COLORMAP;
    span.colormap_id = 0;
    span.fb_stride = 1;
    span.tex_width = 64;
    span.tex_w_mask = 63;
    span.tex_h_mask = 63;
    span.sdivz = span.tdivz = span.zi_persp = 0;
    span.sdivz_step = span.tdivz_step = span.zi_step = 0;

    of_gpu_draw_span(&span);
    gpu_pending = 1;
    return true;
}

boolean R_GPU_DeferLumpRelease(int lumpnum)
{
    if (!gpu_present || !gpu_frame_active || !gpu_pending)
        return false;

    if (gpu_deferred_lump_count == GPU_DEFERRED_LUMPS)
    {
        gpu_finish_pending();
        return false;
    }

    gpu_deferred_lumps[gpu_deferred_lump_count++] = lumpnum;
    return true;
}

#endif
