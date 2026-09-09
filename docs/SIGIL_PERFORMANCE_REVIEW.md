# SIGIL performance follow-up

Baseline: DOOM MiSTer v1.1.23 (`1ae9a09`). This follow-up targets the CPU work
required for detailed maps. It preserves game rules, the 35 Hz simulation,
resolution, music voices, and the existing renderer's rounding. No FPGA logic
or clock changes are involved. The test build is not a published release.

## Findings

The local launcher WADs include four recorded demos each, so both SIGIL editions
can be compared using their actual maps and playback inputs. SIGIL II's E6M1
has 4,048 BSP nodes and 9,437 segs; Ultimate DOOM's E1M1 has 238 and 747. Static
map size alone does not determine frame time, but the recorded workloads also
show substantial geometry work:

| Recorded workload | Wall-distance calls | Axis shortcut calls | General wall math |
| --- | ---: | ---: | ---: |
| SIGIL II DEMO1 / E6M1 | 478,489 | 61,003 | 87.3% |
| SIGIL compatibility DEMO3 / E3M7 | 170,518 | 72,532 | 57.5% |

The previous release's axis shortcut helps comparatively little in SIGIL II's
opening demo. Its general wall setup performs two signed 64-bit divisions per
call. In the compiled RV32 library, the sampled calls execute eight native
division instructions in total to calculate those two results.

The existing bounding-box angle cache is already close to its best possible
hit rate on these traces. Of 2,671,936 angle requests in the SIGIL II sample,
1,582,443 are first requests for a coordinate in that view. Only 5,369 extra
misses remain after accounting for compulsory misses. A tested cache of the
last wall result reused just 665 of 478,489 calculations. Neither experiment
justified adding runtime work or memory, and neither is in the candidate.

## Implemented change

`r_bsp.c` computes a reciprocal of each nonzero half wall length when building
the level's render data. `r_segs.c` uses that reciprocal for exact distance and
texture-offset division; the existing axis shortcuts remain.

For divisor `d > 1`, the stored reciprocal is `floor(2^64 / d)`. Taking the high
64 bits of the numerator magnitude times this reciprocal gives either the
correct quotient or one less. A remainder comparison corrects the latter.
Sign restoration preserves C's truncation toward zero. Divisor one is handled
directly. This is exact integer arithmetic, without an approximate texture or
distance result.

Halved coordinate differences are formed with 32-bit arithmetic before widening
the products. Boundary fixes also avoid signed overflow while loading a wall
whose endpoints span the coordinate range, summing the two largest dot-product
terms, and scaling the outputs. The distance remains saturated and the texture
offset retains its low 32 bits.

The reciprocal increases the RV32 render-seg structure from 44 to 56 bytes:
12 extra bytes per map seg including alignment. The largest seg count among
these SIGIL WADs adds 136,224 bytes, approximately 133 KiB of level memory. That
reduces the memory available to texture caches by the same amount. The fast
memory allocation remains 14,320 of 14,336 bytes, leaving 16 bytes free.

## Validation and measured scope

The address/undefined-behavior sanitizer suite passes all 21 scenarios,
including:

- 1,000,000 randomized wall distance/offset comparisons and 4,116 boundary
  cases against independent 128-bit equations.
- 2,000,000 exact division, high-product, and half-difference comparisons, plus
  divisor, sign, power-of-two, and coordinate boundary cases.
- The existing pacing, sprite sorting, masked-post bounds, precache, gamma,
  configuration lifetime, and shared audio regressions.

Unicorn 2.1.4 executes the actual production RV32 wall function and its library
helpers from the baseline and candidate ELFs. Inputs are sampled every 128th
wall calculation in the recorded demos. Both ELFs match all recorded distance
and offset results:

| Workload | Samples | Baseline instructions | Candidate instructions | Reduction | Native divides before → after |
| --- | ---: | ---: | ---: | ---: | ---: |
| SIGIL II E6M1 | 3,739 | 879,782 | 619,182 | 29.6% | 26,104 → 0 |
| SIGIL E3M7 | 1,333 | 252,655 | 175,578 | 30.5% | 6,224 → 0 |

These are **executed instructions in wall setup**, not measured clock cycles,
complete-frame speedups, or FPGA FPS. The new routine uses more multiplies while
eliminating the divisions. The emulator does not model CPU caches, instruction
latencies, memory contention, music interrupts, or GPU execution.

The host comparison harness now accepts launcher-order PWAD merges and reports
BSP, planes, and masked-rendering stage times. It excludes display waiting,
presentation, and audio. The final result files contain seven alternating runs
per demo and separate framebuffer/player-state traces:

- `build/review-sigil/final-sigil1/results.json`: SIGIL compatibility DEMO1–4.
- `build/review-sigil/final-sigil2/results.json`: SIGIL II DEMO1–4.
- `build/review-sigil/final-doom2/results.json`: stock DOOM II DEMO1–3.

All 11 demos match across **24,050 gameplay tics**, including framebuffer-index
hashes, position, angle, health, and weapon. Initial wall-time-dependent wipe
frames are excluded. Median total CPU render time per complete demo:

| Game / demo | Baseline | Candidate |
| --- | ---: | ---: |
| SIGIL I / 1 | 174.469 ms | 167.011 ms |
| SIGIL I / 2 | 140.733 ms | 131.555 ms |
| SIGIL I / 3 | 121.177 ms | 115.903 ms |
| SIGIL I / 4 | 162.025 ms | 163.071 ms |
| SIGIL II / 1 | 232.673 ms | 243.359 ms |
| SIGIL II / 2 | 162.950 ms | 166.451 ms |
| SIGIL II / 3 | 173.727 ms | 174.317 ms |
| SIGIL II / 4 | 320.681 ms | 320.059 ms |
| DOOM II / 1 | 54.831 ms | 55.696 ms |
| DOOM II / 2 | 81.815 ms | 84.302 ms |
| DOOM II / 3 | 197.881 ms | 199.996 ms |

The host results are mixed, including regressions, and do not establish an
overall speedup. This change targets RV32's multi-instruction 64-bit division;
the host has native 64-bit division and a different memory/cache system.
MiSTer and Pocket ELF builds use the firmware container and retain the same
fast-memory budget. The candidate and source/checksum manifest are staged under
`build/review-sigil/candidate/`.

## Reproduction

Run the focused checks with:

```sh
python3 tools/check_smoothness.py --sanitize --output build/sigil-checks
```

For a fresh host comparison, provide a v1.1.23 source checkout/archive and local
WAD paths. No WADs are included in the candidate or source changes:

```sh
python3 tools/benchmark_doom.py --baseline /path/to/v1.1.23 \
  --iwad /path/to/doomu.wad --merge /path/to/SIGIL_II_V1_0.WAD \
  --demos demo1 demo2 demo3 demo4 --runs 7 --output build/sigil2-compare
```

`tools/check_wall_riscv.py` replays nine-word wall samples through the release
ELFs with `pyelftools` and `unicorn`; its help describes the sample format and
compiler calling-convention assumption. Exact artifact hashes, compiler build
logs, local sample files, and the emulator report are retained under
`build/review-sigil/`.

## Next measurement

Measure frame intervals on SS1 during the user's slow scenes, with music
enabled. A steady 60 FPS result has not been established. The existing MiSTer
UART connection is stubbed, so useful on-device profiling needs a file or
on-screen report rather than ordinary serial output.

The next CPU candidates are wall scale division and BSP angle calculations:
SIGIL II's opening demo makes 873,004 scale calls and 2,365,508 point-to-angle
calls. GPU wait time and masked rendering also need board measurements before
choosing the next change. A CPU clock increase is not justified while the
current core build still has negative setup slack.

## Heavy-scene follow-up: exact wall scales (2026-09-08)

This comparison starts with the working tree that already contains the cached
wall reciprocals and status-bar fixes above, plus the current SDK updates. It
isolates one further production change in `r_main.c`: replace wall scaling's
16-step fractional division loop with integer reciprocal refinement and an
exact remainder correction. Gameplay, draw ordering, visibility and precision
are preserved. No shared shim, SDK or RTL code changes in this pass.

The reciprocal table occupies 128 bytes. The helper stays in cached SDRAM;
`R_ScaleFromGlobalAngle` remains in application BRAM. Normalization is local to
avoid a library call on RV32IM. Both official container builds produce the same
MiSTer/Pocket ELF. Application BRAM falls from 14,320 to 14,304 bytes out of
14,336; SDRAM text and read-only data grow by 368 bytes. This software change
makes no claim about FPGA ALM savings.

### Why the result remains exact

The input domain is an unsigned numerator and `0 < den <= INT32_MAX`, as in the
positive wall-scale path. The initial guard detects `num >= 64*den` without
multiplication overflow. Otherwise the unclamped 16.16 quotient is below `2^22`.

Normalize `D = den << s` into `[2^31, 2^32)` and let `X = 2^61 / D` be the exact
real reciprocal. The 32 table entries are `floor(2^61 / C)`, where `C` is the
center of each equal-width bin. The seed's relative error is less than
`1/65 + 2^-29 < 1/64`.

Each integer Newton step computes:

```
h = floor(D*r / 2^32)
r = floor(r * (2^30 - h) / 2^29)
```

Writing the two truncation fractions as `e` and `f`, both in `[0,1)`, the error
after a step is exactly `-(r-X)^2/X + e*r/2^29 - f`. Thus the first step is less
than two units above `X`, and its absolute error is below `X/4096 + 1`. The
second step is less than three units above `X`. Subtracting four therefore
makes the final reciprocal a strict underestimate. Its relative shortfall is
less than `2^-24 + 5.001/2^29 < 2^-23`.

Because the real quotient is below `2^22`, its estimated value is less than
half a quotient unit low before truncation. The integer estimate is therefore
either the exact truncated quotient or one less. The residual is in
`[0, 2*den)`, which fits in 32 bits; modular multiplication/subtraction recovers
it exactly, and a single comparison supplies the correction.

The variable product shift uses a high 32-bit multiply. When `s > 13`, the
saturation guard guarantees `num << (s-13) < 2^25`, so shifting the numerator
first loses no bits. For smaller shifts, shifting the high product is exactly
equivalent to the original wide shift. The caller's minimum scale of 256 and
maximum scale of `64*FRACUNIT` are unchanged.

### Validation and artifacts

Artifacts for this pass are in `build/review-sigil-spikes-20260908/`. The initial
working-tree patch and source hashes retain the exact comparison baseline.
`tools/check_smoothness.py --sanitize` passes all 23 cases against both trees,
including 75,422,625 scale comparisons with independent 64-bit division.
The scale test covers small inputs, random inputs, every seed-bin boundary at
every normalization, integer/fractional rounding, and saturation boundaries.

`tools/benchmark_doom.py` now captures one wall-scale input/result in every 256
calls during correctness traces, with capture disabled during timing runs.
`tools/check_scale_riscv.py` replays these seven-word samples through the actual
linked production ELFs. It checks the complete `R_ScaleFromGlobalAngle` result
and counts all executed instructions, including calls. These are instruction
counts, not FPGA cycles: cache misses, interrupt work, GPU contention and
presentation are excluded.

The opening fixtures use vanilla v1.9 demos: header
`[109, 2, episode, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0]`, followed by 35 idle tics,
128 tics turning by four angle bytes, 35 idle tics and the `0x80` end marker.
Each tic is four bytes (`forward, strafe, turn, buttons`). Monsters remain
enabled. These inspect the spawn view and two rotations without movement
commands; ordinary monster damage and knockback remain active. Both players
remain alive through these short fixtures. SIGIL compatibility uses episode 3; SIGIL II uses episode 6.

The 11 built-in SIGIL I, SIGIL II and DOOM II demos plus the two short
opening fixtures match across **24,444 gameplay tics**, including framebuffer
hashes, position, angle, health and weapon. The longer exploratory opening
fixtures are retained separately but excluded from this total.

Production RV32 replay matches all **20,860** sampled results. Total scale
instructions fall from **2,942,264 to 2,068,358 (29.70%)**; executed divide/remainder
instructions fall from **41,714 to zero**. Six rare saturated calls execute more
instructions because of the helper call; the overall saving includes these.

| Scene | Samples | Before instructions | After instructions | Reduction |
| --- | ---: | ---: | ---: | ---: |
| SIGIL I opening (compatibility E3M1) | 117 | 16,574 | 11,590 | 30.07% |
| SIGIL II opening (E6M1) | 253 | 35,920 | 25,052 | 30.26% |
| SIGIL II built-in opening demo | 3,411 | 481,316 | 338,725 | 29.63% |

Five alternating host timing runs per demo show median total render time
reductions of 0.69–4.18% across the 11 built-in demos. The short opening tests
fall from 15.215 to 14.620 ms total (SIGIL I) and 19.927 to 19.291 ms total
(SIGIL II). These are complete host-demo render totals, not frame times on
MiSTer. Tail improvements are not universal: DOOM II DEMO1 host p99 rises
from 83.96 to 88.39 microseconds. The host result cannot establish steady
60 FPS or resolution of the reported frame drops.

Build/check/replay results:

- `validation/results.json`, `baseline-validation/results.json`: sanitized checks.
- `final-sigil1/results.json`, `final-sigil2/results.json`,
  `final-doom2/results.json`, `final-openings/results.json`: demo comparisons.
- `rv32-final.json`: sampled production instruction counts and ELF hashes.
- `source-manifest.json`: baseline/current engine and SDK source hashes.
- `final-mister/obj/app.elf`, `final-pocket/obj/app.elf`: official container builds.

Candidate ELF SHA-256:
`26a29231ab66cfb9f89cb2294e677b5aba608cc5c2f1140e9179431c679f1b29`.
Baseline ELF SHA-256:
`85e7a159a371ed8e3d57af525b0add6f98db7760e39d76ef181e898b52ce5b1d`.

The next measurement remains frame intervals on the affected board at the
episode start, with music enabled and the same core/firmware for both ELFs.
This pass does not establish the share of the remaining spikes caused by GPU
waits, BSP traversal, audio service or memory stalls. The local candidate has
not been deployed or published.

## Reuse view geometry while turning (2026-09-08)

This pass starts from the wall-scale candidate above (ELF SHA-256
`26a29231ab66cfb9f89cb2294e677b5aba608cc5c2f1140e9179431c679f1b29`).
The only further production change is in `r_bsp.c`.

Previously, vertex angles, vertex distances and bounding-box corner angles
used the global gameplay/render `validcount`. A new rendered frame discarded
all those cached results even if only the view direction or height changed.
These values depend on X/Y position and fixed level geometry, so their cache
generation now advances only when X/Y changes or BSP level data is rebuilt.
It uses the full fixed-point coordinates, including interpolated fractions.
View direction, height, projection and field of view are not inputs to these
absolute angle/distance calculations. Sector-plane and gameplay validity
counters keep their existing behavior.

Level setup invalidates the saved camera position, including zero-node levels.
Save loading calls level setup before restoring world state. Generation
rollover clears bbox and vertex stamps before reusing generation 1. The
regression test exercises turning/bobbing, unrelated gameplay counter changes,
one-unit movement, movement back to the original position, level invalidation,
rollover with deliberately stale entries and hash collisions.

This targets standing, aiming and turning. If X/Y changes every frame, the
caches still refresh every frame. It does not reduce geometry, change drawing
order, change game simulation or approximate angles.

### Production RISC-V measurements

`tools/trace_view_cache.py` instruments isolated copies of the baseline source
to capture every frame header and bbox request during tics 0–63 and 200–215.
Those instrumented builds are used only to collect inputs. The replay tool
`tools/check_view_cache_riscv.py` runs the actual uninstrumented release ELFs.
Each window starts with cold caches in both machines; counts include
`R_ClearClipSegs` once per frame and every `R_BBoxPointAngle` call/callee.
Both machines use the common steady-state wall-merge initialization path.
No vertex-cache benefit or other renderer work is included in these counts.

All **265,122** point results match across **280** captured frames.

| Trace window | Points | Angle calculations before → after | Instructions before → after | Instruction reduction |
| --- | ---: | ---: | ---: | ---: |
| SIGIL1 built-in DEMO1 | 97,806 | 52,964 → 44,630 | 4,413,154 → 4,185,192 | 5.17% |
| SIGIL1 opening | 25,656 | 17,390 → 3,175 | 1,264,251 → 866,216 | 31.48% |
| SIGIL2 built-in DEMO1 | 84,600 | 50,334 → 45,967 | 3,994,447 → 3,871,852 | 3.07% |
| SIGIL2 opening | 57,060 | 33,079 → 7,081 | 2,661,567 → 1,928,040 | 27.56% |

The opening windows avoid approximately 79–82% of the bbox angle divisions.
The sampled built-in gameplay windows save considerably less. These are
executed instruction counts, not FPGA cycles or an overall FPS percentage;
cache misses, memory arbitration, GPU work, audio and presentation are excluded.

### Validation, footprint and limits

All **24** focused regression cases pass with ASan/UBSan. The 11 built-in
SIGIL I, SIGIL II and DOOM II demos plus the two 198-tic opening fixtures match
across **24,444 gameplay tics**, including framebuffer-index hashes and player
position, angle, health and weapon. The opening fixtures retain monsters and
both players remain alive. Initial wall-time-dependent wipe frames are excluded.

Host timing uses seven alternating pairs for the openings, five for SIGIL I
and DOOM II, and three for SIGIL II. Results are mixed: SIGIL I opening median
render time is 15.068 → 15.042 ms total; SIGIL II is 19.134 → 18.361 ms total.
Opening host p99 worsens in these runs (144.96 → 171.22 µs and
199.72 → 211.15 µs respectively). Built-in median changes range from a
3.36% improvement to a 1.87% regression. These measurements do not establish
a universal frame-time improvement or steady 60 FPS.

Official MiSTer and Pocket builds produce identical candidate ELFs:
`58c53164e7e505de507af88361039715822d76c9de3e7ab1c4a615991a272372`.
Application BRAM remains **14,304 / 14,336 bytes**. Linked SDRAM text/rodata
grows by 496 bytes and data by 16 bytes; the existing bbox cache does not grow.
There is no RTL change or ALM saving in this pass.

Artifacts are under `build/review-view-cache-20260908/`:

- `baseline/`, `starting-changes.patch`, `source-manifest.json`: exact baseline and source hashes.
- `checks-fixed/results.json`: sanitized regressions.
- `sigil1/`, `sigil2/`, `doom2/`, `openings/`: host comparisons and correctness traces.
- `trace-sigil1/`, `trace-sigil2/`, `rv32-sigil1.json`, `rv32-sigil2.json`: recorded bbox requests and native replay.
- `elf-before/obj/app.elf`, `elf-after/obj/app.elf`, `elf-pocket/obj/app.elf`: official container builds.
- `doom-view-cache-test-20260908.zip`: candidate and baseline ELFs for an isolated board comparison.

The build has not been deployed or published. Compare the same heavy view on
hardware, with music enabled, first while turning in place and then while
moving. Further optimization of movement-related drops needs board frame
intervals and CPU/GPU wait measurements; this change does not establish which
of those costs dominates the remaining stalls.
