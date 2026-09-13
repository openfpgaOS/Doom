/* Compare actual producer records/eviction order, including capacity flushes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gpu_types.h"
typedef int boolean;
#define false 0
#define true 1
#define GPU_PLANE_BANDS 8
#define GPU_PLANE_BAND_RECORDS 512
static int viewwidth,viewheight,gpu_plane_active,gpu_plane_fixed_light;
static int gpu_plane_band_rr,gpu_plane_record_count;
typedef struct {int light,count;of_gpu_param_span_record_t records[512];} gpu_plane_band_t;
static gpu_plane_band_t gpu_plane_bands[8];
static FILE *output;
static unsigned flushed,flushes;
static void R_Perf_CountGpuSpan(unsigned count) {(void)count;}
static void gpu_flush_plane_band(gpu_plane_band_t *band) {
    if(band->count<=0)return;
    assert(band->count<=512 && band->light>=0 && band->light<=63);
    assert(fwrite(&band->light,4,1,output)==1);
    assert(fwrite(&band->count,4,1,output)==1);
    assert(fwrite(band->records,6,band->count,output)==(unsigned)band->count);
    flushed+=band->count;flushes++;
    gpu_plane_record_count-=band->count;band->count=0;
}
#include "gpu_producer.h"
static unsigned seed=0x84102832;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(int argc,char **argv) {
    assert(argc==2);output=fopen(argv[1],"wb");assert(output);
    for(unsigned plane=0;plane<128;plane++) {
        for(unsigned i=0;i<8;i++){assert(gpu_plane_bands[i].count==0);gpu_plane_bands[i].light=-1;}
        assert(gpu_plane_record_count==0);gpu_plane_band_rr=0;
        viewwidth=32+rnd()%289;viewheight=20+rnd()%181;
        gpu_plane_fixed_light=(int)(rnd()%65)-1;
        for(unsigned j=0;j<2000;j++) {
            int y=rnd()%viewheight,x1=rnd()%viewwidth,x2=x1+rnd()%(viewwidth-x1);
            int light=plane%4==0?3:plane%4==1?j%8:plane%4==2?j%64:rnd()%64;
            if(j%97==0)light=64;if(j%99==0)light=-1;
            if(j%101==0)y=-1;if(j%103==0)y=viewheight;
            if(j%107==0)x1=-1;if(j%109==0)x2=viewwidth;
            if(j%113==0)x2=x1-1;
            gpu_plane_active=j%127!=0;
            int ok=R_GPU_PlaneSpanLight(y,x1,x2,light);
            assert(fwrite(&ok,4,1,output)==1);
        }
        for(unsigned i=0;i<8;i++)gpu_flush_plane_band(&gpu_plane_bands[i]);
    }
    assert(flushed>100000 && flushes>1000);
    assert(fclose(output)==0);
    printf("PASS 256000 producer calls, %u records, %u flushes\n",flushed,flushes);
}
