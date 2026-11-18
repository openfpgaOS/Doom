#!/usr/bin/env python3
"""Exercise Doom GPU initialization with the SDK and a register model."""
import argparse
from pathlib import Path
import subprocess

from gpu_test_sources import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    header = (ROOT / "src/sdk/include/of_gpu.h").read_text()
    mmio = "#define OF_GPU_REG(off)         (*(volatile uint32_t *)(_gpu_base + (off)))"
    assert header.count(mmio) == 1
    (out / "of_gpu_test.h").write_text(header.replace(
        mmio, "#define OF_GPU_REG(off) (*test_register(off))"))
    source = (ROOT / "src/doom/cdoom/doom/r_gpu.c").read_text()
    (out / "doom_init.h").write_text("\n".join(
        function(source, name) for name in ("gpu_caps_cover_range", "gpu_init_for_doom")))
    exe = out / "test"
    with (out / "build.log").open("w") as log:
        subprocess.run([
            "cc", "-std=gnu11", "-O2", "-g", "-no-pie",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I" + str(out), "-I" + str(ROOT / "src/sdk/include"),
            str(ROOT / "tools/tests/test_gpu_init.c"), "-o", str(exe),
        ], stdout=log, stderr=subprocess.STDOUT, check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    (out / "run.log").write_text(result.stdout + result.stderr)
    print(result.stdout, end="")


if __name__ == "__main__":
    main()
