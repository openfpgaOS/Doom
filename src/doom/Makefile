# Chocolate Doom — openfpgaOS custom core
#
# Two build paths:
#   make test   — build PC host binary (app_pc) via SDK SDL2 backend
#   make all    — build RISC-V ELF for the Analogue Pocket
#
# The openfpgaOS port replaces chocolate-doom's SDL-based I/O with SDK
# equivalents. Game core (cdoom/) is unmodified chocolate-doom. Shim
# files (shim/) provide i_video / i_input / i_sound / i_main that route
# through the SDK.
#
# Targets:
#   make              Build the custom core (ELF + build/doom/ bundle)
#   make debug        Build, push via UART, stream console
#   make copy         Copy this custom core to Pocket SD
#   make package      Package this custom core into a ZIP
#   make test         Test on desktop (SDL2)
#   make clean        Remove build artifacts
#

APP       = doom
TARGET   ?= pocket
MISTER_IP ?= mister.local
SDK_DIR   = ../sdk
ROOT      = $(realpath $(CURDIR)/../..)
OBJ_DIR   = $(ROOT)/.obj/$(APP)
BUILD_DIR = $(OBJ_DIR)
DIST      = $(ROOT)/dist/$(APP)
OUT       = $(ROOT)/build/$(TARGET)/$(APP)
RUNTIME   = $(ROOT)/runtime
GAME_ASSETS = $(wildcard *.wad *.dat)

# ── Sources ────────────────────────────────────────────────────────────
SHIM_SRCS   = $(wildcard shim/*.c)
CDOOM_SRCS  = $(filter-out cdoom/w_file_win32.c cdoom/z_native.c,$(wildcard cdoom/*.c))
DOOM_SRCS   = $(wildcard cdoom/doom/*.c)

SDK_MIDI    = $(SDK_DIR)/of_midi.c $(SDK_DIR)/of_smp_voice.c \
              $(SDK_DIR)/of_smp_bank.c $(SDK_DIR)/of_smp_tables.c

SRCS        = $(SHIM_SRCS) $(CDOOM_SRCS) $(DOOM_SRCS) $(SDK_MIDI)
SRCS_CXX    =

# ── Per-app CFLAGS (applied on top of sdk.mk SDK_CFLAGS) ──────────────
CFLAGS  = -Ishim -Icdoom -Icdoom/doom
CFLAGS += -DDISABLE_SDL2MIXER -DDISABLE_SDL2NET
CFLAGS += -DSMP_MAX_VOICES=20
CFLAGS += -DOF_DOOM   # selects the Doom-only shim paths (e.g. i_pcmmusic.c)
CFLAGS += -Wno-unused-parameter -Wno-unused-function
CFLAGS += -Wno-unused-variable -Wno-sign-compare
CFLAGS += -Wno-missing-field-initializers -Wno-unused-but-set-variable
CFLAGS += -Wno-format-truncation -Wno-format -Wno-pointer-sign
CFLAGS += -Wno-implicit-fallthrough -Wno-unused-result
CFLAGS += -Wno-maybe-uninitialized -Wno-address
# chocolate-doom uses many old-style prototypes / pre-C99 patterns
CFLAGS += -Wno-strict-prototypes -Wno-old-style-definition
CFLAGS += -MMD -MP

# Keep hot renderer code inside APP_BRAM.  The SDK default 64-byte function
# alignment over-pads this app now that the renderer has more GPU paths.
CFLAGS += -falign-functions=16 -falign-loops=16

# Optional renderer instrumentation.  Keep counters off for release timing:
#   make PERF=1          # enable render pipeline counters
#   make TRACES=1        # pacing/fuzz/slow traces; use -pacingtrace, etc.
#   make PERFDETAIL=1    # PERF plus hot-loop BSP/plane work counters
#   make PERFTIMING=1    # PERFDETAIL plus microsecond timings; heaviest mode
PERF ?= 0
ifeq ($(PERF),1)
CFLAGS += -DR_RENDER_PERF=1
endif

ifeq ($(TRACES),1)
CFLAGS += -DR_RUNTIME_TRACES=1
endif

ifeq ($(PERFDETAIL),1)
CFLAGS += -DR_RENDER_PERF=1 -DR_RENDER_PERF_DETAIL=1
endif

ifeq ($(PERFTIMING),1)
CFLAGS += -DR_RENDER_PERF=1 -DR_RENDER_PERF_DETAIL=1 -DR_RENDER_PERF_DETAIL_TIMING=1
endif

ifeq ($(PERFTIMING),1)
# Timing probes are heavier than count-only detail; use the tightest
# alignment so the diagnostic build remains linkable.
CFLAGS += -falign-functions=4 -falign-loops=4
endif

# PCM/CD-music diagnostics: log the music WAD, track lookups, and refill
# success/fail counters to /pcmlog.txt on the (writable) boot vhd, so a
# "music loops every few seconds" report can be pinned to track-mismatch vs.
# S2 refill failure.  Off by default; enable with `make PCMLOG=1`.
PCMLOG ?= 0
ifeq ($(PCMLOG),1)
CFLAGS += -DPCM_DIAG
endif

.DEFAULT_GOAL := all

include $(SDK_DIR)/sdk.mk

-include $(APP_OBJS:.o=.d) $(OF_INIT_OBJ:.o=.d) $(OF_SDL2_OBJ:.o=.d)

# ── Build: ELF + release bundle ──────────────────────────────────────
all: $(OBJ_DIR)/app.elf release
	@$(SIZE) $<

ifeq ($(TARGET),mister)
release: game-dist        # MiSTer: the per-game bundle IS the release artifact
else
release: $(OBJ_DIR)/app.elf
	@$(SDK_DIR)/platforms/$(TARGET)/image.sh "$(APP)" "$(OBJ_DIR)/app.elf" "$(ROOT)" "$(DIST)" $(GAME_ASSETS)
endif

debug: all
	@$(ROOT)/scripts/debug.sh "$$(ls -d $(OUT)/Assets/*/common 2>/dev/null)/$(APP).elf"

copy: all
ifeq ($(TARGET),mister)
	@MISTER_IP="$(MISTER_IP)" $(SDK_DIR)/platforms/mister/copy.sh game "$(GAME)" "$(GAME_ELF)" "$(OBJ_DIR)/app.elf"
else
	@MISTER_IP="$(MISTER_IP)" $(SDK_DIR)/platforms/$(TARGET)/copy.sh "$(APP)" "$(OBJ_DIR)/app.elf"
endif

# Engine-only update — now identical to `make copy`: for MiSTer that pushes
# the loose games/OpenfpgaOS/<Game>/<GameElf> (the F-loaded engine), leaving
# boot.vhd (your wads) and the saves volume untouched.  Kept as an alias.
copy-app: all
	@$(MAKE) copy

# `make package` for MiSTer builds the per-game locked distribution (a
# boot.vhd shell + .mgl launchers + loose inis + setup.sh + external_files.csv,
# zipped for games/OpenfpgaOS/); every other target keeps the single-image
# path.  scripts/package.sh -> platforms/mister/package.sh auto-detects which.
#
# For MiSTer it ALSO generates the Downloader custom-database delivery artifact
# (releases/mister/doom.json.zip) via mkdb.py — a real ZIP-wrapped JSON the
# stock MiSTer Downloader syncs.  external_files.csv is folded into it here at
# build time (it is NOT a device file).  db_id must never change once published.
#
# Publishing model (edit these for the real release):
#   MISTER_DB_ID       downloader.ini [section] == db_id
#   MISTER_DB_BASE_URL where the STAGED tree is hosted; a local file at sd path
#                      <p> resolves to  $(MISTER_DB_BASE_URL)<p>  (base_files_url
#                      + path — the Downloader's own rule).  Host the game-dist
#                      tree under games/OpenfpgaOS/ at this base.
#   MISTER_DB_URL      where doom.json.zip itself is published (for the ini snippet)
MISTER_DB_ID       ?= openfpgaOS/openfpgaos-doom
MISTER_DB_BASE_URL ?= https://raw.githubusercontent.com/openfpgaOS/Doom/mister-dist/
MISTER_DB_URL      ?= https://raw.githubusercontent.com/openfpgaOS/Doom/mister-dist/doom.json.zip
MISTER_DB          = $(ROOT)/releases/mister/$(APP).json.zip
MISTER_DB_INI      = $(ROOT)/releases/mister/$(APP).downloader.ini
ifeq ($(TARGET),mister)
package: game-dist
	@$(ROOT)/scripts/package.sh $(APP)
	@$(SDK_DIR)/platforms/mister/mkdb.py \
	    --staging "$(GAME_STAGE)" --sd-prefix "games/OpenfpgaOS" \
	    --menu-dir "_Computer/_OpenfpgaOS/_$(GAME)" \
	    --base-url "$(MISTER_DB_BASE_URL)" --db-id "$(MISTER_DB_ID)" \
	    --external "$(GAME_CSV)" --db-url "$(MISTER_DB_URL)" \
	    --output "$(MISTER_DB)" --ini-out "$(MISTER_DB_INI)"
else
package: all
	@$(ROOT)/scripts/package.sh $(APP)
endif

# ── MiSTer per-game locked distribution (S0 boot shell + S1 saves + .mgl) ─
# The core (openfpgaOS.rbf + os.bin) is game-agnostic.  This game ships a pair
# of images: a READ-ONLY boot.vhd (S0 — /$(GAME)/common: bank.ofsf, wads added
# on-device; NO engine ELF) and a writable $(GAME).saves.vhd (S1 — one
# /$(GAME)/<Instance> subfolder of preallocated config/save slots per instance
# ini).  Splitting them keeps saves on a separate, Downloader-reserved volume
# so an update can replace boot.vhd without touching the player's saves.  The
# engine ELF ships as a LOOSE file ($(GAME)/$(GAME_ELF), next to boot.vhd): the
# firmware F-loads it at ioctl index 2 into ELF staging and serves it as slot 3,
# so an engine update is one loose-file swap (mkdb.py indexes it as a Downloader
# `files` entry) and boot.vhd never changes.  Also ships: a 4-line .mgl launcher
# per instance (S0 boot + S1 saves + F-load ELF index 2 + F-load ini index 1;
# ELF BEFORE ini — the ini F-load resets and triggers the app load), the loose
# per-instance inis, setup.sh (seeds the saves image + injects the user's wads)
# and external_files.csv (freeware wad list).
# `make package TARGET=mister` builds and zips this bundle.
#
# GAME_INIS  : canonical per-instance inis (each with [os] GAME/INSTANCE/ELF)
# GAME_STAGE : games/OpenfpgaOS/-rooted staging that package.sh zips
# GAME_COMMON: throwaway dir feeding mkgame the ELF + bank.ofsf (kept OUT of
#              the staging so it is never zipped)
GAME        = Doom
GAME_INIS   = $(DIST)/Assets/doom/common
GAME_STAGE  = $(ROOT)/build/mister/$(APP)
GAME_COMMON = $(ROOT)/build/mister/.$(APP)-common
GAME_ELF    = doom.elf
GAME_CSV    = $(DIST)/mister/external_files.csv

game-dist: $(OBJ_DIR)/app.elf
	@rm -rf "$(GAME_STAGE)" "$(GAME_COMMON)"
	@mkdir -p "$(GAME_STAGE)/$(GAME)/wads" "$(GAME_COMMON)"
	@cp "$(RUNTIME)/bank.ofsf" "$(GAME_COMMON)/bank.ofsf"
	@$(SDK_DIR)/platforms/mister/mkgame.sh --no-elf --saves-out "$(GAME_STAGE)/$(GAME)/$(GAME).saves.vhd" "$(GAME)" "$(GAME_INIS)" "$(GAME_COMMON)" "$(GAME_STAGE)/$(GAME)/boot.vhd"
	@$(SDK_DIR)/platforms/mister/mkmgl.sh "$(GAME)" "$(GAME_INIS)" "$(GAME_STAGE)"
	@cp "$(OBJ_DIR)/app.elf" "$(GAME_STAGE)/$(GAME)/$(GAME_ELF)"
	@cp "$(GAME_INIS)"/*.ini "$(GAME_STAGE)/$(GAME)/"
	@sed 's|^GAME_DEFAULT=.*|GAME_DEFAULT="$(GAME)"|' "$(SDK_DIR)/platforms/mister/setup.sh" > "$(GAME_STAGE)/$(GAME)/setup.sh"
	@chmod +x "$(GAME_STAGE)/$(GAME)/setup.sh"
	@cp "$(GAME_CSV)" "$(GAME_STAGE)/$(GAME)/external_files.csv"
	@rm -rf "$(GAME_COMMON)"
	@echo "  game-dist staged -> $(GAME_STAGE)"

# ── PC build (SDL2) ──────────────────────────────────────────────────
# sdk.mk's generic app_pc rule picks up $(CFLAGS) -- set above -- and
# $(SRCS), so no per-app recipe override is needed.
test: app_pc

clean: sdk-clean
	rm -f $(APP_OBJS:.o=.d) $(OF_INIT_OBJ:.o=.d) $(OF_SDL2_OBJ:.o=.d)
	rm -rf $(OUT) app_pc

.PHONY: all release debug copy package test clean game-dist
