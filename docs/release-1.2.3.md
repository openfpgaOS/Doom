# Doom 1.2.3 for Analogue Pocket

Release candidate — 2026-11-20. Pocket only; the MiSTer package stays at
1.2.0.

## Changes

- Sigil and Sigil II rooms with many see-through textures are faster.
  Masked middle textures that the 2D texture path declines are now drawn by
  the GPU from a per-texture column block with the CPU's own start value and
  step, instead of being composited column by column on the CPU. In the
  simulated os25 system the worst measured E6M2 view drops from 2.39 M to
  0.84 M cycles per frame.
- MIDI music costs less CPU time. The sequencer keeps one song clock and an
  absolute due time per track, and the sample voices recompute volume and
  pitch only when an input changed. Per 1 kHz tick the cost falls from
  10,719 to 6,646 cycles, which is 10.7% to 6.6% of a frame in the measured
  Sigil views. The mixer register writes are identical to 1.2.2.
- Sprites are clipped against fewer wall segments, weapons and ammunition
  dropped by monsters are registered as GPU sprite textures at level start,
  and the masked pass keeps a compact list of ranges.
- Fixed: with a reduced view size, CPU-drawn columns were synchronised with
  the GPU against a rectangle offset from the view window, so the CPU could
  write rows the GPU was still drawing.
- The first frames of a level are cheaper: the CPU-touched part of the
  framebuffer is tracked per cache line instead of flushing whole rows.
- The bundled os25 core has a faster CPU front end (branch predictor that
  no longer halts fetch on updates, restored return-stack pointers, a
  fetch-stage buffer) and a faster GPU memory path (pipelined framebuffer
  writes, longer write bursts, a separate colormap store, 64-byte texture
  lines). In simulation the CPU test programs take 5.9% fewer cycles and
  captured Doom II frames take 35% fewer GPU cycles. Doom is limited by the
  CPU in the measured scenes, so the GPU change alone is not visible.

Rendering output is unchanged from 1.2.2. The heaviest Sigil II rooms are
still well over a 35 fps frame: their cost is wall and BSP work on the CPU,
which this release does not change.

## Release checks

| Check | Result |
| --- | --- |
| Gameplay, frame pacing and sound regressions | 24 pass with sanitizers |
| GPU initialization | pass |
| GPU wall/plane setup, wall columns, plane bands against 1.2.2 | identical |
| Masked columns and ranges against 1.2.2 | identical records |
| GPU masked-texture columns against the CPU path | 400,000 columns identical |
| MIDI mixer register writes against 1.2.2 | identical on 27 songs, 60 s each, with and without forced budget overruns |
| Simulated os25 frames against the previous renderer | 288 flipped frames identical |

Cycle counts come from a simulated os25 system with a stand-in kernel, not
from a Pocket. The packaged core, kernel and executable were booted and
played on a Pocket; frame rate was not measured and no long-session soak
was made. If this version is less stable than 1.2.2 on your Pocket,
reinstall 1.2.2 and report it.

## Install

Extract the ZIP at the SD-card root and merge its folders. Put your WADs in
`Assets/doom/common/`. Install the whole ZIP: `doom.elf`, `os25.rbf_r` and
`os.bin` belong together.

The bundled os25 core stays at the requested 100 MHz and does not close
main-clock setup timing. Seed 22 (best of seeds 13-30) has setup slack
-0.431 ns and worst hold +0.099 ns; 15,782 ALMs and 300 of 308 M10K blocks.
This remains an overclocked build.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Pocket Doom | `8c45b965e014483db8157f72546737289b1e19275b0b8d9d648ba240e25cafc7` |
| Pocket os25 core | `54a39c1df6fec14a63a9786220d731d6d787d427caf5968f027b83467870e358` |
| Pocket OS kernel | `103c8c0ac122c8b9a21b1c4f6da3075ed3ec0f1fb20ee30948e0bce65a5d8b57` |
