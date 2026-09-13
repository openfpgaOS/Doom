#!/usr/bin/env python3
"""Compare GPU and fallback wall records, clipping, planes and stepping state."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--source', type=Path,
                        default=ROOT / 'src/doom/cdoom/doom/r_segs.c')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gpu-wall-loop-') as temporary:
        output = Path(temporary)
        traces = []
        for label, source in [('reference', args.reference), ('candidate', args.source)]:
            command = ['cc', '-std=gnu11', '-O2', '-g', '-DOF_PC',
                       '-ffunction-sections', '-fdata-sections',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                       f'-DWALL_SOURCE="{source.resolve()}"']
            command += ['-I' + str(ROOT / path) for path in
                        ('src/sdk/include', 'src/doom/shim', 'src/doom/cdoom', 'src/doom/cdoom/doom')]
            if 'R_GPU_WallColumns(' in source.read_text():
                command += ['-DTEST_WALL_COLUMN_BATCH']
            command += [str(ROOT / 'tools/tests/test_gpu_wall_loop.c'),
                        str(ROOT / 'src/doom/cdoom/tables.c'),
                        '-Wl,--gc-sections', '-o', str(output / label)]
            subprocess.run(command, check=True)
            with (output / (label + '.bin')).open('wb') as trace:
                subprocess.run([str(output / label)], stdout=trace, check=True,
                               env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
            traces.append((output / (label + '.bin')).read_bytes())
        assert traces[0] == traces[1], 'Wall loop output changed'
        print(f'PASS: 12,000 wall loop cases; {len(traces[0]):,} identical bytes '
              'of GPU/fallback records, clips, planes and stepping state')


if __name__ == '__main__':
    main()
