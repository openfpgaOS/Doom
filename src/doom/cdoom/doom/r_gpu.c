//
// Doom renderer hooks for the openfpgaOS span GPU. Doom submits through
// the SDK's high-level affine span-group helper; current openfpgaOS lowers
// those groups to the unified GPU_CMD_DRAW_PARAM_SPAN_LIST command.
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
#include "r_perf.h"
#include "r_state.h"
#include "w_wad.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef OF_PC

int r_gpu_enabled = 0;

void R_GPU_Init(void) { }
void R_GPU_Shutdown(void) { }
void R_GPU_BeginDisplayFrame(void) { }
void R_GPU_BeginFrame(void) { }
void R_GPU_EndFrame(void) { }
void R_GPU_PrepareForCPUAccess(void) { }
void R_GPU_PrepareForCPUAccessRect(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
void R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    (void)ptr;
    (void)size;
}
void R_GPU_TextureDataFlushAll(void) { }
boolean R_GPU_PresentFrame(void) { return false; }
boolean R_GPU_UsingDirectFramebuffer(void) { return false; }
int R_GPU_CurrentDrawSlot(void) { return -1; }
boolean R_GPU_DrawColumn(void) { return false; }
boolean R_GPU_DrawColumnDirect(int x, int yl, int yh, const byte *source,
                               int texturemid, int iscale,
                               const byte *colormap)
{
    (void)x;
    (void)yl;
    (void)yh;
    (void)source;
    (void)texturemid;
    (void)iscale;
    (void)colormap;
    return false;
}
boolean R_GPU_CanDrawFuzz(void) { return false; }
boolean R_GPU_BeginFuzzSpans(void) { return false; }
void R_GPU_EndFuzzSpans(void) { }
boolean R_GPU_DrawFuzzColumnDirect(int x, int yl, int yh)
{
    (void)x;
    (void)yl;
    (void)yh;
    return false;
}
int R_GPU_ColormapRow(const byte *map)
{
    (void)map;
    return -1;
}
boolean R_GPU_DrawColumnLightDirect(int x, int yl, int yh, const byte *source,
                                    int texturemid, int iscale, int light)
{
    (void)x;
    (void)yl;
    (void)yh;
    (void)source;
    (void)texturemid;
    (void)iscale;
    (void)light;
    return false;
}
boolean R_GPU_DrawColumnLightBatchDirect(int x, int yl, int yh, int lanes,
                                         const byte *const *source,
                                         const int32_t *t,
                                         const int32_t *tstep,
                                         const uint8_t *light)
{
    (void)x;
    (void)yl;
    (void)yh;
    (void)lanes;
    (void)source;
    (void)t;
    (void)tstep;
    (void)light;
    return false;
}
boolean R_GPU_DrawColumnLightVarBatchDirect(int x, int lanes,
                                            const int *yl,
                                            const int *yh,
                                            const byte *const *source,
                                            const int32_t *t,
                                            const int32_t *tstep,
                                            const uint8_t *light)
{
    (void)x;
    (void)lanes;
    (void)yl;
    (void)yh;
    (void)source;
    (void)t;
    (void)tstep;
    (void)light;
    return false;
}
boolean R_GPU_DrawSpan(void) { return false; }
boolean R_GPU_DrawSpanDirect(int y, int x1, int x2, const byte *source,
                             fixed_t xfrac, fixed_t yfrac,
                             fixed_t xstep, fixed_t ystep,
                             const byte *colormap)
{
    (void)y;
    (void)x1;
    (void)x2;
    (void)source;
    (void)xfrac;
    (void)yfrac;
    (void)xstep;
    (void)ystep;
    (void)colormap;
    return false;
}
boolean R_GPU_DrawSpanLightDirect(int y, int x1, int x2, const byte *source,
                                  fixed_t xfrac, fixed_t yfrac,
                                  fixed_t xstep, fixed_t ystep, int light)
{
    (void)y;
    (void)x1;
    (void)x2;
    (void)source;
    (void)xfrac;
    (void)yfrac;
    (void)xstep;
    (void)ystep;
    (void)light;
    return false;
}
boolean R_GPU_BeginPlaneSpans(const byte *source, fixed_t height_delta,
                              int fixed_light)
{
    (void)source;
    (void)height_delta;
    (void)fixed_light;
    return false;
}
boolean R_GPU_PlaneSpanLight(int y, int x1, int x2, int light)
{
    (void)y;
    (void)x1;
    (void)x2;
    (void)light;
    return false;
}
void R_GPU_EndPlaneSpans(void) { }
boolean R_GPU_WallSegBegin(int x1, int x2, fixed_t scale1, fixed_t scalestep,
                           fixed_t distance, fixed_t offset,
                           angle_t centerangle)
{
    (void)x1;
    (void)x2;
    (void)scale1;
    (void)scalestep;
    (void)distance;
    (void)offset;
    (void)centerangle;
    return false;
}
boolean R_GPU_WallTierBegin(int tier, const byte *tex2d, int tex_height,
                            int widthmask, fixed_t texturemid)
{
    (void)tier;
    (void)tex2d;
    (void)tex_height;
    (void)widthmask;
    (void)texturemid;
    return false;
}
boolean R_GPU_WallTierColumn(int tier, int x, int yl, int yh, fixed_t scale)
{
    (void)tier;
    (void)x;
    (void)yl;
    (void)yh;
    (void)scale;
    return false;
}
void R_GPU_WallTiersEnd(void) { }
boolean R_GPU_SpriteBegin(const byte *tex2d, int tex_height, int tex_width,
                          fixed_t texturemid, fixed_t iscale,
                          fixed_t startfrac, fixed_t xiscale, int x1,
                          int light, int cmap_slot)
{
    (void)cmap_slot;
    (void)tex2d;
    (void)tex_height;
    (void)tex_width;
    (void)texturemid;
    (void)iscale;
    (void)startfrac;
    (void)xiscale;
    (void)x1;
    (void)light;
    return false;
}
boolean R_GPU_SpritePost(int x, int yl, int yh)
{
    (void)x;
    (void)yl;
    (void)yh;
    return false;
}
void R_GPU_SpriteEnd(void) { }
void R_GPU_BeginCPUSprite(void) { }
void R_GPU_EndCPUSprite(void) { }
boolean R_GPU_MaskedBegin(const byte *blk, int tex_height, int widthmask,
                          fixed_t texturemid, int x1, int x2,
                          fixed_t scale1, fixed_t scalestep,
                          fixed_t distance, fixed_t offset,
                          unsigned int centerangle)
{
    (void)blk; (void)tex_height; (void)widthmask; (void)texturemid;
    (void)x1; (void)x2; (void)scale1; (void)scalestep;
    (void)distance; (void)offset; (void)centerangle;
    return false;
}
boolean R_GPU_MaskedPost(int x, int yl, int yh)
{
    (void)x; (void)yl; (void)yh;
    return false;
}
void R_GPU_MaskedEnd(void) { }
int R_GPU_TranslationSlot(const byte *translation)
{
    (void)translation;
    return -1;
}
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
static int gpu_acquire_pending;
static int gpu_acquire_idx;
static uint32_t gpu_acquire_token;
static int gpu_acquire_had_work;
static int gpu_display_frame_active;
static int gpu_framebuffer_cpu_ready;
static int gpu_write_prepared;
static int gpu_fuzz_batch_active;
static uint8_t *gpu_draw_fb;
static uint8_t *gpu_draw_render_base;
static uintptr_t gpu_framebuffer_delta;

#define GPU_DEFERRED_LUMPS 64
#define GPU_COLUMN_BATCH_LANES 16
#define GPU_AFFINE_BATCH_LANES OF_GPU_AFFINE_SPAN_GROUP_MAX_LANES
#define GPU_COLUMN_LIST_BATCH_LANES OF_GPU_COLUMN_LIST_MAX_LANES
#define GPU_FB_CACHE_LINE_BYTES 64u
#define GPU_FB_CACHE_LINES \
    ((SCREENWIDTH * SCREENHEIGHT + GPU_FB_CACHE_LINE_BYTES - 1u) \
     / GPU_FB_CACHE_LINE_BYTES)
#define GPU_FB_CACHE_WORDS ((GPU_FB_CACHE_LINES + 31u) / 32u)
static int gpu_deferred_lumps[GPU_DEFERRED_LUMPS];
static int gpu_deferred_lump_count;
static of_gpu_affine_span_group_t gpu_affine_batch;
static int gpu_affine_batch_count;
static int gpu_affine_batch_is_column;
/* CMD_DRAW_COLUMN_LIST (0x4C) batch: 5-word lane records for vertical
 * columns (walls/sprites/fuzz) — drops the always-0 s/sstep words of the
 * 0x48 affine path, ~28% less command traffic on the dominant draw type.
 * At most one of gpu_affine_batch / gpu_column_batch is non-empty at any
 * time so painter's-order between spans and columns is preserved. */
static of_gpu_column_list_group_t gpu_column_batch;
static int gpu_column_batch_count;
static int gpu_use_column_list;
/* Param-record visplane batch (ATTR_PERSP_Q29): planes built once per
 * visplane, records appended per span, one command per light band.
 * R_MakeSpans interleaves top-cursor and bottom-cursor rows — usually in
 * DIFFERENT zlight bands — so several bands stay open at once and flush
 * independently.  Spans within a visplane are pixel-disjoint, so bands
 * may be emitted in any order; everything flushes at EndPlaneSpans
 * before the next surface draws.  Only ever non-empty between
 * R_GPU_BeginPlaneSpans/EndPlaneSpans. */
#define GPU_PLANE_BANDS 4
#define GPU_PLANE_BAND_RECORDS 128
typedef struct {
    int light;               /* colormap row, -1 = unassigned */
    int count;
    of_gpu_param_span_record_t records[GPU_PLANE_BAND_RECORDS];
} gpu_plane_band_t;
static of_gpu_param_span_list_t gpu_plane_params;
static gpu_plane_band_t gpu_plane_bands[GPU_PLANE_BANDS];
static int gpu_plane_record_count;   /* total staged across all bands */
static int gpu_plane_band_rr;        /* round-robin eviction cursor */
static int gpu_plane_active;
static int gpu_plane_fixed_light;    /* fixedcolormap row, -1 = per-span */
static int gpu_use_param_span;
/* Param-record wall batch (ATTR_PERSP_Q29, AXIS_Y): per-seg shared zi and
 * texcol*zi planes, two tier accumulators (0 = mid/top, 1 = bottom), two
 * light bands each (wall light walks monotonically along x).  Only ever
 * non-empty between R_GPU_WallSegBegin/WallTiersEnd. */
#define GPU_WALL_TIERS 2
#define GPU_WALL_BANDS 2
#define GPU_WALL_BAND_RECORDS 128
typedef struct {
    int light;               /* colormap row, -1 = unassigned */
    int count;
    of_gpu_param_span_record_t records[GPU_WALL_BAND_RECORDS];
} gpu_wall_band_t;
typedef struct {
    int active;
    int rr;
    of_gpu_param_span_list_t params;
    gpu_wall_band_t bands[GPU_WALL_BANDS];
} gpu_wall_tier_t;
static gpu_wall_tier_t gpu_wall_tiers[GPU_WALL_TIERS];
static int gpu_wall_record_count;    /* total staged across tiers/bands */
static int gpu_wall_seg_valid;
static float gpu_wall_zi_org, gpu_wall_zi_du;     /* zi(x) plane */
static float gpu_wall_szi_org, gpu_wall_szi_du;   /* texcol*zi(x), un-rebased */
static float gpu_wall_texcol1;                    /* texcol at seg x1 (texels) */
static int gpu_use_wall_param;
/* Affine sprite batch (ATTR_AFFINE, AXIS_Y): single light per sprite,
 * one record per visible post.  Only non-empty between SpriteBegin/End. */
#define GPU_SPRITE_MAX_RECORDS 256
static of_gpu_param_span_list_t gpu_sprite_params;
static of_gpu_param_span_record_t gpu_sprite_records[GPU_SPRITE_MAX_RECORDS];
static int gpu_sprite_record_count;
static int gpu_sprite_active;
static int gpu_use_sprite_param;
static int gpu_masked_active;   /* param-masked range open */
static uint32_t gpu_fb_row_addr[SCREENHEIGHT];
static uint32_t gpu_cpu_dirty_lines[GPU_FB_CACHE_WORDS];
static uint32_t gpu_cpu_valid_lines[GPU_FB_CACHE_WORDS];
static uint8_t gpu_fuzz_source_tex[4] = { 0x80, 0x80, 0x80, 0x80 };
static uint8_t gpu_fuzz_transluc_table[256 * 256];
static uint8_t gpu_probe_fb[64] __attribute__((aligned(64)));
static uint8_t gpu_probe_tex[64] __attribute__((aligned(64)));

static void gpu_flush_affine_batch(void);
static void gpu_flush_column_batch(void);
static void gpu_flush_plane_batch(void);
static void gpu_flush_wall_batch(void);
static void gpu_flush_sprite_batch(void);
static void gpu_flush_draw_batches(void);
static void gpu_release_deferred_lumps(void);
static void gpu_prepare_for_gpu_write(void);

/* ================================================================
 * Translated columns on the GPU: compose translation->colormap into
 * spare palookup slots (slot 0 = plain colormaps).  A translated
 * sprite then rides the affine sprite surface with colormap_id=slot —
 * the palookup applies translation AND light in one lookup, matching
 * software's dc_colormap[dc_translation[texel]] exactly.
 * ================================================================ */
#define GPU_TRANSL_SLOT_BASE 1
#define GPU_TRANSL_MAX 3
static const byte *gpu_transl_tables[GPU_TRANSL_MAX];
static int gpu_transl_count;

static void gpu_upload_translated_palookup(int slot, const byte *transl)
{
    uint8_t *dst = &_gpu_palookup_storage[(uint32_t)slot
                                          * OF_GPU_PALOOKUP_STRIDE];
    int rows = gpu_colormap_rows;

    for (int r = 0; r < rows; r++)
        for (int i = 0; i < 256; i++)
            dst[r * 256 + i] = colormaps[r * 256 + transl[i]];
    if ((uint32_t)rows * 256u < OF_GPU_PALOOKUP_STRIDE)
        memset(dst + rows * 256, 0,
               OF_GPU_PALOOKUP_STRIDE - (uint32_t)rows * 256u);

    of_cache_flush_range(dst, OF_GPU_PALOOKUP_STRIDE);
}

static void gpu_register_translations(void)
{
    gpu_transl_count = 0;
    if (translationtables == NULL || gpu_colormap_rows <= 0)
        return;

    for (int i = 0; i < GPU_TRANSL_MAX
         && GPU_TRANSL_SLOT_BASE + i < OF_GPU_PALOOKUP_SLOTS; i++)
    {
        const byte *t = translationtables + i * 256;

        gpu_upload_translated_palookup(GPU_TRANSL_SLOT_BASE + i, t);
        gpu_transl_tables[i] = t;
        gpu_transl_count = i + 1;
    }
}

int R_GPU_TranslationSlot(const byte *translation)
{
    for (int i = 0; i < gpu_transl_count; i++)
        if (gpu_transl_tables[i] == translation)
            return GPU_TRANSL_SLOT_BASE + i;
    return -1;
}

static void gpu_set_framebuffer_base(uint8_t *base);

#define GPU_SVC_INDEX(field) \
    ((unsigned int)((offsetof(struct of_services_table, field) - \
                     offsetof(struct of_services_table, video_init)) / \
                    sizeof(void *)))

static int gpu_service_has_cache_flush_range(void)
{
    const struct of_services_table *svc = OF_SVC;
    unsigned int index = GPU_SVC_INDEX(cache_flush_range);

    return svc != NULL
        && svc->magic == OF_SVC_MAGIC
        && svc->count > index
        && svc->cache_flush_range != NULL;
}

static int gpu_has_pending_draw_batches(void)
{
    return gpu_affine_batch_count != 0 || gpu_column_batch_count != 0 ||
           gpu_plane_record_count != 0 || gpu_wall_record_count != 0 ||
           gpu_sprite_record_count != 0;
}

static int gpu_probe_affine_span_group(void)
{
    of_gpu_affine_span_group_t group;

    memset(gpu_probe_fb, 0, sizeof(gpu_probe_fb));
    memset(gpu_probe_tex, 0, sizeof(gpu_probe_tex));
    gpu_probe_tex[0] = 0x5a;

    of_cache_flush_range(gpu_probe_fb, sizeof(gpu_probe_fb));
    of_cache_flush_range(gpu_probe_tex, sizeof(gpu_probe_tex));
    GPU_TEX_FLUSH = 1;

    of_gpu_set_framebuffer((uint32_t)(uintptr_t)gpu_probe_fb, 8);

    memset(&group, 0, sizeof(group));
    group.lane_count = 1;
    group.tex_width = 1;
    group.fb_step = 1;
    group.fb_addr[0] = (uint32_t)(uintptr_t)gpu_probe_fb;
    group.tex_addr[0] = (uint32_t)(uintptr_t)gpu_probe_tex;
    group.count[0] = 1;
    group.colormap_id[0] = 0;

    of_gpu_draw_affine_span_group(&group);
    of_gpu_finish();
    of_cache_inval_range(gpu_probe_fb, sizeof(gpu_probe_fb));

    return gpu_probe_fb[0] == 0x5a;
}

/* Verify CMD_DRAW_COLUMN_LIST (0x4C) end-to-end before trusting the caps
 * bit: a bitstream that advertises OF_HW_GPU_COLUMN_LIST but predates the
 * decode path no-ops the command, which this catches (probe pixel stays 0). */
static int gpu_probe_column_list(void)
{
    of_gpu_column_list_group_t group;

    memset(gpu_probe_fb, 0, sizeof(gpu_probe_fb));
    memset(gpu_probe_tex, 0, sizeof(gpu_probe_tex));
    gpu_probe_tex[0] = 0xa5;

    of_cache_flush_range(gpu_probe_fb, sizeof(gpu_probe_fb));
    of_cache_flush_range(gpu_probe_tex, sizeof(gpu_probe_tex));
    GPU_TEX_FLUSH = 1;

    of_gpu_set_framebuffer((uint32_t)(uintptr_t)gpu_probe_fb, 8);

    memset(&group, 0, sizeof(group));
    group.lane_count = 1;
    group.tex_width = 1;
    group.fb_step = 1;
    group.fb_addr[0] = (uint32_t)(uintptr_t)gpu_probe_fb;
    group.tex_addr[0] = (uint32_t)(uintptr_t)gpu_probe_tex;
    group.count[0] = 1;
    group.colormap_id[0] = 0;

    of_gpu_draw_column_list(&group);
    of_gpu_finish();
    of_cache_inval_range(gpu_probe_fb, sizeof(gpu_probe_fb));

    return gpu_probe_fb[0] == 0xa5;
}

/* Verify the param-span PERSP_Q29 path end-to-end: constant planes
 * zi = 1.0, s/z = t/z = 0 resolve to texel (0,0) at the probe pixel. */
static int gpu_probe_param_span(void)
{
    of_gpu_param_span_list_t params;
    of_gpu_param_span_record_t record;

    memset(gpu_probe_fb, 0, sizeof(gpu_probe_fb));
    memset(gpu_probe_tex, 0, sizeof(gpu_probe_tex));
    gpu_probe_tex[0] = 0x3c;

    of_cache_flush_range(gpu_probe_fb, sizeof(gpu_probe_fb));
    of_cache_flush_range(gpu_probe_tex, sizeof(gpu_probe_tex));
    GPU_TEX_FLUSH = 1;

    of_gpu_set_framebuffer((uint32_t)(uintptr_t)gpu_probe_fb, 8);

    memset(&params, 0, sizeof(params));
    params.fb_base = (uint32_t)(uintptr_t)gpu_probe_fb;
    params.fb_major_step = 8;
    params.fb_minor_step = 1;
    params.tex_addr = (uint32_t)(uintptr_t)gpu_probe_tex;
    params.tex_width = 1;
    params.flags = OF_GPU_SPAN_PERSP;
    params.attr_mode = OF_GPU_PARAM_ATTR_PERSP_Q29;
    params.span_axis = OF_GPU_PARAM_AXIS_X;
    params.z_mode = OF_GPU_PARAM_Z_NONE;
    params.q29_attr_shift = 0;
    params.attr_origin[2] = (int32_t)(1u << 29);    /* zi = 1.0 in Q29 */

    record.u = 0;
    record.v = 0;
    record.count = 1;

    of_gpu_draw_param_span_list(&params, &record, 1);
    of_gpu_finish();
    of_cache_inval_range(gpu_probe_fb, sizeof(gpu_probe_fb));

    return gpu_probe_fb[0] == 0x3c;
}

/* Verify AXIS_Y record walking end-to-end: a 2-pixel vertical span must
 * step the FB by the row stride (fb_minor) AND step the attrs by dv —
 * texels 0 and 1 land on rows 0 and 1.  A core that mis-decodes the
 * axis writes the wrong address or repeats texel 0, failing the check. */
static int gpu_probe_param_axis_y(void)
{
    of_gpu_param_span_list_t params;
    of_gpu_param_span_record_t record;

    memset(gpu_probe_fb, 0, sizeof(gpu_probe_fb));
    memset(gpu_probe_tex, 0, sizeof(gpu_probe_tex));
    gpu_probe_tex[0] = 0x11;
    gpu_probe_tex[1] = 0x22;

    of_cache_flush_range(gpu_probe_fb, sizeof(gpu_probe_fb));
    of_cache_flush_range(gpu_probe_tex, sizeof(gpu_probe_tex));
    GPU_TEX_FLUSH = 1;

    of_gpu_set_framebuffer((uint32_t)(uintptr_t)gpu_probe_fb, 8);

    memset(&params, 0, sizeof(params));
    params.fb_base = (uint32_t)(uintptr_t)gpu_probe_fb;
    params.fb_major_step = 1;       /* AXIS_Y: per-column */
    params.fb_minor_step = 8;       /* per-pixel walk = row stride */
    params.tex_addr = (uint32_t)(uintptr_t)gpu_probe_tex;
    params.tex_width = 1;
    params.tex_w_mask = 127;
    params.flags = OF_GPU_SPAN_PERSP;
    params.attr_mode = OF_GPU_PARAM_ATTR_PERSP_Q29;
    params.span_axis = OF_GPU_PARAM_AXIS_Y;
    params.z_mode = OF_GPU_PARAM_Z_NONE;
    params.q29_attr_shift = 0;
    params.attr_dv[0] = (int32_t)(1u << 29);        /* vtex = y */
    params.attr_origin[2] = (int32_t)(1u << 29);    /* zi = 1.0 */

    record.u = 0;
    record.v = 0;
    record.count = 2;

    of_gpu_draw_param_span_list(&params, &record, 1);
    of_gpu_finish();
    of_cache_inval_range(gpu_probe_fb, sizeof(gpu_probe_fb));

    return gpu_probe_fb[0] == 0x11 && gpu_probe_fb[8] == 0x22;
}

static void gpu_line_set(uint32_t *bits, unsigned int line)
{
    bits[line >> 5] |= 1u << (line & 31u);
}

static int gpu_line_test(const uint32_t *bits, unsigned int line)
{
    return (bits[line >> 5] & (1u << (line & 31u))) != 0;
}

static void gpu_clear_line_bits(uint32_t *bits)
{
    memset(bits, 0, sizeof(gpu_cpu_dirty_lines));
}

static void gpu_reset_cpu_cache_tracking(void)
{
    gpu_clear_line_bits(gpu_cpu_dirty_lines);
    gpu_clear_line_bits(gpu_cpu_valid_lines);
    gpu_cpu_dirty = 0;
    gpu_write_prepared = 0;
}

static int gpu_clip_rect(int *x, int *y, int *w, int *h)
{
    int x2;
    int y2;

    if (*w <= 0 || *h <= 0)
        return 0;

    x2 = *x + *w;
    y2 = *y + *h;

    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
    if (x2 > SCREENWIDTH)
        x2 = SCREENWIDTH;
    if (y2 > SCREENHEIGHT)
        y2 = SCREENHEIGHT;

    *w = x2 - *x;
    *h = y2 - *y;

    return *w > 0 && *h > 0;
}

static void gpu_flush_line_run(unsigned int first, unsigned int last)
{
    uint8_t *ptr;
    uint32_t size;

    if (gpu_draw_render_base == NULL || first > last)
        return;

    ptr = gpu_draw_render_base + first * GPU_FB_CACHE_LINE_BYTES;
    size = (uint32_t)(last - first + 1u) * GPU_FB_CACHE_LINE_BYTES;
    of_cache_flush_range(ptr, size);
}

static void gpu_inval_line_run(unsigned int first, unsigned int last)
{
    uint8_t *ptr;
    uint32_t size;

    if (gpu_draw_render_base == NULL || first > last)
        return;

    ptr = gpu_draw_render_base + first * GPU_FB_CACHE_LINE_BYTES;
    size = (uint32_t)(last - first + 1u) * GPU_FB_CACHE_LINE_BYTES;
    of_cache_inval_range(ptr, size);
}

static void gpu_flush_cpu_dirty_lines(void)
{
    unsigned int cache_start;
    unsigned int line = 0;

    if (!gpu_cpu_dirty || gpu_draw_render_base == NULL)
        return;

    cache_start = R_Perf_BeginStage();

    while (line < GPU_FB_CACHE_LINES)
    {
        unsigned int first;

        while (line < GPU_FB_CACHE_LINES &&
               !gpu_line_test(gpu_cpu_dirty_lines, line))
            line++;

        if (line >= GPU_FB_CACHE_LINES)
            break;

        first = line;
        while (line < GPU_FB_CACHE_LINES &&
               gpu_line_test(gpu_cpu_dirty_lines, line))
            line++;

        gpu_flush_line_run(first, line - 1u);
    }

    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    gpu_clear_line_bits(gpu_cpu_dirty_lines);
    gpu_cpu_dirty = 0;
}

static void gpu_invalidate_rect_for_cpu(int x, int y, int w, int h)
{
    unsigned int cache_start;

    if (gpu_draw_render_base == NULL)
        return;
    if (!gpu_clip_rect(&x, &y, &w, &h))
        return;

    cache_start = R_Perf_BeginStage();

    for (int row = y; row < y + h; row++)
    {
        unsigned int row_off = (unsigned int)row * SCREENWIDTH;
        unsigned int first = (row_off + (unsigned int)x)
                           / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = (row_off + (unsigned int)x + (unsigned int)w - 1u)
                          / GPU_FB_CACHE_LINE_BYTES;
        unsigned int line = first;

        while (line <= last)
        {
            unsigned int run_first;

            while (line <= last &&
                   (gpu_line_test(gpu_cpu_dirty_lines, line) ||
                    gpu_line_test(gpu_cpu_valid_lines, line)))
                line++;

            if (line > last)
                break;

            run_first = line;
            while (line <= last &&
                   !gpu_line_test(gpu_cpu_dirty_lines, line) &&
                   !gpu_line_test(gpu_cpu_valid_lines, line))
            {
                gpu_line_set(gpu_cpu_valid_lines, line);
                line++;
            }

            gpu_inval_line_run(run_first, line - 1u);
        }
    }

    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
}

static void gpu_mark_cpu_dirty_rect(int x, int y, int w, int h)
{
    if (!gpu_clip_rect(&x, &y, &w, &h))
        return;

    for (int row = y; row < y + h; row++)
    {
        unsigned int row_off = (unsigned int)row * SCREENWIDTH;
        unsigned int first = (row_off + (unsigned int)x)
                           / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = (row_off + (unsigned int)x + (unsigned int)w - 1u)
                          / GPU_FB_CACHE_LINE_BYTES;

        for (unsigned int line = first; line <= last; line++)
        {
            gpu_line_set(gpu_cpu_dirty_lines, line);
            gpu_line_set(gpu_cpu_valid_lines, line);
        }
    }

    gpu_cpu_dirty = 1;
    gpu_write_prepared = 0;
}

static int gpu_rect_overlaps_view_rows(int x, int y, int w, int h)
{
    int view_top;
    int view_bottom;

    if (!gpu_clip_rect(&x, &y, &w, &h))
        return 0;

    view_top = viewwindowy;
    view_bottom = viewwindowy + viewheight;

    if (view_top < 0)
        view_top = 0;
    if (view_bottom > SCREENHEIGHT)
        view_bottom = SCREENHEIGHT;

    return y < view_bottom && y + h > view_top;
}

static int gpu_prepare_cpu_write_outside_view(int x, int y, int w, int h)
{
    if (!gpu_flip_enabled || !gpu_display_frame_active)
        return 0;

    if (gpu_rect_overlaps_view_rows(x, y, w, h))
        return 0;

    gpu_invalidate_rect_for_cpu(x, y, w, h);
    gpu_mark_cpu_dirty_rect(x, y, w, h);
    return 1;
}

static void gpu_record_debug_snapshot(void)
{
#if R_RENDER_PERF
    of_gpu_debug_snapshot_t snap;

    of_gpu_debug_snapshot(&snap, 1);
    R_Perf_AddGpuDebug(snap.dma_waits, snap.dma_spin_iters,
                       0,
                       snap.ring_waits, snap.ring_spin_iters,
                       0,
                       0, 0,
                       0, 0,
                       snap.min_ring_free, snap.ring_free,
                       snap.status);
#endif
}

static void gpu_mark_framebuffer_gpu_dirty(void)
{
    gpu_framebuffer_cpu_ready = 0;
}

static int gpu_prepare_framebuffer_for_cpu(void)
{
    unsigned int wait_start;

    gpu_flush_draw_batches();

    if (gpu_framebuffer_cpu_ready && !gpu_pending)
        return 0;

    wait_start = R_Perf_BeginStage();
    of_gpu_prepare_framebuffer_for_cpu();
    R_Perf_EndStage(R_PERF_STAGE_GPU_WAIT, wait_start);
    R_Perf_CountGpuFinish();
    gpu_record_debug_snapshot();
    gpu_pending = 0;
    gpu_framebuffer_cpu_ready = 1;
    gpu_release_deferred_lumps();
    return 1;
}

static void gpu_clear_pending_acquire(void)
{
    gpu_acquire_pending = 0;
    gpu_acquire_idx = -1;
    gpu_acquire_token = 0;
    gpu_acquire_had_work = 0;
}

static int gpu_acquire_draw_buffer(void)
{
    unsigned int wait_start;

    if (gpu_draw_idx >= 0)
        return 1;
    if (!gpu_acquire_pending)
        return 0;

    wait_start = R_Perf_BeginStage();
    gpu_draw_idx = of_video_acquire_next(gpu_acquire_idx,
                                         gpu_acquire_token);
    R_Perf_EndStage(R_PERF_STAGE_GPU_WAIT, wait_start);

    if (gpu_acquire_had_work)
        R_Perf_CountGpuFinish();
    gpu_record_debug_snapshot();
    gpu_pending = 0;
    gpu_framebuffer_cpu_ready = 1;
    gpu_write_prepared = 0;
    gpu_release_deferred_lumps();
    gpu_clear_pending_acquire();

    if (gpu_draw_idx < 0)
    {
        gpu_flip_enabled = 0;
        return 0;
    }

    return 1;
}

static void gpu_release_deferred_lumps(void)
{
    for (int i = 0; i < gpu_deferred_lump_count; i++)
        W_ReleaseLumpNum(gpu_deferred_lumps[i]);
    gpu_deferred_lump_count = 0;
}

static inline int __attribute__((always_inline))
gpu_can_batch_affine(uint8_t flags, uint16_t tex_width,
                     uint16_t tex_w_mask, uint16_t tex_h_mask,
                     int32_t fb_step, int is_column)
{
    if (gpu_affine_batch_count <= 0)
        return 1;
    if (gpu_affine_batch_count >= GPU_AFFINE_BATCH_LANES)
        return 0;

    return gpu_affine_batch.flags == flags &&
        gpu_affine_batch.tex_width == tex_width &&
        gpu_affine_batch.tex_w_mask == tex_w_mask &&
        gpu_affine_batch.tex_h_mask == tex_h_mask &&
        gpu_affine_batch.fb_step == fb_step &&
        gpu_affine_batch_is_column == is_column;
}

static inline int __attribute__((always_inline))
gpu_can_batch_column(uint8_t flags, uint16_t tex_width,
                     uint16_t tex_w_mask, uint16_t tex_h_mask)
{
    if (gpu_column_batch_count <= 0)
        return 1;
    if (gpu_column_batch_count >= GPU_COLUMN_LIST_BATCH_LANES)
        return 0;

    return gpu_column_batch.flags == flags &&
        gpu_column_batch.tex_width == tex_width &&
        gpu_column_batch.tex_w_mask == tex_w_mask &&
        gpu_column_batch.tex_h_mask == tex_h_mask;
}

static void gpu_flush_affine_batch(void)
{
    int lanes = gpu_affine_batch_count;

    if (lanes <= 0)
        return;

    gpu_affine_batch.lane_count = (uint8_t)lanes;
    of_gpu_draw_affine_span_group(&gpu_affine_batch);

    if (gpu_affine_batch_is_column)
        R_Perf_CountGpuColumnBatch((unsigned int)lanes);

    gpu_affine_batch_count = 0;
    gpu_affine_batch_is_column = 0;
    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();

    /* Advisory: publishes only past the lazy-kick threshold, so the GPU
     * starts chewing on staged work mid-frame instead of idling until
     * the staging buffer fills or the frame ends. */
    of_gpu_kick();
}

static void gpu_flush_column_batch(void)
{
    int lanes = gpu_column_batch_count;

    if (lanes <= 0)
        return;

    gpu_column_batch.lane_count = (uint8_t)lanes;
    of_gpu_draw_column_list(&gpu_column_batch);

    R_Perf_CountGpuColumnBatch((unsigned int)lanes);

    gpu_column_batch_count = 0;
    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();

    of_gpu_kick();
}

/* Emit one light band as a param-span command.  The band stays assigned
 * to its light so accumulation can continue after a capacity flush. */
static void gpu_flush_plane_band(gpu_plane_band_t *band)
{
    int n = band->count;

    if (n <= 0)
        return;

    gpu_plane_params.light_origin = (int32_t)band->light << 16;
    of_gpu_draw_param_span_list(&gpu_plane_params, band->records,
                                (uint32_t)n);

    gpu_plane_record_count -= n;
    band->count = 0;
    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();

    of_gpu_kick();
}

static void gpu_flush_plane_batch(void)
{
    if (gpu_plane_record_count <= 0)
        return;

    for (int i = 0; i < GPU_PLANE_BANDS; i++)
        gpu_flush_plane_band(&gpu_plane_bands[i]);
}

/* Emit one wall light band with its tier's params.  The band keeps its
 * light so accumulation continues after a capacity flush. */
static void gpu_flush_wall_band(gpu_wall_tier_t *tier, gpu_wall_band_t *band)
{
    int n = band->count;

    if (n <= 0)
        return;

    tier->params.light_origin = (int32_t)band->light << 16;
    of_gpu_draw_param_span_list(&tier->params, band->records, (uint32_t)n);

    gpu_wall_record_count -= n;
    band->count = 0;
    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();

    of_gpu_kick();
}

static void gpu_flush_wall_batch(void)
{
    if (gpu_wall_record_count <= 0)
        return;

    for (int t = 0; t < GPU_WALL_TIERS; t++)
        for (int b = 0; b < GPU_WALL_BANDS; b++)
            gpu_flush_wall_band(&gpu_wall_tiers[t], &gpu_wall_tiers[t].bands[b]);
}

static void gpu_flush_sprite_batch(void)
{
    int n = gpu_sprite_record_count;

    if (n <= 0)
        return;

    of_gpu_draw_param_span_list(&gpu_sprite_params, gpu_sprite_records,
                                (uint32_t)n);

    gpu_sprite_record_count = 0;
    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();

    of_gpu_kick();
}

static void gpu_flush_draw_batches(void)
{
    gpu_flush_affine_batch();
    gpu_flush_column_batch();
    gpu_flush_plane_batch();
    gpu_flush_wall_batch();
    gpu_flush_sprite_batch();
}

static inline void __attribute__((always_inline))
gpu_add_affine_span(uint32_t fb_addr, int count,
                    const byte *source, int32_t s, int32_t t,
                    int32_t sstep, int32_t tstep,
                    uint8_t light, uint8_t flags,
                    uint8_t colormap_id, int32_t fb_step,
                    uint16_t tex_width, uint16_t tex_w_mask,
                    uint16_t tex_h_mask, int is_column)
{
    uint32_t tex_addr = (uint32_t)(uintptr_t)source;
    unsigned int lane;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    /* Spans and columns share painter's order: staged columns must land
     * before this span. */
    if (gpu_column_batch_count != 0)
        gpu_flush_column_batch();

    if (!gpu_can_batch_affine(flags, tex_width, tex_w_mask, tex_h_mask,
                              fb_step, is_column))
    {
        gpu_flush_affine_batch();
    }

    if (gpu_affine_batch_count >= GPU_AFFINE_BATCH_LANES)
        gpu_flush_affine_batch();

    if (gpu_affine_batch_count == 0)
    {
        gpu_affine_batch.flags = flags;
        gpu_affine_batch.reserved[0] = 0;
        gpu_affine_batch.reserved[1] = 0;
        gpu_affine_batch.tex_width = tex_width;
        gpu_affine_batch.tex_w_mask = tex_w_mask;
        gpu_affine_batch.tex_h_mask = tex_h_mask;
        gpu_affine_batch.fb_step = fb_step;
        gpu_affine_batch_is_column = is_column;
    }

    lane = (unsigned int)gpu_affine_batch_count;
    if (lane >= GPU_AFFINE_BATCH_LANES)
        return;

    gpu_affine_batch_count = (int)lane + 1;
    gpu_affine_batch.fb_addr[lane] = fb_addr;
    gpu_affine_batch.tex_addr[lane] = tex_addr;
    gpu_affine_batch.count[lane] = (uint16_t)count;
    gpu_affine_batch.s[lane] = s;
    gpu_affine_batch.t[lane] = t;
    gpu_affine_batch.sstep[lane] = sstep;
    gpu_affine_batch.tstep[lane] = tstep;
    gpu_affine_batch.light[lane] = light;
    gpu_affine_batch.colormap_id[lane] = colormap_id;

    if (gpu_affine_batch_count == GPU_AFFINE_BATCH_LANES)
        gpu_flush_affine_batch();
}

/* Append one column lane to the 0x4C batch.  Callers guarantee
 * gpu_use_column_list is set.  fb_step is SCREENWIDTH for every Doom
 * column, so it is not part of the batch-compat key. */
static inline void __attribute__((always_inline))
gpu_column_list_add(uint32_t fb_addr, int count,
                    const byte *source, int32_t t,
                    int32_t tstep, uint8_t light,
                    uint8_t flags, uint8_t colormap_id,
                    uint16_t tex_width, uint16_t tex_w_mask,
                    uint16_t tex_h_mask)
{
    unsigned int lane;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    /* Painter's order vs staged floor/ceiling spans. */
    if (gpu_affine_batch_count != 0)
        gpu_flush_affine_batch();

    if (!gpu_can_batch_column(flags, tex_width, tex_w_mask, tex_h_mask))
        gpu_flush_column_batch();

    if (gpu_column_batch_count == 0)
    {
        gpu_column_batch.flags = flags;
        gpu_column_batch.reserved[0] = 0;
        gpu_column_batch.reserved[1] = 0;
        gpu_column_batch.tex_width = tex_width;
        gpu_column_batch.tex_w_mask = tex_w_mask;
        gpu_column_batch.tex_h_mask = tex_h_mask;
        gpu_column_batch.fb_step = SCREENWIDTH;
    }

    lane = (unsigned int)gpu_column_batch_count;
    gpu_column_batch_count = (int)lane + 1;
    gpu_column_batch.fb_addr[lane] = fb_addr;
    gpu_column_batch.tex_addr[lane] = (uint32_t)(uintptr_t)source;
    gpu_column_batch.count[lane] = (uint16_t)count;
    gpu_column_batch.t[lane] = t;
    gpu_column_batch.tstep[lane] = tstep;
    gpu_column_batch.light[lane] = light;
    gpu_column_batch.colormap_id[lane] = colormap_id;

    if (gpu_column_batch_count == GPU_COLUMN_LIST_BATCH_LANES)
        gpu_flush_column_batch();
}

static inline void __attribute__((always_inline))
gpu_add_column(int x, int yl, int count,
               const byte *source, int32_t t,
               int32_t tstep, uint8_t light,
               uint8_t flags, uint8_t colormap_id,
               uint16_t tex_width, uint16_t tex_w_mask,
               uint16_t tex_h_mask)
{
    uint32_t fb_addr = gpu_fb_row_addr[yl] + (uint32_t)x;

    if (gpu_use_column_list)
    {
        gpu_column_list_add(fb_addr, count, source, t, tstep, light,
                            flags, colormap_id, tex_width,
                            tex_w_mask, tex_h_mask);
        return;
    }

    gpu_add_affine_span(fb_addr, count, source, 0, t, 0, tstep, light,
                        flags, colormap_id, SCREENWIDTH, tex_width,
                        tex_w_mask, tex_h_mask, 1);
}

static inline void __attribute__((always_inline))
gpu_add_wall_column_batch_same(int screen_x, int screen_yl, int count,
                               int lanes,
                               const byte *const *source,
                               const int32_t *t,
                               const int32_t *tstep,
                               const uint8_t *light)
{
    uint32_t fb_addr = gpu_fb_row_addr[screen_yl] + (uint32_t)screen_x;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    if (gpu_use_column_list)
    {
        if (gpu_affine_batch_count != 0)
            gpu_flush_affine_batch();

        if (!gpu_can_batch_column(OF_GPU_SPAN_COLORMAP, 1, 0, 127))
            gpu_flush_column_batch();

        for (int i = 0; i < lanes; i++)
        {
            unsigned int lane;

            if (gpu_column_batch_count >= GPU_COLUMN_LIST_BATCH_LANES)
                gpu_flush_column_batch();

            if (gpu_column_batch_count == 0)
            {
                gpu_column_batch.flags = OF_GPU_SPAN_COLORMAP;
                gpu_column_batch.reserved[0] = 0;
                gpu_column_batch.reserved[1] = 0;
                gpu_column_batch.tex_width = 1;
                gpu_column_batch.tex_w_mask = 0;
                gpu_column_batch.tex_h_mask = 127;
                gpu_column_batch.fb_step = SCREENWIDTH;
            }

            lane = (unsigned int)gpu_column_batch_count;
            gpu_column_batch_count = (int)lane + 1;
            gpu_column_batch.fb_addr[lane] = fb_addr + (uint32_t)i;
            gpu_column_batch.tex_addr[lane] = (uint32_t)(uintptr_t)source[i];
            gpu_column_batch.count[lane] = (uint16_t)count;
            gpu_column_batch.t[lane] = t[i];
            gpu_column_batch.tstep[lane] = tstep[i];
            gpu_column_batch.light[lane] = light[i];
            gpu_column_batch.colormap_id[lane] = 0;
        }

        gpu_pending = 1;
        gpu_mark_framebuffer_gpu_dirty();
        return;
    }

    if (!gpu_can_batch_affine(OF_GPU_SPAN_COLORMAP, 1, 0, 127,
                              SCREENWIDTH, 1))
    {
        gpu_flush_affine_batch();
    }

    for (int i = 0; i < lanes; i++)
    {
        unsigned int lane;

        if (gpu_affine_batch_count >= GPU_AFFINE_BATCH_LANES)
            gpu_flush_affine_batch();

        if (gpu_affine_batch_count == 0)
        {
            gpu_affine_batch.flags = OF_GPU_SPAN_COLORMAP;
            gpu_affine_batch.reserved[0] = 0;
            gpu_affine_batch.reserved[1] = 0;
            gpu_affine_batch.tex_width = 1;
            gpu_affine_batch.tex_w_mask = 0;
            gpu_affine_batch.tex_h_mask = 127;
            gpu_affine_batch.fb_step = SCREENWIDTH;
            gpu_affine_batch_is_column = 1;
        }

        lane = (unsigned int)gpu_affine_batch_count;
        gpu_affine_batch_count = (int)lane + 1;
        gpu_affine_batch.fb_addr[lane] = fb_addr + (uint32_t)i;
        gpu_affine_batch.tex_addr[lane] = (uint32_t)(uintptr_t)source[i];
        gpu_affine_batch.count[lane] = (uint16_t)count;
        gpu_affine_batch.s[lane] = 0;
        gpu_affine_batch.t[lane] = t[i];
        gpu_affine_batch.sstep[lane] = 0;
        gpu_affine_batch.tstep[lane] = tstep[i];
        gpu_affine_batch.light[lane] = light[i];
        gpu_affine_batch.colormap_id[lane] = 0;

        if (gpu_affine_batch_count == GPU_AFFINE_BATCH_LANES)
            gpu_flush_affine_batch();
    }

    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();
}

static inline void __attribute__((always_inline))
gpu_add_wall_column_batch_var(int screen_x, int lanes,
                              const int *yl, const int *yh,
                              const byte *const *source,
                              const int32_t *t,
                              const int32_t *tstep,
                              const uint8_t *light)
{
    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    if (gpu_use_column_list)
    {
        if (gpu_affine_batch_count != 0)
            gpu_flush_affine_batch();

        if (!gpu_can_batch_column(OF_GPU_SPAN_COLORMAP, 1, 0, 127))
            gpu_flush_column_batch();

        for (int i = 0; i < lanes; i++)
        {
            unsigned int lane;
            int screen_yl = yl[i] + viewwindowy;
            int count = yh[i] - yl[i] + 1;

            if (gpu_column_batch_count >= GPU_COLUMN_LIST_BATCH_LANES)
                gpu_flush_column_batch();

            if (gpu_column_batch_count == 0)
            {
                gpu_column_batch.flags = OF_GPU_SPAN_COLORMAP;
                gpu_column_batch.reserved[0] = 0;
                gpu_column_batch.reserved[1] = 0;
                gpu_column_batch.tex_width = 1;
                gpu_column_batch.tex_w_mask = 0;
                gpu_column_batch.tex_h_mask = 127;
                gpu_column_batch.fb_step = SCREENWIDTH;
            }

            lane = (unsigned int)gpu_column_batch_count;
            gpu_column_batch_count = (int)lane + 1;
            gpu_column_batch.fb_addr[lane] =
                gpu_fb_row_addr[screen_yl] + (uint32_t)(screen_x + i);
            gpu_column_batch.tex_addr[lane] = (uint32_t)(uintptr_t)source[i];
            gpu_column_batch.count[lane] = (uint16_t)count;
            gpu_column_batch.t[lane] = t[i];
            gpu_column_batch.tstep[lane] = tstep[i];
            gpu_column_batch.light[lane] = light[i];
            gpu_column_batch.colormap_id[lane] = 0;
        }

        gpu_pending = 1;
        gpu_mark_framebuffer_gpu_dirty();
        return;
    }

    if (!gpu_can_batch_affine(OF_GPU_SPAN_COLORMAP, 1, 0, 127,
                              SCREENWIDTH, 1))
    {
        gpu_flush_affine_batch();
    }

    for (int i = 0; i < lanes; i++)
    {
        unsigned int lane;
        int screen_yl = yl[i] + viewwindowy;
        int count = yh[i] - yl[i] + 1;

        if (gpu_affine_batch_count >= GPU_AFFINE_BATCH_LANES)
            gpu_flush_affine_batch();

        if (gpu_affine_batch_count == 0)
        {
            gpu_affine_batch.flags = OF_GPU_SPAN_COLORMAP;
            gpu_affine_batch.reserved[0] = 0;
            gpu_affine_batch.reserved[1] = 0;
            gpu_affine_batch.tex_width = 1;
            gpu_affine_batch.tex_w_mask = 0;
            gpu_affine_batch.tex_h_mask = 127;
            gpu_affine_batch.fb_step = SCREENWIDTH;
            gpu_affine_batch_is_column = 1;
        }

        lane = (unsigned int)gpu_affine_batch_count;
        gpu_affine_batch_count = (int)lane + 1;
        gpu_affine_batch.fb_addr[lane] =
            gpu_fb_row_addr[screen_yl] + (uint32_t)(screen_x + i);
        gpu_affine_batch.tex_addr[lane] = (uint32_t)(uintptr_t)source[i];
        gpu_affine_batch.count[lane] = (uint16_t)count;
        gpu_affine_batch.s[lane] = 0;
        gpu_affine_batch.t[lane] = t[i];
        gpu_affine_batch.sstep[lane] = 0;
        gpu_affine_batch.tstep[lane] = tstep[i];
        gpu_affine_batch.light[lane] = light[i];
        gpu_affine_batch.colormap_id[lane] = 0;

        if (gpu_affine_batch_count == GPU_AFFINE_BATCH_LANES)
            gpu_flush_affine_batch();
    }

    gpu_pending = 1;
    gpu_mark_framebuffer_gpu_dirty();
}

static void gpu_finish_pending(void)
{
    unsigned int wait_start;
    int waited_for_pending_acquire;

    gpu_flush_draw_batches();

    if (!gpu_pending)
        return;

    waited_for_pending_acquire = gpu_acquire_pending;
    wait_start = R_Perf_BeginStage();
    of_gpu_finish();
    R_Perf_EndStage(R_PERF_STAGE_GPU_WAIT, wait_start);
    R_Perf_CountGpuFinish();
    gpu_record_debug_snapshot();
    gpu_pending = 0;
    if (waited_for_pending_acquire)
        gpu_acquire_had_work = 0;

    /* GPU writes bypass the CPU cache.  Flush+invalidate gives any dirty
     * CPU-only lines a chance to land and drops stale lines for GPU-written
     * pixels before the CPU renders overlays or copies the screen out. */
    if (I_VideoBuffer != NULL && !gpu_flip_enabled)
    {
        unsigned int cache_start = R_Perf_BeginStage();
        of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
        R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    }

    gpu_release_deferred_lumps();
}

static void gpu_prepare_for_gpu_write(void)
{
    if (!gpu_frame_active || I_VideoBuffer == NULL)
        return;
    if (gpu_write_prepared)
        return;

    if (gpu_cpu_dirty)
    {
        if (gpu_flip_enabled)
        {
            gpu_flush_cpu_dirty_lines();
            gpu_clear_line_bits(gpu_cpu_valid_lines);
        }
        else
        {
            unsigned int cache_start = R_Perf_BeginStage();
            of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
            R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
            gpu_cpu_dirty = 0;
        }
    }

    gpu_write_prepared = 1;
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

int R_GPU_ColormapRow(const byte *map)
{
    return gpu_colormap_row((const lighttable_t *)map);
}

static void gpu_upload_fuzz_translucency(void)
{
    const byte *darkmap;
    int row;

    if (colormaps == NULL || gpu_colormap_rows <= 0)
        return;

    row = gpu_colormap_rows > 6 ? 6 : 0;
    darkmap = colormaps + row * 256;

    for (int src = 0; src < 256; src++)
        memcpy(gpu_fuzz_transluc_table + src * 256, darkmap, 256);

    of_gpu_translucency_upload(gpu_fuzz_transluc_table,
                               sizeof(gpu_fuzz_transluc_table));
}

static int gpu_caps_cover_range(const struct of_capabilities *caps,
                                uintptr_t ptr, uint32_t size)
{
    uint64_t base;
    uint64_t end;
    uint64_t range_start = ptr;
    uint64_t range_end = range_start + size;

    if (caps == NULL || caps->sdram_base == 0 || caps->sdram_size == 0)
        return 0;
    if (range_end < range_start)
        return 0;

    base = caps->sdram_base;
    end = base + caps->sdram_size;

    return range_start >= base && range_end <= end;
}

static int gpu_init_for_doom(const struct of_capabilities *caps)
{
    uint32_t batch_base;
    uint32_t pal_base;
    int caps_cover_batch;
    int caps_cover_pal;

    _gpu_base = caps->gpu_base;

    _gpu_wrptr = 0;
    _gpu_known_rdptr = 0;
    _gpu_fence_next = 1;
    _gpu_cmd_words = 0;
    _gpu_batch_dma_base = 0;
    _gpu_batch_dma_addr = 0;
    _gpu_batch_index = 0;
    _gpu_batch_inflight_mask = 0;
    _gpu_unflushed_sync = 0;
    _gpu_batch_buf_base = NULL;
    _gpu_batch_buf = NULL;
    _gpu_palookup_dma_base = 0;
    _gpu_dbg_dma_waits = 0;
    _gpu_dbg_dma_spin_iters = 0;
    _gpu_dbg_ring_waits = 0;
    _gpu_dbg_ring_spin_iters = 0;
    _gpu_dbg_min_ring_free = OF_GPU_RING_SIZE;
    _gpu_state_valid = 0;

    GPU_CTRL = 6;
    for (volatile int i = 0; i < 100; i++) {}
    GPU_CTRL = 4;
    GPU_CTRL = 1;

    batch_base = (uint32_t)(uintptr_t)&_gpu_batch_storage[0][0];
    pal_base = (uint32_t)(uintptr_t)&_gpu_palookup_storage[0];

    caps_cover_batch =
        gpu_caps_cover_range(caps, (uintptr_t)&_gpu_batch_storage[0][0],
                             OF_GPU_BATCH_BUF_BYTES);
    caps_cover_pal =
        gpu_caps_cover_range(caps, (uintptr_t)&_gpu_palookup_storage[0],
                             OF_GPU_PALOOKUP_BYTES);

    if (!caps_cover_batch || !caps_cover_pal)
    {
        printf("Doom GPU: caps SDRAM range base=%08lx size=%08lx "
               "uncached=%08lx gpu=%08lx does not cover GPU buffers "
               "batch=%08lx pal=%08lx\n",
               (unsigned long)caps->sdram_base,
               (unsigned long)caps->sdram_size,
               (unsigned long)caps->sdram_uncached_base,
               (unsigned long)caps->gpu_base,
               (unsigned long)batch_base,
               (unsigned long)pal_base);
    }

    if ((pal_base & (OF_GPU_PALOOKUP_STRIDE - 1u)) != 0u)
    {
        printf("Doom GPU: palookup buffer misaligned; disabling GPU\n");
        return 0;
    }

    _gpu_batch_dma_base = batch_base;
    _gpu_batch_buf_base = &_gpu_batch_storage[0][0];
    _gpu_select_batch_buffer(0);

    _gpu_palookup_dma_base = pal_base;
    GPU_PALOOKUP_BASE = pal_base;
    GPU_TEX_FLUSH = 1;

    return 1;
}

static void gpu_upload_palookup(uint8_t slot, const uint8_t *data,
                                uint32_t size)
{
    uint8_t *dst;

    if (slot >= OF_GPU_PALOOKUP_SLOTS || size > OF_GPU_PALOOKUP_STRIDE)
        return;

    dst = &_gpu_palookup_storage[(uint32_t)slot * OF_GPU_PALOOKUP_STRIDE];
    memcpy(dst, data, size);
    if (size < OF_GPU_PALOOKUP_STRIDE)
        memset(dst + size, 0, OF_GPU_PALOOKUP_STRIDE - size);

    of_cache_flush_range(dst, OF_GPU_PALOOKUP_STRIDE);
}

static void gpu_set_framebuffer_base(uint8_t *base)
{
    uint32_t fb_addr = (uint32_t)(uintptr_t)base;

    of_gpu_set_framebuffer(fb_addr, SCREENWIDTH);

    for (int y = 0; y < SCREENHEIGHT; y++)
        gpu_fb_row_addr[y] = fb_addr + (uint32_t)(y * SCREENWIDTH);
}

static int32_t gpu_column_t_start_direct(int yl, int texturemid, int iscale)
{
    uint32_t y = (uint32_t)(yl - centery);
    return (int32_t)((uint32_t)texturemid + y * (uint32_t)iscale);
}

void R_GPU_Init(void)
{
    gpu_present = 0;
    gpu_frame_active = 0;
    gpu_pending = 0;
    gpu_cpu_dirty = 0;
    gpu_colormap_rows = 0;
    gpu_fuzz_batch_active = 0;
    gpu_flip_enabled = 0;
    gpu_draw_idx = -1;
    gpu_clear_pending_acquire();
    gpu_display_frame_active = 0;
    gpu_framebuffer_cpu_ready = 1;
    gpu_write_prepared = 0;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;
    gpu_framebuffer_delta = 0;
    gpu_deferred_lump_count = 0;
    gpu_affine_batch_count = 0;
    gpu_affine_batch_is_column = 0;
    gpu_column_batch_count = 0;
    gpu_use_column_list = 0;
    gpu_plane_record_count = 0;
    gpu_plane_active = 0;
    gpu_plane_band_rr = 0;
    gpu_plane_fixed_light = -1;
    gpu_use_param_span = 0;
    for (int i = 0; i < GPU_PLANE_BANDS; i++)
    {
        gpu_plane_bands[i].light = -1;
        gpu_plane_bands[i].count = 0;
    }
    gpu_wall_record_count = 0;
    gpu_wall_seg_valid = 0;
    gpu_use_wall_param = 0;
    gpu_sprite_record_count = 0;
    gpu_sprite_active = 0;
    gpu_use_sprite_param = 0;
    gpu_masked_active = 0;
    for (int t = 0; t < GPU_WALL_TIERS; t++)
    {
        gpu_wall_tiers[t].active = 0;
        gpu_wall_tiers[t].rr = 0;
        for (int b = 0; b < GPU_WALL_BANDS; b++)
        {
            gpu_wall_tiers[t].bands[b].light = -1;
            gpu_wall_tiers[t].bands[b].count = 0;
        }
    }
    gpu_reset_cpu_cache_tracking();

    if (!r_gpu_enabled || M_CheckParm("-nogpu") > 0)
    {
        printf("Doom GPU: renderer disabled; using software renderer\n");
        return;
    }

    const struct of_capabilities *caps = of_get_caps();
    if (caps == NULL || caps->magic != OF_CAPS_MAGIC)
    {
        printf("Doom GPU: capability table missing; using software renderer\n");
        return;
    }

    if (caps->version < 2)
    {
        printf("Doom GPU: capability table v%lu lacks GPU memory bases; "
               "using software renderer\n",
               (unsigned long)caps->version);
        return;
    }

    if (caps->gpu_base == 0 || (caps->hw_features & OF_HW_GPU_SPAN) == 0)
    {
        return;
    }

    if (!gpu_service_has_cache_flush_range())
    {
        printf("Doom GPU: cache_flush_range service missing; "
               "disabling GPU for this openfpgaOS runtime.\n");
        return;
    }

    if (!gpu_init_for_doom(caps))
        return;

    if (!gpu_probe_affine_span_group())
    {
        of_gpu_shutdown();
        printf("Doom GPU: probe failed; using software renderer\n");
        return;
    }

    /* Prefer the 5-word column-list command (0x4C) for walls/sprites/fuzz
     * when the core advertises it AND the probe draws through it; otherwise
     * columns fall back to 7-word 0x48 records with s/sstep = 0. */
    if (of_has_feature(OF_HW_GPU_COLUMN_LIST) && gpu_probe_column_list())
    {
        gpu_use_column_list = 1;
        printf("Doom GPU: column-list (0x4C) command path enabled\n");
    }

    /* Param-record visplanes need the span-list command plus the Q29
     * dynamic scale (the SDK emitter drops Q29 commands with a nonzero
     * shift on cores without the bit). */
    if (of_has_feature(OF_HW_GPU_PARAM_SPAN_LIST) &&
        of_has_feature(OF_HW_GPU_PARAM_SPAN_Q29_SCALE) &&
        gpu_probe_param_span())
    {
        gpu_use_param_span = 1;
        printf("Doom GPU: param-span visplane path (PERSP_Q29) enabled\n");

        /* Walls additionally need vertical (AXIS_Y) record walking.
         * -nogpuwalls / -nogpusprites bisect the new param paths on
         * hardware without a rebuild. */
        if (gpu_probe_param_axis_y())
        {
            if (M_CheckParm("-nogpuwalls") > 0)
            {
                printf("Doom GPU: param-wall path disabled (-nogpuwalls)\n");
            }
            else
            {
                gpu_use_wall_param = 1;
                printf("Doom GPU: param-wall path (AXIS_Y) enabled\n");
            }

            if (M_CheckParm("-nogpusprites") > 0)
            {
                printf("Doom GPU: affine-sprite path disabled (-nogpusprites)\n");
            }
            else
            {
                gpu_use_sprite_param = 1;
                printf("Doom GPU: affine-sprite path (ATTR_AFFINE) enabled\n");
            }
        }
    }

    lumpindex_t lump = W_GetNumForName("COLORMAP");
    int cmap_size = W_LumpLength(lump);
    if (cmap_size > 64 * 256)
        cmap_size = 64 * 256;

    gpu_colormap_rows = cmap_size / 256;

    /* Make all already-loaded WAD/cache data visible to the GPU, then copy
     * Doom's palette remap rows into the fabric palookup table. */
    of_cache_flush();
    gpu_upload_palookup(0, colormaps, (uint32_t)cmap_size);
    gpu_register_translations();
    gpu_upload_fuzz_translucency();
    of_cache_flush_range(gpu_fuzz_source_tex, sizeof(gpu_fuzz_source_tex));
    GPU_TEX_FLUSH = 1;

    gpu_present = 1;

    gpu_draw_idx = of_video_acquire_next(-1, 0);
    gpu_flip_enabled = gpu_draw_idx >= 0;

}

void R_GPU_Shutdown(void)
{
    if (!gpu_present)
        return;

    if (gpu_flip_enabled && gpu_acquire_pending)
        gpu_acquire_draw_buffer();
    if (gpu_flip_enabled)
        of_video_wait_flip();
    gpu_finish_pending();
    of_gpu_shutdown();
    gpu_flip_enabled = 0;
    gpu_draw_idx = -1;
    gpu_clear_pending_acquire();
    gpu_display_frame_active = 0;
    gpu_framebuffer_cpu_ready = 1;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;
    gpu_framebuffer_delta = 0;
    gpu_affine_batch_count = 0;
    gpu_affine_batch_is_column = 0;
    gpu_column_batch_count = 0;
    gpu_use_column_list = 0;
    gpu_plane_record_count = 0;
    gpu_plane_active = 0;
    gpu_use_param_span = 0;
    for (int i = 0; i < GPU_PLANE_BANDS; i++)
    {
        gpu_plane_bands[i].light = -1;
        gpu_plane_bands[i].count = 0;
    }
    gpu_wall_record_count = 0;
    gpu_wall_seg_valid = 0;
    gpu_use_wall_param = 0;
    gpu_wall_tiers[0].active = 0;
    gpu_wall_tiers[1].active = 0;
    gpu_sprite_record_count = 0;
    gpu_sprite_active = 0;
    gpu_use_sprite_param = 0;
    gpu_masked_active = 0;
    gpu_reset_cpu_cache_tracking();
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
    if (!gpu_present || !gpu_flip_enabled)
        return;
    if (gpu_display_frame_active)
        return;
    if (!gpu_acquire_draw_buffer())
        return;

    gpu_finish_pending();

    gpu_draw_fb = of_video_buffer_addr(gpu_draw_idx);
    if (gpu_draw_fb == NULL)
    {
        gpu_flip_enabled = 0;
        return;
    }

    gpu_draw_render_base = gpu_draw_fb;
    pixel_t *video_buffer = (pixel_t *)gpu_draw_render_base;
    gpu_framebuffer_delta = 0;
    gpu_reset_cpu_cache_tracking();

    if (I_VideoBuffer != video_buffer)
    {
        I_VideoBuffer = video_buffer;
        R_RetargetBuffer();
    }

    V_RestoreBuffer();

    gpu_display_frame_active = 1;
    gpu_framebuffer_cpu_ready = 1;

    /* All three 320x200 hardware buffers are cleared during video init, so
     * the framebuffer does not need per-frame GPU clears. */
}

void R_GPU_BeginFrame(void)
{
    if (!gpu_present || I_VideoBuffer == NULL)
        return;
    if (gpu_flip_enabled && !gpu_display_frame_active)
        return;

    gpu_finish_pending();
    gpu_frame_active = 1;
    gpu_write_prepared = 0;

    /* Direct-FB mode now lets CPU HUD/menu drawing use the cached alias.
     * If status/border code dirtied lines before the 3D view starts, publish
     * those lines before the GPU writes, then force later CPU overlays to
     * revalidate against GPU-produced pixels. */
    if (gpu_flip_enabled)
    {
        gpu_flush_cpu_dirty_lines();
        gpu_clear_line_bits(gpu_cpu_valid_lines);
    }
    else
    {
        unsigned int cache_start;

        gpu_cpu_dirty = 0;
        cache_start = R_Perf_BeginStage();
        of_cache_flush_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
        R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    }
    gpu_set_framebuffer_base(gpu_flip_enabled && gpu_draw_render_base != NULL
                             ? gpu_draw_render_base
                             : (uint8_t *)I_VideoBuffer);
    gpu_framebuffer_cpu_ready = 1;
    gpu_write_prepared = 1;
}

void R_GPU_EndFrame(void)
{
    if (!gpu_present || !gpu_frame_active)
        return;

    if (gpu_flip_enabled)
    {
        gpu_finish_pending();
    }
    else
    {
        if (gpu_prepare_framebuffer_for_cpu())
        {
            unsigned int cache_start = R_Perf_BeginStage();
            of_cache_inval_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
            R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
        }
    }
    gpu_frame_active = 0;
    gpu_write_prepared = 0;
    gpu_release_deferred_lumps();
}

void R_GPU_PrepareForCPUAccess(void)
{
    R_GPU_PrepareForCPUAccessRect(0, 0, SCREENWIDTH, SCREENHEIGHT);
}

void R_GPU_PrepareForCPUAccessRect(int x, int y, int w, int h)
{
    if (gpu_present && gpu_flip_enabled && gpu_acquire_pending)
        gpu_acquire_draw_buffer();

    if (gpu_present && gpu_prepare_cpu_write_outside_view(x, y, w, h))
        return;

    if (!gpu_present || !gpu_frame_active)
    {
        if (gpu_present && gpu_flip_enabled && gpu_display_frame_active)
        {
            gpu_prepare_framebuffer_for_cpu();
            gpu_invalidate_rect_for_cpu(x, y, w, h);
            gpu_mark_cpu_dirty_rect(x, y, w, h);
        }
        return;
    }

    if (gpu_cpu_dirty && gpu_framebuffer_cpu_ready &&
        !gpu_pending && !gpu_has_pending_draw_batches())
    {
        if (gpu_flip_enabled)
        {
            gpu_invalidate_rect_for_cpu(x, y, w, h);
            gpu_mark_cpu_dirty_rect(x, y, w, h);
        }
        return;
    }

    R_Perf_CountPrepareCPU();
    int waited_for_gpu = gpu_prepare_framebuffer_for_cpu();

    if (gpu_flip_enabled)
    {
        gpu_invalidate_rect_for_cpu(x, y, w, h);
        gpu_mark_cpu_dirty_rect(x, y, w, h);
    }
    else
    {
        if (waited_for_gpu)
        {
            unsigned int cache_start = R_Perf_BeginStage();
            of_cache_inval_range(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
            R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
        }
        gpu_cpu_dirty = 1;
        gpu_write_prepared = 0;
    }
}

void R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    unsigned int cache_start;

    if (!gpu_present || ptr == NULL || size == 0)
        return;

    /* Texture-cache invalidation is only safe while the GPU is idle.  This
     * path is cold: it runs when Doom loads a lump or builds a composite
     * texture, not for every already-cached texel fetch. */
    gpu_finish_pending();
    cache_start = R_Perf_BeginStage();
    of_cache_flush_range(ptr, size);
    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    GPU_TEX_FLUSH = 1;
}

void R_GPU_TextureDataFlushAll(void)
{
    unsigned int cache_start;

    if (!gpu_present)
        return;

    gpu_finish_pending();
    cache_start = R_Perf_BeginStage();
    of_cache_flush();
    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    GPU_TEX_FLUSH = 1;
}

boolean R_GPU_PresentFrame(void)
{
    unsigned int present_start;
    unsigned int stage_start;
    uint32_t token;
    int had_gpu_work;

    if (!gpu_present || !gpu_flip_enabled || I_VideoBuffer == NULL ||
        gpu_draw_idx < 0 || !gpu_display_frame_active)
        return false;

    present_start = R_Perf_BeginStage();

    /* Queue the flip and return immediately.  The next display frame will
     * acquire a safe draw buffer after this flip's fence retires, which gives
     * game/audio work room to run while the swap is pending.
     */
    gpu_flush_draw_batches();
    had_gpu_work = gpu_pending;
    gpu_flush_cpu_dirty_lines();

    stage_start = R_Perf_BeginStage();
    token = of_gpu_flip_to(gpu_draw_idx);
    of_gpu_kick();
    R_Perf_EndStage(R_PERF_STAGE_GPU_FLIP, stage_start);

    gpu_acquire_pending = 1;
    gpu_acquire_idx = gpu_draw_idx;
    gpu_acquire_token = token;
    gpu_acquire_had_work = had_gpu_work;
    gpu_draw_idx = -1;

    gpu_display_frame_active = 0;
    gpu_frame_active = 0;
    gpu_fuzz_batch_active = 0;
    gpu_plane_active = 0;
    gpu_wall_seg_valid = 0;
    gpu_wall_tiers[0].active = 0;
    gpu_wall_tiers[1].active = 0;
    gpu_sprite_active = 0;
    gpu_masked_active = 0;
    gpu_draw_fb = NULL;
    gpu_draw_render_base = NULL;
    gpu_reset_cpu_cache_tracking();

    R_Perf_EndStage(R_PERF_STAGE_PRESENT, present_start);
    return true;
}

boolean R_GPU_DrawColumn(void)
{
    return R_GPU_DrawColumnDirect(dc_x, dc_yl, dc_yh, dc_source,
                                  dc_texturemid, dc_iscale,
                                  (const byte *)dc_colormap);
}

boolean R_GPU_DrawColumnDirect(int x, int yl, int yh, const byte *source,
                               int texturemid, int iscale,
                               const byte *colormap)
{
    int light = gpu_colormap_row((const lighttable_t *)colormap);
    return R_GPU_DrawColumnLightDirect(x, yl, yh, source, texturemid, iscale,
                                       light);
}

static boolean gpu_can_draw_fuzz(void)
{
    return gpu_present && gpu_frame_active && I_VideoBuffer != NULL;
}

boolean R_GPU_CanDrawFuzz(void)
{
    return gpu_can_draw_fuzz();
}

boolean R_GPU_BeginFuzzSpans(void)
{
    if (!gpu_can_draw_fuzz())
        return false;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();
    gpu_flush_draw_batches();
    gpu_fuzz_batch_active = 1;
    return true;
}

void R_GPU_EndFuzzSpans(void)
{
    if (gpu_fuzz_batch_active)
        gpu_flush_draw_batches();

    gpu_fuzz_batch_active = 0;
}

boolean R_GPU_DrawFuzzColumnDirect(int x, int yl, int yh)
{
    int count = yh - yl + 1;
    int screen_x = x + viewwindowx;
    int screen_yl = yl + viewwindowy;
    int screen_yh = yh + viewwindowy;
    unsigned int submit_start = 0;
    int timing_enabled;

    if (!gpu_can_draw_fuzz())
        return false;
    if (screen_x < 0 || screen_x >= SCREENWIDTH ||
        screen_yl < 0 || screen_yh >= SCREENHEIGHT)
        return false;
    if (count <= 0 || count > 4095)
        return false;

    timing_enabled = R_Perf_FuzzTimingEnabled();
    if (timing_enabled)
        submit_start = R_Perf_NowUS();

    gpu_add_column(screen_x, screen_yl, count, gpu_fuzz_source_tex, 0, 0, 0,
                   OF_GPU_SPAN_TRANSLUC, 0, 1, 0, 0);

    R_Perf_CountGpuColumn((unsigned int)count);
    R_Perf_CountFuzzSpan(timing_enabled ? R_Perf_NowUS() - submit_start : 0,
                         (unsigned int)count);
    return true;
}

boolean R_GPU_DrawColumnLightDirect(int x, int yl, int yh, const byte *source,
                                    int texturemid, int iscale, int light)
{
    int count = yh - yl + 1;
    int screen_x = x + viewwindowx;
    int screen_yl = yl + viewwindowy;
    int screen_yh = yh + viewwindowy;
    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (screen_x < 0 || screen_x >= SCREENWIDTH ||
        screen_yl < 0 || screen_yh >= SCREENHEIGHT)
        return false;
    if (count <= 0 || count > 4095 || source == NULL)
        return false;
    if (light < 0 || light > 63)
        return false;

    gpu_add_column(screen_x, screen_yl, count, source,
                   gpu_column_t_start_direct(yl, texturemid, iscale),
                   iscale, (uint8_t)light, OF_GPU_SPAN_COLORMAP, 0,
                   1, 0, 127);

    R_Perf_CountGpuColumn((unsigned int)count);
    return true;
}

boolean R_GPU_DrawColumnLightBatchDirect(int x, int yl, int yh, int lanes,
                                         const byte *const *source,
                                         const int32_t *t,
                                         const int32_t *tstep,
                                         const uint8_t *light)
{
    int count = yh - yl + 1;
    int screen_x = x + viewwindowx;
    int screen_yl = yl + viewwindowy;
    int screen_yh = yh + viewwindowy;

    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (lanes <= 0 || lanes > GPU_COLUMN_BATCH_LANES)
        return false;
    if (screen_x < 0 || screen_x + lanes > SCREENWIDTH ||
        screen_yl < 0 || screen_yh >= SCREENHEIGHT)
        return false;
    if (count <= 0 || count > 4095)
        return false;

#ifdef RANGECHECK
    for (int i = 0; i < lanes; i++)
    {
        if (source[i] == NULL || light[i] > 63)
            return false;
    }
#endif

    gpu_add_wall_column_batch_same(screen_x, screen_yl, count, lanes,
                                   source, t, tstep, light);

    R_Perf_CountGpuColumns((unsigned int)lanes,
                           (unsigned int)(lanes * count));
    return true;
}

boolean R_GPU_DrawColumnLightVarBatchDirect(int x, int lanes,
                                            const int *yl,
                                            const int *yh,
                                            const byte *const *source,
                                            const int32_t *t,
                                            const int32_t *tstep,
                                            const uint8_t *light)
{
    unsigned int pixels = 0;
    int screen_x = x + viewwindowx;

    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (lanes <= 0 || lanes > GPU_COLUMN_BATCH_LANES ||
        yl == NULL || yh == NULL)
        return false;
    if (screen_x < 0 || screen_x + lanes > SCREENWIDTH)
        return false;

    for (int i = 0; i < lanes; i++)
    {
        int count = yh[i] - yl[i] + 1;
#ifdef RANGECHECK
        int screen_yl = yl[i] + viewwindowy;
        int screen_yh = yh[i] + viewwindowy;

        if (count <= 0 || count > 4095)
            return false;
        if (source[i] == NULL || light[i] > 63)
            return false;
        if (screen_yl < 0 || screen_yh >= SCREENHEIGHT)
            return false;
#endif

        pixels += (unsigned int)count;
    }

    gpu_add_wall_column_batch_var(screen_x, lanes, yl, yh,
                                  source, t, tstep, light);

    R_Perf_CountGpuColumns((unsigned int)lanes, pixels);
    return true;
}

boolean R_GPU_DrawSpan(void)
{
    return R_GPU_DrawSpanDirect(ds_y, ds_x1, ds_x2, ds_source,
                                ds_xfrac, ds_yfrac, ds_xstep, ds_ystep,
                                ds_colormap);
}

boolean R_GPU_DrawSpanDirect(int y, int x1, int x2, const byte *source,
                             fixed_t xfrac, fixed_t yfrac,
                             fixed_t xstep, fixed_t ystep,
                             const byte *colormap)
{
    int light = gpu_colormap_row((const lighttable_t *)colormap);
    return R_GPU_DrawSpanLightDirect(y, x1, x2, source,
                                     xfrac, yfrac, xstep, ystep, light);
}

boolean R_GPU_DrawSpanLightDirect(int y, int x1, int x2, const byte *source,
                                  fixed_t xfrac, fixed_t yfrac,
                                  fixed_t xstep, fixed_t ystep, int light)
{
    int count = x2 - x1 + 1;
    int screen_x1 = x1 + viewwindowx;
    int screen_x2 = x2 + viewwindowx;
    int screen_y = y + viewwindowy;
    if (!gpu_present || !gpu_frame_active || I_VideoBuffer == NULL)
        return false;
    if (count <= 0 || count > 4095 || source == NULL)
        return false;
    if (light < 0 || light > 63)
        return false;
    if (screen_x1 < 0 || screen_x2 >= SCREENWIDTH ||
        screen_y < 0 || screen_y >= SCREENHEIGHT)
        return false;

    gpu_add_affine_span(gpu_fb_row_addr[screen_y] + (uint32_t)screen_x1, count, source,
                        xfrac, yfrac, xstep, ystep,
                        (uint8_t)light, OF_GPU_SPAN_COLORMAP, 0, 1,
                        64, 63, 63, 0);

    R_Perf_CountGpuSpan((unsigned int)count);
    return true;
}

/* Q29 dynamic-scale encode of the value-unit float planes — same scheme
 * as the proven Quake2 world path (sw_scan.c R_WTriBuildParams): bound
 * the magnitude over the view-rect corners, pick the shift from the
 * float exponent, scale all three planes by 2^(29-shift). */
static void gpu_param_encode_q29(of_gpu_param_span_list_t *p,
                                 const float f_org[3], const float f_du[3],
                                 const float f_dv[3])
{
    const float fmu = (float)viewwidth;
    const float fmv = (float)viewheight;
    float fmax = 0.0f;
    float scale;
    int sh;

    for (int i = 0; i < 3; i++)
    {
        float o = f_org[i];
        float du_span = f_du[i] * fmu;
        float dv_span = f_dv[i] * fmv;
        float c;

        c = __builtin_fabsf(o);                     if (c > fmax) fmax = c;
        c = __builtin_fabsf(f_du[i]);               if (c > fmax) fmax = c;
        c = __builtin_fabsf(f_dv[i]);               if (c > fmax) fmax = c;
        c = __builtin_fabsf(o + du_span);           if (c > fmax) fmax = c;
        c = __builtin_fabsf(o + dv_span);           if (c > fmax) fmax = c;
        c = __builtin_fabsf(o + du_span + dv_span); if (c > fmax) fmax = c;
    }

    {
        union { float f; uint32_t u; } mb, sb;

        mb.f = fmax;
        sh = (int)((mb.u >> 23) & 0xFFu) - 126 - 1;
        if (sh < 0)
            sh = 0;
        else if (sh > 31)
            sh = 31;

        sb.u = (uint32_t)(127 + 29 - sh) << 23;     /* 2^(29-sh) */
        scale = sb.f;
    }

    p->q29_attr_shift = (uint8_t)sh;
    for (int i = 0; i < 3; i++)
    {
        p->attr_origin[i] = (int32_t)(f_org[i] * scale);
        p->attr_du[i] = (int32_t)(f_du[i] * scale);
        p->attr_dv[i] = (int32_t)(f_dv[i] * scale);
    }
}

boolean R_GPU_BeginPlaneSpans(const byte *source, fixed_t height_delta,
                              int fixed_light)
{
    float f_org[3], f_du[3], f_dv[3];
    float cos_va, sin_va, cx_f, cy_f, w2, ph_f, invD;
    float vx_f, ty_f, zi_org, zi_dv, edge;
    const float inv16 = 1.0f / 65536.0f;
    unsigned int fa;
    fixed_t ph;

    if (!gpu_use_param_span || !gpu_present || !gpu_frame_active ||
        I_VideoBuffer == NULL)
        return false;
    if (source == NULL || height_delta == 0)
        return false;
    if (fixed_light > 63)
        return false;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    /* Painter's order vs staged wall/sky work. */
    gpu_flush_affine_batch();
    gpu_flush_column_batch();

    /* Screen-space planes for a horizontal world plane.  Along a row z is
     * constant, so the HW per-pixel divide degenerates to exactly Doom's
     * per-row-constant steps:
     *   1/z(v)  = +-(v + 0.5 - centery) / (|planeheight| * viewwidth/2)
     *   s/z     = viewx*zi(v) + cos(va) - sin(va)*(centerx - u)/focal
     *   t/z     = -viewy*zi(v) - sin(va) - cos(va)*(centerx - u)/focal
     * (focal = centerxfrac for Doom's fixed 90-degree FOV; the same scale
     * basexscale/baseyscale are built from.)  Flats tile every 64 world
     * units, so viewx/-viewy are rebased mod 64 — texel-identical, and it
     * keeps plane magnitudes (and the Q29 shift) small. */
    fa = (unsigned int)(viewangle >> ANGLETOFINESHIFT);
    cos_va = (float)finecosine[fa] * inv16;
    sin_va = (float)finesine[fa] * inv16;
    cx_f = (float)centerxfrac * inv16;
    cy_f = (float)centery;
    w2 = (float)((viewwidth << detailshift) >> 1);
    ph = height_delta < 0 ? -height_delta : height_delta;
    ph_f = (float)ph * inv16;
    invD = 1.0f / (ph_f * w2);

    vx_f = (float)(viewx & ((64 << FRACBITS) - 1)) * inv16;
    ty_f = (float)((-viewy) & ((64 << FRACBITS) - 1)) * inv16;

    if (height_delta < 0)
    {
        /* floor: rows below the horizon */
        zi_dv = invD;
        zi_org = (0.5f - cy_f) * invD;
    }
    else
    {
        /* ceiling: rows above the horizon */
        zi_dv = -invD;
        zi_org = (cy_f - 0.5f) * invD;
    }

    edge = (float)centerx / cx_f;

    f_du[0] = sin_va / cx_f;
    f_dv[0] = vx_f * zi_dv;
    f_org[0] = cos_va - sin_va * edge + vx_f * zi_org;

    f_du[1] = cos_va / cx_f;
    f_dv[1] = ty_f * zi_dv;
    f_org[1] = -sin_va - cos_va * edge + ty_f * zi_org;

    f_du[2] = 0.0f;
    f_dv[2] = zi_dv;
    f_org[2] = zi_org;

    memset(&gpu_plane_params, 0, sizeof(gpu_plane_params));
    gpu_plane_params.fb_base = gpu_fb_row_addr[viewwindowy]
                             + (uint32_t)viewwindowx;
    gpu_plane_params.fb_major_step = SCREENWIDTH;
    gpu_plane_params.fb_minor_step = 1;
    gpu_plane_params.tex_addr = (uint32_t)(uintptr_t)source;
    gpu_plane_params.tex_width = 64;
    gpu_plane_params.tex_w_mask = 63;
    gpu_plane_params.tex_h_mask = 63;
    gpu_plane_params.flags = OF_GPU_SPAN_COLORMAP | OF_GPU_SPAN_PERSP;
    gpu_plane_params.colormap_id = 0;
    gpu_plane_params.attr_mode = OF_GPU_PARAM_ATTR_PERSP_Q29;
    gpu_plane_params.span_axis = OF_GPU_PARAM_AXIS_X;
    gpu_plane_params.z_mode = OF_GPU_PARAM_Z_NONE;
    gpu_param_encode_q29(&gpu_plane_params, f_org, f_du, f_dv);

    /* All bands were flushed by the previous EndPlaneSpans; unassign the
     * lights so stale rows never bleed into this visplane. */
    for (int i = 0; i < GPU_PLANE_BANDS; i++)
        gpu_plane_bands[i].light = -1;
    gpu_plane_band_rr = 0;
    gpu_plane_fixed_light = fixed_light;
    gpu_plane_active = 1;
    return true;
}

boolean R_GPU_PlaneSpanLight(int y, int x1, int x2, int light)
{
    of_gpu_param_span_record_t *r;
    gpu_plane_band_t *band;
    int count;

    if (!gpu_plane_active)
        return false;

    if (light < 0)
        light = gpu_plane_fixed_light;
    if (light < 0 || light > 63)
        return false;
    if ((unsigned int)y >= (unsigned int)viewheight ||
        x1 < 0 || x2 >= viewwidth)
        return false;

    count = x2 - x1 + 1;
    if (count <= 0)
        return true;

    band = &gpu_plane_bands[0];
    if (band->light != light)
    {
        if (gpu_plane_bands[1].light == light)
            band = &gpu_plane_bands[1];
        else if (gpu_plane_bands[2].light == light)
            band = &gpu_plane_bands[2];
        else if (gpu_plane_bands[3].light == light)
            band = &gpu_plane_bands[3];
        else
        {
            /* Evict round-robin; bands are pixel-disjoint within the
             * visplane, so emission order between them is free. */
            band = &gpu_plane_bands[gpu_plane_band_rr];
            gpu_plane_band_rr = (gpu_plane_band_rr + 1) & (GPU_PLANE_BANDS - 1);
            gpu_flush_plane_band(band);
            band->light = light;
        }
    }

    if (band->count >= GPU_PLANE_BAND_RECORDS)
        gpu_flush_plane_band(band);

    r = &band->records[band->count++];
    gpu_plane_record_count++;
    r->u = (uint16_t)x1;
    r->v = (uint16_t)y;
    r->count = (uint16_t)count;

    R_Perf_CountGpuSpan((unsigned int)count);
    return true;
}

void R_GPU_EndPlaneSpans(void)
{
    if (!gpu_plane_active)
        return;

    gpu_flush_plane_batch();
    gpu_plane_active = 0;
}

/* texcol(x) in texels, mirroring R_TextureColumnForX's table math. */
static float gpu_wall_texcol_at(int x, fixed_t offset, fixed_t distance,
                                angle_t centerangle)
{
    const float inv16 = 1.0f / 65536.0f;
    unsigned int a = (unsigned int)((centerangle + xtoviewangle[x])
                                    >> ANGLETOFINESHIFT);

    return (float)offset * inv16
         - ((float)finetangent[a] * inv16) * ((float)distance * inv16);
}

boolean R_GPU_WallSegBegin(int x1, int x2, fixed_t scale1, fixed_t scalestep,
                           fixed_t distance, fixed_t offset,
                           angle_t centerangle)
{
    const float inv16 = 1.0f / 65536.0f;
    float proj, zi1, texcol2;

    gpu_wall_seg_valid = 0;
    gpu_wall_tiers[0].active = 0;
    gpu_wall_tiers[1].active = 0;

    if (!gpu_use_wall_param || !gpu_present || !gpu_frame_active ||
        I_VideoBuffer == NULL)
        return false;
    if (x2 < x1 || scale1 <= 0)
        return false;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    /* zi(x) = scale(x)/projection; Doom's linear scale walk along the
     * seg IS the exact affine zi for a vertical plane. */
    proj = (float)centerxfrac * inv16;
    gpu_wall_zi_du = ((float)scalestep * inv16) / proj;
    zi1 = ((float)scale1 * inv16) / proj;
    gpu_wall_zi_org = zi1 - (float)x1 * gpu_wall_zi_du;

    /* texcol*zi is affine in x: fit it through the two endpoint columns
     * evaluated with Doom's own finetangent quantization. */
    gpu_wall_texcol1 = gpu_wall_texcol_at(x1, offset, distance, centerangle);
    if (x2 > x1)
    {
        float zi2 = zi1 + (float)(x2 - x1) * gpu_wall_zi_du;

        texcol2 = gpu_wall_texcol_at(x2, offset, distance, centerangle);
        gpu_wall_szi_du = (texcol2 * zi2 - gpu_wall_texcol1 * zi1)
                        / (float)(x2 - x1);
    }
    else
    {
        gpu_wall_szi_du = 0.0f;
    }
    gpu_wall_szi_org = gpu_wall_texcol1 * zi1 - (float)x1 * gpu_wall_szi_du;

    gpu_wall_seg_valid = 1;
    return true;
}

boolean R_GPU_WallTierBegin(int tier, const byte *tex2d, int tex_height,
                            int widthmask, fixed_t texturemid)
{
    const float inv16 = 1.0f / 65536.0f;
    float f_org[3], f_du[3], f_dv[3];
    float proj, tm_f, rebase;
    gpu_wall_tier_t *t;
    fixed_t tmr;
    int k;

    if (!gpu_wall_seg_valid || tex2d == NULL)
        return false;
    if ((unsigned int)tier >= GPU_WALL_TIERS)
        return false;
    if (tex_height <= 0 || tex_height > 0xFFFF ||
        widthmask < 0 || widthmask > 0xFFFF)
        return false;

    t = &gpu_wall_tiers[tier];
    proj = (float)centerxfrac * inv16;

    /* vtex wraps &127 like R_DrawColumn, so texturemid rebases mod 128
     * texels — texel-identical, keeps the Q29 shift small. */
    tmr = texturemid & ((128 << FRACBITS) - 1);
    tm_f = (float)tmr * inv16;

    /* texcol rebases by a multiple of the texture's wrap period. */
    k = (int)(gpu_wall_texcol1 / (float)(widthmask + 1));
    if (gpu_wall_texcol1 < (float)k * (float)(widthmask + 1))
        k--;
    rebase = (float)k * (float)(widthmask + 1);

    /* attr0 = vtex*zi, attr1 = texcol*zi, attr2 = zi.  The texture roles
     * are swapped to fit the column-major 2D block: tex_width = column
     * stride (texheight), w_mask = 127 vertical wrap, h_mask = width. */
    f_du[0] = tm_f * gpu_wall_zi_du;
    f_dv[0] = 1.0f / proj;
    f_org[0] = tm_f * gpu_wall_zi_org - (float)centery * f_dv[0];

    f_du[1] = gpu_wall_szi_du - rebase * gpu_wall_zi_du;
    f_dv[1] = 0.0f;
    f_org[1] = gpu_wall_szi_org - rebase * gpu_wall_zi_org;

    f_du[2] = gpu_wall_zi_du;
    f_dv[2] = 0.0f;
    f_org[2] = gpu_wall_zi_org;

    memset(&t->params, 0, sizeof(t->params));
    t->params.fb_base = gpu_fb_row_addr[viewwindowy] + (uint32_t)viewwindowx;
    t->params.fb_major_step = 1;            /* AXIS_Y: per-column */
    t->params.fb_minor_step = SCREENWIDTH;  /* per-pixel walk = row stride */
    t->params.tex_addr = (uint32_t)(uintptr_t)tex2d;
    t->params.tex_width = (uint16_t)tex_height;
    t->params.tex_w_mask = 127;
    t->params.tex_h_mask = (uint16_t)widthmask;
    t->params.flags = OF_GPU_SPAN_COLORMAP | OF_GPU_SPAN_PERSP;
    t->params.colormap_id = 0;
    t->params.attr_mode = OF_GPU_PARAM_ATTR_PERSP_Q29;
    t->params.span_axis = OF_GPU_PARAM_AXIS_Y;
    t->params.z_mode = OF_GPU_PARAM_Z_NONE;
    gpu_param_encode_q29(&t->params, f_org, f_du, f_dv);

    for (int b = 0; b < GPU_WALL_BANDS; b++)
        t->bands[b].light = -1;
    t->rr = 0;
    t->active = 1;
    return true;
}

boolean R_GPU_WallTierColumn(int tier, int x, int yl, int yh, fixed_t scale)
{
    of_gpu_param_span_record_t *r;
    gpu_wall_tier_t *t = &gpu_wall_tiers[tier];
    gpu_wall_band_t *band;
    unsigned int lindex;
    int light;
    int count;

    if (!t->active)
        return false;
    if ((unsigned int)x >= (unsigned int)viewwidth ||
        yl < 0 || yh >= viewheight)
        return false;

    lindex = (unsigned int)scale >> LIGHTSCALESHIFT;
    if (lindex >= MAXLIGHTSCALE)
        lindex = MAXLIGHTSCALE - 1;
    light = walllightrows[lindex];
    if (light < 0 || light > 63)
        return false;

    count = yh - yl + 1;
    if (count <= 0)
        return true;

    band = &t->bands[0];
    if (band->light != light)
    {
        if (t->bands[1].light == light)
            band = &t->bands[1];
        else
        {
            band = &t->bands[t->rr];
            t->rr ^= 1;
            gpu_flush_wall_band(t, band);
            band->light = light;
        }
    }

    if (band->count >= GPU_WALL_BAND_RECORDS)
        gpu_flush_wall_band(t, band);

    r = &band->records[band->count++];
    gpu_wall_record_count++;
    r->u = (uint16_t)x;     /* AXIS_Y record: u = column, v = top row */
    r->v = (uint16_t)yl;
    r->count = (uint16_t)count;

    R_Perf_CountGpuColumn((unsigned int)count);
    return true;
}

void R_GPU_WallTiersEnd(void)
{
    for (int i = 0; i < GPU_WALL_TIERS; i++)
    {
        gpu_wall_tier_t *t = &gpu_wall_tiers[i];

        if (!t->active)
            continue;

        for (int b = 0; b < GPU_WALL_BANDS; b++)
            gpu_flush_wall_band(t, &t->bands[b]);
        t->active = 0;
    }
    gpu_wall_seg_valid = 0;
}

boolean R_GPU_SpriteBegin(const byte *tex2d, int tex_height, int tex_width,
                          fixed_t texturemid, fixed_t iscale,
                          fixed_t startfrac, fixed_t xiscale, int x1,
                          int light, int cmap_slot)
{
    /* Shares the AXIS_Y walker the wall probe validated. */
    if (!gpu_use_sprite_param || !gpu_present || !gpu_frame_active ||
        I_VideoBuffer == NULL)
        return false;
    if (tex2d == NULL || light < 0 || light > 63)
        return false;
    if (tex_height <= 0 || tex_height > 0xFFFF || tex_width <= 0)
        return false;
    if (cmap_slot < 0 || cmap_slot >= OF_GPU_PALOOKUP_SLOTS)
        return false;

    if (!gpu_write_prepared)
        gpu_prepare_for_gpu_write();

    /* Painter's order vs staged masked/sprite column work. */
    gpu_flush_affine_batch();
    gpu_flush_column_batch();

    memset(&gpu_sprite_params, 0, sizeof(gpu_sprite_params));
    gpu_sprite_params.fb_base = gpu_fb_row_addr[viewwindowy]
                              + (uint32_t)viewwindowx;
    gpu_sprite_params.fb_major_step = 1;            /* AXIS_Y: per-column */
    gpu_sprite_params.fb_minor_step = SCREENWIDTH;  /* walk = row stride */
    gpu_sprite_params.tex_addr = (uint32_t)(uintptr_t)tex2d;
    gpu_sprite_params.tex_width = (uint16_t)tex_height;  /* column stride */
    gpu_sprite_params.tex_w_mask = 0xFFFF;  /* ranges enforced by clamps */
    gpu_sprite_params.tex_h_mask = 0xFFFF;
    gpu_sprite_params.flags = OF_GPU_SPAN_COLORMAP;
    gpu_sprite_params.colormap_id = (uint8_t)cmap_slot;
    gpu_sprite_params.attr_mode = OF_GPU_PARAM_ATTR_AFFINE;
    gpu_sprite_params.span_axis = OF_GPU_PARAM_AXIS_Y;
    gpu_sprite_params.z_mode = OF_GPU_PARAM_Z_NONE;

    /* Exact Q16.16 planes — identical integer math to the software
     * column walk (32-bit wrap included), so output is bit-exact.
     * attr0 = vtex (along column), attr1 = sprite column. */
    gpu_sprite_params.attr_origin[0] =
        (int32_t)((uint32_t)texturemid - (uint32_t)centery * (uint32_t)iscale);
    gpu_sprite_params.attr_du[0] = 0;
    gpu_sprite_params.attr_dv[0] = iscale;
    gpu_sprite_params.attr_origin[1] =
        (int32_t)((uint32_t)startfrac - (uint32_t)x1 * (uint32_t)xiscale);
    gpu_sprite_params.attr_du[1] = xiscale;
    gpu_sprite_params.attr_dv[1] = 0;

    /* Boundary roundings clamp to the sprite box instead of sampling
     * out of the block. */
    gpu_sprite_params.clamp_min[0] = 0;
    gpu_sprite_params.clamp_max[0] = ((int32_t)tex_height << 16) - 1;
    gpu_sprite_params.clamp_min[1] = 0;
    gpu_sprite_params.clamp_max[1] = ((int32_t)tex_width << 16) - 1;

    gpu_sprite_params.light_origin = (int32_t)light << 16;

    gpu_sprite_record_count = 0;
    gpu_sprite_active = 1;
    return true;
}

boolean R_GPU_SpritePost(int x, int yl, int yh)
{
    of_gpu_param_span_record_t *r;
    int count;

    if (!gpu_sprite_active)
        return false;
    if ((unsigned int)x >= (unsigned int)viewwidth ||
        yl < 0 || yh >= viewheight)
        return false;

    count = yh - yl + 1;
    if (count <= 0)
        return true;

    if (gpu_sprite_record_count >= GPU_SPRITE_MAX_RECORDS)
        gpu_flush_sprite_batch();

    r = &gpu_sprite_records[gpu_sprite_record_count++];
    r->u = (uint16_t)x;
    r->v = (uint16_t)yl;
    r->count = (uint16_t)count;

    R_Perf_CountGpuColumn((unsigned int)count);
    return true;
}

void R_GPU_SpriteEnd(void)
{
    if (!gpu_sprite_active)
        return;

    gpu_flush_sprite_batch();
    gpu_sprite_active = 0;
}

/* ================================================================
 * Param-masked midtextures: reuse the wall tier machinery during the
 * masked/sprite phase.  Unlike the solid phase, masked surfaces
 * interleave with sprites back-to-front, so Begin flushes the staged
 * column/sprite work first and End emits before the next surface.
 * Posts append through R_GPU_MaskedPost (light band from spryscale).
 * ================================================================ */

boolean R_GPU_MaskedBegin(const byte *blk, int tex_height, int widthmask,
                          fixed_t texturemid, int x1, int x2,
                          fixed_t scale1, fixed_t scalestep,
                          fixed_t distance, fixed_t offset,
                          unsigned int centerangle)
{
    gpu_masked_active = 0;

    if (blk == NULL)
        return false;
    if (!gpu_use_wall_param || !gpu_present || !gpu_frame_active ||
        I_VideoBuffer == NULL)
        return false;

    gpu_flush_affine_batch();
    gpu_flush_column_batch();
    gpu_flush_sprite_batch();

    if (!R_GPU_WallSegBegin(x1, x2, scale1, scalestep,
                            distance, offset, centerangle))
        return false;
    if (!R_GPU_WallTierBegin(0, blk, tex_height, widthmask, texturemid))
    {
        gpu_wall_seg_valid = 0;
        return false;
    }

    gpu_masked_active = 1;
    return true;
}

boolean R_GPU_MaskedPost(int x, int yl, int yh)
{
    if (!gpu_masked_active)
        return false;

    return R_GPU_WallTierColumn(0, x, yl, yh, spryscale);
}

void R_GPU_MaskedEnd(void)
{
    if (!gpu_masked_active)
        return;

    R_GPU_WallTiersEnd();
    gpu_masked_active = 0;
}



/* Bracket a CPU-drawn sprite (translated columns in MP have no GPU
 * path) so its cached framebuffer writes stay coherent with the GPU
 * work around it: without this the CPU columns reach SDRAM only on
 * random cache eviction — late evictions stamp stale 64-byte lines
 * over GPU pixels, unevicted lines vanish (horizontal banding). */
static uint8_t *gpu_cpu_sprite_band(uint32_t *size)
{
    uint8_t *base = gpu_draw_render_base != NULL
                  ? gpu_draw_render_base
                  : (uint8_t *)I_VideoBuffer;

    if (base == NULL)
        return NULL;

    *size = (uint32_t)viewheight * SCREENWIDTH;
    return base + (uint32_t)viewwindowy * SCREENWIDTH;
}

void R_GPU_BeginCPUSprite(void)
{
    uint8_t *band;
    uint32_t size;

    if (!gpu_present || !gpu_frame_active)
        return;

    band = gpu_cpu_sprite_band(&size);
    if (band == NULL)
        return;

    gpu_prepare_framebuffer_for_cpu();
    of_cache_inval_range(band, size);
}

void R_GPU_EndCPUSprite(void)
{
    uint8_t *band;
    uint32_t size;

    if (!gpu_present || !gpu_frame_active)
        return;

    band = gpu_cpu_sprite_band(&size);
    if (band == NULL)
        return;

    of_cache_flush_range(band, size);
}

boolean R_GPU_DeferLumpRelease(int lumpnum)
{
    if (!gpu_present || !gpu_frame_active ||
        (!gpu_pending && !gpu_has_pending_draw_batches()))
        return false;

    for (int i = 0; i < gpu_deferred_lump_count; i++)
    {
        if (gpu_deferred_lumps[i] == lumpnum)
            return true;
    }

    if (gpu_deferred_lump_count == GPU_DEFERRED_LUMPS)
    {
        gpu_finish_pending();
        return false;
    }

    gpu_deferred_lumps[gpu_deferred_lump_count++] = lumpnum;
    return true;
}

#endif
