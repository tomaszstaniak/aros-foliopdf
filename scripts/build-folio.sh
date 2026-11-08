#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build the reader front end (src/folio.c) against the libmupdf built by
# scripts/build-mupdf.sh for the same AROS_TARGET.
#
#   scripts/build-folio.sh
#   AROS_TARGET=mainline scripts/build-folio.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

MUPDF_OUT="$BUILD_DIR/mupdf"
[ -f "$MUPDF_OUT/libmupdf.a" ] || { echo "build-folio: no $MUPDF_OUT/libmupdf.a; run scripts/build-mupdf.sh first" >&2; exit 1; }
[ -d "$AROS_BUILD_VOLUME" ] || { echo "build-folio: $AROS_BUILD_VOLUME is not mounted; cross-linking needs it" >&2; exit 1; }

OUT="$BUILD_DIR/Folio"
LOG="$BUILD_DIR/folio-build.log"

# -lpthread: the bundled lcms2mt (cmserr.o) takes pthread mutexes.
# Headers come from the patched work copy, the same tree the library was
# built from. No strip: a full strip breaks AROS x86_64 relocations.
set +e
{
  echo "build-folio: $(date '+%F %T') target=$AROS_TARGET"
  echo "build-folio: source $(git -C "$PROJECT_ROOT" rev-parse --short HEAD 2>/dev/null), dirty=$(git -C "$PROJECT_ROOT" status --porcelain -- src 2>/dev/null | wc -l | tr -d ' ')"
  echo "build-folio: CC=${AROS_CC_ARGV[*]} ($("$AROS_CC" -dumpversion 2>/dev/null)) SDK=$AROS_SDK"
} > "$LOG"
"${AROS_CC_ARGV[@]}" -O2 -Wall -Wsign-compare \
  -I"$WORK_DIR/include" \
  -o "$OUT" "$PROJECT_ROOT/src/folio.c" \
  -L"$MUPDF_OUT" -lmupdf -lmupdf-third -lpthread -lm >> "$LOG" 2>&1
RC=$?
set -e
cat "$LOG"
[ $RC -eq 0 ] && ls -l "$OUT"
echo "build-folio: exit $RC (target $AROS_TARGET); log $LOG"
exit $RC
