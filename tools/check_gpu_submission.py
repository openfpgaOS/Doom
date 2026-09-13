#!/usr/bin/env python3
"""Compare production GPU command bytes, including clamps and continuations."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

from gpu_test_sources import function, gpu_types

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Baseline of_gpu.h")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generic", action="store_true", help="Force the endian-neutral packing path")
    parser.add_argument("--doom-spans", action="store_true", help="Compare Doom's validated span path with the public API")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    hashes = []
    for label, path in [("before", args.reference), ("after", ROOT / "src/sdk/include/of_gpu.h")]:
        job = output / label
        job.mkdir(exist_ok=True)
        source = path.read_text()
        (job / "gpu_types.h").write_text(gpu_types(source))
        names = ["_gpu_count12", "_gpu_build_param_span_header", "_gpu_emit_param_span_header_words",
                 "_gpu_emit_param_span_list", "of_gpu_draw_param_span_list", "of_gpu_draw_persp_span_group"]
        (job / "gpu_functions.h").write_text("\n".join(function(source, n, last=False) for n in names))
        if args.doom_spans:
            with (job / "gpu_functions.h").open("a") as header_file:
                if label == "after":
                    header_file.write(function((ROOT / "src/doom/cdoom/doom/r_gpu.c").read_text(),
                                               "gpu_emit_screen_span_records"))
                else:
                    header_file.write("\n#define gpu_emit_screen_span_records of_gpu_draw_param_span_list\n")
        command = ["cc", "-std=gnu11", "-O2", "-g", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer", "-no-pie", "-I" + str(job),
                   str(ROOT / "tools/tests/test_gpu_submission.c"), "-o", str(job / "test")]
        if args.doom_spans:
            command += ["-DTEST_DOOM_SPANS", "-DSCREENWIDTH=320", "-DSCREENHEIGHT=200"]
        if args.generic:
            command.insert(1, "-U__BYTE_ORDER__")
        with (job / "build.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (job / "run.log").open("w") as log:
            subprocess.run([str(job / "test"), str(job / "commands.bin")], stdout=log,
                           stderr=subprocess.STDOUT, check=True,
                           env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=1"))
        hashes.append(hashlib.sha256((job / "commands.bin").read_bytes()).hexdigest())
        print(label, (job / "run.log").read_text().strip(), hashes[-1], flush=True)
    if hashes[0] != hashes[1]:
        raise RuntimeError("GPU command bytes changed")
    print("PASS identical command streams", flush=True)


if __name__ == "__main__":
    main()
