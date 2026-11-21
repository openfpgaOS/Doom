#!/usr/bin/env python3
"""Compare the mixer register write stream (time, register, voice, value) of
the SDK MIDI engine against a reference copy on real songs and a real bank.

  check_midi_write_stream.py --reference <dir with of_midi.c, of_smp_voice.c,
      of_smp_bank.c, of_smp_tables.c> --bank bank.ofsf --songs a.mid b.mid ...

Each version is built with the same host mixer model and driven by the same
scripted clock, once with free register stores and once with a CPU-time cost
per store (pump budget overruns).  Songs and banks are user data: pass your
own (e.g. MIDI lumps extracted from a WAD)."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['of_midi.c', 'of_smp_voice.c', 'of_smp_bank.c', 'of_smp_tables.c']


def build(sdk, include, binary, voices):
    sdk = sdk.resolve()
    subprocess.run([
        'cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-function',
        '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-include', str(ROOT / 'tools/tests/midi_write_stream_host.h'),
        f'-I{include}', f'-I{include}/include', f'-DSMP_MAX_VOICES={voices}',
        '-DSMP_VOICE_FAST_TICK=1', f'-DVOICE_SOURCE="{sdk / "of_smp_voice.c"}"',
        str(ROOT / 'tools/tests/test_midi_write_stream.c'),
        *[str(sdk / s) for s in SOURCES if s != 'of_smp_voice.c'], '-o', str(binary),
    ], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--source', type=Path, default=ROOT / 'src/sdk')
    ap.add_argument('--bank', type=Path, required=True)
    ap.add_argument('--songs', type=Path, nargs='+', required=True)
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--voices', type=int, default=20)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='midi-stream-') as tmp:
        tmp = Path(tmp)
        for name, sdk in [('reference', args.reference), ('candidate', args.source)]:
            # The engine includes "include/of_*.h" relative to its own file.
            (tmp / name).mkdir()
            for s in SOURCES:
                (tmp / name / s).write_bytes((sdk / s).read_bytes())
            (tmp / name / 'include').symlink_to(ROOT / 'src/sdk/include', target_is_directory=True)
            build(tmp / name, tmp / name, tmp / (name + '.bin'), args.voices)
        for cost in (0, 400):
            traces = []
            for name in ('reference', 'candidate'):
                trace = tmp / f'{name}-{cost}.trace'
                env = dict(os.environ, MT_WRITE_COST_US=str(cost), ASAN_OPTIONS='detect_leaks=0')
                subprocess.run([str(tmp / (name + '.bin')), str(args.bank), str(trace),
                                str(args.seconds), *map(str, args.songs)],
                               env=env, stderr=subprocess.DEVNULL, check=True)
                traces.append(trace.read_bytes())
            if traces[0] != traces[1]:
                n = next((i for i in range(0, min(map(len, traces)), 16)
                          if traces[0][i:i + 16] != traces[1][i:i + 16]), min(map(len, traces)))
                raise SystemExit(f'FAIL: write streams differ at record {n // 16} (store cost {cost} us)')
            print(f'PASS: {len(traces[0]) // 16:,} identical mixer writes; {len(args.songs)} songs x '
                  f'{args.seconds} s; register-store cost {cost} us')


if __name__ == '__main__':
    main()
