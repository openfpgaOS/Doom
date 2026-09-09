# Pocket crash report: issue 15

Reviewed 2026-09-09. This investigation identifies defects in the distributed
Pocket application and a release gap. It does not reproduce the reporter's
hardware crash or establish its cause. No production code was changed during
this investigation, and no issue comment, release or deployment was made.

## Report and screenshots

[Issue 15](https://github.com/openfpgaOS/Doom/issues/15) reports crashes across
Doom, Doom II and SIGIL after reinstalling through Pocket Updater. Some happen
before entering a level; others happen after seconds or several levels. The
reporter also mentions occasional unusual music. Their exact core version,
Pocket firmware version and dock state are not specified.

All four [screenshots](https://imgur.com/a/NORxmWS) were downloaded and inspected.
They show startup and load-menu history, without exception registers, a fault
address or an explicit fatal engine error. Three show the optional
`couldn't open DOOMMUS.WAD` message; the fourth ends earlier in the startup log.

The tagged Pocket source's `pcm_ensure_init()` in `shim/i_pcmmusic.c` returns
normally when that optional PCM WAD is absent, allowing MIDI playback. This
message does not establish a missing required asset or explain the crash.

The core's `fatal_trap()` restores the terminal display, which can expose old
startup output, while sending exception details over UART. The current local
direct crash-register display is guarded for MiSTer. The screenshots are
consistent with this recovery path, but do not prove which exception or exit
path occurred.

## Distributed version

Both inventories checked on this date list `ThinkElastic.doom` at **1.1.20**:

- [openFPGA Library inventory](https://openfpga-library.github.io/analogue-pocket/api/v1/cores.json)
- [openFPGA Cores Inventory](https://openfpga-cores-inventory.github.io/analogue-pocket/api/v1/cores.json)

The [Pocket v1.1.20 release](https://github.com/openfpgaOS/Doom/releases/tag/doom-v1.1.20)
was published August 10. Its ZIP contains Pocket `Cores/` and `Assets/` trees,
and its `core.json` declares version 1.1.20 and metadata date 2026-07-11.
The differing metadata and publication dates should not be conflated.

The newer [MiSTer v1.1.23 release](https://github.com/openfpgaOS/Doom/releases/tag/doom-mister-v1.1.23)
was published September 6. Despite the generic asset name `doom-v1.1.23.zip`,
its ZIP contains the MiSTer `games/OpenfpgaOS/` layout, not a Pocket update.
An updater reinstall therefore does not establish that the reporter received
the later fixes. Confirm their installed version rather than assuming it.

Downloaded Pocket v1.1.20 artifacts:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `os25.rbf_r` | 2,135,596 | `584885133d6cf45ba35480ce8e8766150f67ca47f9019c6f771b12e89cca12c1` |
| `os.bin` | 134,424 | `7ee410f1e22dbf9ecb696cb0a9541ab532d128d9d36440f71081dbe3c4fa5506` |
| `doom.elf` | 1,287,224 | `2e9a8ce96ca519d0eaffc99178bb84b6b14a5ebd37f18070fca4771cb3295380` |

## Confirmed defects and focused validation

The release tag resolves to `70440e32fd608b719e7ae9219552e9df97db6911`.
Focused host tests include its production source and compare it with the
current source, whose relevant fixes are already committed in `1ae9a09`.

| Test | Tagged Pocket source | Current source |
| --- | --- | --- |
| Configuration binding lifetime | ASan stack-use-after-return | Pass |
| Failed SFX metadata allocation | ASan null-pointer write | Pass |
| SFX PCM conversion | UBSan left shift of negative value | Pass |
| Palette regression | Expected palette-value assertion fails | Pass |

The configuration test retains pointers registered by `I_BindVideoVariables()`
and writes them after the function returns, as configuration loading does.
In v1.1.20, `startup_delay`, `max_scaling_buffer_pixels` and `grabmouse` bind
temporary stack arrays. The current implementation uses static storage.

Disassembly of the **downloaded release ELF** confirms these bindings pass
`sp+4`, `sp+8` and `sp+12` at `0x1040463c`, `0x1040467c` and `0x104046ac`,
then release the stack frame before returning. This defect is present in the
distributed binary as well as the source tag.

The allocation test deliberately fails the second SFX allocation. The release
ELF likewise calls `malloc` at `0x10402eaa`, then immediately stores through
its result at `0x10402eae`, without a null check. The current code frees the
already allocated PCM buffer and returns failure safely. The test does not
show that the reporter ran out of memory.

The allocation case uses ASan alone so the earlier signed-shift defect does
not stop execution before the allocation failure. PCM conversion uses both
ASan and UBSan. The palette test fails at its ordinary palette-value assertion
in the old source, before reaching its negative-gamma case; it is not evidence
of the reported crash.

The tagged source also predates the masked-column bounds fix. This review did
not reproduce the reporter's specific sprite, music or map sequence. Passing
host tests establishes the focused fixes, not Pocket gameplay stability.

Artifacts are under `build/review-pocket-issue15-20260909/`: both release ZIPs,
four PNGs, inventory snapshots, archived tagged sources, selected video test
harness, build/run logs, disassembly and `stability-regressions.json` containing
the exact compilation commands. The video harness retains the existing gamma
and configuration cases while excluding unrelated later pacing tests. SFX
tests use the existing `tools/tests/test_sfx.c` harness without modification.

## Next investigation

1. Confirm installed Doom version, Pocket firmware, handheld/docked state and
   one repeatable map/save sequence. Preserve the reporter's WADs and saves.
2. Prepare a matched Pocket test bundle with the existing engine and firmware
   stability fixes. Updating only `doom.elf` does not install the OS mixer
   ownership fixes described in `MISTER_REPORT_FOLLOWUP.md`.
3. Add a Pocket-visible fault report containing cause, PC and fault address.
   The earlier full MiSTer renderer exceeded Pocket's reserved OS BRAM when
   enabled there; a Pocket implementation needs a smaller renderer or other
   verified space recovery. Changing boot-resident diagnostics requires a
   matching bitstream update, not just `os.bin`.
4. Compare the same reproduction with normal music and with `-nomusic` appended
   to that launcher's `ARGS`. This disables the MIDI/PCM music paths while
   retaining sound effects. Setting music volume to zero is not an equivalent
   isolation test. A difference would narrow the investigation, not prove an
   audio defect by itself because timing and memory use also change.

The test bundle and Pocket diagnostic change above are next work, not artifacts
produced by this issue review. Further performance tuning should not be used
as a claimed remedy for these unexplained crashes.
