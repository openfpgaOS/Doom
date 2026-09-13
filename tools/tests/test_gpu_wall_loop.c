/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Run the real wall loop with deterministic clipping and GPU/fallback mocks. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define OF_FASTRAM_H
#define OF_FASTTEXT __attribute__((noinline))
#include WALL_SOURCE

int viewwidth = 320, viewheight = 168, detailshift, bsp_view_validcount;
int centery = 84;
fixed_t viewx, viewy;
angle_t viewangle;
int dc_x, dc_yl, dc_yh, dc_texheight;
fixed_t dc_iscale, dc_texturemid;
byte *dc_source, *dc_translation;
lighttable_t *dc_colormap, *fixedcolormap;
void (*colfunc)(void), (*basecolfunc)(void);
short ceilingclip[SCREENWIDTH], floorclip[SCREENWIDTH];
visplane_t *ceilingplane, *floorplane;
fixed_t *textureheight;
angle_t xtoviewangle[SCREENWIDTH+1];
static lighttable_t maps[48][256];
static lighttable_t *lights[48];
static byte lightrows[48];
static byte texels[8][128], *columns[8];
static fixed_t heights[4] = {128*FRACUNIT,128*FRACUNIT,128*FRACUNIT,128*FRACUNIT};
static short masked_columns[SCREENWIDTH];
static visplane_t cp, fp;
static unsigned selected_tex, gpu_mask, seed = 0x9a76042d;
static unsigned gpu_columns, fallback_columns;
static void emit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t values[] = {a,b,c,d,e};
    assert(fwrite(values, sizeof(values), 1, stdout) == 1);
}
static unsigned rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
void I_Error(const char *error, ...) { (void)error; abort(); }
byte **R_GetColumnTable(int tex) { (void)tex; return columns; }
int R_GetTextureWidthMask(int tex) { (void)tex; return 7; }
byte *R_GetWallTexture2D(int tex) { (void)tex; return texels[0]; }
void R_GPU_UseWallTexture(int tex) { selected_tex = tex; }
boolean R_GPU_WallSegBegin(int x1, int x2, fixed_t s, fixed_t ds, fixed_t dist, fixed_t off, angle_t ang)
{ (void)x1;(void)x2;(void)s;(void)ds;(void)dist;(void)off;(void)ang;return true; }
boolean R_GPU_WallTierBegin(int t, const byte *tex, int h, int mask, fixed_t mid)
{ (void)t;(void)tex;(void)h;(void)mask;(void)mid;return (gpu_mask & (1u << selected_tex)) != 0; }
boolean R_GPU_WallTierColumn(int tier, int x, int yl, int yh, fixed_t scale)
{
    assert(x >= 0 && x < viewwidth && yl >= 0 && yh < viewheight && yl <= yh);
    unsigned lightindex = (unsigned)scale >> LIGHTSCALESHIFT;
    if (lightindex >= MAXLIGHTSCALE) lightindex = MAXLIGHTSCALE - 1;
    if (walllightrows[lightindex] >= 64) return false;
    gpu_columns++;
    emit(1, tier, x, (yl << 16) | yh, scale);
    return true;
}
#ifdef TEST_WALL_COLUMN_BATCH
void R_GPU_WallColumns(int x, int count, const r_gpu_wall_column_t *columns,
                       fixed_t scale, fixed_t scalestep)
{
    for (int i = 0; i < count; ++i, ++x) {
        if (columns[i].upper >> 16) {
            int y = columns[i].upper & 0xffffu;
            assert(R_GPU_WallTierColumn(0, x, y, y + (columns[i].upper >> 16) - 1, scale));
        }
        if (columns[i].lower >> 16) {
            int y = columns[i].lower & 0xffffu;
            assert(R_GPU_WallTierColumn(1, x, y, y + (columns[i].lower >> 16) - 1, scale));
        }
        scale = (fixed_t)((uint32_t)scale + (uint32_t)scalestep);
    }
}
#endif
void R_GPU_WallTiersEnd(void) { emit(2,0,0,0,0); }
boolean R_GPU_DrawColumnLightDirect(int x,int yl,int yh,const byte *src,int tm,int iscale,int light)
{ (void)src;fallback_columns++;emit(3,x,yl,yh,light);emit(4,tm,iscale,0,0);return true; }
boolean R_GPU_DrawColumnLightBatchDirect(int x,int yl,int yh,int n,const byte *const *src,const int32_t *t,const int32_t *step,const uint8_t *light)
{ (void)src;fallback_columns+=n;for(int i=0;i<n;i++){emit(5,x+i,yl,yh,light[i]);emit(6,t[i],step[i],0,0);}return true; }
boolean R_GPU_DrawColumnLightVarBatchDirect(int x,int n,const int *yl,const int *yh,const byte *const *src,const int32_t *t,const int32_t *step,const uint8_t *light)
{ (void)src;fallback_columns+=n;for(int i=0;i<n;i++){emit(7,x+i,yl[i],yh[i],light[i]);emit(8,t[i],step[i],0,0);}return true; }
static void drawcolumn(void) { fallback_columns++;emit(9,dc_x,dc_yl,dc_yh,dc_iscale); }
int main(void)
{
    for(int i=0;i<8;i++)columns[i]=texels[i];
    for(int i=0;i<48;i++){lights[i]=maps[i];lightrows[i]=i%32;}
    walllights=lights;walllightrows=lightrows;textureheight=heights;
    colfunc=basecolfunc=drawcolumn;
    for (int trial=0;trial<12000;trial++) {
        unsigned r=rnd();
        viewwidth=trial%3==0?160:320;viewheight=trial%5==0?96:168;
        rw_x=r%viewwidth;rw_stopx=rw_x+1+(r>>16)%(viewwidth-rw_x);
        rw_scale=FRACUNIT+(rnd()%FRACUNIT);rw_scalestep=(int)(rnd()%201)-100;
        topfrac=((int)(rnd()%256)-48)*HEIGHTUNIT;
        bottomfrac=((int)(rnd()%256)-48)*HEIGHTUNIT;
        topstep=(int)(rnd()%65)-32;bottomstep=(int)(rnd()%65)-32;
        pixhigh=((int)(rnd()%256)-48)*HEIGHTUNIT;
        pixlow=((int)(rnd()%256)-48)*HEIGHTUNIT;
        pixhighstep=(int)(rnd()%65)-32;pixlowstep=(int)(rnd()%65)-32;
        markceiling=r&1;markfloor=(r>>1)&1;
        midtexture=trial%4==0?1:0;toptexture=midtexture?0:2;
        bottomtexture=midtexture?0:3;
        if(trial%7==0)toptexture=0;if(trial%11==0)bottomtexture=0;
        maskedtexture=trial%9==0 && !midtexture;
        fixedcolormap=trial%13==0?maps[0]:NULL;
        for (int i=0; i<48; i++)
            lightrows[i]=fixedcolormap?(trial%26==0?32:255):i%32;
        detailshift=trial%17==0;
        gpu_mask=trial%6==0?0:trial%6==1?4:14;
        rw_distance=2*FRACUNIT;rw_offset=3*FRACUNIT;rw_centerangle=ANG90;
        rw_midtexturemid=2*FRACUNIT;rw_toptexturemid=3*FRACUNIT;rw_bottomtexturemid=4*FRACUNIT;
        memset(&cp,0xa5,sizeof(cp));memset(&fp,0xa5,sizeof(fp));
        ceilingplane=&cp;floorplane=&fp;maskedtexturecol=masked_columns;
        for(int x=0;x<viewwidth;x++) {
            ceilingclip[x]=(int)(rnd()%(viewheight+1))-1;
            floorclip[x]=rnd()%(viewheight+1);
            masked_columns[x]=-1234;
        }
        R_RenderSegLoop();
        emit(10,rw_x,rw_scale,topfrac,bottomfrac);emit(11,pixhigh,pixlow,0,0);
        assert(fwrite(ceilingclip,sizeof(short),viewwidth,stdout)==(size_t)viewwidth);
        assert(fwrite(floorclip,sizeof(short),viewwidth,stdout)==(size_t)viewwidth);
        assert(fwrite(masked_columns,sizeof(short),viewwidth,stdout)==(size_t)viewwidth);
        assert(fwrite(&cp,sizeof(cp),1,stdout)==1);assert(fwrite(&fp,sizeof(fp),1,stdout)==1);
    }
    assert(gpu_columns > 10000 && fallback_columns > 10000);
    fprintf(stderr, "%u GPU columns, %u fallback columns\n", gpu_columns, fallback_columns);
    return 0;
}
