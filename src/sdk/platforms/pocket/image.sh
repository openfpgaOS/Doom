#!/bin/bash
#------------------------------------------------------------------------------
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileType: SOURCE
# SPDX-FileCopyrightText: (c) 2026, ThinkElastic <Think@Elastic.com>
#------------------------------------------------------------------------------
#
# openfpgaOS SDK — Pocket platform: assemble ONE app's deliverable.
#
# For a custom core (dist_dir given) this builds the standalone APF SD
# tree under build/pocket/<app>/.  For an SDK demo app (no dist_dir) it
# is a no-op — those bundle into the shared demo core via
# `make build CORE=sdk`.  Called by the core-Makefile template and the
# SDK Makefile APP hook; adding a target = add platforms/<t>/image.sh.
#
# Usage: image.sh <app> <elf> <sdk_root> <dist_dir|""> [assets...]
#
set -e
APP="$1"; ELF="$2"; ROOT="$3"; DIST="$4"; shift 4 2>/dev/null || true
ASSETS=("$@")

# No per-core dist → this is an SDK demo app; the Pocket demo core bundles
# it elsewhere, nothing standalone to assemble here.
{ [ -n "$DIST" ] && [ -d "$DIST" ]; } || exit 0

OUT="$ROOT/build/pocket/$APP"
RT="$ROOT/runtime/pocket"   # pocket-specific artifacts (bank.ofsf stays at runtime/)
rm -rf "$OUT"
mkdir -p "$(dirname "$OUT")"
cp -r "$DIST" "$OUT"

# Resolve the single Cores/<id>/ and Assets/<platform>/common dirs the APF
# dist tree provides.  Build the paths explicitly (no "ls | head" string
# concat — an empty glob there would have produced a relative "common"
# written into the CWD).
CORE_DIR=""; for d in "$OUT"/Cores/*/;  do [ -d "$d" ] && { CORE_DIR="$d"; break; }; done
ASSET_PLAT=""; for d in "$OUT"/Assets/*/; do [ -d "$d" ] && { ASSET_PLAT="$d"; break; }; done
[ -n "$CORE_DIR" ]   || { echo "Error: $DIST has no Cores/<id>/ — not an APF core tree"; exit 1; }
[ -n "$ASSET_PLAT" ] || { echo "Error: $DIST has no Assets/<platform>/"; exit 1; }
ASSET_DIR="${ASSET_PLAT}common"
mkdir -p "$ASSET_DIR"

# Runtime FPGA artifacts.  The bitstream ships under the name core.json's
# "filename" references -- currently os25.rbf_r (OS30 trees also ship
# os30.rbf_r; older trees named it bitstream.rbf_r).  Copy every *.rbf_r present
# so the bundle matches whichever variant the core uses, instead of one
# hardcoded name that goes stale on a rename.  loader.bin is copied separately
# so a missing one is reported rather than silently swallowed.
rbf_found=0
for f in "$RT"/*.rbf_r; do
    [ -f "$f" ] && { cp "$f" "$CORE_DIR"; rbf_found=1; }
done
[ "$rbf_found" = 1 ] || echo "  warn: runtime FPGA bitstream (*.rbf_r) missing (run 'make sdk' in openfpgaOS)"
[ -f "$RT/loader.bin" ] && cp "$RT/loader.bin" "$CORE_DIR" || echo "  warn: runtime/loader.bin missing (run 'make sdk' in openfpgaOS)"
[ -f "$RT/os.bin" ] && cp "$RT/os.bin" "$ASSET_DIR/" || echo "  warn: runtime/os.bin missing"

# Soundfonts (bank.ofsf + any game .ofsf) — target-agnostic, at runtime/ root.
for s in "$ROOT"/runtime/*.ofsf; do [ -f "$s" ] && cp "$s" "$ASSET_DIR/"; done

# Kernel ELF + app data files.
cp "$ELF" "$ASSET_DIR/$APP.elf"
for a in "${ASSETS[@]}"; do [ -f "$a" ] && cp "$a" "$ASSET_DIR/"; done

echo "Ready: build/pocket/$APP/"
