#!/usr/bin/env python3
"""Compile focused regressions against the production renderer and shared shim."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=ROOT / "build/review-smoothness/checks")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source, output = args.source_root.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    includes = [source / p for p in ("src/sdk/include", "src/doom/shim", "src/doom/cdoom", "src/doom/cdoom/doom")]
    common = [os.environ.get("CC", "cc"), "-DOF_PC", "-O2", "-g", "-ffunction-sections", "-fdata-sections"]
    common += ["-I" + str(p) for p in includes]
    if args.sanitize:
        common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
    tests = [
        ("video_pacing", "VIDEO", "src/doom/shim/i_video.c", ["mister-period", "late-frame", "fresh-frame", "timedemo", "overload", "config-lifetime", "gamma"]),
        ("sprite_sort", "SORT", "src/doom/cdoom/doom/r_things.c", [None]),
        ("wall_math", "WALL", "src/doom/cdoom/doom/r_segs.c", [None]),
        ("wall_scale", "SCALE", "src/doom/cdoom/doom/r_main.c", [None]),
        ("wall_divide", "WALL", "src/doom/cdoom/doom/r_segs.c", [None]),
        ("view_cache", "BSP", "src/doom/cdoom/doom/r_bsp.c", [None]),
        ("sprite_precache", "DATA", "src/doom/cdoom/doom/r_data.c", [None]),
        ("statusbar_overlay", "STATUS", "src/doom/cdoom/doom/st_stuff.c", [None]),
        ("masked_bounds", "DRAW", "src/doom/cdoom/doom/r_draw.c", [None]),
        ("sfx", "SFX", "src/doom/shim/i_sdlsound.c", ["allocation", "pcm", "params"]),
    ]
    results = []
    env = dict(os.environ, ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1")
    for test, macro, path, cases in tests:
        for game in (["DOOM", "HERETIC", "HEXEN"] if test == "sfx" else ["DOOM"]):
            name = test + "-" + game.lower()
            binary = output / name
            cmd = common + ["-DOF_" + game, '-DDOOM_' + macro + '_SOURCE="' + str(source / path) + '"']
            if test == "statusbar_overlay" and "void ST_InvalidateBuffer(void)" in (source / path).read_text():
                cmd.append("-DTEST_STATUS_INVALIDATE")
            if test == "sfx":
                cmd.insert(1, "-I" + str(ROOT / "tools/tests/audio_mock"))
            if test == "view_cache":
                cmd.append(str(source / "src/doom/cdoom/tables.c"))
            cmd += [str(ROOT / "tools/tests" / ("test_" + test + ".c")), "-Wl,--gc-sections", "-o", str(binary)]
            built = subprocess.run(cmd, capture_output=True, text=True)
            (output / (name + "-build.log")).write_text(built.stdout + built.stderr)
            if built.returncode:
                results.append(dict(test=name, phase="build", passed=False))
                print("FAIL:", name, "build:", built.stderr[-2000:])
                continue
            for case in cases:
                label = name + ("-" + case if case else "")
                result = subprocess.run([str(binary)] + ([case] if case else []), env=env, capture_output=True, text=True, timeout=30)
                log = result.stdout + result.stderr
                (output / (label + ".log")).write_text(log)
                results.append(dict(test=label, passed=result.returncode == 0, returncode=result.returncode))
                print(("PASS: " if result.returncode == 0 else "FAIL: ") + label)
                if result.returncode:
                    print(log[-1500:])
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(not r["passed"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
