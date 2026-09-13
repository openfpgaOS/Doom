# Doom 1.2.0

This release reduces CPU renderer preparation and MIDI interrupt work without
changing gameplay, rendering precision, GPU commands or save compatibility.

- Reuse exact per-view wall geometry and floor/ceiling coefficients.
- Batch clipped opaque wall columns and keep their compact producer in fast RAM.
- Reduce visplane clearing, repeated GPU header copies and span-count scans.
- Defer texture stepping until a span needs the software fallback.
- Cache unchanged MIDI pitch and volume calculations while retaining every
  1 kHz envelope/LFO step and the original mixer writes.
- Retain the Pocket renderer object order and package the Pocket build as one
  ZIP containing the standard `Cores/`, `Assets/` and `Platforms/` directories.

Both normal executables rebuild byte for byte from the previously measured
optimized renderer. Diagnostic recording and forced clock-switch hooks are
absent from the release binaries. Application fast RAM uses 14,152 of 14,336
bytes. Heavy SIGIL scenes can still exceed the 60 FPS frame budget.

## Release checks

| Check | Result |
| --- | --- |
| Gameplay, renderer math, frame pacing and sound regressions | 24 pass with sanitizers |
| GPU wall/plane setup | 300,000 exact parameter comparisons pass |
| Batched wall producer | 1,746,437 columns, 89,975,483 pixels and 41,746 flushes match |
| MIDI envelope/pitch optimization | 711,157 mixer/state records match over 49,500 ticks |
| Packaging and release upload fixtures | All seven pass |
| MiSTer package | ZIP integrity, version, ELF identity, unique flat assets and Downloader hashes/URLs pass |
| Pocket package | ZIP integrity, version, standard root folders, runtime pairing and ELF identity pass |

Game WADs are supplied by the user and are not included in either archive.
The MiSTer boot template is assembled from the SoundFont and launch/save
metadata; the engine ships separately as `doom.elf`.

## Install

For MiSTer, install the companion openfpgaOS core 0.9.5 and extract the Doom
archive at the SD-card root. Run `Scripts/openfpgaos-doom-setup.sh` after
updating. Existing saves remain in their separate save image; setup only
creates that image when it is absent.

For Pocket, extract the single Doom ZIP at the SD-card root and merge its
folders. Put your WADs in `Assets/doom/common/`. The bundled os25 core stays
at the requested 100 MHz. A fresh fit reproduces its existing image exactly;
two slow-corner setup checks fail, with worst slack −0.645 ns. The other
18 timing checks pass. This remains an overclocked build, and physical Pocket
FPS and long-duration stability have not been measured. The same timing
notice is included in `INSTALL.txt` inside the ZIP.

## Executable and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| MiSTer Doom | `81bbaddca8678a32fec52747cffc8ce33d634692a0b07c1f46425e220c9e9ffe` |
| Pocket Doom | `9e1ce779997dc28e3bd6d09bd881c863a0da6c9a602a83cae9f148ce72538f9e` |
| Pocket os25 core | `37cc7990cffeadf36112a830acc64b70c4692b6ede8b5353fa5eb7a8915b72fe` |
| Pocket OS kernel | `95a2cd387fe3346d61f4534ac37840bf02b32406a224cd96b6df701c803c267b` |
| MiSTer OS kernel | `c43aac4daf0820fd5fc65f9f2de6178710954c258212d2dd910be1ae7da97582` |

The core release report records its final bitstream, timing and SS1 validation.
The normal MiSTer kernel is unchanged from core 0.9.4.

The selected companion MiSTer bitstream measured 59 FPS minimum and 60.01 FPS
average at both 100 MHz and 90 MHz during the reference E1M1 save rotation,
with no presentation intervals over 25 ms. The normal executable also returned
from MiSTer OSD navigation and Doom Options without a crash or visible HUD
residue in a short smoke test. The companion core retains a known −0.114 ns
setup miss at 100 MHz; full timing and test limitations are recorded in its
[release report](https://github.com/openfpgaOS/openfpgaCore/blob/openfpgaos-mister-v0.9.5/docs/RELEASE_0.9.5.md).
