/* Compare actual wall records and flush order for scalar and batch producers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gpu_types.h"
typedef int boolean;
typedef int32_t fixed_t;
typedef struct { uint32_t upper, lower; } r_gpu_wall_column_t;
#define true 1
#define false 0
#define LIGHTSCALESHIFT 12
#define MAXLIGHTSCALE 48
#define OF_GPU_PARAM_SPAN_MAX_RECORDS 512
static int viewwidth=320,viewheight=200;
static uint8_t walllightrows[MAXLIGHTSCALE];
#include "gpu_wall_defs.h"
static FILE *output;
static uint32_t total_pixels,total_columns,flushes;
static void R_Perf_CountGpuColumn(unsigned n) {total_pixels+=n;total_columns++;}
static void gpu_flush_wall_band(gpu_wall_tier_t *tier,gpu_wall_band_t *band) {
 if(!band->count)return;
 assert(band->count>0 && band->count<=GPU_WALL_BAND_RECORDS && band->light>=0 && band->light<64);
 int32_t head[]={tier-gpu_wall_tiers,band->light,band->count};
 assert(fwrite(head,sizeof(head),1,output)==1);
 assert(fwrite(band->records,6,band->count,output)==(unsigned)band->count);
 gpu_wall_record_count-=band->count;band->count=0;flushes++;
}
#include "gpu_wall_functions.h"
#ifndef HAS_WALL_BATCH
static void R_GPU_WallColumns(int x,int n,const r_gpu_wall_column_t *columns,fixed_t scale,fixed_t step) {
 for(int i=0;i<n;i++,x++) {
  for(int t=0;t<2;t++) {
   uint32_t p=t?columns[i].lower:columns[i].upper;
   if(p>>16) assert(R_GPU_WallTierColumn(t,x,p&65535u,(p&65535u)+(p>>16)-1,scale));
  }
  scale=(fixed_t)((uint32_t)scale+(uint32_t)step);
 }
}
#endif
static unsigned seed=0x41a72ccd;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(int argc,char **argv) {
 assert(argc==2);output=fopen(argv[1],"wb");assert(output);
 r_gpu_wall_column_t cols[320];
 for(int seg=0;seg<1024;seg++) {
  for(int t=0;t<2;t++) {
   gpu_wall_tiers[t].active=1;gpu_wall_tiers[t].rr=0;
   for(int b=0;b<2;b++){assert(!gpu_wall_tiers[t].bands[b].count);gpu_wall_tiers[t].bands[b].light=-1;}
  }
  for(int i=0;i<48;i++)walllightrows[i]=seg%4==0?32:seg%4==1?i/2:rnd()%64;
  /* Repeated ranges exercise capacity drains and stable/alternating light tags. */
  for(int range=0;range<12;range++) {
   int x=rnd()%320,n=1+rnd()%(320-x);
   fixed_t scale=rnd(),step=rnd();
   if(seg%3){scale=rnd()%262144;step=(int)(rnd()%1001)-500;}
   for(int i=0;i<n;i++) {
    uint32_t y=rnd()%200,n1=rnd()%(201-y);cols[i].upper=(n1<<16)|y;
    y=rnd()%200;uint32_t n2=rnd()%(201-y);cols[i].lower=(n2<<16)|y;
    if(seg%5==0)cols[i].lower=0;
   }
   R_GPU_WallColumns(x,n,cols,scale,step);
  }
  for(int t=0;t<2;t++)for(int b=0;b<2;b++)gpu_flush_wall_band(&gpu_wall_tiers[t],&gpu_wall_tiers[t].bands[b]);
  assert(gpu_wall_record_count==0);
 }
 assert(total_columns>100000 && flushes>1000);
 assert(fwrite(&total_pixels,4,1,output)==1);assert(fwrite(&total_columns,4,1,output)==1);
 assert(fclose(output)==0);
 printf("PASS %u columns; %u pixels; %u flushes\n",total_columns,total_pixels,flushes);
}
