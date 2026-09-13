# Further SIGIL optimizations — 2026-09-12

This pass starts from the build reported to run SIGIL much better: Pocket
ELF `224bfd61288865111cfd3a3e375075ae9d0b1495c01ca067b3fd3f3ece95d1c7`.
The goal is another 10% in complete frames. The implementation below improves
several measured CPU paths, but a full-frame 10% gain remains unverified.

## Implementation

All production changes in this pass are in `src/doom/cdoom/doom/r_gpu.c`.
The SDK header and RTL remain unchanged from the preceding optimization pass.

- Cache the exact nine Q29 coefficients and shift for 32 plane heights per
  view. Separate floors/ceilings at the same signed height reuse the same
  coefficients even when their texture or lighting differs. The full fixed-
  point height is checked; hash collisions recompute rather than approximate.
  `R_GPU_BeginView`, called after `R_SetupFrame`, invalidates the cache, so
  movement, turning, interpolation, viewport changes and level changes cannot
  reuse an old view's coefficients. Height zero remains the existing fallback.
- Initialize invariant wall/plane parameters once per view, including their
  framebuffer base, stride, format, reserved fields, clamps and depth fields.
  Surface setup still writes all changing coefficients, texture dimensions,
  addresses and light state. This removes a full structure clear and repeated
  constant stores from every surface.
- Compute Q29 bounds through independent `fmax.s` chains, giving the RISC-V
  compiler more freedom to overlap FPU latency. The same corner expressions,
  coefficient bounds and zero bound participate; rounding and shifts remain
  unchanged. No approximate division or fast-math mode is introduced.
- Skip the redundant count scan for Doom's private wall, floor and sprite
  record lists. Their producers already accept only positive spans bounded by
  the 320×200 viewport. A small private helper supplies that known bound to
  the existing SDK emitter. Record limits, capability/depth guards, command
  reservation, cache publication and DMA behavior remain in the SDK. Larger
  compile-time viewports retain the generic scanned path.
- Replace each floor span's linear search through eight light bands with a
  64-byte light-to-slot table. The band's light tag validates every lookup.
  Stale table entries therefore need no clearing, and the original round-robin
  eviction, capacity flushes and command order are preserved.

The new tables occupy 1,472 bytes of ordinary SDRAM declarations. No FPGA
RAM blocks, ALMs or DSPs are added. APP_BRAM still ends at `0x77e0`, using
14,304 of 14,336 bytes including alignment. Pocket's linked BSS size remains
unchanged because existing alignment padding absorbs the new tables.

## Measurements

The baseline and candidate are normal, uninstrumented application ELFs.
A separate driver calls their existing functions on the production Pocket
single-issue CPU RTL, caches and SDRAM controller, with active scanout traffic.
The same core configuration and workload are used for both builds.

| Renderer setup capture | Previous build | New build | Fewer CPU cycles |
|---|---:|---:|---:|
| Doom II demo1 | 1,643,651 | 1,539,868 | 6.31% |
| SIGIL 1 compatibility demo1, E3M2 | 5,985,585 | 5,521,369 | 7.76% |
| SIGIL II demo1, E6M1 | 9,053,613 | 8,513,656 | 5.96% |

Each capture includes fourteen selected view sequences from gameplay tics
0–7 and 200–207, with actual bbox requests, wall/plane setup and view state.
All captured bbox results and per-view parameter hashes match.

Two separate fixtures cover the span work omitted by the renderer setup replay:

| CPU fixture | Previous build | New build | Fewer CPU cycles |
|---|---:|---:|---:|
| Wall/floor/sprite end-surface submission | 1,620,528 | 1,195,912 | 26.20% |
| Floor-span generation and submission | 2,084,124 | 1,385,837 | 33.51% |

The submission fixture calls `R_GPU_EndPlaneSpans`, `R_GPU_WallTiersEnd` and
`R_GPU_SpriteEnd`, so the previous build's count scan is included. It covers
81 cohorts: three surface types, repeated/texture-changing/light-changing
headers, and 1–256 records, with sixteen repetitions per cohort.

The producer fixture calls `R_GPU_PlaneSpanLight` and `R_GPU_EndPlaneSpans`.
It covers constant light, eight resident lights and sixteen lights causing
repeated eviction; 1–128 records per surface; sixteen repetitions. Every
cohort improves, including the constant-light case (11.8–23.4%). Emitted
command hashes match in both fixtures.

These fixtures model an always-ready command consumer and keep queued packets
below the DMA kick threshold. They include actual application submission code
but exclude GPU rasterization, DMA contention and audio. The producer fixture
also includes the per-call cycle-counter overhead equally in both builds.
Simulator CPU/SDRAM, scanout and write-confirmation checks report no errors.

**These percentages are separate CPU measurements and cannot be added or
reported as game FPS.** A physical Pocket was not connected, and MiSTer
performance was not measured. Both targets build successfully and use their
existing cores. Actual full-frame gains need a comparison on the target
machine at the same clock, viewport and music settings, particularly the
heavy SIGIL locations reported by the user.

## Correctness

- 300,000 randomized setup comparisons, repeated with host FMA contraction:
  identical full parameter bytes. Cases reuse heights across changing views,
  change textures/lights, collide cache entries, and alternate opaque and
  masked wall wrapping. Address, undefined-behavior and float-cast sanitizers
  pass in both modes.
- 256,000 calls to the production floor-span producer produce identical return
  values, 239,684 accepted records and 117,934 flushes. This exercises stale
  lookup entries across planes, invalid coordinates/lights, empty spans,
  capacity flushes and all 64 light indices under sanitizers.
- 19,496 command comparison cases exercise the real SDK implementation,
  including Doom's validated span path. 8,612 calls emit 3,187,177 words;
  every word matches. The generic packing path also matches, as does a
  comparison against the older released SDK header.
- All 24 existing smoothness regressions pass with sanitizers, covering frame
  pacing, renderer math, status-bar handling and sound-effect behavior.
- Clean Pocket and MiSTer builds pass without warnings. Both ELFs are
  1,287,604 bytes, 80 bytes larger than the previous Pocket build.

The command test needed a correction during this pass: the old extraction
helper selected the last SDK function definition, which was the desktop stub
for public draw functions. Its original host comparison did not validate
emitted payloads. The helper now explicitly selects the hardware definition,
and the test asserts that substantial command data was emitted. The corrected
checks above were rerun against both the previous and older released SDKs.
The preceding pass's separate RISC-V command simulations did run real code.

## Builds and reproduction

Local normal builds:

```text
Pocket: build/sigil-next-20260912/artifacts/pocket/app.elf
SHA-256 d61bb0bc1ab77b9280436f63786c382b11ab75a7f205728fc06fe97d7a64e99f

MiSTer: build/sigil-next-20260912/artifacts/mister/app.elf
SHA-256 6fc83525cec02ddcbd63e2442ea8339d6c2b951fcaf23d3dd080ef355a58bded
```

No version bump, release or device deployment was performed. See
[the preceding pass](rv32-gpu-performance.md) for the standard build/capture
workflow. Use separate object directories to preserve the exact baseline.
Commercial WADs and captured inputs remain local and are not packaged.

```sh
python3 tools/check_gpu_setup.py --reference /path/to/before/r_gpu.c \
  --output build/setup-check
# Repeat with --fused on a host with x86 FMA support.
python3 tools/check_gpu_plane_bands.py --reference /path/to/before/r_gpu.c \
  --output build/plane-band-check
python3 tools/check_gpu_submission.py --doom-spans \
  --reference /path/to/before/of_gpu.h --output build/command-check
# Repeat with --generic for the endian-neutral packing path.
python3 tools/check_smoothness.py --sanitize --output build/smoothness-check
```

For CPU replay, prepare each ELF with `replay_renderer_layout.py` and compare
with `run_pocket_gpu_replay.py`. Select `--mode renderer` for geometry/setup,
`--mode bands` for end-surface submission, or `--mode planes` for floor-span
generation and submission. The two span drivers ignore the required trace
input, which is supplied only to satisfy the simulator's loader interface.
Use these public-function fixtures when the compiler splits or inlines the
private SDK emitter; do not call a compiler-generated clone with an assumed ABI.

Final evidence is under `build/sigil-next-20260912/`: `final-{doom2,sigil1,sigil2,bands}/`,
`planes-comparison/`, `setup-final/`, `setup-final-fma/`, `plane-bands-check/`,
`command-spans-real/`, `command-generic/`, `command-release/` and `smoothness/`.
Each replay result records the exact normal ELF hashes. `starting.patch`,
`starting-status.txt`, reference source files and `current-r_gpu.patch`
preserve the boundary between this pass and earlier uncommitted work.
