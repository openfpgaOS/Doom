// Replay geometry and GPU parameter setup in an unmodified production ELF.
// Texture/framebuffer addresses are placeholders; no pixels are rendered.
// The stream contains a count followed by fixed 32-word captured records.
typedef unsigned int uint32_t;
typedef unsigned int uintptr_t;
#include "symbols.h"
#define U(n) (*(volatile uint32_t *)(uintptr_t)A_##n)
#define P(n) ((volatile uint32_t *)(uintptr_t)A_##n)
#define F(n,t) ((t)(uintptr_t)A_##n)
static void puts_uart(const char *s){while(*s){while(!(*(volatile unsigned*)0x4f000000u&2)){}*(volatile unsigned*)0x4f000004u=(unsigned char)*s++;}}
static void hex(unsigned x){char b[10];for(int i=0;i<8;i++)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]=' ';b[9]=0;puts_uart(b);}
static unsigned cycle(void){return *(volatile unsigned*)0x40000004u;}
static unsigned crc=0x811c9dc5;
static void hash(volatile const unsigned *p,unsigned words){for(unsigned i=0;i<words;i++)crc=(crc^p[i])*16777619u;}
void fail(void){puts_uart("FAIL renderer\n");for(;;){}}
void trap_report(unsigned cause,unsigned pc,unsigned addr){puts_uart("TRAP ");hex(cause);hex(pc);hex(addr);puts_uart("\n");fail();}
static void frame_result(unsigned index,unsigned start){unsigned elapsed=cycle()-start;puts_uart("FRAME ");hex(index);hex(elapsed);hex(crc);puts_uart("\n");}
int main(void){
 U(gpu_use_wall_param)=U(gpu_use_param_span)=U(gpu_use_sprite_param)=1;
 U(gpu_present)=U(gpu_frame_active)=U(gpu_write_prepared)=1;
 U(I_VideoBuffer)=0x13000000;U(gpu_src_tex)=0x11000000;U(gpu_src_delta)=0;
 U(wallmerge_enabled)=0;U(numvertexes)=0;
 for(unsigned y=0;y<200;y++)P(gpu_fb_row_addr)[y]=0x13000000+y*320;
 volatile unsigned *data=(volatile unsigned*)0x13800000;
 unsigned count=*data++,frame=0,start=0;
 if(count==0||count>50000)fail();
 puts_uart("MAP REPLAY BEGIN\n");
 for(unsigned i=0;i<count;i++,data+=32){
  unsigned kind=data[0];
  if(kind==0){
   if(frame)frame_result(frame-1,start);
   U(viewx)=data[2];U(viewy)=data[3];U(viewangle)=data[4];U(validcount)=data[5];
   U(viewwidth)=data[6];U(viewheight)=data[7];U(centerx)=data[8];U(centery)=data[9];
   U(centerxfrac)=data[10];U(centeryfrac)=data[11];U(projection)=data[12];
   U(viewwindowx)=data[13];U(viewwindowy)=data[14];U(detailshift)=data[15];
   U(viewcos)=data[16];U(viewsin)=data[17];
   start=cycle();frame++;
   if(A_R_GPU_BeginView)F(R_GPU_BeginView,void(*)(void))();
   F(R_ClearClipSegs,void(*)(void))();
  } else if(kind==1){
   int ok=F(R_GPU_WallSegBegin,int(*)(int,int,int,int,int,int,unsigned))(data[1],data[2],data[3],data[4],data[5],data[6],data[7]);
   crc^=ok;
   if(ok)for(unsigned j=0;j<3;j++)if(data[8+j*5]){
    int tier=data[9+j*5];
    ok=F(R_GPU_WallTierBegin,int(*)(int,const void*,int,int,int))(tier,(void*)0x11000000,data[10+j*5],data[11+j*5],data[12+j*5]);
    crc^=ok;if(ok)hash(P(gpu_wall_tiers)+tier*(SIZE_gpu_wall_tiers/8)+2,29);
   }
  } else if(kind==2){
   int ok=F(R_GPU_BeginPlaneSpans,int(*)(const void*,int,int))((void*)0x11004000,data[1],data[2]);
   crc^=ok;if(ok)hash(P(gpu_plane_params),29);
  } else if(kind==4){
   if(data[1]+data[2]>321)fail();
   for(unsigned j=0;j<data[2];j++)P(xtoviewangle)[data[1]+j]=data[3+j];
  } else if(kind==5){
   unsigned angle=F(R_BBoxPointAngle,unsigned(*)(unsigned,unsigned))(data[1],data[2]);
   if(angle!=data[3]){puts_uart("BBOX mismatch ");hex(i);hex(angle);hex(data[3]);fail();}
   crc^=angle;
  } else fail();
 }
 if(frame)frame_result(frame-1,start);
 puts_uart("MAP REPLAY PASS HAL init\n");return 0;
}
