#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "gpu_types.h"

#define GPU_CMD_DRAW_PARAM_SPAN_LIST 0x48
#define GPU_CMD_PARAM_SPAN_CONT 0x58
#define OF_GPU_PARAM_SPAN_MAX_RECORDS 512u
#define OF_GPU_PARAM_SPAN_LIST_WORDS(n) (31u + 3u * (((n) + 1u) >> 1))
#define OF_HW_GPU_PARAM_SPAN_Q29_SCALE 1
#define OF_HW_GPU_SPAN_CONT 2
static unsigned features = 6;
static int of_has_feature(unsigned bit) { return (features >> bit) & 1; }
static uint32_t stream[4096];
static unsigned cursor, reserved, emitted_cases, emitted_words;
static uint32_t _gpu_span_hdr_cache[29];
static int _gpu_span_hdr_valid;
static void _gpu_cmd_header(unsigned cmd, unsigned n) {
    assert(cursor == reserved);
    reserved = cursor + 1 + n;
    assert(reserved <= sizeof(stream) / sizeof(*stream));
    stream[cursor++] = (cmd << 24) | n;
}
static uint32_t *_gpu_ring_claim(void) { return stream + cursor; }
static void _gpu_ring_commit(unsigned n) { cursor += n; assert(cursor <= reserved); }
#include "gpu_functions.h"

static uint32_t seed = 0x3853abcdu;
static uint32_t rnd(void) { seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5; return seed; }
static void save(FILE *f) {
    assert(cursor == reserved);
    emitted_cases += cursor != 0;
    emitted_words += cursor;
    assert(fwrite(&cursor, 4, 1, f) == 1);
    assert(fwrite(stream, 4, cursor, f) == cursor);
    cursor = reserved = 0;
    for (unsigned i=0;i<4096;i++)stream[i]=0xdeadbeef;
}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"wb");assert(f);
    of_gpu_param_span_list_t p={0};
    p.tex_width=128;p.tex_w_mask=p.tex_h_mask=127;
    p.flags=OF_GPU_SPAN_COLORMAP|OF_GPU_SPAN_PERSP;
    p.attr_mode=OF_GPU_PARAM_ATTR_PERSP_Q29;
    unsigned cases=0;
    for(unsigned n=0;n<=OF_GPU_PARAM_SPAN_MAX_RECORDS+1;n++)
    for(unsigned offset=0;offset<=2;offset+=2)
    for(unsigned mode=0;mode<4;mode++) {
        unsigned count=n>OF_GPU_PARAM_SPAN_MAX_RECORDS?OF_GPU_PARAM_SPAN_MAX_RECORDS:n;
        unsigned char *allocation=malloc(offset+count*sizeof(of_gpu_param_span_record_t));
        assert(allocation);
        of_gpu_param_span_record_t *r=(void *)(allocation+offset);
        for(unsigned i=0;i<count;i++) {
            r[i].u=rnd();r[i].v=rnd();
            r[i].count=mode==0?0:mode==1?rnd()%321:mode==2?rnd()%4096:rnd();
        }
        features=(n%5==0)?0:6;
        p.q29_attr_shift=n%32;
        p.attr_origin[0]=rnd();p.light_origin=(rnd()%64)<<16;
        p.span_axis=n%2;p.z_mode=n%7; // includes rejected depth modes
        of_gpu_draw_param_span_list(&p,r,n);save(f);
        // Same surface, then lighting change, then explicit invalidation.
        of_gpu_draw_param_span_list(&p,r,n);save(f);
        p.light_origin^=1u<<16;
        of_gpu_draw_param_span_list(&p,r,n);save(f);
        _gpu_span_hdr_valid=0;
        of_gpu_draw_param_span_list(&p,r,n);save(f);
        free(allocation);cases+=4;
    }
    for(unsigned n=0;n<1000;n++) {
        of_gpu_persp_span_group_t p={0};
        p.lane_count=n%10;p.tex_width=64;p.light=rnd();
        for(unsigned i=0;i<8;i++){p.start[i]=rnd();p.count[i]=rnd();}
        of_gpu_draw_persp_span_group(&p);save(f);cases++;
    }
#ifdef TEST_DOOM_SPANS
    features=6;p.attr_mode=OF_GPU_PARAM_ATTR_PERSP_Q29;p.q29_attr_shift=4;p.z_mode=0;
    for(unsigned n=1;n<=512;n++)for(unsigned offset=0;offset<=2;offset+=2) {
        unsigned char *allocation=malloc(offset+n*sizeof(of_gpu_param_span_record_t));assert(allocation);
        of_gpu_param_span_record_t *r=(void *)(allocation+offset);
        for(unsigned i=0;i<n;i++){r[i].u=rnd()%320;r[i].v=rnd()%200;r[i].count=1+rnd()%320;}
        gpu_emit_screen_span_records(&p,r,n);save(f);
        p.light_origin^=1u<<16;
        gpu_emit_screen_span_records(&p,r,n);save(f);
        free(allocation);cases+=2;
    }
#endif
    assert(fclose(f)==0);assert(emitted_cases > 1000 && emitted_words > 100000);
    printf("PASS %u command cases (%u emitted, %u words)\n",cases,emitted_cases,emitted_words);
}
