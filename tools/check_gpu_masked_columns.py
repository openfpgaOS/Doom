#!/usr/bin/env python3
"""Compare production masked post walks, records, flush order and fallbacks."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
from gpu_test_sources import function, gpu_types

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', type=Path, required=True,
                    help='Preserved Doom source root containing src/')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    hashes = []
    for name, root in [('before', args.reference), ('after', ROOT)]:
        out = args.output.resolve() / name
        out.mkdir(parents=True, exist_ok=True)
        source = (root / 'src/doom/cdoom/doom/r_gpu.c').read_text()
        things = (root / 'src/doom/cdoom/doom/r_things.c').read_text()
        start = source.index('#define GPU_WALL_TIERS')
        end = source.index('static int gpu_wall_seg_valid', start)
        (out / 'gpu_wall_defs.h').write_text(source[start:end])
        (out / 'gpu_types.h').write_text(gpu_types(
            (ROOT / 'src/sdk/include/of_gpu.h').read_text()))
        names = ['gpu_append_wall_column', 'R_GPU_WallTierColumn',
                 'R_GPU_MaskedPost']
        if 'gpu_append_sprite_post(' in source:
            names.append('gpu_append_sprite_post')
        names.append('R_GPU_SpritePost')
        prefix = ''
        if 'boolean R_GPU_DrawMaskedColumn(' in source:
            names.append('R_GPU_DrawMaskedColumn')
            prefix = '#define HAS_MASKED_COLUMN 1\n'
        (out / 'gpu_functions.h').write_text(prefix + '\n'.join(
            function(source, n) for n in names)
            + function(things, 'R_DrawMaskedColumn'))
        cmd = ['cc', '-std=gnu11', '-O2', '-g', '-fwrapv',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-no-pie', '-I' + str(out),
               str(ROOT / 'tools/tests/test_gpu_masked_columns.c'),
               '-o', str(out / 'test')]
        with (out / 'build.log').open('w') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (out / 'run.log').open('w') as log:
            subprocess.run([str(out / 'test'), str(out / 'records.bin')],
                           stdout=log, stderr=subprocess.STDOUT, check=True,
                           env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
        hashes.append(hashlib.sha256((out / 'records.bin').read_bytes()).hexdigest())
        print(name, (out / 'run.log').read_text().strip(), flush=True)
    assert hashes[0] == hashes[1], 'Masked records or fallback work changed'
    print('PASS identical masked/sprite records, flushes and fallback work', hashes[0])


if __name__ == '__main__':
    main()
