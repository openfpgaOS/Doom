# Doom for openfpgaOS

Chocolate Doom port for the Analogue Pocket using openfpgaOS.

Current release: `1.1.6` (`2026-05-26`).

This repository contains the Doom custom core, the openfpgaOS SDK subset it
builds against, instance files, runtime binaries, and the Doom-specific shims
that replace the SDL platform layer.

## Features

- Chocolate Doom based game code with an openfpgaOS platform layer.
- GPU accelerated 320x200 Doom renderer using native grouped span commands.
- Uncapped interpolated rendering for smoother output between Doom's 35 Hz tics.
- Adaptive Pocket LCD pacing with fixed 60 Hz startup and runtime VRR tuning.
- Direct GPU flip path with rotating framebuffers.
- Hardware mixed Doom SFX through the openfpgaOS mixer.
- OPL/SoundFont MIDI playback through the bundled `bank.ofsf`.
- Analog stick input, button mapping for Pocket controls, and Duke-style
  use/open behavior on `B`.
- Per-instance config and save files.
- Legacy single PocketDoom save migration into per-instance save slots.
- DeHackEd/BEX support for vanilla-compatible WADs.

## Supported Content

The port targets Doom-engine content compatible with Chocolate Doom plus the
safe limit and compatibility fixes already imported into this tree.

Supported:

- Doom shareware
- Registered Doom
- Ultimate Doom
- Doom II
- Final Doom: TNT and Plutonia
- Chex Quest
- Vanilla-compatible PWADs
- Vanilla DeHackEd and many BEX patches
- SIGIL using the compatibility WAD
- SIGIL II when using the Doom-compatible WAD set

Not supported:

- GZDoom or ZDoom-only mods
- PK3-only mods
- DECORATE, ZScript, ACS, SNDINFO, MAPINFO, UDMF, or Hexen-format maps
- Heretic, Hexen, and Strife, which need their own game binaries
- Chex Quest 3, which relies on ZDoom/GZDoom features

## Build

Build the Pocket core from the repository root:

```sh
make build CORE=doom
```

Or work directly from the Doom app directory:

```sh
make -C src/doom
```

Package a release zip:

```sh
make package CORE=doom
```

Copy to an SD card using the SDK copy script:

```sh
make copy CORE=doom
```

Clean the Doom build:

```sh
make -C src/doom clean
```

## Performance Builds

Release builds keep runtime tracing disabled by default. Enable diagnostics only
when measuring a specific issue:

```sh
make -C src/doom PERF=1
make -C src/doom PERFDETAIL=1
make -C src/doom PERFTIMING=1
make -C src/doom TRACES=1
```

`PERF=1` prints a compact render summary. `PERFDETAIL=1` adds BSP, wall, plane,
and scene counters. `PERFTIMING=1` adds heavier microsecond timing probes.
`TRACES=1` enables runtime trace switches such as pacing and fuzz diagnostics.

Do not compare release pacing against a trace-heavy build. UART output and the
extra counters can materially change frame timing.

## Asset Layout

Pocket assets live under:

```text
Assets/doom/ThinkElastic.doom/
Assets/doom/common/
```

All WAD, DEH, ELF, OS, and music-bank files are loaded from
`Assets/doom/common/`. Instance JSON files only describe which common files to
bind to the runtime slots.

Important data slots:

| Slot | Purpose |
|------|---------|
| 1 | `os.bin` |
| 2 | `doom.elf` |
| 3 | IWAD |
| 4 | Optional PWAD |
| 5 | Optional external DEH/BEX patch |
| 7 | `bank.ofsf` MIDI/SoundFont bank |
| 9 | Per-instance config file |
| 10-19 | Per-instance Doom save slots |
| 20 | Legacy PocketDoom save migration slot |

The loader injects `-iwad slot:3 -noautoload`. If slot 4 opens, it adds
`-merge slot:4 -dehlump`. If slot 5 looks like a DeHackEd patch, it adds
`-deh slot:5`.

## Instances

Root instances are stored in:

```text
dist/doom/Assets/doom/ThinkElastic.doom/
```

Current root instances include:

- `Doom.json`
- `Doom2.json`
- `UltimateDoom.json`
- `Shareware.json`
- `TNT.json`
- `Plutonia.json`
- `ChexQuest.json`
- `Earth.json`
- `Revolution.json`
- `REKKR.json`
- `SIGIL.json`
- `SIGILII.json`
- `Batman.json`
- `Hellbound.json`

Mod families with multiple IWAD variants live in subdirectories:

- `Aldente/`
- `BlackMagwell/`
- `D4V/`
- `Gorenuggets/`
- `OHM/`

Each subdirectory contains Doom II, TNT, and Plutonia variants when supported by
that mod.

## Saves And Config

Each instance has its own config file in slot 9 and ten save slots in slots
10-19. Save names are filtered by game id so stale nonvolatile memory from a
different instance is ignored.

Slot 20 is reserved for one-time migration of older PocketDoom-style single save
files into this port's per-instance save layout.

New save names default to the current map name.

## Controls

| Pocket input | Doom action |
|--------------|-------------|
| D-pad | Move/turn/menu navigation |
| A | Enter/menu confirm |
| B hold | Run |
| B tap/release under 500 ms | Use/open |
| X | Next weapon |
| Y | Previous weapon |
| L1 | Strafe left |
| R1 | Strafe right |
| L2 | Use/open |
| R2 | Fire |
| Start | Menu/Escape |
| Select | Automap |
| Left stick | Analog turn and forward/back; full push engages run |
| Right stick | Analog strafe left/right |

The joystick variables are bound through Chocolate Doom's normal config system,
but analog input is enabled by default for Pocket.

## Rendering

The renderer keeps Doom's span generation model and accelerates the pixel work
through openfpgaOS GPU commands.

Current GPU paths:

- `of_gpu_draw_affine_span_group()` for affine horizontal spans, masked posts,
  sprites, fuzz/translucent posts, menus, and UI-like spans.
- `of_gpu_draw_persp_span_group()` for perspective wall columns and clipped wall
  fragments.

Retired scalar/surface GPU APIs are not used. The release path is controlled by
one high-level hardware switch in the Doom renderer, with CPU drawing retained
as the fallback.

The framebuffer is a normal SDRAM framebuffer. The port uses rotating
framebuffers and GPU flip commands. The 320x200 Doom image is centered in the
320x240 Pocket output with border handling for reduced screen sizes.

## Frame Pacing

Doom game simulation still runs at 35 Hz. Rendering is uncapped and interpolates
between tics when it is safe to do so. Interpolation is disabled for cases where
Chocolate Doom expects exact tic output, such as demos or non-gameplay states.

Video startup requests `OF_VIDEO_VTOTAL_60HZ`. The Pocket video shim then uses
openfpgaOS timing feedback to fine tune refresh timing within the supported
Pocket LCD range. The goal is to keep the display cadence close to the measured
render cadence without introducing avoidable queue gaps.

## CPU/GPU Framebuffer Ownership

GPU rendering writes directly to SDRAM. CPU HUD, menu, border, and overlay paths
can still read or write the framebuffer. Those paths use centralized ownership
hooks:

- `R_GPU_PrepareForCPUAccess()`
- `R_GPU_PrepareForCPUAccessRect()`
- dirty-line tracking before flip

Before CPU access, pending GPU work is finished and the affected cache lines are
invalidated. Before presentation, CPU-dirtied lines are flushed so scanout sees
HUD and menu writes.

## Audio

SFX are decoded from Doom DMX sound lumps into PCM and played through the
openfpgaOS hardware mixer via the SDL_mixer shim. MIDI music is played through
the openfpgaOS MIDI/SoundFont path using `bank.ofsf`.

SFX panning follows Doom's positional sound model. The openfpgaOS mixer handles
voice priority and hardware mixing, while the Doom shim keeps channel behavior
compatible with the game code.

## Source Layout

| Path | Purpose |
|------|---------|
| `src/doom/cdoom/` | Chocolate Doom source used by the port |
| `src/doom/cdoom/doom/r_gpu.c` | Doom GPU renderer bridge |
| `src/doom/cdoom/doom/r_perf.*` | Optional render performance counters |
| `src/doom/shim/` | openfpgaOS replacements for SDL/video/input/audio/system glue |
| `src/sdk/` | Vendored openfpgaOS SDK headers and support code |
| `dist/doom/` | Core metadata and instance JSON files |
| `runtime/` | Runtime binaries copied into release packages |

## Troubleshooting

`Unknown or invalid IWAD file` usually means slot 3 is missing or points at the
wrong file. Put the IWAD directly in `Assets/doom/common/` with the exact
filename used by the instance JSON.

`This is not the registered version` means a registered-only PWAD was loaded on
top of the shareware IWAD, or the wrong IWAD was selected.

`Demo is from a different game version` means the WAD contains a demo lump
recorded for another Doom executable version. Start a game manually or use a
compatible WAD build.

DeHackEd warnings about unknown code pointers or high frame numbers generally
mean the patch targets MBF21, DSDHacked, or a ZDoom-family engine. The port will
load what it can, but unsupported code pointers cannot behave correctly.

Missing textures, duplicate sprite warnings, or invalid sidedefs usually mean the
WAD depends on a different IWAD or a more modern engine than this Chocolate Doom
based port.
