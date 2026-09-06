#!/usr/bin/env python3
"""Compare DOOM CPU-renderer demos in isolated host builds; these are not FPGA FPS."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WRAPS = ["I_StartFrame", "I_FinishUpdate", "R_RenderPlayerView", "TryRunTics", "S_UpdateSounds",
         "R_PrecacheLevel", "R_GPU_TextureDataUpdated"]


def build(source, output, label):
    stage = output / (label + "-source")
    if stage.exists():
        raise SystemExit(f"Build directory already exists: {stage}; choose a new --output")
    for name in ("doom", "sdk"):
        shutil.copytree(source / "src" / name, stage / "src" / name, symlinks=True,
                        ignore=shutil.ignore_patterns("*.wad", "*.WAD", "*.vhd", "*.elf", "*.o", "app_pc", "__pycache__"))
    cmd = ["make", "-C", str(stage / "src/doom"), "app_pc",
           "PC_EXTRA_CFLAGS=-g " + str(ROOT / "tools/tests/doom_benchmark.c"),
           "PC_EXTRA_LIBS=-Wl," + ",".join("--wrap=" + s for s in WRAPS)]
    with (output / (label + "-build.log")).open("w") as log:
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = output / ("doom-" + label)
    shutil.copy2(stage / "src/doom/app_pc", binary)
    return binary


def run(binary, output, label, wad, demo, number, trace):
    stem = f"{label}-{demo}-{number}"
    work = output / stem
    work.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    if trace:
        env["DOOM_BENCH_TRACE"] = str(work / "trace.csv")
    cmd = [str(binary), "-iwad", str(wad), "-timedemo", demo, "-nosound",
           "-config", str(work / "doom.cfg"), "-extraconfig", str(work / "extra.cfg")]
    result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True, timeout=60)
    log = result.stdout + result.stderr
    (work / "run.log").write_text(log)
    # This port reports successful timedemo completion through I_Error (exit 1).
    if result.returncode not in (0, 1) or "gametics in" not in log:
        raise RuntimeError(f"Demo did not complete: {stem}\n{log[-2000:]}")
    stats = [line.removeprefix("DOOM_BENCH ") for line in log.splitlines() if line.startswith("DOOM_BENCH ")]
    if len(stats) != 1:
        raise RuntimeError(f"Missing benchmark report: {stem}")
    return json.loads(stats[0]), work


def gameplay_trace(path):
    # Wipe animation at a fixed gametic depends on wall time. Compare the last
    # completed display of every gameplay tic, excluding the initial wipe.
    with path.open() as stream:
        return {int(row[0]): row[1:] for row in csv.reader(stream) if int(row[0]) > 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True, help="Unmodified checkout or git archive")
    parser.add_argument("--current", type=Path, default=ROOT)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/review-smoothness/demos")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--demos", nargs="+", default=["demo1", "demo2", "demo3"])
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    output, wad = args.output.resolve(), args.iwad.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {label: build(source.resolve(), output, label)
                for label, source in (("before", args.baseline), ("after", args.current))}
    report = dict(iwad_sha256=hashlib.sha256(wad.read_bytes()).hexdigest(),
                  scope="Host CPU renderer with display waits, SDL presentation and audio excluded; not FPGA FPS", demos={})
    for demo in args.demos:
        traces = {}
        for label, binary in binaries.items():
            stats, work = run(binary, output, label, wad, demo, "trace", True)
            traces[label] = gameplay_trace(work / "trace.csv")
        if not traces["before"] or traces["before"] != traces["after"]:
            raise RuntimeError(f"Gameplay/framebuffer mismatch in {demo}; inspect trace.csv files")
        measurements = {label: [] for label in binaries}
        for n in range(args.runs):
            # Alternate order to reduce warmup/thermal bias; trace cost excluded.
            for label in (list(binaries) if n % 2 == 0 else list(reversed(binaries))):
                stats, _ = run(binaries[label], output, label, wad, demo, n, False)
                measurements[label].append(stats)
        medians = {label: {key: statistics.median(s[key] for s in samples) for key in samples[0]}
                   for label, samples in measurements.items()}
        report["demos"][demo] = dict(matching_gameplay_tics=len(traces["before"]), medians=medians, runs=measurements)
        print(demo, "PASS:", len(traces["before"]), "matching gameplay tics; render ms before/after:",
              medians["before"]["render_ns"] / 1e6, medians["after"]["render_ns"] / 1e6, flush=True)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
