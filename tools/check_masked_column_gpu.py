#!/usr/bin/env python3
"""Compare masked midtexture columns drawn through column blocks (GPU affine
columns, R_DrawMaskedColumnGPU) with the production CPU post walk, pixel for
pixel, including the clamped post-boundary roundings."""
import argparse
import os
from pathlib import Path
import subprocess
from gpu_test_sources import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    doom = ROOT / 'src/doom/cdoom/doom'
    things = (doom / 'r_things.c').read_text()
    draw = (doom / 'r_draw.c').read_text()
    start = things.index('#define MASKED_GPU_POSTS')
    end = things.index('\n}\n', things.index('boolean R_DrawMaskedColumnGPU(')) + 3
    (out / 'masked_functions.h').write_text(
        function(draw, 'R_DrawColumn', last=False)
        + function(draw, 'R_DrawClampedMaskedColumn')
        + function(things, 'R_DrawMaskedColumn') + things[start:end])
    cmd = ['cc', '-std=gnu11', '-O2', '-g', '-fwrapv', '-Wall', '-Wno-unused-function',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
           '-I' + str(out), str(ROOT / 'tools/tests/test_masked_column_gpu.c'),
           '-o', str(out / 'test')]
    subprocess.run(cmd, check=True)
    subprocess.run([str(out / 'test')], check=True,
                   env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    main()
