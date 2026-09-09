#!/usr/bin/env python3
"""Validate the production SoundFont binder under address/undefined sanitizers."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/sdk/of_smp_bank.c")
    parser.add_argument("--output", type=Path, default=ROOT / "build/review-stability/bank")
    parser.add_argument("--bank", type=Path, default=ROOT / "runtime/bank.ofsf")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "bank-test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-address",
                    "-O2", "-g", "-fsanitize=address,undefined", "-no-pie",
                    '-DBANK_SOURCE="' + str(args.source.resolve()) + '"',
                    "-I" + str(ROOT / "src/sdk"), "-I" + str(ROOT / "src/sdk/include"),
                    str(ROOT / "tools/tests/test_bank_bounds.c"), "-o", str(binary)], check=True)
    results = []
    for case in ("valid", "zone-wrap", "metadata-overlap", "truncated-zone",
                 "truncated-header", "sample-range", "sample-wrap", "sample-offset",
                 "loop-range", "loop-empty", "preset-range", "odd-offset", "odd-blob",
                 "zero-rate", "file"):
        cmd = [str(binary), case] + ([str(args.bank.resolve())] if case == "file" else [])
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=30,
                             env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=1"))
        (output / (case + ".log")).write_text(run.stdout + run.stderr)
        results.append(dict(test=case, passed=run.returncode == 0, returncode=run.returncode))
        print(("PASS: " if run.returncode == 0 else "FAIL: ") + case)
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(not r["passed"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
