#!/usr/bin/env python3
"""Capture bbox requests in isolated host sources for production RV32 replay.

Requires local IWAD/PWADs. The source under review is never instrumented in place.
Capture complete frames for tics 0-63 and 200-215; timing from this build is not
used. The eight uint32 words are kind (0=frame, 1=point), tic, view X/Y,
validcount, point X/Y, result. Every selected frame is recorded.
"""
import argparse
import os
from pathlib import Path
import shutil
import benchmark_doom as bench

PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
static FILE *bbox_probe_file;
static int bbox_probe_checked;
static void WriteBBoxProbe(unsigned kind, fixed_t x, fixed_t y, angle_t result)
{
    if (!bbox_probe_checked) {
        const char *path = getenv("DOOM_BENCH_BBOX");
        if (path) {
            bbox_probe_file = fopen(path, "wb");
            if (!bbox_probe_file) abort();
        }
        bbox_probe_checked = 1;
    }
    if (bbox_probe_file && (gametic < 64 || (gametic >= 200 && gametic < 216))) {
        uint32_t words[] = {kind, gametic, viewx, viewy, validcount, x, y, result};
        for (unsigned i = 0; i < 8; ++i) {
            unsigned char bytes[4];
            for (unsigned j = 0; j < 4; ++j) bytes[j] = words[i] >> (8*j);
            if (fwrite(bytes, 4, 1, bbox_probe_file) != 1) abort();
        }
    }
}
OF_FASTTEXT static angle_t R_BBoxPointAngle(fixed_t x, fixed_t y)
{
    angle_t result = R_BBoxPointAngleProbeOriginal(x, y);
    WriteBBoxProbe(1, x, y, result);
    return result;
}
__attribute__((destructor)) static void CloseBBoxProbe(void)
{
    if (bbox_probe_file && fclose(bbox_probe_file)) abort();
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--iwad', type=Path, required=True)
    parser.add_argument('--merge', type=Path, nargs='+', default=[])
    parser.add_argument('--demos', nargs='+', default=['demo1'])
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'instrumented'
    for name in ('doom', 'sdk'):
        shutil.copytree(args.source / 'src' / name, source / 'src' / name, symlinks=True,
                        ignore=shutil.ignore_patterns('*.wad', '*.WAD', '*.o', '*.elf', '*.d', 'app_pc', '__pycache__'))
    path = source / 'src/doom/cdoom/doom/r_bsp.c'
    text = path.read_text()
    signature = 'OF_FASTTEXT static angle_t R_BBoxPointAngle(fixed_t x, fixed_t y)'
    if text.count(signature) != 1:
        raise ValueError('Expected one bbox-angle function')
    text = text.replace(signature, signature.replace('R_BBoxPointAngle', 'R_BBoxPointAngleProbeOriginal'))
    # Insert immediately after the original function, before its callers.
    start = text.index(signature.replace('R_BBoxPointAngle', 'R_BBoxPointAngleProbeOriginal'))
    end = text.index('\n}', start) + 2
    text = text[:end] + '\n' + PROBE + text[end:]
    frame = 'void R_ClearClipSegs (void)\n{'
    if text.count(frame) != 1:
        raise ValueError('Expected one frame-cache setup function')
    path.write_text(text.replace(frame, frame + '\n    WriteBBoxProbe(0, 0, 0, 0);'))
    binary = bench.build(source, output, 'probe')
    for demo in args.demos:
        path = output / (Path(demo).stem + '.bin')
        os.environ['DOOM_BENCH_BBOX'] = str(path)
        bench.run(binary, output, 'probe', args.iwad.resolve(), demo, 'capture', False,
                  [p.resolve() for p in args.merge])
        if not path.exists() or not path.stat().st_size:
            raise RuntimeError(f'No samples: {path}')
        print(path, path.stat().st_size // 32, 'records', flush=True)


if __name__ == '__main__':
    main()
