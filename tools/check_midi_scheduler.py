#!/usr/bin/env python3
"""Run the hardware MIDI parser on the host with mocked timer/voice services."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=ROOT / "src/sdk/of_midi.c")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="openfpgaos-midi-") as tmp:
        build = Path(tmp)
        (build / "include").symlink_to(ROOT / "src/sdk/include", target_is_directory=True)
        binary = build / "midi-test"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
            f'-DMIDI_SOURCE="{args.source.resolve()}"', f"-I{build}",
            str(ROOT / "tools/tests/test_midi_scheduler.c"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
