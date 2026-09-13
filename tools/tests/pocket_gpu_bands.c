/* Time complete Doom end-surface submission in normal application ELFs.
 * Batches stay below the DMA kick threshold; the consumer is always ready. */
#include "gpu_types.h"
#include "of_caps.h"
#include "symbols.h"
#define U(n) (*(volatile uint32_t *)(uintptr_t)A_##n)
#define P(n) ((volatile uint32_t *)(uintptr_t)A_##n)
#define F(n,t) ((t)(uintptr_t)A_##n)
static void puts_uart(const char *s) { while (*s) { while (!(*(volatile unsigned *)0x4f000000u & 2)) {} *(volatile unsigned *)0x4f000004u = (unsigned char)*s++; } }
static void hex(unsigned x) { char b[10]; for (int i=0;i<8;i++) b[i]="0123456789abcdef"[(x>>(28-4*i))&15]; b[8]=' ';b[9]=0;puts_uart(b); }
static unsigned cycle(void) { return *(volatile unsigned *)0x40000004u; }
static unsigned crc=0x811c9dc5;
static void hash(volatile const unsigned *p,unsigned words) { for(unsigned i=0;i<words;i++) crc=(crc^p[i])*16777619u; }
static void fail(void) { puts_uart("FAIL bands\n"); for (;;) {} }
void trap_report(unsigned cause,unsigned pc,unsigned addr) { puts_uart("TRAP ");hex(cause);hex(pc);hex(addr);puts_uart("\n");fail(); }
static struct of_capabilities caps;
int main(void)
{
    static const unsigned sizes[]={1,2,3,8,16,32,64,128,256};
    U(_of_caps_ptr)=(uintptr_t)&caps;
    U(_gpu_batch_buf)=(uintptr_t)P(_gpu_batch_storage);
    U(gpu_spancont_enabled)=1;
    caps.hw_features=OF_HW_GPU_PARAM_SPAN_Q29_SCALE|OF_HW_GPU_SPAN_CONT;
    puts_uart("BAND SUBMISSION BEGIN\n");
    for(unsigned kind=0;kind<3;kind++)for(unsigned pattern=0;pattern<3;pattern++)for(unsigned s=0;s<9;s++) {
        unsigned n=sizes[s],elapsed=0;
        volatile unsigned *band=kind==0 ? P(gpu_plane_bands) : P(gpu_wall_tiers)+31;
        of_gpu_param_span_list_t *params=(void *)(kind==0 ? P(gpu_plane_params) : kind==1 ? P(gpu_wall_tiers)+2 : P(gpu_sprite_params));
        of_gpu_param_span_record_t *records=(void *)(kind==2 ? P(gpu_sprite_records) : band+2);
        /* Match the real structs without changing application code. */
        if(SIZE_gpu_wall_tiers!=12568||SIZE_gpu_plane_params!=116)fail();
        for(unsigned j=0;j<29;j++)((unsigned *)params)[j]=0;
        params->fb_base=0x03000000;params->fb_major_step=kind==0?320:1;params->fb_minor_step=kind==0?1:320;
        params->tex_addr=0x01000000;params->tex_width=64;params->tex_w_mask=63;params->tex_h_mask=63;
        params->attr_mode=kind==2?OF_GPU_PARAM_ATTR_AFFINE:OF_GPU_PARAM_ATTR_PERSP_Q29;
        params->span_axis=kind==0?OF_GPU_PARAM_AXIS_X:OF_GPU_PARAM_AXIS_Y;
        params->flags=OF_GPU_SPAN_COLORMAP | (kind==2?0:OF_GPU_SPAN_PERSP);
        params->attr_origin[0]=0x10000000;params->attr_origin[1]=0x20000000;params->attr_origin[2]=0x08000000;
        params->attr_du[0]=0x01000000;params->attr_dv[1]=0x01000000;
        for(unsigned j=0;j<n;j++) {
            records[j].u=(j*13)%160;records[j].v=(j*7)%100;
            records[j].count=1+(j*11)%100;
        }
        /* A fresh line prevents a false cache-only progress watchdog trip. */
        (void)*(volatile unsigned *)(0x13700000u+((kind*3+pattern)*9+s)*64);
        U(_gpu_span_hdr_valid)=0;
        for(unsigned rep=0;rep<16;rep++) {
            U(_gpu_cmd_words)=U(_gpu_wrptr)=U(_gpu_known_rdptr)=0;
            params->tex_addr=0x01000000+(pattern==1?rep*4096:0);
            params->light_origin=(pattern==2?rep:rep/4)<<16;
            if(kind!=2){band[0]=params->light_origin>>16;band[1]=n;}
            if(kind==0){U(gpu_plane_active)=1;U(gpu_plane_record_count)=n;}
            if(kind==1){U(gpu_wall_tiers)=1;U(gpu_wall_record_count)=n;}
            if(kind==2){U(gpu_sprite_active)=1;U(gpu_sprite_record_count)=n;}
            unsigned start=cycle();
            if(kind==0)F(R_GPU_EndPlaneSpans,void(*)(void))();
            if(kind==1)F(R_GPU_WallTiersEnd,void(*)(void))();
            if(kind==2)F(R_GPU_SpriteEnd,void(*)(void))();
            elapsed+=cycle()-start;
            unsigned words=U(_gpu_cmd_words);
            if(!words||words>=512)fail();
            hash(&words,1);hash(P(_gpu_batch_storage),words);
            if(kind!=2&&band[1])fail();
        }
        puts_uart("BAND ");hex(kind);hex(pattern);hex(n);hex(elapsed);hex(crc);puts_uart("\n");
    }
    puts_uart("MAP REPLAY PASS HAL init\n");return 0;
}
