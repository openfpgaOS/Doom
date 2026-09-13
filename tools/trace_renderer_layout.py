#!/usr/bin/env python3
"""Capture renderer setup inputs from local demos in isolated host sources.

Selects gameplay tics 0-7 and 200-207. Captures wall/plane setup, view state,
the view-angle table and bbox calls with expected results. Host timing is
not used. Requires a local IWAD and optional PWADs; no assets are bundled.
"""
from pathlib import Path
import argparse,os,shutil,struct,json,hashlib
root=Path(__file__).resolve().parents[1]
import benchmark_doom as bench
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=root)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--iwad',type=Path,required=True)
parser.add_argument('--merge',type=Path,nargs='+',default=[])
parser.add_argument('--demos',nargs='+',default=['demo1'])
args=parser.parse_args()
out=args.output.resolve()
iwad=args.iwad.resolve();merges=[p.resolve()for p in args.merge]
out.mkdir(parents=True,exist_ok=False)
src=out/'instrumented'
for name in ('doom','sdk'):
 shutil.copytree(args.source.resolve()/'src'/name,src/'src'/name,ignore=shutil.ignore_patterns('*.wad','*.WAD','*.o','*.elf','*.d','app_pc','__pycache__'))
base=src/'src/doom/cdoom/doom'
def edit(name,a,b):
 p=base/name;s=p.read_text();assert s.count(a)==1,(name,a,s.count(a));p.write_text(s.replace(a,b))
(base/'capture.h').write_text('void Capture(const unsigned *args, unsigned count);\nvoid CaptureFrame(void);\n')
for name in ('r_bsp.c','r_segs.c','r_gpu.c'):
 p=base/name;p.write_text('#include "capture.h"\n'+p.read_text())
edit('r_bsp.c','void R_ClearClipSegs (void)\n{','void R_ClearClipSegs (void)\n{\n    CaptureFrame();')
a='OF_FASTTEXT static angle_t R_BBoxPointAngle(fixed_t x, fixed_t y)'
edit('r_bsp.c',a,a.replace('R_BBoxPointAngle','CaptureOriginalBBox'))
p=base/'r_bsp.c';s=p.read_text();start=s.index(a.replace('R_BBoxPointAngle','CaptureOriginalBBox'));end=s.index('\n}',start)+2
s=s[:end]+'''
OF_FASTTEXT static angle_t R_BBoxPointAngle(fixed_t x, fixed_t y)
{
    angle_t result = CaptureOriginalBBox(x,y);
    unsigned args[] = {5, x, y, result};
    Capture(args,4);
    return result;
}
'''+s[end:];p.write_text(s)
edit('r_segs.c','    if (!R_GPU_WallSegBegin(x, stopx - 1, scale, scalestep,','''    {
        unsigned args[23] = {1, x, stopx-1, scale, scalestep, rw_distance, rw_offset, rw_centerangle};
        int textures[3] = {midtexture,toptexture,bottomtexture};
        fixed_t mids[3] = {rw_midtexturemid,rw_toptexturemid,rw_bottomtexturemid};
        for (int i=0;i<3;i++) if(textures[i]) {
            args[8+i*5]=1; args[9+i*5]=(i==2);
            args[10+i*5]=textureheight[textures[i]]>>FRACBITS;
            args[11+i*5]=R_GetTextureWidthMask(textures[i]);
            args[12+i*5]=mids[i];
        }
        Capture(args,23);
    }
    if (!R_GPU_WallSegBegin(x, stopx - 1, scale, scalestep,''')
edit('r_gpu.c','    (void)height_delta;\n    (void)fixed_light;','    unsigned args[] = {2, height_delta, fixed_light};\n    Capture(args,3);')
probe=r'''
#include <stdlib.h>
extern int gametic;
static FILE *capture_file;
static int opened;
void Capture(const unsigned *args,unsigned n) {
    if (!opened) { capture_file=fopen(getenv("DOOM_RENDER_CAPTURE"),"wb"); if(!capture_file)abort(); opened=1; }
    if (!(gametic<8 || (gametic>=200 && gametic<208)))return;
    for(unsigned i=0;i<32;i++){unsigned v=i<n?args[i]:0;unsigned char b[4];for(unsigned j=0;j<4;j++)b[j]=v>>(8*j);if(fwrite(b,4,1,capture_file)!=1)abort();}
}
void CaptureFrame(void) {
    unsigned args[]={0,gametic,viewx,viewy,viewangle,validcount,viewwidth,viewheight,centerx,centery,centerxfrac,centeryfrac,projection,viewwindowx,viewwindowy,detailshift,viewcos,viewsin};
    Capture(args,18);
    for(unsigned i=0;i<=320;i+=29){unsigned lut[32]={4,i,0};for(unsigned j=0;j<29 && i+j<=320;j++){lut[3+j]=xtoviewangle[i+j];lut[2]++;}Capture(lut,32);}
}
__attribute__((destructor)) static void CaptureClose(void){if(capture_file && fclose(capture_file))abort();}
'''
edit('r_gpu.c','#ifdef OF_PC','#ifdef OF_PC\n'+probe)
binary=bench.build(src,out,'probe')
report={}
for demo in args.demos:
 label=Path(demo).stem
 trace=out/(label+'.bin');os.environ['DOOM_RENDER_CAPTURE']=str(trace)
 stats,work=bench.run(binary,out,label,iwad,demo,'capture',False,merges)
 data=trace.read_bytes();records=list(struct.iter_unpack('<32I',data));counts={kind:sum(row[0]==kind for row in records)for kind in (0,1,2,4,5)}
 assert counts[0]>0 and counts[1]>0 and counts[2]>0
 report[label]=dict(counts=counts,iwad=iwad.name,iwad_sha256=hashlib.sha256(iwad.read_bytes()).hexdigest(),merges=[dict(name=x.name,sha256=hashlib.sha256(x.read_bytes()).hexdigest())for x in merges],stats=stats)
 print(label,counts,flush=True)
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
