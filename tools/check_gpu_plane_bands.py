#!/usr/bin/env python3
"""Compare production plane-band lookup, eviction and record bytes."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
from gpu_test_sources import function,gpu_types

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    hashes=[]
    for label,path in [('before',args.reference),('after',ROOT/'src/doom/cdoom/doom/r_gpu.c')]:
        job=output/label;job.mkdir(exist_ok=True);source=path.read_text()
        prefix=''
        if 'gpu_plane_light_slot[' in source:
            prefix=re.search(r'static uint8_t gpu_plane_light_slot\[64\];',source)[0]+'\n'
        (job/'gpu_types.h').write_text(gpu_types((ROOT/'src/sdk/include/of_gpu.h').read_text()))
        (job/'gpu_producer.h').write_text(prefix+function(source,'R_GPU_PlaneSpanLight'))
        command=['cc','-std=gnu11','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
                 '-I'+str(job),str(ROOT/'tools/tests/test_gpu_plane_bands.c'),'-o',str(job/'test')]
        with (job/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (job/'run.log').open('w') as log:
            subprocess.run([str(job/'test'),str(job/'records.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,
                           env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
        hashes.append(hashlib.sha256((job/'records.bin').read_bytes()).hexdigest())
        print(label,(job/'run.log').read_text().strip(),hashes[-1],flush=True)
    if hashes[0]!=hashes[1]:raise RuntimeError('Plane band order, records or return values changed')
    print('PASS identical plane-band behavior')

if __name__=='__main__':main()
