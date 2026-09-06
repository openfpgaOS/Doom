#!/usr/bin/env python3
"""Test the hardware synth's voice lifetime against a finite mixer pool."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=ROOT / "src/sdk/of_smp_voice.c")
    parser.add_argument("--voices", type=int, default=20, choices=(12, 20, 28))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="openfpgaos-voices-") as tmp:
        build = Path(tmp)
        (build / "include").symlink_to(ROOT / "src/sdk/include", target_is_directory=True)
        binary = build / "voice-test"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
            f"-DSMP_MAX_VOICES={args.voices}",
            f'-DVOICE_SOURCE="{args.source.resolve()}"', f"-I{build}",
            f'-I{ROOT / "src/sdk/include"}',
            str(ROOT / "tools/tests/test_smp_voice_lifetime.c"),
            str(ROOT / "src/sdk/of_smp_tables.c"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
