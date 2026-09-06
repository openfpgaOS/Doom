# DOOM smoothness review — 6 September 2026

Reviewed baseline: `9777167` (full commit in the candidate manifest). Changes are
implemented in this checkout. The candidate retains the 35 Hz game simulation,
rendering resolution, game rules, and audio polyphony. It does not change the
FPGA clock or logic utilization.

The strongest findings concern frame pacing and intermittent work, rather than
average host rendering speed. These fixes are useful candidates for MiSTer
testing, but **steady 60 FPS on SuperStation One has not been established**.

## Findings and implemented changes

| Priority | Finding | Change |
| --- | --- | --- |
| High | MiSTer's scanout is fixed at 60 Hz, but DOOM permits its Pocket VRR mode and treats FIXED as a 750-line, approximately 42 Hz display in its timing predictor. | Select FIXED for MiSTer and use 525 lines for its period calculation. Preserve Pocket's refresh choices and Analogizer handling. `m_menu.c`, `i_video.c`. |
| High | The vblank waiter consumes an already-arrived vblank before waiting, so a late frame can unnecessarily wait for another refresh. | Return immediately when a newer vblank is already available, retaining the elapsed-vblank count. `i_video.c`. |
| High | A repeated-overrun heuristic inserts a resynchronization wait after four slow frames, adding periodic stalls under sustained load. | Remove that heuristic. Keep the previous-flip wait before submitting another flip, which preserves the existing single-pending-flip constraint. `i_video.c`. |
| High | MIDI voice stealing loses handles to fading hardware voices. Looped voices can remain allocated silently and eventually exhaust the mixer. Separately, dispatch-budget overruns can omit elapsed time from later MIDI tracks. | Import the already-tested SDK fixes into this repository, preserving DOOM's priority setting. Retain generation-aware retired handles until reclamation and advance every track's clock before budgeted dispatch. `of_smp_voice.c`, `of_midi.c`. |
| Medium | Axis-aligned wall setup performs two signed 64-bit software divisions on RV32 even when the numerator's factor cancels the denominator exactly. | Use an exact signed axis shortcut, retaining the original division fallback whenever the cached length differs. Remove negative signed shifts in the result scaling. `r_segs.c`. |
| Medium | Sprite sorting repeatedly calls an indirect `qsort` comparator. | Use stable insertion runs and merges, preserving equal-scale spawn order. Additional scratch storage is 4 KiB in ordinary SDRAM on RV32. `r_things.c`. |
| Medium | Initial thinker precaching misses later monster projectiles, arch-vile fire, pain-elemental skulls, and barrel explosions. A first-use texture build can drain the GPU during gameplay. | Mark these effects for actors present in the level. Existing texture budgets and allocation safeguards still apply. Arbitrary DeHackEd spawn dependencies still use the existing fallback. `r_data.c`. |
| Medium | Sound parameters repeat across interpolated display frames, even when the underlying 35 Hz state has not changed. Each update recomputes gains and calls the mixer. | Cache the requested volume/separation per channel. Always set initial gain for a new voice and validate generation/group ownership before skipping an update. `i_sdlsound.c`. |
| Medium | Reciprocal rounding can make the first sample of a masked sprite post slightly negative. The ordinary column drawer masks that index with 127, reading beyond the post. | Add a bounded fallback for out-of-range posts, covering normal/translated colors and high/low detail. In-range posts keep their existing path. GPU sprite/masked surfaces retain their existing handling. `r_draw.c`, `r_things.c`. |
| Medium | Three configuration bindings store pointers to automatic compound literals that die on return. | Give each binding separate static storage. `i_video.c`. |
| Medium | The second SFX allocation is unchecked; allocation failure dereferences NULL and leaks the PCM buffer. PCM conversion also left-shifts negative signed values. | Handle allocation failure and use defined arithmetic with identical valid PCM output. `i_sdlsound.c`. |
| Medium | Four gamma-table rows are all zero, so using the gamma key can black out the display. Negative saved gamma values also index before the table. | Restore the four stock curves, preserve this port's existing default curve, and handle negative settings. `i_video.c`. |
| Low | The software timedemo path includes display pacing, obscuring CPU measurements. | Exclude artificial vblank/sleep pacing for `singletics`. A GPU timedemo still has normal GPU/flip backpressure; the separate host benchmark explicitly bypasses presentation. `i_video.c`, `tools/benchmark_doom.py`. |

The restored gamma curves come from [id Software's DOOM gamma tables](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/v_video.c).

## Validation and measurements

All 20 focused scenarios pass under AddressSanitizer and UndefinedBehaviorSanitizer:

- Vblank consumption, correct MiSTer period, normal waiting, timedemo pacing,
  repeated overload, persistent configuration bindings, and all gamma levels.
- 5,125 stable-sort cases, covering every count from 0 through 1,024 and random,
  repeated, ordered, reverse-ordered, and equal depths.
- 1,000,000 comparisons against the original wall equations, covering both
  axis directions, non-axis walls, and cached-length mismatches.
- 7,140 masked-post cases with allocations bounded by ASan redzones, including
  negative/overshooting coordinates, translated colors, and both detail modes.
- Map-dependent effect selection and shared DOOM/Heretic/Hexen sound tests for
  allocation failure, PCM output, unchanged gain updates, and recycled voices.

The MIDI scheduler and finite-pool synth lifetime suites also pass with both
sanitizers. The old code reproduces the targeted failures; the new precache and
clamped-column helpers naturally do not exist in the old checkout.

In the deterministic display-clock test, 24 frames with 18 ms preparation each
take **470.701 ms before versus 432.050 ms after**. The longest interval falls
from **27.994 ms to 18.003 ms**, and four forced frame-start waits become zero.
This is a model of pacing overhead, not a measurement of display FPS on a board.

Native host sort microbenchmark, nanoseconds per sort:

| Visible sprites | Original | Updated |
| ---: | ---: | ---: |
| 16 | 141 | 41 |
| 64 | 707 | 202 |
| 256 | 3,529 | 1,030 |
| 1,024 | 16,883 | 5,507 |

These are approximately 3.1–3.5 times faster sorts on this host. They are not a
whole-frame or RISC-V speedup. The demo maps contain 69–90% axis-aligned map segs;
that is map geometry, not a measured fraction of visible wall submissions.

Three DOOM II demos pass comparisons of framebuffer-index hashes and player
position, angle, health, and weapon for **7,674 gameplay tics**. Initial wipe
frames are excluded because their animation uses wall time. The original build
also differs from itself at isolated pixels in DEMO3: the sprite bounds bug
above reads outside `BON1A0`. To make the optimization comparison deterministic,
the reference has **only that isolated bounds fix** applied. Its exact patch is
included in the candidate. The original and updated runs agree on all recorded
player-state fields; the bounds correction is an intentional visual bug fix.

Five alternating, non-profiled host runs per demo give these median total CPU
render times, excluding SDL presentation, display waits, trace hashing, and audio:

| Demo / map | Rendered gameplay tics | Reference | Updated |
| --- | ---: | ---: | ---: |
| DEMO1 / MAP11 | 1,204 | 53.699 ms | 53.397 ms |
| DEMO2 / MAP05 | 2,000 | 79.188 ms | 79.105 ms |
| DEMO3 / MAP26 | 4,470 | 192.790 ms | 193.533 ms |

These differences are small and mixed; they do **not** demonstrate a meaningful
whole-renderer improvement on the host. The host software renderer also does
not exercise the FPGA GPU. Additional precache texture-update bytes in those
maps are respectively 49,256, 20,571, and 42,644. The demos do not exercise lazy
GPU texture creation during rendering, so the first-attack hitch reduction is
supported by the code path and dependency tests, not by a measured demo stall.

The final palette change preserves gamma level 0 exactly and passes its focused
palette tests. The full demo measurements precede that palette-only correction
and the MIDI comment correction; neither changes the measured renderer paths.

## Builds and artifacts

DOOM, Heretic, and Hexen link successfully with `TARGET=mister` using the official
`openfpgaos-firmware` container. Each game has isolated object and SDK-object
directories. DOOM uses **14,320 of 14,336 bytes of APP_BRAM**, leaving 16 bytes.
The renderer scratch array uses ordinary SDRAM. The candidate is an application
ELF update, not a new RBF or an ALM reduction.

`build/review-smoothness/candidate/` contains the three ELFs, source patch,
manifest with SHA-256 hashes and memory sections, the isolated bounds fix, and
test results. No WAD or sound bank is included. The DOOM candidate was installed
on SuperStation One on 6 September 2026, replacing the
previous audio-fix ELF. Its installed SHA-256 is
`0fe3d5fbbcb634ec07e1cbee9f6e6c8c7a84d4fe49e74c730628b80c04d49a1d`.
The RBF, boot ROM, launchers, settings, game VHD and save VHD were preserved.
The previous ELF and a rollback script are stored on the device in
`/media/fat/.openfpgaos-backups/smoothness-20260906T203135Z/`, with a local copy
under `build/review-smoothness/deployment-ss1/20260906T203135Z/`.
Heretic and Hexen remain undeployed candidates. Installation does not establish
hardware frame-rate or audio quality.

Reproduce focused checks from the repository root:

```sh
python3 tools/check_smoothness.py --sanitize
python3 tools/check_midi_scheduler.py
python3 tools/check_smp_voice_lifetime.py
```

For demo comparisons, make a reference checkout at the reviewed commit and
apply `candidate/masked-bounds-fix.patch` there with `patch -p1`. Then run:

```sh
python3 tools/benchmark_doom.py --baseline /path/to/reference \
    --iwad /path/to/doom2.wad --output build/demo-comparison
```

The runner creates isolated host sources and executables, records individual
runs, and checks gameplay traces before reporting timing. It refuses to reuse
its build directories. It does not package the supplied WAD.

## Remaining performance questions

1. **Measure the affected board and map.** Compare presented-frame intervals,
   CPU preparation, GPU-fence time, and missed refreshes using the same WAD,
   sound bank, settings, OS, and RBF. Include sustained combat, opening doors,
   first attacks, menus/wipes, and music on/off. Average FPS alone misses hitches.
2. **Measure MIDI interrupt cost.** The SDK requests 50 Hz, but the current OS
   service delivers callbacks at 1 kHz. The existing 2 ms budget caps one call;
   it does not cap music to 10% CPU. The misleading comment is corrected. Do not
   lower the envelope tick rate: the sound tables assume 1 ms ticks.
3. **Profile CPU/GPU overlap before moving buffer waits.** Draw-buffer acquisition
   occurs after the simulation/interpolation sample. Moving it may reduce stale
   samples but can also serialize useful work or disrupt wipe-buffer ownership.
   The review does not change that ordering without a hardware trace.
4. **Profile dense masked/fuzz scenes and SDRAM contention.** Existing batching,
   per-level texture residency, cache tracking, and GPU fallbacks are retained.
   Larger caches or indiscriminate precaching can consume the zone reserve and
   make complex mods worse.
5. **Treat CPU overclocking as a separate timing-closure task.** The previously
   deployed core did not have clean overall timing in the reviewed reports.
   Raising its clock is not an evidence-backed route to reliable smoothness.

Fixing leaked music voices can reduce mixer traffic and cure dropouts, but it
does not establish that the reported frame drops share the same cause. Hardware
testing is still needed to determine whether the frame budget is limited by CPU,
GPU, memory arbitration, music interrupts, or presentation on the affected map.
