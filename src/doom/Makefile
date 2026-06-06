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

# GAME selects the chocolate-doom game module under cdoom/ to build into
# this core. Default 'doom' keeps the original build byte-identical; pass
# GAME=heretic to build the Heretic module (cdoom/heretic) into heretic.elf
# instead, reusing the same shared engine (cdoom/) and FPGA shim (shim/).
GAME     ?= doom
APP       = $(GAME)
TARGET    = pocket
SDK_DIR   = ../sdk
ROOT      = $(realpath $(CURDIR)/../..)
OBJ_DIR   = $(ROOT)/.obj/$(APP)
BUILD_DIR = $(OBJ_DIR)
DIST      = $(ROOT)/dist/$(APP)
OUT       = $(ROOT)/build/$(APP)
RUNTIME   = $(ROOT)/runtime

# ── Sources ────────────────────────────────────────────────────────────
SHIM_SRCS   = $(wildcard shim/*.c)
CDOOM_SRCS  = $(filter-out cdoom/w_file_win32.c cdoom/z_native.c,$(wildcard cdoom/*.c))
GAME_SRCS   = $(wildcard cdoom/$(GAME)/*.c)

SDK_MIDI    = $(SDK_DIR)/of_midi.c $(SDK_DIR)/of_smp_voice.c \
              $(SDK_DIR)/of_smp_bank.c $(SDK_DIR)/of_smp_tables.c

SRCS        = $(SHIM_SRCS) $(CDOOM_SRCS) $(GAME_SRCS) $(SDK_MIDI)
SRCS_CXX    =

# ── Per-app CFLAGS (applied on top of sdk.mk SDK_CFLAGS) ──────────────
CFLAGS  = -Ishim -Icdoom -Icdoom/$(GAME)
# Heretic builds a separate ELF; -DOF_HERETIC lets the shim specialise its
# controls for Heretic without affecting the Doom build.
CFLAGS += $(if $(filter heretic,$(GAME)),-DOF_HERETIC)
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
