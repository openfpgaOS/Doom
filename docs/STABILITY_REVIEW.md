# DOOM stability review — 2026-09-09

Implemented a stability pass across DOOM, its shared SDK, firmware and audio
RTL. Existing rendering optimizations remain. These changes are included in
source version 1.1.24. The test candidate below predates that version bump.

## Fixed in the engine and SDK

- Validate WAD headers, directory reads/counts and nonempty lump extents before
  allocation or use. Empty markers still accept arbitrary unused offsets.
- Reject negative or overflowing zone allocation sizes before alignment and
  block-header arithmetic. Handle shutdown callback allocation failure.
- Validate SoundFont metadata, presets, sample ranges, alignment, loop bounds
  and the hardware sample-length limit before copying/binding. The canonical
  validator also runs in the core's kernel before sample filtering.
- Bound MIDI header/chunk arithmetic and require the supported declared tracks
  to exist; unknown chunks no longer consume a track count. Valid music retains
  the existing scheduler and voice behavior.
- Keep SDK object files inside each game's private build directory. Previously
  `../sdk` source paths escaped to a shared `.obj/sdk` directory, allowing
  different game builds to reuse or overwrite each other's objects.

## Fixed in the core

The HAL now stops a voice before clearing its old end IRQ or changing sample
parameters. The audio RTL cancels an in-progress pass for the replaced voice,
draining already-issued AXI reads before discarding old work. This prevents an
old note's delayed completion from prematurely retiring the replacement voice.
All three AXI response states require an actual valid/ready handshake.

Pocket now displays CPU cause, PC, fault address, SP and RA directly from BRAM
when an unrecoverable trap occurs. The compact display fits both targets and
does not depend on the allocator or mutable terminal state. The shared timer
also calculates fractional seconds correctly at both 90 and 100 MHz.

## Evidence

The new stop/rearm tests fail the turn-start audio RTL in seven checks; the
fixed Pocket and MiSTer configurations pass all 176 checks each, including 288
stop/rearm interleavings per configuration. Mixer HAL tests pass 11 cases under
ASan/UBSan. The SoundFont binder passes 15 cases, including the actual bank;
11 invalid cases fail the previous code. WAD/zone tests pass 16 cases, of which
13 fail the previous code. MIDI parser/scheduler and timer regressions pass.

All 24 existing smoothness tests pass under ASan/UBSan. Fourteen final-build
DOOM, DOOM II, SIGIL I and SIGIL II demos match 31,967 gameplay tics and
framebuffer hashes against the source at the start of this review. These host
demos exclude audio and presentation waits; they are not FPGA FPS or listening
tests. AXI peripheral, eight GPU acceptance configurations and four SDRAM
configurations also pass. Official RISC-V builds of DOOM, Heretic and Hexen pass
after the shared SDK changes, with all objects isolated per game.

```sh
python3 tools/check_asset_bounds.py
python3 tools/check_bank_bounds.py
python3 tools/check_sdk_object_paths.py
python3 tools/check_midi_scheduler.py
python3 tools/check_smoothness.py --sanitize
```

Results and before/after logs are under `build/review-stability-20260909/`.
`demos/final-results.json` contains the final demo comparisons. The complete
core findings, timing and reproduction details are in the sibling core
repository's `docs/DOOM_STABILITY_REVIEW.md`.

## Pocket test candidate and remaining limitations

The matched local archive is
`build/review-stability-20260909/doom-pocket-stability-90mhz-20260909.zip`.
It contains the core, OS, DOOM ELF, loader, bank, manifests and checksums, with
installation instructions. It has no WADs or save files. Install its APF
directories together; the new crash display needs the matching core and OS.

This Pocket candidate uses the existing 90 MHz clock option, leaving the
default source variant unchanged. Worst constrained setup is +0.694 ns and hold
is +0.015 ns across all four timing corners; recovery/removal/pulse width also
pass. It uses 15,168 ALMs and lowers available CPU cycles by 10%. The standard
100 MHz seed-35 build fails CPU setup at -0.665 ns; the new MiSTer fit fails at
-0.399 ns. Existing unconstrained board pins still limit the timing guarantee.
The original 20% ALM reduction goal is not met.

Two further Pocket placements at 100 MHz also fail setup (seed 36: -1.269 ns;
seed 37: -1.291 ns). The 90 MHz candidate is the only fit from this pass with
nonnegative constrained timing checks. Further CPU timing work is still needed
to retain the original clock with adequate margin.

The source fixes and deterministic tests do not prove the cause of issue #15,
nor that all music dropouts or heavy-scene frame drops are resolved. Sustained
audio memory contention can still exceed the mixer budget. No hardware boot,
listening session, deployment or public release was performed in this pass.
