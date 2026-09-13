/* Exercise command packing in a normal production ELF on the Pocket CPU.
 * The consumer is modeled as always ready. DMA, rasterization and audio
 * are excluded; only time inside the real packing routine is measured. */
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
static void fail(void) { puts_uart("FAIL submission\n"); for (;;) {} }
void trap_report(unsigned cause,unsigned pc,unsigned addr) { puts_uart("TRAP ");hex(cause);hex(pc);hex(addr);puts_uart("\n");fail(); }
static struct of_capabilities caps;
static of_gpu_param_span_list_t params;
static uint16_t storage[3*512+2] __attribute__((aligned(4)));
int main(void)
{
    static const unsigned sizes[]={1,2,3,8,16,32,64,128,512};
    U(_of_caps_ptr)=(uintptr_t)&caps;
    U(_gpu_batch_buf)=(uintptr_t)P(_gpu_batch_storage);
    params.fb_base=0x03000000;params.fb_major_step=320;params.fb_minor_step=1;
    params.tex_addr=0x01000000;params.tex_width=64;params.tex_w_mask=63;params.tex_h_mask=63;
    params.attr_mode=OF_GPU_PARAM_ATTR_PERSP_Q29;
    params.attr_origin[0]=0x10000000;params.attr_origin[1]=0x20000000;params.attr_origin[2]=0x08000000;
    params.attr_du[0]=0x01000000;params.attr_dv[1]=0x01000000;
    puts_uart("SUBMISSION BEGIN\n");
    /* 0: legacy core, 1: repeated header, 2: count clamping,
     * 3: texture changes, 4: lighting changes on the same surface. */
    for(unsigned mode=0;mode<5;mode++)for(unsigned alignment=0;alignment<2;alignment++)for(unsigned s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++) {
        unsigned n=sizes[s],elapsed=0,counts_or=0;
        of_gpu_param_span_record_t *records=(void *)(storage+alignment);
        caps.hw_features=OF_HW_GPU_PARAM_SPAN_Q29_SCALE | (mode?OF_HW_GPU_SPAN_CONT:0);
        for(unsigned j=0;j<n;j++) {
            records[j].u=(j*13)%320;records[j].v=(j*7)%200;
            records[j].count=mode==2 ? (j+1)*151 : 1+(j*11)%320;
            counts_or|=records[j].count;
        }
        /* A fresh cache line between cohorts keeps the simulation's
         * SDRAM-progress watchdog live even when this loop fits in cache. */
        (void)*(volatile unsigned *)(0x13700000u+((mode*2+alignment)*9+s)*64);
        U(_gpu_span_hdr_valid)=0;
        for(unsigned rep=0;rep<16;rep++) {
            U(_gpu_cmd_words)=U(_gpu_wrptr)=U(_gpu_known_rdptr)=0;
            /* Repeated headers followed by a light change exercise residency. */
            params.light_origin=(mode==4 ? rep : rep/4)<<16;
            params.tex_addr=0x01000000+(mode==3 ? rep*4096 : 0);
            unsigned start=cycle();
            /* The baseline's three-argument ABI ignores the extra a3 value. */
            F(_gpu_emit_param_span_list,void(*)(const void*,const void*,unsigned,unsigned))(&params,records,n,counts_or);
            elapsed+=cycle()-start;
            unsigned words=U(_gpu_cmd_words);
            if(!words||words>32+3*((n+1)/2))fail();
            hash(&words,1);hash(P(_gpu_batch_storage),words);
        }
        puts_uart("PACK ");hex(mode);hex(alignment*2);hex(n);hex(elapsed);hex(crc);puts_uart("\n");
    }
    puts_uart("MAP REPLAY PASS HAL init\n");return 0;
}
