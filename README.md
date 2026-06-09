# 🕹️ Doom · Heretic · Hexen for the Analogue Pocket

Three classic id Tech 1 / Raven shooters packaged as standalone openFPGA cores
for the [Analogue Pocket](https://www.analogue.co/pocket), built on the
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

## ⬇️ Download & install

1. Grab the ZIP for the game you want from the **[Releases page](https://github.com/openfpgaOS/Doom/releases)** — each core publishes as `<game>-v<version>.zip`.
2. Extract it to the **root of your Pocket's SD card**, merging folders when prompted.
3. Drop your IWAD into the core's asset folder on the SD card:
   `Assets/<game>/common/<GAME>.WAD` — e.g. `Assets/hexen/common/HEXEN.WAD`.

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
