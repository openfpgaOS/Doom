#!/usr/bin/env python3
"""Check real level/recording startup order with an isolated host build."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out / "source"
    for name in ("doom", "sdk"):
        shutil.copytree(ROOT / "src" / name, source / "src" / name,
                        ignore=shutil.ignore_patterns("*.wad", "*.WAD", "*.o", "*.elf", "*.d", "app_pc"))
    wrappers = ["I_InitGraphics", "G_InitNew", "G_BeginRecording", "TryRunTics"]
    with (out / "build.log").open("w") as log:
        subprocess.run([
            "make", "-C", str(source / "src/doom"), "app_pc",
            "PC_EXTRA_CFLAGS=-g " + str(ROOT / "tools/tests/test_startup.c"),
            "PC_EXTRA_LIBS=-Wl," + ",".join("--wrap=" + name for name in wrappers),
        ], stdout=log, stderr=subprocess.STDOUT, check=True)
    for record in (False, True):
        work = out / ("record" if record else "warp")
        work.mkdir()
        cmd = [str(source / "src/doom/app_pc"), "-iwad", str(args.iwad.resolve()),
               "-warp", "1", "1", "-skill", "4", "-nomonsters", "-fast", "-respawn",
               "-nosound", "-noautoload", "-config", "doom.cfg", "-extraconfig", "extra.cfg"]
        if record:
            cmd += ["-record", "startup"]
        result = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=60,
                                env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
        log = result.stdout + result.stderr
        (work / "run.log").write_text(log)
        if result.returncode or "PASS startup" not in log:
            raise RuntimeError(f"Startup failed: inspect {work / 'run.log'}")
        print("PASS startup: " + work.name)


if __name__ == "__main__":
    main()
