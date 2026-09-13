#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define OF_FASTRAM_H
#define OF_FASTTEXT
#include WALL_SOURCE
int viewwidth=320, viewheight=168, detailshift, centery=84;
fixed_t centeryfrac=84*FRACUNIT, viewz;
int dc_x,dc_yl,dc_yh,dc_texheight,extralight,maskedcolormaprow;
fixed_t dc_texturemid,dc_iscale,spryscale,sprtopscreen;
byte *dc_source,*dc_translation;
lighttable_t *dc_colormap,*fixedcolormap;
lighttable_t *scalelight[LIGHTLEVELS][MAXLIGHTSCALE];
byte scalelightrow[LIGHTLEVELS][MAXLIGHTSCALE];
fixed_t *textureheight;
int *texturetranslation;
short *mfloorclip,*mceilingclip;
seg_t *segs;
seg_t *curline;
sector_t *frontsector, *backsector;
rendersegcache_t *rendersegcache;
static lighttable_t maps[64][256];
static byte data[8][256], *cols[8];
static int trans[1]={0};
static fixed_t heights[1]={128*FRACUNIT};
static unsigned state=0x2461fa97, gpu_columns,cpu_columns,begins;
static int accepted, mode;
static uint32_t params[11];
static unsigned rnd(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void emit(const void *p,size_t n){assert(fwrite(p,1,n,stdout)==n);}
void I_Error(const char *s,...){(void)s;abort();}
byte **R_GetColumnTable(int t){(void)t;return cols;}
int R_GetTextureWidthMask(int t){(void)t;return 7;}
byte *R_GetMaskedTexture2D(int t){(void)t;return data[0];}
boolean R_ColumnHasPosts(int t,int c){(void)t;return c&1;}
int R_GPU_ColormapRow(const byte *p){return (p-(byte *)maps)/256;}
void R_GPU_UseMaskedTexture(int t){(void)t;}
void R_GPU_UseNoTexture(void){accepted=0;}
boolean R_GPU_MaskedBegin(const byte *blk,int height,int widthmask,
 fixed_t mid,int x1,int x2,fixed_t scale,fixed_t step,fixed_t dist,
 fixed_t offset,unsigned int angle){
 (void)blk;begins++; accepted=mode!=0;
 uint32_t p[]={height,widthmask,mid,x1,x2,scale,step,dist,offset,angle,mode};
 memcpy(params,p,sizeof(p));return accepted;
}
void R_GPU_MaskedEnd(void){accepted=0;}
static void drawposts(const byte *p){
 for(;p[0]!=255;p+=p[1]+4){
  int32_t top=(uint32_t)sprtopscreen+(uint32_t)spryscale*p[0];
  int32_t bottom=(uint32_t)top+(uint32_t)spryscale*p[1];
  int yl=(int32_t)((uint32_t)top+FRACUNIT-1)>>FRACBITS;
  int yh=(int32_t)((uint32_t)bottom-1)>>FRACBITS;
  if(yh>=mfloorclip[dc_x])yh=mfloorclip[dc_x]-1;
  if(yl<=mceilingclip[dc_x])yl=mceilingclip[dc_x]+1;
  if(yl>yh)continue;
  int32_t head[]={accepted,dc_x,yl,yh,maskedcolormaprow};emit(head,sizeof(head));
  if(accepted){emit(params,sizeof(params));gpu_columns++;}
  else {int32_t a[]={dc_texturemid-p[0]*FRACUNIT,dc_iscale,p[1]};emit(a,sizeof(a));emit(p+3,p[1]);cpu_columns++;}
 }
}
void R_DrawMaskedColumn(column_t *p){drawposts((const byte *)p);}
boolean R_GPU_DrawMaskedColumn(const byte *p){if(!accepted)return false;drawposts(p);return true;}
int main(void){
 seg_t seg={0}; side_t side={0};sector_t front={0},back={0};
 rendersegcache_t cache={0}; drawseg_t ds={0};
 short consumed[320],topclip[320],botclip[320];
 segs=&seg;rendersegcache=&cache;cache.sidedef=&side;cache.frontsector=&front;cache.backsector=&back;
 ds.curline=&seg;ds.maskedtexturecol=consumed;ds.sprtopclip=topclip;ds.sprbottomclip=botclip;
 texturetranslation=trans;textureheight=heights;
 for(int l=0;l<LIGHTLEVELS;l++)for(int s=0;s<MAXLIGHTSCALE;s++){
  scalelightrow[l][s]=(l+s)%32;scalelight[l][s]=maps[(l+s)%32];
 }
 for(int c=0;c<8;c++){
  for(int j=0;j<256;j++)data[c][j]=j+c;
  if(c&1){data[c][0]=c*2;data[c][1]=32;data[c][36]=64;data[c][37]=24;data[c][64]=255;cols[c]=data[c]+3;}
  else cols[c]=data[c];
 }
 for(int trial=0;trial<12000;trial++){
  ds.x1=0;ds.x2=319;ds.scale1=FRACUNIT/2+rnd()%FRACUNIT;
  ds.scalestep=(int)(rnd()%51)-25;ds.gpu_mdistance=FRACUNIT;ds.gpu_moffset=trial*31;ds.gpu_mcenterangle=rnd();
  cache.lightbias=(int)(rnd()%3)-1;cache.pegflags=trial%2?ML_DONTPEGBOTTOM:0;
  front.lightlevel=rnd()%256;front.floorheight=-8*FRACUNIT;back.floorheight=0;
  front.ceilingheight=back.ceilingheight=128*FRACUNIT;viewz=48*FRACUNIT;side.rowoffset=trial%7*FRACUNIT;
  fixedcolormap=trial%7==0?maps[32]:NULL;detailshift=trial%11==0;mode=trial%5!=0;
  for(int x=0;x<320;x++){
   consumed[x]=trial%4==0||rnd()%3==0?SHRT_MAX:rnd()%64;
   topclip[x]=(int)(rnd()%80)-1;botclip[x]=80+rnd()%89;
  }
  // Overlapping sprite ranges followed by the final full-wall pass, including
  // ranges whose leading/trailing columns have already been consumed.
  for(int r=0;r<6;r++){
   int x1=rnd()%320,x2=x1+rnd()%(320-x1);
   R_RenderMaskedSegRange(&ds,x1,x2);
  }
  R_RenderMaskedSegRange(&ds,0,319);R_RenderMaskedSegRange(&ds,0,319);
  emit(consumed,sizeof(consumed));
 }
 assert(gpu_columns>10000&&cpu_columns>10000);
 fprintf(stderr,"PASS 96000 range visits; %u GPU posts; %u CPU posts; %u surface setups\n",gpu_columns,cpu_columns,begins);
}
