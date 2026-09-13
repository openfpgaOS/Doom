#!/usr/bin/env python3
"""Compare normal-ELF CPU work on the Pocket RTL with active scanout.

Prepare each directory with replay_renderer_layout.py first. A renderer
trace is required by the simulator in both modes; the submission driver
ignores its contents. Results exclude full-frame GPU and audio timing.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def run(sim, directory, trace, output, label, mode):
    env = dict(os.environ, SCAN_ENABLE="1", SCAN_PERIOD="8333",
               SCAN_LEN="80", SCAN_ACTIVE="200", SCAN_TOTAL="200",
               SCAN_BASE_HW="0x1800000", STRIKE_SCAN_PERIOD="1000000000")
    log_path = output / (label + ".log")
    with log_path.open("w") as log_file:
        result = subprocess.run([str(sim), "replay.bin", "100000000", str(trace)],
                                cwd=directory, env=env, stdout=log_file,
                                stderr=subprocess.STDOUT, timeout=1200)
    log = log_path.read_text()
    uart = log.split("=== UART BEGIN ===")[-1].split("=== UART END ===")[0]
    prefix = dict(renderer="FRAME", submission="PACK", bands="BAND", planes="PLANE")[mode]
    rows = [[int(word, 16) for word in line.split()]
            for line in re.findall(r"^" + prefix + r" ([0-9a-f ]+)$", uart, re.M)]
    if result.returncode or not rows or "MAP REPLAY PASS HAL init" not in uart:
        raise RuntimeError(f"{label} failed: inspect {log_path}")
    if mode == "submission" and len(rows) != 90:
        raise RuntimeError(f"Expected 90 submission cohorts, got {len(rows)}")
    if mode == "bands" and len(rows) != 81:
        raise RuntimeError(f"Expected 81 band cohorts, got {len(rows)}")
    if mode == "planes" and len(rows) != 24:
        raise RuntimeError(f"Expected 24 plane cohorts, got {len(rows)}")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("sim", "before", "after", "trace", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", choices=["renderer", "submission", "bands", "planes"], default="renderer")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cycle_column = 1 if args.mode == "renderer" else 3
    results = {}
    for label in ("before", "after"):
        directory = getattr(args, label).resolve()
        manifest = json.loads((directory / "manifest.json").read_text())
        if manifest.get("mode", "renderer") != args.mode:
            raise ValueError(f"{label} driver mode does not match {args.mode}")
        rows = run(args.sim.resolve(), directory, args.trace.resolve(),
                   args.output.resolve(), label, args.mode)
        results[label] = dict(rows=rows, cycles=sum(row[cycle_column] for row in rows),
                              elf_sha256=manifest["sha256"])
        print(label, results[label]["cycles"], "cycles", flush=True)
    before, after = results["before"]["rows"], results["after"]["rows"]
    if len(before) != len(after) or any(
            a[:cycle_column] != b[:cycle_column] or a[-1] != b[-1]
            for a, b in zip(before, after)):
        raise RuntimeError("Output hashes or workload IDs differ")
    results["fewer_cycles_percent"] = 100 * (
        1 - results["after"]["cycles"] / results["before"]["cycles"])
    results["mode"] = args.mode
    results["trace"] = str(args.trace.resolve())
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"PASS identical output; {results['fewer_cycles_percent']:.2f}% fewer "
          "cycles in this CPU fixture (not full-frame FPS)")


if __name__ == "__main__":
    main()
