#!/usr/bin/env python3
"""Compare scalar and batched production wall records and band flush ordering."""
import argparse,hashlib,os,subprocess
from pathlib import Path
from gpu_test_sources import function,gpu_types
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();hashes=[]
 for name,path in [('before',a.reference),('after',ROOT/'src/doom/cdoom/doom/r_gpu.c')]:
  job=a.output.resolve()/name;job.mkdir(parents=True,exist_ok=True)
  s=path.read_text().replace('OF_FASTTEXT ','');start=s.index('#define GPU_WALL_TIERS');end=s.index('static int gpu_wall_seg_valid',start)
  (job/'gpu_wall_defs.h').write_text(s[start:end]);(job/'gpu_types.h').write_text(gpu_types((ROOT/'src/sdk/include/of_gpu.h').read_text()))
  names=['R_GPU_WallTierColumn'];prefix=''
  if 'gpu_append_wall_column(' in s:names.insert(0,'gpu_append_wall_column')
  if 'void R_GPU_WallColumns(' in s:names+=['R_GPU_WallColumns'];prefix='#define HAS_WALL_BATCH 1\n'
  (job/'gpu_wall_functions.h').write_text(prefix+'\n'.join(function(s,n) for n in names))
  cmd=['cc','-std=gnu11','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(job),str(ROOT/'tools/tests/test_gpu_wall_columns.c'),'-o',str(job/'test')]
  with (job/'build.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
  with (job/'run.log').open('w') as log:subprocess.run([str(job/'test'),str(job/'records.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
  hashes.append(hashlib.sha256((job/'records.bin').read_bytes()).hexdigest());print(name,(job/'run.log').read_text().strip(),flush=True)
 assert hashes[0]==hashes[1],'Wall records or band ordering changed'
 print('PASS identical wall records, flushes and pixel counters',hashes[0])
if __name__=='__main__':main()
