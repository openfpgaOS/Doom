# DOOM for MiSTer v1.1.24

This MiSTer release packages the committed 1.1.24 engine source from
`862e50c0bad551f079b15172578ee5fbde530f0d`. It includes the wall/view-cache
optimizations, menu/HUD redraw corrections and the engine/SDK stability fixes
in [the stability review](STABILITY_REVIEW.md). The companion core is
[openfpgaOS MiSTer v0.9.2](https://github.com/openfpgaOS/openfpgaCore/releases/tag/openfpgaos-mister-v0.9.2).

The executable was rebuilt from a fresh object directory in the official
RISC-V container. It is byte-identical to the previous local v1.1.24 executable.

`doom.elf`: 1,287,432 bytes; SHA-256
`e48740fcdb9ed7c9cbf012b3f5e59e23d2bb615e96a9727351a7ec51dc465a34`.

Release checks passed: 24 sanitizer scenarios, 16 WAD/allocation checks,
15 SoundFont checks and MIDI scheduler regressions. Existing source validation
also includes 14 DOOM, DOOM II and SIGIL demo comparisons covering 31,967 tics.
No new SS1 test was completed for this executable because the board became
unreachable during release preparation. Its previously installed core/firmware
pair is the one published in v0.9.2.

`doom-v1.1.24.zip` has an SD-rooted layout, 29 menu launchers, the loose engine,
setup scripts and boot/save templates. The boot template contains only the
sound bank; no WADs or personal saves are bundled. All 63 local Downloader
entries match their release assets by size/hash. Mutable saves are marked
`overwrite: false`. Payload URLs are pinned to `doom-mister-v1.1.24` and the
stable database URL remains `main/releases/mister/doom.json.zip`. The two
previously unpinned Freedoom CSV entries remain excluded from the database.

Install the ZIP at the SD-card root and run its setup script, or update through
MiSTer Downloader. Keep commercial IWADs in the game's `wads/` directory.
Existing working data and saves remain separate from the shipped templates.

Local build, validation and packaging evidence is retained under
`build/release-mister-1.1.24/`. The GitHub release includes SHA256SUMS.txt.
