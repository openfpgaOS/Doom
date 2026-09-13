/* Exercise the real post walk with both GPU surface types and CPU fallbacks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_types.h"
typedef int boolean;
typedef int32_t fixed_t;
typedef uint8_t byte;
typedef struct { byte topdelta, length, pad; } column_t;
#define true 1
#define false 0
#define FRACBITS 16
#define FRACUNIT 65536
#define LIGHTSCALESHIFT 12
#define MAXLIGHTSCALE 48
#define OF_GPU_PARAM_SPAN_MAX_RECORDS 512
#define GPU_SPRITE_MAX_RECORDS 512
static int viewwidth, viewheight, dc_x, dc_yl, dc_yh, detailshift;
static fixed_t dc_texturemid, dc_iscale, spryscale, sprtopscreen;
static byte *dc_source;
static short mfloorclip[320], mceilingclip[320];
static byte walllightrows[MAXLIGHTSCALE];
static int maskedcolormaprow, gpu_masked_active, gpu_sprite_active;
static of_gpu_param_span_record_t gpu_sprite_records[GPU_SPRITE_MAX_RECORDS];
static int gpu_sprite_record_count;
#include "gpu_wall_defs.h"
static FILE *output;
static uint32_t pixels, columns, flushes, fallback, consumed;
static byte posts[4096];
static void emit(const void *p, size_t n) { assert(fwrite(p, 1, n, output) == n); }
static void R_Perf_CountGpuColumn(unsigned n) { pixels += n; columns++; }
static void gpu_flush_wall_band(gpu_wall_tier_t *tier, gpu_wall_band_t *band) {
    if (!band->count) return;
    assert(band->count <= GPU_WALL_BAND_RECORDS);
    int32_t head[] = {1, tier-gpu_wall_tiers, band->light, band->count};
    emit(head, sizeof(head)); emit(band->records, 6*band->count);
    gpu_wall_record_count -= band->count; band->count = 0; flushes++;
}
static void gpu_flush_sprite_batch(void) {
    if (!gpu_sprite_record_count) return;
    assert(gpu_sprite_record_count <= GPU_SPRITE_MAX_RECORDS);
    int32_t head[] = {2, gpu_sprite_record_count};
    emit(head, sizeof(head)); emit(gpu_sprite_records, 6*gpu_sprite_record_count);
    gpu_sprite_record_count = 0; flushes++;
}
static void drawcolumn(void) {
    int32_t values[] = {3, dc_x, dc_yl, dc_yh, dc_texturemid,
                        dc_iscale, dc_source-posts, maskedcolormaprow};
    emit(values, sizeof(values)); fallback++;
}
static void (*colfunc)(void) = drawcolumn;
static void (*basecolfunc)(void) = drawcolumn;
static boolean R_DrawClampedMaskedColumn(int length) {
    (void)length; return false;
}
static boolean R_GPU_DrawColumnLightDirect(int x, int yl, int yh,
    const byte *src, int mid, int step, int light) {
    (void)x; (void)yl; (void)yh; (void)src; (void)mid; (void)step; (void)light;
    drawcolumn(); return true;
}
#include "gpu_functions.h"
static unsigned seed=0x8b2d397a;
static unsigned rnd(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
int main(int argc, char **argv) {
    assert(argc==2); output=fopen(argv[1], "wb"); assert(output);
    for (int surface=0; surface<3072; surface++) {
        gpu_masked_active=surface%3==0; gpu_sprite_active=surface%3==1;
        viewwidth=surface%7?320:160; viewheight=surface%5?168:96;
        detailshift=surface%13==0; maskedcolormaprow=surface%9?32:-1;
        for(int l=0;l<48;l++) walllightrows[l]=surface%11?rnd()%64:255;
        gpu_wall_tiers[0].active=surface%17!=0; gpu_wall_tiers[0].rr=0;
        for(int b=0;b<2;b++)gpu_wall_tiers[0].bands[b].light=-1;
        for(int c=0;c<640;c++) {
            dc_x=c%viewwidth;
            mfloorclip[dc_x]=(int)(rnd()%(viewheight+10))-5;
            mceilingclip[dc_x]=(int)(rnd()%(viewheight+10))-5;
            spryscale=surface%19?1+rnd()%(8*FRACUNIT):(int32_t)rnd();
            sprtopscreen=surface%19?((int)(rnd()%400)-200)*FRACUNIT:(int32_t)rnd();
            dc_texturemid=(int32_t)rnd(); dc_iscale=(int32_t)rnd();
            int at=0;
            /* Dense and disjoint posts, empty/zero-length columns, and enough
             * records to cross the real 512-entry capacity on both paths. */
            for(int n=0, count=rnd()%13;n<count;n++) {
                int length=rnd()%255;
                posts[at]=rnd()%255; posts[at+1]=length; posts[at+2]=0;
                for(int p=0;p<length;p++)posts[at+3+p]=(byte)rnd();
                posts[at+3+length]=0; at+=length+4;
            }
            posts[at]=255;
#ifdef HAS_MASKED_COLUMN
            if (R_GPU_DrawMaskedColumn(posts)) consumed++;
            else
#endif
                R_DrawMaskedColumn((column_t *)posts);
        }
        for(int b=0;b<2;b++)gpu_flush_wall_band(&gpu_wall_tiers[0],&gpu_wall_tiers[0].bands[b]);
        gpu_flush_sprite_batch(); assert(gpu_wall_record_count==0);
    }
    uint32_t totals[]={pixels, columns, flushes, fallback}; emit(totals,sizeof(totals));
    assert(columns>100000 && fallback>100000 && flushes>1000);
    assert(fclose(output)==0);
    printf("PASS %u columns; %u pixels; %u flushes; %u fallback posts; %u batch columns\n",
           columns,pixels,flushes,fallback,consumed);
}
