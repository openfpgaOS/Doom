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
static void drain(void)
{
    unsigned words=U(_gpu_cmd_words);
    if(words>=512)fail();
    hash(&words,1);hash(P(_gpu_batch_storage),words);
    U(_gpu_cmd_words)=U(_gpu_wrptr)=U(_gpu_known_rdptr)=0;
}
int main(void)
{
    static const unsigned sizes[]={1,2,3,8,16,32,64,128};
    U(_of_caps_ptr)=(uintptr_t)&caps;
    U(_gpu_batch_buf)=(uintptr_t)P(_gpu_batch_storage);
    U(gpu_spancont_enabled)=1;U(viewwidth)=320;U(viewheight)=200;
    caps.hw_features=OF_HW_GPU_PARAM_SPAN_Q29_SCALE|OF_HW_GPU_SPAN_CONT;
    of_gpu_param_span_list_t *params=(void *)P(gpu_plane_params);
    params->fb_base=0x03000000;params->fb_major_step=320;params->fb_minor_step=1;
    params->tex_addr=0x01000000;params->tex_width=64;params->tex_w_mask=63;params->tex_h_mask=63;
    params->attr_mode=OF_GPU_PARAM_ATTR_PERSP_Q29;
    params->flags=OF_GPU_SPAN_COLORMAP|OF_GPU_SPAN_PERSP;
    params->attr_origin[0]=0x10000000;params->attr_origin[1]=0x20000000;params->attr_origin[2]=0x08000000;
    params->attr_du[0]=0x01000000;params->attr_dv[1]=0x01000000;
    puts_uart("PLANE PRODUCER BEGIN\n");
    for(unsigned pattern=0;pattern<3;pattern++)for(unsigned s=0;s<8;s++) {
        unsigned n=sizes[s],elapsed=0;
        (void)*(volatile unsigned *)(0x13700000u+(pattern*8+s)*64);
        U(_gpu_span_hdr_valid)=0;
        for(unsigned rep=0;rep<16;rep++) {
            U(_gpu_cmd_words)=U(_gpu_wrptr)=U(_gpu_known_rdptr)=0;
            U(gpu_plane_active)=1;U(gpu_plane_record_count)=0;U(gpu_plane_band_rr)=0;
            for(unsigned b=0;b<8;b++) {
                volatile unsigned *band=P(gpu_plane_bands)+b*(SIZE_gpu_plane_bands/8/4);
                band[0]=0xffffffffu;band[1]=0;
            }
            for(unsigned j=0;j<n;j++) {
                unsigned y=j%200,x1=j%32,x2=x1+31,light=pattern==0?3:pattern==1?j%8:j%16;
                unsigned start=cycle();
                int ok=F(R_GPU_PlaneSpanLight,int(*)(int,int,int,int))(y,x1,x2,light);
                elapsed+=cycle()-start;
                if(!ok)fail();
                drain();
            }
            unsigned start=cycle();
            F(R_GPU_EndPlaneSpans,void(*)(void))();
            elapsed+=cycle()-start;
            drain();
            if(U(gpu_plane_record_count))fail();
        }
        puts_uart("PLANE ");hex(pattern);hex(0);hex(n);hex(elapsed);hex(crc);puts_uart("\n");
    }
    puts_uart("MAP REPLAY PASS HAL init\n");return 0;
}
