#!/usr/bin/env python3
"""Exercise WAD parsing and zone allocation against invalid sizes and ranges."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=ROOT / "build/review-stability/assets")
    args = parser.parse_args()
    source, output = args.source_root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for name, macro, path, cases in [
        ("wad_bounds", "WAD", "w_wad.c", ["valid", "short-header", "short-directory",
            "directory-offset", "directory-size", "directory-negative", "count-overflow",
            "count-negative", "lump-offset", "lump-size", "lump-negative", "size-negative", "empty-marker"]),
        ("zone_bounds", "ZONE", "z_zone.c", ["valid", "negative", "overflow"]),
    ]:
        binary = output / name
        cmd = ["cc", "-std=gnu11", "-DOF_PC", "-DOF_DOOM", "-O2", "-g", "-fsanitize=address,undefined",
               "-no-pie", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
               '-D' + macro + '_SOURCE="' + str(source / "src/doom/cdoom" / path) + '"']
        cmd += ["-I" + str(source / p) for p in ("src/sdk/include", "src/doom/shim", "src/doom/cdoom", "src/doom/cdoom/doom")]
        cmd += [str(ROOT / "tools/tests" / ("test_" + name + ".c")), "-o", str(binary)]
        subprocess.run(cmd, check=True)
        for case in cases:
            run = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=30,
                                 env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=1"))
            (output / (name + "-" + case + ".log")).write_text(run.stdout + run.stderr)
            results.append(dict(test=name, case=case, passed=run.returncode == 0, returncode=run.returncode))
            print(("PASS: " if run.returncode == 0 else "FAIL: ") + name + " " + case)
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(not r["passed"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
