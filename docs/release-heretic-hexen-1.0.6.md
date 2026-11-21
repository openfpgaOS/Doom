# Heretic 1.0.6 and Hexen 1.0.6 for Analogue Pocket

Release candidates — 2026-11-20. Pocket only; the MiSTer packages stay at
1.0.4.

## Changes

- MIDI music costs less CPU time. The sequencer keeps one song clock and an
  absolute due time per track, and the sample voices recompute volume and
  pitch only when an input changed. In Doom's measurements the cost per
  1 kHz tick falls from 10,719 to 6,646 cycles; it was not measured in these
  two games. The mixer register writes are identical to the previous
  sequencer on the songs tested with Doom.
- The bundled os25 core is the one shipped with Doom 1.2.3: a faster CPU
  front end and a faster GPU memory path. In simulation the CPU test
  programs take 5.9% fewer cycles.

The renderers of both games are unchanged from 1.0.5.

## Release checks

| Check | Result |
| --- | --- |
| Shared gameplay, frame pacing and sound regressions (includes Heretic and Hexen sound parameters) | 24 pass with sanitizers |
| MIDI mixer register writes against the previous sequencer | identical on 27 Doom-family songs |
| Both executables build | pass |

These are host checks. The new executables were built but not run on a
Pocket. If either is less stable than 1.0.5 on your Pocket, reinstall 1.0.5
and report it.

## Install

Extract the ZIP at the SD-card root and merge its folders. Put your WADs in
`Assets/heretic/common/` or `Assets/hexen/common/`. Install the whole ZIP:
the executable, `os25.rbf_r` and `os.bin` belong together.

The bundled os25 core stays at the requested 100 MHz and does not close
main-clock setup timing: seed 22 (best of seeds 13-30) has setup slack
-0.431 ns and worst hold +0.099 ns. This remains an overclocked build.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Pocket Heretic | `dbd87b842f5e25ed35504a7a44bcb91c5c9403cfb00b4dacddc0d99697647a33` |
| Pocket Hexen | `9a6c0bd58c56542dab14d0b668fd6a824d31885f52b4cce12d063fc22ed603e0` |
| Pocket os25 core | `54a39c1df6fec14a63a9786220d731d6d787d427caf5968f027b83467870e358` |
| Pocket OS kernel | `103c8c0ac122c8b9a21b1c4f6da3075ed3ec0f1fb20ee30948e0bce65a5d8b57` |
