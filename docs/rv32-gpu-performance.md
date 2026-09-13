# RISC-V renderer and GPU submission — 2026-09-12

For further optimizations and a correction to the original host command-test coverage, see [sigil-next-performance.md](sigil-next-performance.md).

This pass reduces CPU work in Doom without changing the GPU command format,
rendering precision, game rules, clock rate or audio path. It builds for
Pocket and MiSTer. The shared GPU header is also updated in the core's SDK
source, `src/firmware/api/of_gpu.h`, so a future SDK export retains the changes.

## Changes

- Calculate the camera/projection basis once after `R_SetupFrame`, after
  interpolation and viewport setup have finalized the view. Retain the
  original division operations and rounding; do not substitute approximate
  reciprocals. Negating the tiled Y coordinate now uses unsigned arithmetic
  to avoid signed overflow at `INT_MIN`.
- Specialize Q29 encoding for walls and floors. Remove duplicate bounds for
  known zero derivatives. Inline and unroll the three attributes so RV32
  can eliminate zero terms and intermediate stack traffic. Retain the
  surviving corner arithmetic, dynamic shift and integer conversion rules.
- Compare resident GPU headers as 29 words instead of libc's byte-at-a-time
  116-byte `memcmp`. Copy a new header to the command stream and its cache
  in one pass, reusing the already calculated words.
- Reuse the existing count scan to select bulk copying for valid span lists
  of at least 16 records with word alignment. Short or two-byte-aligned lists
  retain direct packing. Oversized pixel counts retain the original 12-bit
  clamp. Odd lists still end with a complete zero record. Other endianness
  uses the original packing path.

No new GPU opcode or capability bit is required. Older cores retain their
full-header path. This pass does not modify RTL or require an FPGA rebuild.

## Measured results

These are cycles measured on the production **single-issue Pocket CPU RTL**
with its instruction/data caches, SDRAM controller and active scanout traffic.
The driver calls functions in normal, uninstrumented application ELFs.

The baseline already includes the earlier [renderer object order](pocket-performance.md)
and per-bank SDRAM tracking. These are additional gains over that baseline.

| Renderer input capture | Baseline cycles | New cycles | Fewer cycles |
|---|---:|---:|---:|
| Doom II demo1 | 1,801,765 | 1,643,651 | 8.78% |
| SIGIL 1 compatibility demo1, E3M2 | 6,572,502 | 5,985,585 | 8.93% |
| SIGIL II demo1, E6M1 | 9,774,940 | 9,053,613 | 7.38% |

Each capture covers fourteen selected view sequences from gameplay tics
0–7 and 200–207. The replay includes bbox calculations, wall/plane setup,
input reads, hash work and the new once-per-view setup. All per-frame hashes
and captured bbox results match. The simulator reports no CPU/SDRAM data,
response-ID, burst, scanout or write-confirmation errors.

A separate command-submission fixture takes **1,778,768 cycles versus
2,709,437**, or **34.35% fewer cycles**, across 90 synthetic cohorts. It covers
1, 2, 3, 8, 16, 32, 64, 128 and 512 records; both allowed alignments; legacy
cores; repeated headers; texture changes; lighting changes; and oversized
counts. Each cohort submits sixteen commands. Output hashes all match.
The exact routine in the production ELF is timed, with the command consumer
always ready. DMA, GPU execution, count scanning in the public caller and
framebuffer rendering are excluded from this fixture.

For aligned lists with repeated headers, the command fixture saves roughly
51–63% across the tested sizes. Changing textures or lighting also improves.
This distribution is deliberately synthetic; its aggregate is not an estimate
of how frequently these paths execute in a level.

**These percentages cannot be added, and they are not complete frame-time or
FPS measurements.** Neither fixture includes complete GPU rasterization or
audio contention. No physical Pocket was connected. A 10% gain in complete
frames remains unverified; it needs an on-device comparison at the same
clock, viewport and music settings, including the heavy SIGIL scenes.
MiSTer builds successfully, but its performance was not measured by this
Pocket CPU fixture.

## Correctness and build checks

- 300,000 randomized wall/plane setup comparisons with separate operations,
  plus 300,000 with host FMA contraction. Before/after parameter bytes match
  within each mode. Both run with address, undefined-behavior and float-cast
  sanitizers, using the real Doom trigonometry tables.
- The original 17,448-case host command comparison selected desktop stubs
  for public SDK draw functions and therefore did not validate emitted payloads.
  This was corrected and the actual SDK implementations were revalidated in
  the [following pass](sigil-next-performance.md#correctness). The separate
  RISC-V command simulations reported above exercised real application code.
- All 24 existing smoothness regressions pass with sanitizers, covering frame
  pacing, renderer math, status-bar handling and sound-effect behavior.
- Pocket and MiSTer builds complete without warnings. APP_BRAM still ends at
  `0x77e0`: 14,304 of 14,336 bytes including alignment. No additional FPGA RAM,
  DSPs or ALMs are needed. The Pocket ELF shrinks by 4,068 bytes to 1,287,524.

Normal ELF SHA-256 values:

```text
baseline Pocket
58530ae88b66616c29e1bbf6c457d4398f5277f952cd8ee8931756bec9dcb456
new Pocket
224bfd61288865111cfd3a3e375075ae9d0b1495c01ca067b3fd3f3ece95d1c7
new MiSTer
48feeadcb22a5dfdf281c86238aa53bfa0d6f9c2351a0faa1a02882520632bcb
```

This is a local optimization build. Version numbers, published releases and
installed devices were not changed.

## Reproduce

First preserve normal before/after ELFs and baseline copies of `r_gpu.c` and
`of_gpu.h`. Use separate object directories and the normal release flags;
profiling instrumentation changes code placement on Pocket. See
[pocket-performance.md](pocket-performance.md) for capture and simulator build
instructions. Commercial WADs and captured asset data are not included.

```sh
python3 tools/check_gpu_setup.py --reference /path/to/before/r_gpu.c \
  --output build/gpu-setup-check
# On a host supporting x86 FMA, repeat with --fused.
python3 tools/check_gpu_submission.py --reference /path/to/before/of_gpu.h \
  --output build/gpu-command-check
# Repeat with --generic to exercise the endian-neutral packing path.
python3 tools/check_smoothness.py --sanitize --output build/gpu-smoothness

python3 tools/replay_renderer_layout.py --elf /path/to/before/app.elf \
  --output build/renderer-before
python3 tools/replay_renderer_layout.py --elf /path/to/after/app.elf \
  --output build/renderer-after
python3 tools/run_pocket_gpu_replay.py --sim /path/to/Vtb_system \
  --before build/renderer-before --after build/renderer-after \
  --trace /path/to/capture/demo1.bin --output build/renderer-comparison
```

For command submission, prepare two new directories with `--mode submission`
and pass the same mode to `run_pocket_gpu_replay.py`. The simulator requires a
renderer trace argument, which this driver ignores. The runner enables
320×200 scanout traffic at approximately 60 Hz/100 MHz, checks output hashes,
and saves the simulator logs, ELF hashes and cycle results.

Local evidence and prepared binaries for this pass are under
`build/rv32-gpu-20260912/`: `inline-{doom2,sigil1,sigil2,submission}/results.json`,
`setup-inline/`, `setup-final-fma/`, `command-final/`, `command-generic/`,
`smoothness/`, and `artifacts/{pocket,mister}/app.elf`.
