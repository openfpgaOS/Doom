# CLAUDE.md — working in this repo

Guidance for Claude (and humans) hacking on the Doom / Heretic / Hexen openFPGA
cores. Read this before touching the build system, the shim, or a core's assets.

## 🗺️ What this repo is

A fork of the **openfpgaOS SDK** ([`openfpgaOS/openfgpaSDK`](https://github.com/openfpgaOS/openfgpaSDK))
that adds three Analogue Pocket game cores — **Doom**, **Heretic**, and **Hexen** —
on top of one shared chocolate-doom engine and FPGA shim.

- **Remotes:** `origin` = `TheDiscordian/Doom` (fork), `upstream` = `openfpgaOS/Doom` (canonical, where releases publish).
- **Maintainer:** `thinkelastic` owns the SDK and the Doom core. `TheDiscordian` authored the Heretic and Hexen cores and has write access for those + their releases.
- **Commit author email:** `43145244+TheDiscordian@users.noreply.github.com`.
- Sibling game ports live in the same org (Wolfenstein, Quake, Duke3D).

## 📁 Repo layout

The annotated directory tree lives in the README — see [Project structure](README.md#-project-structure). `src/sdk/`, `runtime/`, and `scripts/` are upstream-owned (update via fetch/merge); the cores live in `src/<game>/` and `dist/<game>/`. The conventions below assume that layout.

## 🔗 The shared-engine + symlink pattern (read this first)

There is **one** copy of the engine (`src/doom/cdoom/`) and the FPGA shim
(`src/doom/shim/`). Heretic and Hexen reach them through `cdoom` / `shim`
symlinks into `../doom`. Each game's own code is its engine module
(`src/<game>/<game>/`).

The per-game build defines `-DOF_DOOM`, `-DOF_HERETIC`, or `-DOF_HEXEN`, which
selects the game's code paths **inside the shared shim**. Objects compile into
`.obj/<game>/`, so the three cores never collide.

**Consequence:** `shim/i_input.c`, `shim/i_video.c`, etc. are shared by all
three games. Any change there must be guarded with `#ifdef OF_<GAME>` so it
can't break the other two. When you fix a Hexen control bug in the shim, build
Doom and Heretic too.

## 🔨 Build, test, package, release

Driven from the repo root; `CORE` is `doom`, `heretic`, `hexen`, or `sdk` (the demo core).

| Command | Result |
| --- | --- |
| `make setup` | Install the RISC-V toolchain (`riscv64-elf-gcc`) |
| `make build CORE=<game>` | RISC-V ELF + flashable `build/<game>/` bundle |
| `make test CORE=<game>` | Desktop SDL2 binary `app_pc` (playtest on a PC) |
| `make copy CORE=<game>` | Build + copy onto a mounted Pocket SD card |
| `make package CORE=<game>` | Zip → `releases/<game>-v<version>.zip` |
| `make release CORE=<game> [PREV=<tag>] [PUBLISH=1]` | Package + draft a GitHub release |
| `make clean` | Remove `build/`, `.obj/`, `releases/` |

From inside a core dir: `cd src/hexen && make` (= ELF + bundle, via the `all` →
`release` targets), `make test`, `make copy`, `make package`.

- `make` (`all`) builds the ELF then runs the per-core **`release`** target, which assembles `build/<game>/` — copies `dist/<game>/` plus `runtime/` (`os.bin`, `bitstream.rbf_r`, `loader.bin`, soundfont) and the ELF into the Cores/Assets layout. It prints `Ready: build/<game>/`. (`release` here is the bundle step, not a GitHub release.)
- `make package CORE=<game>` runs `scripts/package.sh`, reading the name/version from `dist/<game>/Cores/*/core.json`.
- **`make release CORE=<game>`** (root target, added in `scripts/release.sh`) packages, then `gh release create` a **draft**: tag `<game>-v<version>`, title `<Game> for <Product> v<version>`, notes = `git log` subjects since the previous `<game>-v*` tag (or "First release…"). `PUBLISH=1` makes it live; `PREV=<tag>` overrides the changelog baseline; needs `gh` auth and refuses to clobber an existing tag — bump `version` in `core.json` first.
- The root Makefile **auto-detects** cores: any `src/<name>/` with a `Makefile` that isn't `apps/`, `sdk/`, or `tools/`.

**Test IWADs (local):** Hexen wants the **classic DOS `HEXEN.WAD`** (~20 MB) — the
KEX re-release WAD crashes in `R_Init`. Heretic wants the **classic *Shadow of
the Serpent Riders* `HERETIC.WAD`** (~14 MB), not the larger KEX WAD. IWADs are
commercial — never commit them.

## 🧱 Per-game engine module conventions (`src/<game>/<game>/`)

Files the port adds on top of stock chocolate-doom (each opens with a one-line
`/* <file> — <purpose> (not upstream) */` header):

- **`of_<game>_compat.c`** — backing definitions for the shim globals the port added (e.g. `swap_run_walk`, `refresh_mode`). The game reuses the Doom-targeted shim, so it must define these. *Required for every core.*
- **`of_<game>_stubs.c`** *(Hexen)* — link stubs for subsystems the port drops: CD audio, the hi-res startup screen, and empty dehacked section/signature tables. Heretic doesn't need its own — the shim's `i_net_stub.c` covers it.
- **`of_<game>_save.c`** *(Hexen)* — the NVRAM save layer (see Saves).
- **`r_perf.h` / `r_gpu.h`** — stub headers. These games run the stock software renderer, so the Doom core's PERF/GPU instrumentation isn't compiled.
- **`doom_loading_logo.h`** — the boot splash (see below).

## 🎮 Controls (`shim/i_input.c`, guarded by `#ifdef OF_<GAME>`)

- **Doom** (`#else` branch) — a straightforward `BTN_MAP` table; B-hold = run; L2/R2 = use/fire.
- **Heretic + Hexen** carry **two layouts** gated on `control_scheme` (an Options toggle; see below). `I_PollInput` and the shared `#if defined(OF_HERETIC) || defined(OF_HEXEN)` joystick block both branch on it — **edit one branch, check the other.**
  - **DEFAULT** (`CONTROL_SCHEME_DEFAULT`) — the unified map: A/R2 fire, L2 use, B tap = use / hold = run (`update_b_button`), L1 = strafe modifier, R1 = action modifier (fly / inventory / weapon chords). Heretic X = use artifact, Y = next weapon; Hexen X = jump, Y = use artifact, R1+X = next weapon.
  - **DISCO** (`CONTROL_SCHEME_DISCO`) — the B-modifier map: A fire, B back **only while a menu is up**, B tap = use / hold (`HER/HEX_MOD_HOLD_MS`) = modifier (fly / inventory / B+Y prev weapon), L1/R1 = strafe (Heretic) or jump with B+L/R strafe (Hexen); L2/R2 unused.
- Either scheme keeps `key_fire` at its **default (RCTRL)** — do **not** remap it to `KEY_ENTER` like the Doom branch does, because X posts `key_useartifact` (ENTER) in these games, so `key_fire = ENTER` makes **X fire as well as use an item** (the "A and X both attack" bug).

`CONTROLS.md` documents the DEFAULT map; `DISCO_CONTROLS.md` the DISCO one. Controls only — no Options-menu / settings rows ("settings, not controls").

## 🎚️ Options menu (per-game `m_menu.c` / `mn_menu.c`, persisted in `<Game>.cfg`)

- **`swap_run_walk`** — `0` = hold Speed to run (vanilla), `1` = run by default. Doom and Heretic default `0`; **Hexen defaults `1`** (its puzzles are speed-gated). Bound via `M_BindIntVariable`; the menu toggle calls `M_SaveDefaults`. Menu label: MOVEMENT / Swap Run-Walk.
- **`refresh_mode`** — `REFRESH_MODE_VRR` (default) or `REFRESH_MODE_FIXED`, implemented in `shim/i_video.c` (variable-refresh vsync vs fixed-rate). Menu label: REFRESH.
- Config is stored as **DOS scancodes** in `Saves/<game>/common/<game>/<Game>.cfg` (e.g. ENTER = 28, RCTRL = 29). It's written on-device when a menu toggle fires — there's no in-game free-text editor.
- **Hexen status bar:** the H2BAR status bar (65 px tall at y=134) sits under the 7-row Options menu, so `SB_state` is forced to `-1` while a menu is active and on deactivate, to repaint and clear overdraw. QUIT GAME is dropped from the main menu; the menu joystick branch is gated with `joywait`.

## 🖼️ Boot loading logo (`doom_loading_logo.h` + `shim/i_main.c` `ShowLoadingLogo`)

Each core ships a grayscale wordmark shown before the engine starts — like the Doom core's splash.

- **Header format:** `doom_loading_logo_w` / `doom_loading_logo_h` = **320×240** (the full framebuffer), `doom_loading_logo_palette[256]` (uint32 `0xRRGGBB`, a **gray ramp** where index == brightness, **`palette[0] = 0x000000`**), `doom_loading_logo_pixels[w*h]` (uint8 indices).
- **Style:** grayscale, **centered with a healthy black border** — not edge-to-edge. `palette[0]` **must** be black: a logo shorter than the framebuffer leaves bands that render as `palette[0]`, so a non-black `palette[0]` shows up as white bars.
- **Blit:** `i_main.c` sets an 8-bit logo video mode, `of_video_palette_bulk(palette, 256)`, then `memcpy`s if the dims match the framebuffer, else centers and copies.
- **How to (re)generate one:** take a source logo image, LANCZOS-resize it to fit *centered* within 320×240 on a black background, convert to grayscale, build a 256-entry gray-ramp palette (`palette[i] = i,i,i`), map each pixel to its brightness index, and emit the `.h`. Done with a one-off Python/PIL script — not committed to the repo.

## 🏷️ Platform banner (`dist/<game>/Platforms/_images/<game>.bin`)

The Pocket platform-menu banner: **521×165, 16-bit grayscale, column-major
right-to-left, no header** — one tile per core. Generated from the WAD's `TITLE`
lump (centered, wings darkened). See the `reference_pocket_platform_image_format`
note for the exact byte layout.

## 💾 Saves

- **Doom / Heretic** — route through the shim's in-RAM save buffer into one Pocket `.sav` slot (`shim/i_save.*`).
- **Hexen** (`of_hexen_save.c`, `#ifndef OF_PC`) — Hexen hub saves are *many* files (`hex6.hxs` + a `hex6NN.hxs` per visited map), ~520–556 KB a hub, well over the Pocket's fixed **256 KB** NVRAM slot. During play the files live in an in-RAM VFS (`sv_save.c` rerouted through `fmemopen`); a manual save packs the slot into one blob, LZSS-compresses it (~4.3×, ~132 KB worst case), and writes one `Hexen_<N>.sav`. The desktop (`OF_PC`) build uses real files and bypasses this path — **on-device save/load is the part to hardware-test.**

## 📦 `dist/<game>/` layout

```
Cores/<Author>.<game>/
  core.json       metadata (shortname, author, version, date_release) + framework (target_product, dock, link_port)
  data.json       data-slot map (IWAD goes in a Data slot)
  input.json      Pocket button labels
  interact.json   Pocket menu variables
  video.json      scaler modes (320×240 and friends)
  audio.json  variants.json  icon.bin
Platforms/<game>.json        category / name / year / manufacturer
Platforms/_images/<game>.bin banner (see above)
Assets/<game>/common/<game>.ini    [os] ELF=<game>.elf, ARGS=-iwad "<GAME>.WAD" -config "<Game>.cfg" -saveprefix "<Game>"
Assets/<game>/<Author>.<game>/     instance JSON dir
```

`Author` is `ThinkElastic` for the SDK/Doom core, `TheDiscordian` for Heretic and
Hexen. The IWAD is **user-supplied** — the repo ships none; the `release` target
copies any `*.wad` it finds in the core dir, but none are committed.

## ✍️ Comment style

Terse. **Describe what the code does**, not its rationale, history, or tuning
numbers. Match the surrounding density. Brief correctness footguns are fine.
Good examples from the tree:

```c
/* Hold B this long before it acts as the modifier, so a quick tap stays Use. */
#define HEX_MOD_HOLD_MS 100
```
```c
static void hex_emit(int *slot, int key)   /* key 0 = released */
```

Glue files open with a single `/* <file> — <purpose> (not upstream) */` line.
The `trim-doom-comments` branch exists specifically to pull verbose comments
back to this convention — don't reintroduce slop.

## ➕ Adding a new game core

1. Drop the chocolate-`<game>` engine module into `src/<game>/<game>/`.
2. `mkdir src/<game>`; symlink `cdoom -> ../doom/cdoom` and `shim -> ../doom/shim`.
3. Copy a sibling `Makefile` (heretic/hexen): set `APP=<game>`, `-I<game>`, `-DOF_<GAME>`, and tune `SMP_MAX_VOICES`.
4. Add `of_<game>_compat.c` (backing shim globals), `r_perf.h` / `r_gpu.h` stubs, `of_<game>_stubs.c` if subsystems are missing, and `of_<game>_save.c` if saves don't fit a 256 KB slot.
5. Add the `OF_<GAME>` block in `shim/i_input.c` (and the joystick block near the top), plus any `shim/i_video.c` specialization — all guarded per-game.
6. Wire the Options toggles (see the Options menu section above). `swap_run_walk` and `refresh_mode` are already registered in the shared `cdoom/m_config.c`, but each game supplies its own storage, binding, menu items, and consumers:
   - `refresh_mode` (+ `frame_interpolation`) backing lives in `of_<game>_compat.c` (from step 4).
   - Add `int swap_run_walk = <default>;` to the game's `g_game.c` (Doom/Heretic `0`, Hexen `1`), `extern` it in the game header, and read it in the movement code.
   - `M_BindIntVariable("swap_run_walk", …)` and `M_BindIntVariable("refresh_mode", …)` in the game's `d_main.c` / `h2_main.c`.
   - Add the `MOVEMENT` and `REFRESH` Options items to the game's menu (`m_menu.c` / `mn_menu.c`) with `SCSwapRunWalk` / `SCRefreshMode` handlers that flip the vars and draw `RUN`/`WALK` and `VRR`/`FIXED`. The shared `shim/i_video.c` consumes `refresh_mode`.
7. Add `doom_loading_logo.h` (boot splash).
8. Build `dist/<game>/`: copy a sibling, set `core.json` (`shortname` / `author` / `version` / `platform_ids`), `Platforms/<game>.json` + banner, `Assets/<game>/common/<game>.ini`, and `input.json` labels.
9. `make build CORE=<game>` (RISC-V) and `make test CORE=<game>` (desktop) to verify both targets.
10. The root Makefile picks it up automatically.

## ⚠️ Gotchas

- The shim is shared — guard everything with `#ifdef OF_<GAME>` and rebuild all three cores after shim changes.
- Use the **classic DOS** Hexen/Heretic IWADs; KEX re-release WADs fail to load.
- IWADs are commercial — never commit them, and never `git add -f` them.
- The desktop (`OF_PC`) build bypasses the NVRAM save layer; on-device saves need real hardware to verify.
- Don't amend or force-push an open PR branch; upstream squashes on merge.
- The SDK (`src/sdk/`), `runtime/`, and `scripts/` are upstream-owned and update via fetch/merge — the cores live in `src/<game>/` and `dist/<game>/`.
