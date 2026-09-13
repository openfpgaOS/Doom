#!/usr/bin/env python3
"""Compare actual masked range rendering, including repeated and empty ranges."""
import argparse, hashlib, os, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    hashes = []
    for name, root in [('before', a.reference.resolve()), ('after', ROOT)]:
        out = a.output.resolve()/name
        out.mkdir(parents=True, exist_ok=True)
        source = root/'src/doom/cdoom/doom/r_segs.c'
        cmd = ['cc', '-DOF_PC', '-DOF_DOOM', '-O2', '-g', '-fwrapv',
               '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-no-pie', '-DWALL_SOURCE="'+str(source)+'"']
        cmd += ['-I'+str(ROOT/d) for d in ['src/sdk/include', 'src/doom/shim',
                                        'src/doom/cdoom', 'src/doom/cdoom/doom']]
        cmd += [str(ROOT/'tools/tests/test_gpu_masked_ranges.c'),
                '-Wl,--gc-sections', '-o', str(out/'test')]
        with (out/'build.log').open('w') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (out/'records.bin').open('wb') as records, (out/'run.log').open('w') as log:
            subprocess.run([str(out/'test')], stdout=records, stderr=log, check=True,
                           env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
        hashes.append(hashlib.sha256((out/'records.bin').read_bytes()).hexdigest())
        print(name, (out/'run.log').read_text().strip(), flush=True)
    assert hashes[0] == hashes[1], 'Masked range output changed'
    print('PASS identical projection endpoints, posts, fallbacks and consumed columns', hashes[0])


if __name__ == '__main__':
    main()
