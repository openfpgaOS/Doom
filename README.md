# 🕹️ Doom · Heretic · Hexen for openfpgaOS

Three classic id Tech 1 / Raven shooters packaged as standalone openFPGA cores, built on the
[openfpgaOS SDK](https://github.com/openfpgaOS/openfgpaSDK).

> 🎮 **Button maps:** see **[CONTROLS.md](CONTROLS.md)**.

---

## 👾 What's inside

| Game | Core | You supply |
| --- | --- | --- |
| 💥 **Doom** | `ThinkElastic.doom` | `DOOM.WAD` / `DOOM2.WAD` / `PLUTONIA.WAD` / `TNT.WAD` (or a free WAD like `freedoom`) |
| 🐉 **Heretic** | `TheDiscordian.heretic` | `HERETIC.WAD` (classic *Shadow of the Serpent Riders*, ~14 MB) |
| 🛡️ **Hexen** | `TheDiscordian.hexen` | `HEXEN.WAD` (classic DOS, ~20 MB) |

> 📦 **IWADs aren't included** — supply your own game `.WAD` (the classic DOS releases work best).

---

## ⬇️ Download & install (Analogue Pocket)

1. Grab the ZIP for the game you want from the **[Releases page](https://github.com/openfpgaOS/Doom/releases)** — each core publishes as `<game>-v<version>.zip`.
2. Extract it to the **root of your Pocket's SD card**, merging folders when prompted.
3. Drop your IWAD into the core's asset folder on the SD card:
   `Assets/<game>/common/<GAME>.WAD` — e.g. `Assets/hexen/common/HEXEN.WAD`.

---

## 🖥️ MiSTer

Doom (and the whole Doom II / Final Doom / freeware-megawad family) also runs on
**MiSTer** through the game-agnostic **openfpgaOS** core. The core is installed
once; each game is a self-contained bundle that drops into
`games/OpenfpgaOS/`, and your saves live on a **separate volume** so an engine
update never touches them.

1. **Install the core once** from the game-agnostic **openfpgaOS core release**
   (`openfpgaos-core-v<version>.zip`, or via **MiSTer Downloader**) — it ships
   separately from the per-game bundles: `OpenfpgaOS.rbf` →
   `/media/fat/_Computer/`, `boot.rom` → `/media/fat/games/OpenfpgaOS/`.
2. **Unzip** the MiSTer release into `/media/fat/games/OpenfpgaOS/` — you get one
   `.mgl` launcher per game/mod plus a `Doom/` folder (read-only `boot.vhd`, a
   saves template, the loose `doom.elf` engine, per-instance `.ini`s, an empty
   `wads/`, and `setup.sh`).
3. **Add your IWADs:** copy the commercial WADs you own (`DOOM2.WAD`,
   `PLUTONIA.WAD`, `TNT.WAD`, …) into `games/OpenfpgaOS/Doom/wads/`. Freeware
   WADs (Freedoom, REKKR, SIGIL) can instead be fetched automatically with
   **MiSTer Downloader**.
4. **Run setup once** (and again after adding WADs): copy `Doom/setup.sh` to
   `/media/fat/Scripts/` and run it from the **Scripts** menu, or over ssh
   `bash Doom/setup.sh Doom`. It seeds your saves image **only if absent** (your
   saves are never overwritten) and injects your WADs into the boot image.
5. **Play:** pick a Doom `.mgl` from the MiSTer menu.

Engine updates are a single loose-file swap (`doom.elf`) — `boot.vhd` and your
saves are left alone. Push a rebuilt engine straight to a running MiSTer with
`make copy CORE=doom TARGET=mister`, which atomically replaces just the loose
`doom.elf`; build a full release bundle with
`make package CORE=doom TARGET=mister`. The full packaging/deployment flow is
documented in the SDK's
[`platforms/mister/PACKAGING.md`](src/sdk/platforms/mister/PACKAGING.md).

---

## 🔨 Build from source

You need the RISC-V toolchain (`make setup` installs it) and, for desktop test
builds, SDL2. Publishing releases also needs the GitHub CLI (`gh`).

```bash
git clone https://github.com/openfpgaOS/Doom.git
cd Doom
make setup                       # install riscv64-elf-gcc (prompts per-OS)
make build CORE=hexen            # RISC-V ELF + flashable build/hexen/
```

Everything is driven from the repo root with `CORE=doom`, `CORE=heretic`, or `CORE=hexen`:

| Command | What it does |
| --- | --- |
| `make build CORE=<game>` | Build the RISC-V ELF and assemble the flashable `build/<game>/` bundle |
| `make test CORE=<game>` | Build a desktop SDL2 binary (`app_pc`) to playtest on your computer |
| `make copy CORE=<game>` | Build and copy the core straight onto a mounted Pocket SD card |
| `make package CORE=<game>` | Zip the bundle into `releases/<game>-v<version>.zip` |
| `make release CORE=<game>` | Package, then draft a GitHub release |
| `make clean` | Remove all build artifacts |

You can also work from inside a core directory — `cd src/hexen && make` builds the
ELF and bundle, and `make test` builds the desktop binary.

---

## 🧩 How the cores fit together

Each game lives in `src/<game>/` with its own `Makefile` and engine module
(`src/<game>/<game>/`). The shared chocolate-doom engine (`cdoom/`) and the
openfpgaOS FPGA shim (`shim/`) are **symlinks into `src/doom/`**, so all three
cores compile against one copy of the engine and shim; a `-DOF_DOOM` /
`-DOF_HERETIC` / `-DOF_HEXEN` define selects each game's controls and quirks.
Build objects land under `.obj/<game>/`, so the cores never collide.

---

## 📁 Project structure

```
Makefile               Root dispatch — CORE=<game> / APP=<sdk-app>
README.md              This file — games, download, build
CONTROLS.md            Shipped button maps
src/
  doom/                Base Doom core AND the shared engine + shim
    Makefile           APP=doom, -DOF_DOOM
    doom/              Doom-specific engine files
    cdoom/             ← SHARED chocolate-doom engine (real dir lives here)
    shim/              ← SHARED openfpgaOS FPGA shim (real dir lives here)
  heretic/
    Makefile           APP=heretic, -DOF_HERETIC
    heretic/           Heretic engine module + glue files
    cdoom -> ../doom/cdoom    (symlink)
    shim  -> ../doom/shim     (symlink)
  hexen/
    Makefile           APP=hexen, -DOF_HEXEN
    hexen/             Hexen engine module + glue files
    cdoom -> ../doom/cdoom    (symlink)
    shim  -> ../doom/shim     (symlink)
  sdk/                 openfpgaOS SDK: of_* headers, musl libc, sdk.mk, platforms/, pc/ (SDL2 shim)
  apps/                Bundled SDK demo apps (the "sdk" core)
  tools/phdp/          PHDP host tools (UART streaming debugger)
dist/
  <game>/              Per-core SD-card config: Cores/, Assets/, Platforms/
  sdk/                 SDK demo core config
runtime/               FPGA bitstream, os.bin, loader.bin, soundfont banks
scripts/               setup / customize / copy / debug / package / release
build/ .obj/ releases/ Generated — never committed
```

---

## 🛠️ Built on the openfpgaOS SDK

This repository is an instance of the **[openfpgaOS SDK](https://github.com/openfpgaOS/openfgpaSDK)** —
a RISC-V toolchain and runtime for writing Analogue Pocket cores in C/C++. The
SDK repo is the source of truth for the `of_*` API (video, audio, input, MIDI,
saves, the SDL2 compatibility layer, the memory map, and the PHDP UART debug
tools). The same org hosts sibling game ports — Wolfenstein, Quake, and Duke3D.

The SDK's [`GETTING_STARTED.md`](https://github.com/openfpgaOS/openfgpaSDK/blob/main/GETTING_STARTED.md) covers a from-scratch setup.

---

## 🙌 Credits & licensing

- **Engine:** [chocolate-doom](https://github.com/chocolate-doom/chocolate-doom) (GPLv2), atop the original Doom, Heretic, and Hexen source releases by id Software and Raven Software.
- **SDK & Doom core:** the openfpgaOS SDK by **thinkelastic** (Apache-2.0).
- **Heretic & Hexen cores:** ported by **TheDiscordian**.
