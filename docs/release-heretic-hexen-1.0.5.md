# Heretic 1.0.5 and Hexen 1.0.5 for Analogue Pocket

Release candidates — 2026-11-18. Pocket only; the MiSTer packages stay at
1.0.4 until that core is rebuilt with the new mixer.

## Changes

- Louder output. The bundled openfpgaOS 0.9.1 core divides the final audio
  mix by 4 instead of 16 (+12 dB). Sound effects gain the full 12 dB. The
  top of the music slider maps to half the previous MIDI master
  level so dense MIDI passages keep headroom, which leaves MIDI music about
  6 dB louder than before. The MIDI adjustment applies only when the kernel
  reports 0.9.1 or later.
- GPU commands go directly into the GPU's command ring instead of being
  written to SDRAM and read back. The bundled os25 core no longer has the
  old readback path, so each executable and the core must be installed
  together.
- GPU fence tokens start after the value the core retained across a soft
  reset, which prevents an early fence completion after reset.
- Time queries call the kernel's service table instead of trapping.

The bundled os25 core is the one shipped with Doom 1.2.2: it gathers masked
framebuffer writes into SDRAM bursts, uses a dedicated span multiplier and a
radix-4 divider, and reads each video line in shorter bursts. Its CPU
completes shift instructions in one pipeline stage instead of two, which
saves 7.5% of cycles in the core's CPU test programs.

Unlike Doom 1.2.1, levels started from the command line are still created
before graphics initialization in these two games.

## Release checks

| Check | Result |
| --- | --- |
| Shared gameplay, frame pacing and sound regressions (includes Heretic and Hexen sound parameters) | 24 pass with sanitizers |
| GPU initialization fixture run against each game's initializer | pass for both |
| Packaging fixtures, SDK object paths, asset and bank bounds | pass |

These are host checks. Neither game has renderer comparison fixtures like
Doom's. Each game's packaged core, kernel and executable were booted and
played on a Pocket; frame rate was not measured and no long-session soak was
made.

## Install

Extract the ZIP at the SD-card root and merge its folders. Put your WADs in
`Assets/heretic/common/` or `Assets/hexen/common/`. Install the whole ZIP:
the executable, `os25.rbf_r` and `os.bin` belong together.

The bundled os25 core stays at the requested 100 MHz and does not close
main-clock setup timing: seed 28 (best of seeds 13-30) has setup slack
-0.601 ns and worst hold +0.092 ns. This remains an overclocked build.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Pocket Heretic | `4e0070d2a9b57604396982cbea19a37bcb5c298f3265505adcb0633320480bc1` |
| Pocket Hexen | `7bdc3ac92a2f603813fcd7110b95fb20da1fa76a9f10c576783160422979a358` |
| Pocket os25 core | `3b16fbf743706c8aa867e187a89b2aac50309247e1ee67b5f2f338c9fe293cc8` |
| Pocket OS kernel | `103c8c0ac122c8b9a21b1c4f6da3075ed3ec0f1fb20ee30948e0bce65a5d8b57` |
