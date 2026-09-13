#!/usr/bin/env python3
"""Compare production plane mapping across mixed GPU and fallback spans."""
import argparse, os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='plane-mapping-') as d:
  d=Path(d);traces=[]
  for name,source in [('before',a.reference),('after',ROOT/'src/doom/cdoom/doom/r_plane.c')]:
   cmd=['cc','-std=gnu11','-O2','-g','-DOF_PC','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',f'-DPLANE_SOURCE="{source.resolve()}"']
   cmd+=['-I'+str(ROOT/x) for x in ['src/sdk/include','src/doom/shim','src/doom/cdoom','src/doom/cdoom/doom']]
   cmd += [str(ROOT/'tools/tests/test_plane_mapping.c'),str(ROOT/'src/doom/cdoom/tables.c'),'-Wl,--gc-sections','-o',str(d/name)]
   subprocess.run(cmd,check=True)
   with (d/(name+'.bin')).open('wb') as out: subprocess.run([str(d/name)],stdout=out,check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
   traces.append((d/(name+'.bin')).read_bytes())
  assert traces[0]==traces[1],'Plane mapping changed'
  print(f'PASS 262,144 mixed plane spans; {len(traces[0]):,} identical output bytes')
if __name__=='__main__':main()
