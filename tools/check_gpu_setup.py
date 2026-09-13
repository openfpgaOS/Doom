#!/usr/bin/env python3
"""Compare exact GPU parameter bytes from production wall and plane setup."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

from gpu_test_sources import function, gpu_types

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Baseline r_gpu.c")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fused", action="store_true",
                        help="Exercise x86 FMA contraction (requires host FMA)")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    hashes = []
    for label, path in [("before", args.reference),
                        ("after", ROOT / "src/doom/cdoom/doom/r_gpu.c")]:
        job = output / label
        job.mkdir(exist_ok=True)
        source = path.read_text()
        header = (ROOT / "src/sdk/include/of_gpu.h").read_text()
        (job / "gpu_types.h").write_text(gpu_types(header))
        prefix = ""
        if "gpu_view_basis;" in source:
            if "gpu_plane_coeff_cache[" in source:
                start = source.index("#define GPU_PLANE_COEFF_CACHE_")
                end = source.index("static struct {", start)
                prefix = source[start:end]
            prefix += re.search(r"static struct \{[^}]+\} gpu_view_basis;", source)[0]
            prefix += "\n" + function(source, "R_GPU_BeginView") + "\n#define HAS_BEGIN_VIEW 1\n"
        names = ["gpu_param_encode_q29", "R_GPU_BeginPlaneSpans", "gpu_wall_texcol_at",
                 "R_GPU_WallSegBegin", "gpu_wall_tier_begin", "R_GPU_WallTierBegin"]
        (job / "gpu_functions.h").write_text(prefix + "\n".join(function(source, n) for n in names))
        command = ["cc", "-std=gnu11", "-O2", "-g", "-ffp-contract=off",
                   "-fsanitize=address,undefined,float-cast-overflow",
                   "-fno-omit-frame-pointer", "-no-pie", "-I" + str(job),
                   "-I" + str(ROOT / "src/doom/cdoom"), "-I" + str(ROOT / "src/doom/shim"),
                   str(ROOT / "tools/tests/test_gpu_setup.c"),
                   str(ROOT / "src/doom/cdoom/tables.c"), "-lm", "-o", str(job / "test")]
        if args.fused:
            command[command.index("-ffp-contract=off")] = "-ffp-contract=fast"
            command.insert(1, "-mfma")
        with (job / "build.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (job / "run.log").open("w") as log:
            subprocess.run([str(job / "test"), str(job / "params.bin")], stdout=log,
                           stderr=subprocess.STDOUT, check=True,
                           env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=1"))
        hashes.append(hashlib.sha256((job / "params.bin").read_bytes()).hexdigest())
        print(label, (job / "run.log").read_text().strip(), hashes[-1], flush=True)
    if hashes[0] != hashes[1]:
        raise RuntimeError("GPU parameter bytes changed")
    print("PASS identical GPU parameters", flush=True)


if __name__ == "__main__":
    main()
