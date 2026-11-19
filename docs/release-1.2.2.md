# Doom 1.2.2 for Analogue Pocket

Release candidate — 2026-11-18. Pocket only; the MiSTer package stays at
1.2.0.

## Changes

- Faster CPU in the bundled os25 core: shift instructions complete in one
  pipeline stage instead of two. In the core's CPU test programs this saves
  7.5% of cycles overall and 8.5% to 10.6% in the span and blit kernels.
  Frame rate on a Pocket has not been measured.

`doom.elf` and `os.bin` are the same files as in 1.2.1. Only `os25.rbf_r`
and the version in `core.json` differ.

## Release checks

The new CPU passes the core's CPU stress program in simulation, and its
benchmark results match the previous CPU's bit for bit. The interrupt
simulation was not run; interrupts were exercised only on hardware.

The packaged core, kernel and executable were booted and played on a Pocket.
No long-session soak was made. If this version is less stable than 1.2.1 on
your Pocket, reinstall 1.2.1 and report it.

## Install

Extract the ZIP at the SD-card root and merge its folders. Put your WADs in
`Assets/doom/common/`. Install the whole ZIP: `doom.elf`, `os25.rbf_r` and
`os.bin` belong together and should not be mixed with files from 1.2.0.

The bundled os25 core stays at the requested 100 MHz and does not close
main-clock setup timing. Seed 28 (best of seeds 13-30) has setup slack
-0.601 ns and worst hold +0.092 ns; 15,513 ALMs. Version 1.2.1 shipped a
core with -0.399 ns and +0.102 ns, so this release trades 0.20 ns of setup
margin for the faster shifts. This remains an overclocked build.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Pocket Doom | `1b6eb4bdba487cfe4d60a375e490447fb5a2366405291ddae842b114f0ac29e2` |
| Pocket os25 core | `3b16fbf743706c8aa867e187a89b2aac50309247e1ee67b5f2f338c9fe293cc8` |
| Pocket OS kernel | `103c8c0ac122c8b9a21b1c4f6da3075ed3ec0f1fb20ee30948e0bce65a5d8b57` |
