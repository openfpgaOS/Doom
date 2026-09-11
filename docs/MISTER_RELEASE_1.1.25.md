# DOOM for MiSTer v1.1.25

Opening the MiSTer OSD can pause frame presentation while a GPU flip is queued.
Doom now waits for that flip to complete before acquiring another buffer and
continues servicing audio during the wait. The watchdog counts active vblanks,
so a paused system menu does not fill the GPU queue and trigger a trap.

PCM music retains the last complete buffer when reads fail, backs off and
retries the same track data. Delayed DMA completions are retired before staging
is reused. Missing optional music or a failed initial fill falls back to MIDI.
WAD reads check failed seeks and clear stream errors before retrying. Game
rules, save compatibility and the 35 Hz simulation are unchanged.

The official SDK container rebuilt the executable from a fresh object directory.
Its SHA-256 is `a7c03b3b755776646fb11eb31404bf2b9dec5b59b4cc18122e127043d33aaf24`,
identical to the executable installed and tested on the SS1. Six uninstrumented
OSD navigation cycles survived with MiSTer's visibility log confirming each
open and close. The first-slot save volume was byte-identical before and after
installation and loading. All 15 recovery test groups pass under ASan/UBSan.
The Pocket and diagnostic executables also build successfully.

The final rotation sample measured 58.0 distinct DDR view rows per second
against 57.91 for the release baseline. This samples one row, not complete HDMI
frames. An earlier slow sample following manual loader recovery remains an
unresolved anomaly; see [the stability review](STABILITY_REVIEW.md). Listening
confirmation and sustained 60 FPS are not established by these tests.

The SD-rooted ZIP contains 29 menu launchers, the engine, setup scripts and
boot/save templates. Its boot template contains only the sound bank; no WADs
or personal saves are bundled. All local Downloader entries are checked against
their asset size and hash, and payload URLs are pinned to `doom-mister-v1.1.25`.
The stable database URL remains `main/releases/mister/doom.json.zip`.

Install `doom-v1.1.25.zip` at the SD-card root and run the setup script, or update
through MiSTer Downloader. Existing working game data and saves remain separate
from the supplied templates. Compatible with MiSTer core v0.9.2 and later.

Local build, package and validation records are retained under
`build/release-mister-1.1.25/`. The GitHub release includes `SHA256SUMS.txt`.
