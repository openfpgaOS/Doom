# Pocket renderer layout — 2026-09-12

For the subsequent RISC-V/GPU optimization pass, see [rv32-gpu-performance.md](rv32-gpu-performance.md). The results below describe the earlier layout-only change.

Pocket builds now link `r_segs`, `r_bsp`, `r_plane`, `r_things`, `r_gpu` and
`r_main` together, in that order, before the remaining Doom game objects.
This changes SDRAM code placement and the order of their existing APP_BRAM
functions/data. No rendering calculations, game rules or audio logic changed.
MiSTer keeps the previous object order; a clean MiSTer rebuild matched the
released ELF byte for byte. The ELF also depends on the app
Makefile so an incremental build relinks when the order changes.

## Results

These are cycle measurements on the **actual single-issue Pocket CPU RTL**
with its production instruction/data caches and Pocket SDRAM controller.
The applications are normal, uninstrumented ELFs. A separate driver calls
their existing renderer symbols using captured demo inputs.

| Captured workload | Baseline cycles, scanout active | New layout | Fewer cycles |
|---|---:|---:|---:|
| Doom II, demo1 | 1,872,873 | 1,805,981 | 3.57% |
| SIGIL 1 compatibility, demo1 (E3M2) | 6,887,885 | 6,597,491 | 4.22% |
| SIGIL II, demo1 (E6M1) | 10,206,144 | 9,820,915 | 3.77% |

Without scanout, the improvements were 3.68%, 4.19% and 3.73%, respectively.
Instruction-cache refill traffic fell approximately 63% in these replays.
All captured bbox results and all per-frame GPU-parameter/geometry hashes
matched. The simulated CPU/SDRAM boundaries reported no data, response-ID,
burst-length, scanout or write-confirmation errors.

A final SIGIL II replay with **both** the new ELF layout and per-bank SDRAM
tracking used 9,774,940 cycles with scanout, versus the baseline's 10,206,144
(4.22% fewer). All fourteen hashes matched. This confirms the two changes
work together in this fixture; it still excludes complete GPU rendering and
audio work.

Each replay covers fourteen complete selected renderer-input sequences from
gameplay tics 0–7 and 200–207. It includes actual view state and angle tables,
bbox requests, wall geometry/tiers and plane setup. Texture/framebuffer
addresses are fixed placeholders: the GPU does not draw pixels in this test.
Display traffic approximates a 320×200 source image read at 60 Hz. These are
**renderer-setup measurements, not complete frame times or measured Pocket
FPS**. No Pocket was connected for physical testing.

Several other object orders were tested. Grouping every `r_*.c` file first
reduced instruction-cache misses but slowed some replays; it was rejected.
The selected order improved all three workloads both with and without
scanout. The separate core SDRAM row-tracking improvement should not be
arithmetically added to these percentages.

The final clean Pocket build exactly matches the tested candidate:

```text
app.elf SHA-256:
58530ae88b66616c29e1bbf6c457d4398f5277f952cd8ee8931756bec9dcb456
```

ELF size remains 1,291,592 bytes. APP_BRAM ends at `0x77e0`, using
14,304 of 14,336 bytes including alignment. It does not consume more FPGA
RAM. No version number or installed runtime was changed.

## Reproduce

Requires local IWAD/PWADs, the SDK toolchain container, pyelftools, Verilator,
and a sibling core checkout with its generated `VexiiRiscv_os25.v`.
SIGIL requires the Ultimate Doom IWAD; the original three-episode Doom IWAD
does not contain all required textures.

Build the candidate normally, using a separate object directory. For a
baseline on the same source, override `DOOM_SRCS` to its original wildcard
order; use another object directory to preserve both normal ELFs.

```sh
bash tools/sdk-container.sh make -C src/doom TARGET=pocket \
  OBJ_DIR="$PWD/.obj/pocket-before" \
  'DOOM_SRCS=$(wildcard cdoom/doom/*.c)' \
  "$PWD/.obj/pocket-before/app.elf" -j6
bash tools/sdk-container.sh make -C src/doom TARGET=pocket \
  OBJ_DIR="$PWD/.obj/pocket-after" "$PWD/.obj/pocket-after/app.elf" -j6

python3 tools/trace_renderer_layout.py --output build/pocket-capture \
  --iwad /path/to/ultimate-doom.wad --merge /path/to/SIGIL_II_V1_0.WAD
python3 tools/replay_renderer_layout.py --elf .obj/pocket-before/app.elf \
  --output build/replay-before
python3 tools/replay_renderer_layout.py --elf .obj/pocket-after/app.elf \
  --output build/replay-after
python3 ../openfpgaOS/tools/build_pocket_renderer_replay.py \
  --output ../openfpgaOS/build/pocket-renderer-replay
```

From **each prepared replay directory**, run the simulator with an absolute
trace path. `firmware.mif` in that directory contains the application's
original BRAM code/data and the isolated driver's boot entry.

```sh
SCAN_ENABLE=1 SCAN_PERIOD=8333 SCAN_LEN=80 SCAN_ACTIVE=200 SCAN_TOTAL=200 \
SCAN_BASE_HW=0x1800000 STRIKE_SCAN_PERIOD=1000000000 \
  /absolute/path/to/openfpgaOS/build/pocket-renderer-replay/obj/Vtb_system \
  replay.bin 100000000 /absolute/path/to/Doom/build/pocket-capture/demo1.bin
```

Require exit status zero. Compare `FRAME` records inside the final
`=== UART BEGIN ===` / `=== UART END ===` block. Each record contains a frame
index, cycle count and hash, all in hexadecimal. Sum cycles and require identical frame
indices/hashes. Set `SCAN_ENABLE=0` for the no-scanout comparison. Do not set
`STRIKE_SCAN_PERIOD=0`: zero requests an expensive full image check every cycle.

The source probes live only in isolated host copies. Captures, normal ELFs,
source manifests and full test logs for this review remain in
`build/pocket-performance-20260912` in the two repositories. No WAD assets or
captured map data are included in these test tools.
