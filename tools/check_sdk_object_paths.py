#!/usr/bin/env python3
"""Check that shared SDK sources never escape a game's private object tree."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
seen = set()
for game in ("doom", "heretic", "hexen"):
    result = subprocess.run(
        ["make", "-s", "--no-print-directory", "-C", str(ROOT / "src" / game),
         "-f", "Makefile", "-f", "-", "review-object-paths"],
        input=".PHONY: review-object-paths\nreview-object-paths:\n"
              "\t@printf '%s\\n' $(OBJ_DIR) $(APP_OBJS) $(OF_INIT_OBJ) $(OF_SDL2_OBJ)\n",
        text=True, capture_output=True, check=True)
    paths = [Path(line).resolve() for line in result.stdout.splitlines()]
    directory, objects = paths[0], paths[1:]
    assert objects and all(p.is_relative_to(directory) for p in objects), game
    assert len(set(objects)) == len(objects), game
    assert not seen.intersection(objects), game
    seen.update(objects)
    assert any("of_smp_voice.o" == p.name for p in objects), game
    print(f"PASS: {game}: {len(objects)} private object paths")
