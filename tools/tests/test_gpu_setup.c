#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "gpu_types.h"
typedef int fixed_t;
typedef unsigned angle_t;
typedef unsigned char byte;
typedef int boolean;
#define true 1
#define false 0
#define FRACBITS 16
#define ANGLETOFINESHIFT 19
#define SCREENWIDTH 320
#define GPU_PLANE_BANDS 8
#define GPU_WALL_BANDS 2
#define GPU_WALL_TIERS 2
extern const int finesine[10240],finetangent[4096];
#define finecosine (finesine+2048)
static int viewwidth=320,viewheight=200,centerx=160,centery=100,detailshift;
static int centerxfrac=160<<16,viewwindowx,viewwindowy,viewx,viewy;
static unsigned viewangle,xtoviewangle[321];
static int gpu_present=1,gpu_frame_active=1,gpu_use_param_span=1,gpu_use_wall_param=1;
static int gpu_write_prepared=1,gpu_plane_band_rr,gpu_plane_fixed_light,gpu_plane_active;
static void *I_VideoBuffer=(void*)1,*gpu_src_tex=(void*)1;
static uint32_t gpu_fb_row_addr[200];
static of_gpu_param_span_list_t gpu_plane_params;
static struct {int light;} gpu_plane_bands[8];
typedef struct {int active,rr;of_gpu_param_span_list_t params;struct{int light;}bands[2];}gpu_wall_tier_t;
static gpu_wall_tier_t gpu_wall_tiers[2];
static int gpu_wall_seg_valid;
static float gpu_wall_zi_org,gpu_wall_zi_du,gpu_wall_szi_org,gpu_wall_szi_du,gpu_wall_texcol1;
static void gpu_prepare_for_gpu_write(void){gpu_write_prepared=1;}
static void gpu_flush_affine_batch(void){}
static void gpu_flush_column_batch(void){}
static uint32_t gpu_tex_addr(const void *p){return (uint32_t)(uintptr_t)p;}
#include "gpu_functions.h"
static unsigned seed=0x57492af1;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void save(FILE *f,int ok,of_gpu_param_span_list_t *p){assert(fwrite(&ok,4,1,f)==1);if(ok)assert(fwrite(p,sizeof(*p),1,f)==1);}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"wb");assert(f);
 unsigned cases=0;
 for(unsigned frame=0;frame<1000;frame++){
  viewwidth=32+2*(rnd()%145);viewheight=20+rnd()%181;detailshift=frame%2;
  centerx=viewwidth/2;centery=viewheight/2;centerxfrac=centerx<<16;
  viewx=(int)rnd();viewy=(int)(rnd()|1);viewangle=rnd();
  viewwindowx=(320-viewwidth)/2;viewwindowy=(200-viewheight)/2;
  for(unsigned y=0;y<200;y++)gpu_fb_row_addr[y]=0x13000000+y*320;
  for(int x=0;x<=320;x++)xtoviewangle[x]=(int64_t)(160-x)*0x20000000ll/160;
#ifdef HAS_BEGIN_VIEW
  R_GPU_BeginView();
#endif
  int heights[24];
  for(unsigned k=0;k<24;k++)heights[k]=k<8 ? (1+k)*16*65536 : 1+(rnd()%0x10000000);
  for(unsigned j=0;j<100;j++){
   int height=heights[j%24],light=(int)(rnd()%68)-2;
   if(j&1)height=-height;if(j==0)height=0;
   /* Flushes update light_origin in the private parameter block. A cache
    * hit must still open the next surface with the original zero light. */
   gpu_plane_params.light_origin=(rnd()%64)<<16;
   int ok=R_GPU_BeginPlaneSpans((void*)(uintptr_t)(0x11004000+(j%4)*4096),height,light);
   save(f,ok,&gpu_plane_params);cases++;
   int x1=rnd()%(viewwidth-1),x2=x1+rnd()%(viewwidth-x1);
   ok=R_GPU_WallSegBegin(x1,x2,1+rnd()%0x01000000,(int)(rnd()%4097)-2048,
                       (1+rnd()%4096)*65536,(int)(rnd()%0x08000000)-0x04000000,0x40000000);
   assert(ok);
   for(unsigned tier=0;tier<2;tier++){
    int texheight=1<<(3+rnd()%7),widthmask=(1<<(3+rnd()%7))-1;
    gpu_wall_tiers[tier].params.light_origin=(rnd()%64)<<16;
    int texturemid=(int)rnd();
    ok=(j%3==0) ? gpu_wall_tier_begin(tier,(void*)0x11000000,texheight,widthmask,texturemid,texheight)
                  : R_GPU_WallTierBegin(tier,(void*)0x11000000,texheight,widthmask,texturemid);
    save(f,ok,&gpu_wall_tiers[tier].params);cases++;
   }
  }
 }
 assert(fclose(f)==0);printf("PASS %u GPU setup cases\n",cases);
}
