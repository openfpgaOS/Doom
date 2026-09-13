/* Compare emitted spans while alternating GPU acceptance, heights and frames. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define OF_FASTRAM_H
#define OF_FASTTEXT __attribute__((noinline))
#include PLANE_SOURCE
int viewwidth=320, viewheight=168, centerx=160, centery=84;
fixed_t centerxfrac=160*FRACUNIT, viewx, viewy;
angle_t viewangle, xtoviewangle[SCREENWIDTH+1];
lighttable_t *fixedcolormap, *ds_colormap;
byte *ds_source;
int ds_y,ds_x1,ds_x2;
fixed_t ds_xfrac,ds_yfrac,ds_xstep,ds_ystep;
void (*spanfunc)(void);
static byte maps[64][256], rows[128];
static lighttable_t *lights[128];
static int accept_param,accept_direct;
static unsigned seed=0x78532acd, param_count, fallback_count;
static unsigned rnd(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static void emit(int type,int y,int x1,int x2,int xf,int yf,int xs,int ys,int light) {
 int32_t r[]={type,y,x1,x2,xf,yf,xs,ys,light};
 assert(fwrite(r,sizeof(r),1,stdout)==1);
}
void I_Error(const char *error, ...) {(void)error;abort();}
boolean R_GPU_PlaneSpanLight(int y,int x1,int x2,int light) {
 emit(1,y,x1,x2,0,0,0,0,light);param_count+=accept_param;return accept_param;
}
boolean R_GPU_DrawSpanLightDirect(int y,int x1,int x2,const byte *s,fixed_t xf,fixed_t yf,fixed_t xs,fixed_t ys,int light) {
 (void)s;emit(2,y,x1,x2,xf,yf,xs,ys,light);fallback_count++;return accept_direct;
}
boolean R_GPU_DrawSpanDirect(int y,int x1,int x2,const byte *s,fixed_t xf,fixed_t yf,fixed_t xs,fixed_t ys,const byte *map) {
 (void)s;emit(3,y,x1,x2,xf,yf,xs,ys,(map-maps[0])/256);fallback_count++;return accept_direct;
}
void R_DrawSpan(void) {
 emit(4,ds_y,ds_x1,ds_x2,ds_xfrac,ds_yfrac,ds_xstep,ds_ystep,(ds_colormap-maps[0])/256);fallback_count++;
}
static void lowdetail(void) {R_DrawSpan();}
int main(void) {
 for(int i=0;i<128;i++){rows[i]=i%32;lights[i]=maps[rows[i]];}
 planezlight=lights;planezlightrow=rows;ds_source=maps[0];
 for(int frame=0;frame<256;frame++) {
  viewwidth=frame%3?320:160;centerx=viewwidth/2;centerxfrac=centerx*FRACUNIT;
  viewheight=frame%5?168:96;viewangle=ANG90;
  viewx=(int)(rnd()%65536000)-32768000;viewy=(int)(rnd()%65536000)-32768000;
  for(int x=0;x<viewwidth;x++){xtoviewangle[x]=(angle_t)((int64_t)(centerx-x)*ANG45/centerx);distscale[x]=FRACUNIT+(rnd()%FRACUNIT);}
  for(int y=0;y<viewheight;y++)yslope[y]=FRACUNIT+(rnd()%(FRACUNIT*4));
  R_ClearPlanes();
  /* Reset through production code, then vary both step signs without the
   * legacy FixedDiv negative-left-shift in the host frame-setup helper. */
  viewangle=rnd();basexscale=(int)(rnd()%2048)-1024;baseyscale=(int)(rnd()%2048)-1024;
  for(int call=0;call<1024;call++) {
   int y=rnd()%viewheight,x=rnd()%viewwidth,end=x+rnd()%(viewwidth-x);
   planeheight=((call/7)%11)*FRACUNIT;
   fixedcolormap=call%5==0?maps[32]:NULL;
   accept_param=(call%4)!=0;accept_direct=(call%7)!=0;
   spanfunc=call%17==0?lowdetail:R_DrawSpan;
   R_MapPlane(y,x,end);
  }
 }
 assert(param_count>100000 && fallback_count>50000);
 fprintf(stderr,"%u accepted param spans, %u fallback emissions\n",param_count,fallback_count);
}
