#!/usr/bin/env python3
"""Test music read recovery and GPU waits across a paused system menu."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/doom/shim/i_pcmmusic.c')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='doom-pcm-') as directory:
        binary = Path(directory) / 'pcm-test'
        command = ['cc', '-std=gnu11', '-DOF_DOOM', '-O1', '-g', '-Wall', '-Wextra',
                   '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   f'-DPCM_SOURCE="{args.source.resolve()}"']
        command += ['-I' + str(ROOT / path) for path in
                    ('tools/tests/pcm_mock', 'src/doom/shim', 'src/doom/cdoom',
                     'src/doom/cdoom/doom', 'src/sdk/include')]
        subprocess.run(command + [str(ROOT / 'tools/tests/test_pcm_recovery.c'),
                                  '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
        base = [part for part in command if not part.startswith('-DPCM_SOURCE=')]
        subprocess.run(base + [str(ROOT / 'tools/tests/test_gpu_menu_wait.c'),
                               '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))
        command = [part for part in command if not part.startswith('-DPCM_SOURCE=')]
        command += [f'-DWSTD_SOURCE="{ROOT / "src/doom/cdoom/w_file_stdc.c"}"',
                    str(ROOT / 'tools/tests/test_wad_read_recovery.c'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=30,
                       env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__':
    main()
