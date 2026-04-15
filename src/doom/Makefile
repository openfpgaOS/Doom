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
CDOOM_SRCS  = $(filter-out cdoom/w_file_win32.c,$(wildcard cdoom/*.c))
DOOM_SRCS   = $(wildcard cdoom/doom/*.c)

SRCS        = $(SHIM_SRCS) $(CDOOM_SRCS) $(DOOM_SRCS)
SRCS_CXX    =

# ── Per-app CFLAGS (applied on top of sdk.mk SDK_CFLAGS) ──────────────
CFLAGS  = -Ishim -Icdoom -Icdoom/doom
CFLAGS += -DDISABLE_SDL2MIXER -DDISABLE_SDL2NET
CFLAGS += -Wno-unused-parameter -Wno-unused-function
CFLAGS += -Wno-unused-variable -Wno-sign-compare
CFLAGS += -Wno-missing-field-initializers -Wno-unused-but-set-variable
CFLAGS += -Wno-format-truncation -Wno-format -Wno-pointer-sign
CFLAGS += -Wno-implicit-fallthrough -Wno-unused-result
CFLAGS += -Wno-maybe-uninitialized -Wno-address
# chocolate-doom uses many old-style prototypes / pre-C99 patterns
CFLAGS += -Wno-strict-prototypes -Wno-old-style-definition

.DEFAULT_GOAL := all

include $(SDK_DIR)/sdk.mk

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
	rm -rf $(OUT) app_pc

.PHONY: all release debug copy package test clean
