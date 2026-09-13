#!/usr/bin/env python3
"""Compare production visplane allocation and coverage with a reference file."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='visplane-equivalence-') as temporary:
        build = Path(temporary)
        traces = []
        for name, source in [('reference', args.reference),
                             ('candidate', ROOT / 'src/doom/cdoom/doom/r_plane.c')]:
            binary = build / name
            command = ['cc', '-std=gnu11', '-O2', '-g', '-DOF_PC',
                       '-ffunction-sections', '-fdata-sections',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                       f'-DPLANE_SOURCE="{source.resolve()}"']
            command += ['-I' + str(ROOT / path) for path in
                        ('src/sdk/include', 'src/doom/shim', 'src/doom/cdoom', 'src/doom/cdoom/doom')]
            command += [str(ROOT / 'tools/tests/test_visplane_equivalence.c'),
                        '-Wl,--gc-sections', '-o', str(binary)]
            subprocess.run(command, check=True)
            trace = build / (name + '.trace')
            with trace.open('wb') as output:
                subprocess.run([str(binary)], stdout=output, check=True)
            traces.append(trace.read_bytes())
        assert traces[0] == traces[1], 'Visplane selection or column coverage changed'
        print(f'PASS: 131,072 plane checks across 1,024 reused frames; '
              f'{len(traces[0]):,} identical trace bytes; sanitizers enabled')


if __name__ == '__main__':
    main()
