# Doom 1.2.1 for Analogue Pocket

Release candidate — 2026-11-17. Pocket only; the MiSTer package stays at
1.2.0 until its core is rebuilt with the new mixer.

## Changes

- Louder output. The bundled openfpgaOS 0.9.1 core divides the final audio
  mix by 4 instead of 16 (+12 dB). Doom adjusts its own levels to match:
  the +50% sound-effect boost is dropped and the top of the music slider
  maps to half the previous MIDI master level, so dense MIDI passages keep
  headroom. Against 1.2.0, sound effects are about 8.5 dB louder, MIDI music
  about 6 dB louder and PCM music 12 dB louder. Both adjustments apply only
  when the kernel reports 0.9.1 or later; on older cores Doom keeps its
  1.2.0 levels.
- GPU commands go directly into the GPU's command ring instead of being
  written to SDRAM and read back. The bundled os25 core no longer has the
  old readback path, so this executable and core must be installed together.
- Levels started from the command line (`-warp`, autostart, network) are
  created after graphics initialization, so their textures are registered
  with the GPU and they no longer fall back to CPU rendering. Skill and
  gameplay flags are preserved, and demo recording starts afterwards.
- GPU fence tokens start after the value the core retained across a soft
  reset, which prevents an early fence completion after reset.
- Time queries call the kernel's service table instead of trapping.

The bundled os25 core also gathers masked framebuffer writes into SDRAM
bursts, uses a dedicated span multiplier and a radix-4 divider, and reads
each video line in shorter bursts so CPU memory accesses wait less.

Rendering output and GPU command bytes are unchanged from 1.2.0.

## Release checks

| Check | Result |
| --- | --- |
| Gameplay, frame pacing and sound regressions | 24 pass with sanitizers |
| GPU initialization (DMA fallback, direct ring, rejected selection, re-init) | pass |
| GPU wall/plane setup against 1.2.0 | identical parameters |
| GPU command submission, including Doom's span path, against 1.2.0 | identical command streams |
| Wall columns, masked columns/ranges, plane bands against 1.2.0 | identical records |
| Wall loop | 12,000 cases, 39,228,820 identical bytes |
| MIDI scheduler and PCM recovery | pass |
| Packaging fixtures and SDK object paths | pass |

These are host checks. Frame rate on a Pocket has not been measured, and the
new levels have not been measured for clipping; the balance between effects
and music was chosen from the mixer arithmetic and should be judged by ear.

## Install

Extract the ZIP at the SD-card root and merge its folders. Put your WADs in
`Assets/doom/common/`. Install the whole ZIP: `doom.elf`, `os25.rbf_r` and
`os.bin` belong together and should not be mixed with files from 1.2.0.

The bundled os25 core stays at the requested 100 MHz and does not close
main-clock setup timing. Seed 25 (best of seeds 13-30) estimates 96.16 MHz at
the worst corner, with setup slack -0.399 ns and worst hold +0.102 ns;
15,525 ALMs and 296 M10Ks. This remains an overclocked build.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Pocket Doom | `1b6eb4bdba487cfe4d60a375e490447fb5a2366405291ddae842b114f0ac29e2` |
| Pocket os25 core | `57bb11f0037e753dccf93c09b23fc137378dac86837506e8a5e0b1e3b70b86b0` |
| Pocket OS kernel | `103c8c0ac122c8b9a21b1c4f6da3075ed3ec0f1fb20ee30948e0bce65a5d8b57` |
