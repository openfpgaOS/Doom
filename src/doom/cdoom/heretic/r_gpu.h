/* r_gpu.h — openfpgaOS Heretic stub (not upstream chocolate-doom).
 *
 * The openfpgaOS port accelerates Doom's column/span drawing on the FPGA
 * through R_GPU_* (cdoom/doom/r_gpu.c). Heretic runs the stock software
 * renderer, so these are no-ops: UsingDirectFramebuffer() and PresentFrame()
 * report false, which routes the shared video/wad layer (shim/i_video.c,
 * cdoom/v_video.c, cdoom/w_wad.c) down the plain CPU framebuffer path.
 * Heretic's own renderer never references r_gpu. Only the shared/shim layer
 * includes this header when building the Heretic core. */
#ifndef OF_HERETIC_R_GPU_STUB_H
#define OF_HERETIC_R_GPU_STUB_H

#include "doomtype.h"

static inline void R_GPU_Init(void)     { }
static inline void R_GPU_Shutdown(void) { }
static inline void R_GPU_EndFrame(void) { }
static inline void R_GPU_PrepareForCPUAccess(void) { }
static inline void R_GPU_PrepareForCPUAccessRect(int x, int y, int w, int h)
{
    (void) x; (void) y; (void) w; (void) h;
}
static inline void R_GPU_TextureDataUpdated(void *ptr, unsigned int size)
{
    (void) ptr; (void) size;
}
static inline boolean R_GPU_PresentFrame(void)          { return false; }
static inline boolean R_GPU_UsingDirectFramebuffer(void) { return false; }

#endif /* OF_HERETIC_R_GPU_STUB_H */
