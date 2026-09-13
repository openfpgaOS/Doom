#!/usr/bin/env python3
"""Compare synth mixer-write and state traces against a reference source."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/sdk/of_smp_voice.c')
    parser.add_argument('--voices', type=int, default=20, choices=(12, 20, 28))
    parser.add_argument('--fast-tick', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='synth-equivalence-') as temporary:
        build = Path(temporary)
        (build / 'include').symlink_to(ROOT / 'src/sdk/include', target_is_directory=True)
        traces = []
        for name, source in [('reference', args.reference), ('candidate', args.source)]:
            binary = build / name
            subprocess.run([
                'cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                f'-DSMP_MAX_VOICES={args.voices}', f'-DVOICE_SOURCE="{source.resolve()}"',
                f'-DSMP_VOICE_FAST_TICK={int(args.fast_tick)}',
                f'-I{build}', f'-I{ROOT / "src/sdk/include"}',
                str(ROOT / 'tools/tests/test_smp_voice_equivalence.c'),
                str(ROOT / 'src/sdk/of_smp_tables.c'), '-o', str(binary),
            ], check=True)
            trace = build / (name + '.trace')
            with trace.open('wb') as output:
                subprocess.run([str(binary)], stdout=output, check=True)
            traces.append(trace.read_bytes())
        if traces[0] != traces[1]:
            offset = next((i for i, (a, b) in enumerate(zip(*traces)) if a != b),
                          min(map(len, traces)))
            raise SystemExit(f'FAIL: synth traces differ at byte {offset}')
        print(f'PASS: {len(traces[0]) // 20:,} identical mixer/state records; '
              f'49,500 envelope ticks; {args.voices} voices; sanitizers enabled')


if __name__ == '__main__':
    main()
