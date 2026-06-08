# Chocolate Hexen — openfpgaOS custom core
#
# Standalone Hexen core built from the same engine and FPGA shim as the
# Doom core: hexen/ holds the chocolate-doom Hexen game module, while
# cdoom/ and shim/ are symlinks into ../doom (shared engine and shim).
# -DOF_HEXEN specialises the shim's controls for Hexen.
#
# Two build paths:
#   make test   — build PC host binary (app_pc) via SDK SDL2 backend
#   make all    — build RISC-V ELF for the Analogue Pocket
#
# Targets:
#   make              Build the custom core (ELF + build/hexen/ bundle)
#   make debug        Build, push via UART, stream console
#   make copy         Copy this custom core to Pocket SD
#   make package      Package this custom core into a ZIP
#   make test         Test on desktop (SDL2)
#   make clean        Remove build artifacts
#

APP       = hexen
TARGET    = pocket
SDK_DIR   = ../sdk
ROOT      = $(realpath $(CURDIR)/../..)
OBJ_DIR   = $(ROOT)/.obj/$(APP)
BUILD_DIR = $(OBJ_DIR)
DIST      = $(ROOT)/dist/$(APP)
OUT       = $(ROOT)/build/$(APP)
RUNTIME   = $(ROOT)/runtime

# ── Sources ────────────────────────────────────────────────────────────
# cdoom/ and shim/ resolve through the symlinks into ../doom, so objects
# land under .obj/hexen/ and never collide with the Doom core's objects.
SHIM_SRCS    = $(wildcard shim/*.c)
CDOOM_SRCS   = $(filter-out cdoom/w_file_win32.c cdoom/z_native.c,$(wildcard cdoom/*.c))
HEXEN_SRCS = $(wildcard hexen/*.c)

SDK_MIDI    = $(SDK_DIR)/of_midi.c $(SDK_DIR)/of_smp_voice.c \
              $(SDK_DIR)/of_smp_bank.c $(SDK_DIR)/of_smp_tables.c

SRCS        = $(SHIM_SRCS) $(CDOOM_SRCS) $(HEXEN_SRCS) $(SDK_MIDI)
SRCS_CXX    =

# ── Per-app CFLAGS (applied on top of sdk.mk SDK_CFLAGS) ──────────────
CFLAGS  = -Ishim -Icdoom -Ihexen
CFLAGS += -DOF_HEXEN
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
# here — Hexen runs the stock software renderer and does not compile
# r_perf.c (hexen/r_perf.h + of_hexen_compat.c stub it out).

.DEFAULT_GOAL := all

include $(SDK_DIR)/sdk.mk

-include $(APP_OBJS:.o=.d) $(OF_INIT_OBJ:.o=.d) $(OF_SDL2_OBJ:.o=.d)

# ── Build: ELF + release bundle ──────────────────────────────────────
all: $(OBJ_DIR)/app.elf release
	@$(SIZE) $<

release: $(OBJ_DIR)/app.elf
	@rm -rf $(OUT)
	@mkdir -p $(dir $(OUT))
	@cp -r $(DIST) $(OUT)
	@cp $(RUNTIME)/bitstream.rbf_r $(RUNTIME)/loader.bin $$(ls -d $(OUT)/Cores/*/)/
	@mkdir -p $$(ls -d $(OUT)/Assets/*/)/common
	@cp $(RUNTIME)/os.bin $$(ls -d $(OUT)/Assets/*/)/common/
	@cp $(RUNTIME)/bank.ofsf $$(ls -d $(OUT)/Assets/*/)/common/ 2>/dev/null || true
	@cp $(RUNTIME)/8mbgmsfx.ofsf $$(ls -d $(OUT)/Assets/*/)/common/ 2>/dev/null || true
	@cp $< $$(ls -d $(OUT)/Assets/*/)/common/$(APP).elf
	@for f in *.wad *.dat; do [ -f "$$f" ] && cp "$$f" $$(ls -d $(OUT)/Assets/*/)/common/; done 2>/dev/null; true
	@echo "Ready: build/$(APP)/"

debug: all
	@$(ROOT)/scripts/debug.sh "$$(ls -d $(OUT)/Assets/*/common)/$(APP).elf"

copy: all
	@$(SDK_DIR)/platforms/$(TARGET)/copy.sh "$(APP)" "$$(ls -d $(OUT)/Assets/*/common)/$(APP).elf"

package: all
	@$(ROOT)/scripts/package.sh $(APP)

# ── PC build (SDL2) ──────────────────────────────────────────────────
# sdk.mk's generic app_pc rule picks up $(CFLAGS) -- set above -- and
# $(SRCS), so no per-app recipe override is needed.
test: app_pc

clean: sdk-clean
	rm -f $(APP_OBJS:.o=.d) $(OF_INIT_OBJ:.o=.d) $(OF_SDL2_OBJ:.o=.d)
	rm -rf $(OUT) app_pc

.PHONY: all release debug copy package test clean
