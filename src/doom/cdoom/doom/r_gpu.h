//
// Doom renderer hooks for the openfpgaOS span GPU.
//

#ifndef __R_GPU__
#define __R_GPU__

#include "doomtype.h"

extern int r_gpu_enabled;

void R_GPU_Init(void);
void R_GPU_Shutdown(void);
void R_GPU_BeginDisplayFrame(void);
void R_GPU_BeginFrame(void);
void R_GPU_EndFrame(void);
void R_GPU_PrepareForCPUAccess(void);
void R_GPU_TextureDataUpdated(void *ptr, unsigned int size);
boolean R_GPU_PresentFrame(void);
boolean R_GPU_UsingDirectFramebuffer(void);
int R_GPU_CurrentDrawSlot(void);

boolean R_GPU_DrawColumn(void);
boolean R_GPU_DrawSpan(void);
boolean R_GPU_DeferLumpRelease(int lumpnum);

#endif
