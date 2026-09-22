#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build the reader front end (src/pdfman.c) against the libmupdf built by
# scripts/build-mupdf.sh for the same AROS_TARGET.
#
#   scripts/build-pdfman.sh
#   AROS_TARGET=mainline scripts/build-pdfman.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

MUPDF_OUT="$BUILD_DIR/mupdf"
[ -f "$MUPDF_OUT/libmupdf.a" ] || { echo "build-pdfman: no $MUPDF_OUT/libmupdf.a; run scripts/build-mupdf.sh first" >&2; exit 1; }
[ -d "$AROS_BUILD_VOLUME" ] || { echo "build-pdfman: $AROS_BUILD_VOLUME is not mounted; cross-linking needs it" >&2; exit 1; }

OUT="$BUILD_DIR/pdfman"
LOG="$BUILD_DIR/pdfman-build.log"

# -lpthread: the bundled lcms2mt (cmserr.o) takes pthread mutexes.
# Headers come from the patched work copy, the same tree the library was
# built from. No strip: a full strip breaks AROS x86_64 relocations.
set +e
"$AROS_CC" -O2 -Wall -Wsign-compare \
  -I"$WORK_DIR/include" \
  -o "$OUT" "$PROJECT_ROOT/src/pdfman.c" \
  -L"$MUPDF_OUT" -lmupdf -lmupdf-third -lpthread -lm > "$LOG" 2>&1
RC=$?
set -e
cat "$LOG"
[ $RC -eq 0 ] && ls -l "$OUT"
echo "build-pdfman: exit $RC (target $AROS_TARGET); log $LOG"
exit $RC
