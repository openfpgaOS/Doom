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
boolean R_GPU_DeferLumpRelease(int lumpnum);

#endif
