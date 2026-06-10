//
// Doom renderer hooks for the openfpgaOS span GPU.
//

#ifndef __R_GPU__
#define __R_GPU__

#include "doomtype.h"
#include "m_fixed.h"

extern int r_gpu_enabled;

void R_GPU_Init(void);
void R_GPU_Shutdown(void);
void R_GPU_BeginDisplayFrame(void);
void R_GPU_BeginFrame(void);
void R_GPU_EndFrame(void);
void R_GPU_PrepareForCPUAccess(void);
void R_GPU_PrepareForCPUAccessRect(int x, int y, int w, int h);
void R_GPU_TextureDataUpdated(void *ptr, unsigned int size);
void R_GPU_TextureDataFlushAll(void);
boolean R_GPU_PresentFrame(void);
boolean R_GPU_UsingDirectFramebuffer(void);
int R_GPU_CurrentDrawSlot(void);

boolean R_GPU_DrawColumn(void);
boolean R_GPU_DrawTLColumn(void);
/* Raven sky: raw texel column from a pre-offset source pointer. */
boolean R_GPU_DrawSkyColumn(int x, int yl, int yh, const byte *source);
boolean R_GPU_TLEnabled(void);  /* false under -nogputl (CPU TL columns) */
/* Drain the GPU before a translucent sprite: TL columns READ the
 * framebuffer, and pixels written by immediately-preceding commands may
 * still be in the GPU's write pipeline rather than SDRAM — blending
 * against stale background in write-burst-sized runs. */
void R_GPU_TLSpriteSync(void);  /* Heretic: tinttable blend (TRANSLUC) */
boolean R_GPU_DrawColumnDirect(int x, int yl, int yh, const byte *source,
                               int texturemid, int iscale,
                               const byte *colormap);
boolean R_GPU_CanDrawFuzz(void);
boolean R_GPU_BeginFuzzSpans(void);
void R_GPU_EndFuzzSpans(void);
boolean R_GPU_DrawFuzzColumnDirect(int x, int yl, int yh);
int R_GPU_ColormapRow(const byte *map);
boolean R_GPU_DrawColumnLightDirect(int x, int yl, int yh, const byte *source,
                                    int texturemid, int iscale, int light);
boolean R_GPU_DrawColumnLightBatchDirect(int x, int yl, int yh, int lanes,
                                         const byte *const *source,
                                         const int32_t *t,
                                         const int32_t *tstep,
                                         const uint8_t *light);
boolean R_GPU_DrawColumnLightVarBatchDirect(int x, int lanes,
                                            const int *yl,
                                            const int *yh,
                                            const byte *const *source,
                                            const int32_t *t,
                                            const int32_t *tstep,
                                            const uint8_t *light);
boolean R_GPU_DrawSpan(void);
boolean R_GPU_DrawSpanDirect(int y, int x1, int x2, const byte *source,
                             fixed_t xfrac, fixed_t yfrac,
                             fixed_t xstep, fixed_t ystep,
                             const byte *colormap);
boolean R_GPU_DrawSpanLightDirect(int y, int x1, int x2, const byte *source,
                                  fixed_t xfrac, fixed_t yfrac,
                                  fixed_t xstep, fixed_t ystep, int light);

/* Param-record visplane path (GPU_CMD_DRAW_PARAM_SPAN_LIST, ATTR_PERSP_Q29):
 * one perspective-plane header per visplane light band, 1.5 words per span
 * record instead of ~8 words per affine lane.  Begin computes the s/z, t/z,
 * 1/z screen-space planes for the current visplane (they are exactly affine
 * for a horizontal plane); spans then submit as bare {x1, y, count} records.
 * height_delta is pl->height - viewz (signed; sign picks the horizon side).
 * fixed_light >= 0 pins the colormap row (fixedcolormap); -1 = per-span
 * zlight rows.  Returns false (and draws nothing) when the param-span path
 * is unavailable — callers fall back to the per-span emission. */
boolean R_GPU_BeginPlaneSpans(const byte *source, fixed_t height_delta,
                              int fixed_light);
boolean R_GPU_PlaneSpanLight(int y, int x1, int x2, int light);
void R_GPU_EndPlaneSpans(void);

/* Param-record wall path (ATTR_PERSP_Q29, AXIS_Y records): a wall seg is a
 * vertical world plane, so s/z, t/z, 1/z are affine in screen space — the
 * zi plane comes straight from rw_scale/rw_scalestep (Doom's own linear
 * scale walk IS the exact affine zi).  One command per tier x light band;
 * each column is a 1.5-word {x, ytop, count} record against a flat 2D
 * texture block from R_GetWallTexture2D().  Tier 0 = mid/top (mutually
 * exclusive), tier 1 = bottom.  All Begin calls return false when the
 * path is unavailable — callers keep the column-batch emission. */
boolean R_GPU_WallSegBegin(int x1, int x2, fixed_t scale1, fixed_t scalestep,
                           fixed_t distance, fixed_t offset,
                           unsigned int centerangle); /* angle_t */
boolean R_GPU_WallTierBegin(int tier, const byte *tex2d, int tex_height,
                            int widthmask, fixed_t texturemid);
/* scale = rw_scale at this column; the light row (walllightrows) lookup
 * happens inside so the OF_FASTTEXT seg loop stays small. */
boolean R_GPU_WallTierColumn(int tier, int x, int yl, int yh, fixed_t scale);
void R_GPU_WallTiersEnd(void);

/* Affine sprite path (ATTR_AFFINE, AXIS_Y records): one param command
 * per vissprite against a flat 2D block from R_GetSpriteTexture2D();
 * posts append {x, ytop, count} records.  Planes are exact Q16.16
 * integers (bit-identical to the software walk).  translucent = ghost
 * sprites (TINTTAB blend); Begin returns false when unavailable. */
boolean R_GPU_SpriteBegin(const byte *tex2d, int tex_height, int tex_width,
                          fixed_t texturemid, fixed_t iscale,
                          fixed_t startfrac, fixed_t xiscale, int x1,
                          int light, boolean translucent, int cmap_slot);
boolean R_GPU_SpritePost(int x, int yl, int yh);
void R_GPU_SpriteEnd(void);

/* Coherence bracket for sprites whose columns draw on the CPU
 * (translated / alt-TL).  See r_gpu.c for the failure mode. */
void R_GPU_BeginCPUSprite(void);
void R_GPU_EndCPUSprite(void);

/* Param-masked midtextures: wall-tier machinery during the masked
 * phase; geometry comes from the drawseg stash (gpu_m*).  Posts append
 * via R_GPU_MaskedPost; Begin false = keep column emission. */
boolean R_GPU_MaskedBegin(const byte *blk, int tex_height, int widthmask,
                          fixed_t texturemid, int x1, int x2,
                          fixed_t scale1, fixed_t scalestep,
                          fixed_t distance, fixed_t offset,
                          unsigned int centerangle);
boolean R_GPU_MaskedPost(int x, int yl, int yh);
void R_GPU_MaskedEnd(void);

boolean R_GPU_DeferLumpRelease(int lumpnum);

#endif
