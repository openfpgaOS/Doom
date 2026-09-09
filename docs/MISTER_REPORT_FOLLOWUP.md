# MiSTer music, HUD and crash report

The current candidate retains the SIGIL wall-math work and fixes status-bar
invalidation after menu overlays. The matching core firmware fixes mixer voice
ownership races and displays exception registers on MiSTer's crash screen.
Neither an audible cure nor a gameplay FPS increase has been verified on the
reporter's hardware.

## HUD defect

`ST_Drawer()` caches each direct framebuffer's widget state after drawing the
status bar. `M_Drawer()` runs later and can overwrite those same pixels, while
the cached slot still says its HUD is valid. Moving from Options to a shorter
parent menu keeps `menuactive` true, so the existing close-menu clear does not
run. Automap menus bypass that close-menu path as well.

`ST_InvalidateBuffer()` now marks the current draw slot invalid before menu,
message or debug overlays. The next status-bar draw to that slot refreshes it,
including when the buffer is reused much later. The software framebuffer uses
the existing first-refresh flag. Unchanged HUD frames retain their incremental
update path.

The production status-bar regression exercises all three slots, delayed and
repeated reuse, Options-to-parent-menu, automap and software framebuffer cases.
It fails against v1.1.23 and passes with the fix. The complete sanitizer suite
passes all 22 scenarios; the official MiSTer ELF builds with the existing
16-byte application BRAM margin.

## Music and crash evidence

The music ownership fixes live in the core's `hal/mixer.c`, so updating only
`doom.elf` does not install them. They protect allocation, group assignment and
retriggering against the timer callback, and remove the use of a delayed
hardware active mask to free newly started voices. Nine regression scenarios
fail against the previous firmware and pass with the changes.

The screenshot's `DOOMMUS.WAD` warning comes from an optional PCM-pack lookup;
MIDI fallback is supported. It does not identify the cause of the gameplay exit.
The previous fatal-trap path displayed the startup console while sending its
register report only to UART. The new MiSTer RBF places that report on screen.
Any future exception photo can therefore be matched to the candidate ELF.

The paired test package and full firmware review are under
`openfpgaOS/build/review-mister-report/` and
`openfpgaOS/docs/MISTER_DOOM_REPORT_FOLLOWUP.md` in the sibling core repository.
Install its RBF, `boot.rom` and loose `doom.elf` together. Existing WAD volumes,
saves and launchers remain usable. The package has not been published or
deployed to the SS1.
