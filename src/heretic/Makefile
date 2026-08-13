# Chocolate Heretic — openfpgaOS custom core
#
# Standalone Heretic core built from the same engine and FPGA shim as the
# Doom core: heretic/ holds the chocolate-doom Heretic game module, while
# cdoom/ and shim/ are symlinks into ../doom (shared engine and shim).
# -DOF_HERETIC specialises the shim's controls for Heretic.
#
# Two build paths:
#   make test   — build PC host binary (app_pc) via SDK SDL2 backend
#   make all    — build RISC-V ELF for the Analogue Pocket
#
# Targets:
#   make              Build the custom core (ELF + build/heretic/ bundle)
#   make debug        Build, push via UART, stream console
#   make copy         Copy this custom core to Pocket SD
#   make package      Package this custom core into a ZIP
#   make test         Test on desktop (SDL2)
#   make clean        Remove build artifacts
#

APP       = heretic
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
# cdoom/ and shim/ resolve through the symlinks into ../doom, so objects
# land under .obj/heretic/ and never collide with the Doom core's objects.
SHIM_SRCS    = $(wildcard shim/*.c)
CDOOM_SRCS   = $(filter-out cdoom/w_file_win32.c cdoom/z_native.c,$(wildcard cdoom/*.c))
HERETIC_SRCS = $(wildcard heretic/*.c)

SDK_MIDI    = $(SDK_DIR)/of_midi.c $(SDK_DIR)/of_smp_voice.c \
              $(SDK_DIR)/of_smp_bank.c $(SDK_DIR)/of_smp_tables.c

SRCS        = $(SHIM_SRCS) $(CDOOM_SRCS) $(HERETIC_SRCS) $(SDK_MIDI)
SRCS_CXX    =

# ── Per-app CFLAGS (applied on top of sdk.mk SDK_CFLAGS) ──────────────
CFLAGS  = -Ishim -Icdoom -Iheretic
CFLAGS += -DOF_HERETIC
CFLAGS += -DDISABLE_SDL2MIXER -DDISABLE_SDL2NET
CFLAGS += -DSMP_MAX_VOICES=20
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
# alignment over-pads this app.
CFLAGS += -falign-functions=16 -falign-loops=16

# Note: the Doom core's PERF/TRACES instrumentation knobs are not available
# here — Heretic runs the stock software renderer and does not compile
# r_perf.c (heretic/r_perf.h + of_heretic_compat.c stub it out).

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
	@MISTER_IP="$(MISTER_IP)" $(SDK_DIR)/platforms/$(TARGET)/copy.sh "$(APP)" "$(OBJ_DIR)/app.elf"


# ── MiSTer Downloader DB (mirrors src/doom/Makefile) ─────────────────
MISTER_DB_ID   ?= openfpgaOS/openfpgaos-heretic
MISTER_DB_VER  := $(shell python3 -c "import glob,json;print(json.load(open(sorted(glob.glob('$(DIST)/Cores/*/core.json'))[0]))['core']['metadata']['version'])" 2>/dev/null || echo 0.0.0)
# Asset URLs pin to THIS release stream's own tag: releases/latest is one
# pointer for the whole repo and three cores share it here.
MISTER_DB_BASE_URL ?= https://github.com/openfpgaOS/Doom/releases/download/heretic-mister-v$(MISTER_DB_VER)/
# The DB itself is committed and served via raw — a stable per-game db_url.
MISTER_DB_URL      ?= https://raw.githubusercontent.com/openfpgaOS/Doom/main/releases/mister/heretic.json.zip
MISTER_DB          = $(ROOT)/releases/mister/$(APP).json.zip
MISTER_DB_INI      = $(ROOT)/releases/mister/$(APP).downloader.ini

package: export ASSET_LABEL    = the commercial IWAD
package: export ASSET_EXAMPLES = HERETIC.WAD
ifeq ($(TARGET),mister)
package: game-dist
	@$(ROOT)/scripts/package.sh $(APP)
	@$(SDK_DIR)/platforms/mister/mkdb.py \
	    --staging "$(GAME_STAGE)" --sd-prefix "games/OpenfpgaOS" \
	    --menu-dir "_Computer/_OpenfpgaOS/_$(GAME)" \
	    --base-url "$(MISTER_DB_BASE_URL)" --url-mode flat --db-id "$(MISTER_DB_ID)" \
	    --db-url "$(MISTER_DB_URL)" \
	    --output "$(MISTER_DB)" --ini-out "$(MISTER_DB_INI)"
else
package: all
	@$(ROOT)/scripts/package.sh $(APP)
endif

# ── MiSTer per-game locked distribution ──────────────────────────────
# Mirrors src/doom/Makefile: a read-only boot.vhd shell (the wads), a
# writable saves volume, one .mgl per instance and the loose F-loaded
# engine ELF.  Heretic is a sub-game of this repo, so it gets its own
# GAME name (its own games/OpenfpgaOS/Heretic/ tree and menu folder).
GAME        = Heretic
GAME_INIS   = $(DIST)/Assets/heretic/common
GAME_STAGE  = $(ROOT)/build/mister/$(APP)
GAME_COMMON = $(ROOT)/build/mister/.$(APP)-common
GAME_ELF    = heretic.elf
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
	@if [ -f "$(GAME_CSV)" ]; then cp "$(GAME_CSV)" "$(GAME_STAGE)/$(GAME)/external_files.csv"; fi
	@rm -rf "$(GAME_COMMON)"
	@echo "  game-dist staged -> $(GAME_STAGE)"

# ── PC build (SDL2) ──────────────────────────────────────────────────
# sdk.mk's generic app_pc rule picks up $(CFLAGS) -- set above -- and
# $(SRCS), so no per-app recipe override is needed.
test: app_pc

clean: sdk-clean
	rm -f $(APP_OBJS:.o=.d) $(OF_INIT_OBJ:.o=.d) $(OF_SDL2_OBJ:.o=.d)
	rm -rf $(OUT) app_pc

.PHONY: all release debug copy package test clean
