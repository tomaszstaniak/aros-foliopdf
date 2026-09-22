#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Cross-build MuPDF (libmupdf, libmupdf-third, mutool) from work/mupdf for the
# ABI selected by AROS_TARGET. Objects go to build/<target>/mupdf, so the two
# ABIs never share a directory.
#
#   scripts/build-mupdf.sh [extra make arguments or targets]
#   AROS_TARGET=mainline scripts/build-mupdf.sh
#
# This drives upstream's own GNU make build with upstream's flags. Every
# divergence from a plain `make build=release libs tools` is listed below
# with its reason.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

[ -d "$WORK_DIR/.git" ] || { echo "build-mupdf: no work copy; run scripts/bootstrap.sh first" >&2; exit 1; }
[ -x "$AROS_CC" ] || { echo "build-mupdf: compiler not found: $AROS_CC" >&2; exit 1; }
[ -d "$AROS_BUILD_VOLUME" ] || { echo "build-mupdf: $AROS_BUILD_VOLUME is not mounted; cross-linking needs it" >&2; exit 1; }

# macOS ships GNU make 3.81; upstream's argument-file path for `ar` needs 4.x.
MAKE="${MAKE:-$(command -v gmake || command -v make)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

OUT="$BUILD_DIR/mupdf"
LOG="$BUILD_DIR/mupdf-build.log"
mkdir -p "$OUT"

MAKE_ARGS=(
  -C "$WORK_DIR"
  "OUT=$OUT"
  build=release
  verbose=yes

  # OS selects upstream's platform block. Any value other than Darwin/Linux
  # avoids the host's xcrun compiler; the AROS block added by patch 0001
  # keys on this name.
  OS=AROS
  "CC=$AROS_CC"
  "CXX=$AROS_CXX"
  "AR=$AROS_AR"

  # Host pkg-config probes, --gc-sections and the full strip are switched
  # off inside Makerules by patch 0001, keyed on OS=AROS.

  # Bundled third-party sources, built statically: the SDK's libz/libjpeg/
  # libfreetype2 are link stubs for shared libraries (see docs/BUILDING.md).
  USE_SYSTEM_LIBS=no

  # Smallest configuration that reads PDF (see docs/BUILDING.md). Widen
  # after the first run; each of these drops a dependency, not PDF support.
  tesseract=no
  barcode=no
  brotli=no
  mujs=no
  html=no
  xps=no
  svg=no
  extract=no
)

# First positional arguments replace the default targets.
if [ $# -gt 0 ]; then
  TARGETS=("$@")
else
  TARGETS=(libs tools)
fi

{
  echo "build-mupdf: $(date '+%F %T') target=$AROS_TARGET"
  echo "build-mupdf: work HEAD $(git -C "$WORK_DIR" rev-parse HEAD), dirty=$(git -C "$WORK_DIR" status --porcelain | wc -l | tr -d ' ')"
  echo "build-mupdf: CC=$AROS_CC ($("$AROS_CC" -dumpversion)) SDK=$AROS_SDK"
  echo "build-mupdf: $MAKE -j$JOBS ${MAKE_ARGS[*]} ${TARGETS[*]}"
} | tee "$LOG"

set +e
"$MAKE" -j"$JOBS" -k "${MAKE_ARGS[@]}" "${TARGETS[@]}" >> "$LOG" 2>&1
RC=$?
set -e

echo "build-mupdf: make exit $RC; log $LOG"
echo "build-mupdf: $(grep -c ' error: ' "$LOG" || true) compiler error line(s), $(grep -c '^make.*\*\*\*' "$LOG" || true) failed make target(s)"
exit "$RC"
