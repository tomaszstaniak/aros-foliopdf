#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Stage the release archives. Nothing here is uploaded: publication is a
# separate decision.
#
#   LHA_WRITER=/path/to/lha scripts/make-release.sh
#       -> dist/Folio-<ver>.x86_64-aros-v11.lha   (the AROS package)
#          dist/Folio-<ver>-source.zip            (corresponding source)
#
# LHA_WRITER must be a create-capable classic lha (LHa for UNIX 1.14i or
# similar); the Lhasa shipped by Homebrew only extracts.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

VER="${VER:-0.1}"
LHA_WRITER="${LHA_WRITER:?set LHA_WRITER to a create-capable classic lha executable}"
"$LHA_WRITER" --help 2>&1 | grep -q "a   Add" || { echo "make-release: $LHA_WRITER cannot create archives" >&2; exit 1; }
[ "$AROS_TARGET" = one ] || { echo "make-release: this release is ABIv11 only (AROS_TARGET=one)" >&2; exit 1; }
BIN="$BUILD_DIR/pdfman"
[ -f "$BIN" ] || { echo "make-release: build first (scripts/build-pdfman.sh)" >&2; exit 1; }

OUT="$PROJECT_ROOT/dist"
NAME="Folio-$VER.x86_64-aros-v11"
STAGE="$OUT/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE/Folio/licenses" "$STAGE/.arospkg"

# --strip-unneeded only: a full strip breaks AROS x86_64 relocations.
"$AROS_STRIP" --strip-unneeded --remove-section .comment -o "$STAGE/Folio/Folio" "$BIN"
cp "$PROJECT_ROOT/packaging/Folio.info" "$STAGE/Folio/Folio.info"
cp "$PROJECT_ROOT/packaging/Folio.info" "$STAGE/Folio.info"      # drawer icon, beside the drawer
cp "$PROJECT_ROOT/packaging/README" "$STAGE/Folio/README"
cp "$PROJECT_ROOT/CHANGELOG.md" "$STAGE/Folio/CHANGELOG"
cp "$PROJECT_ROOT/LICENSE" "$STAGE/Folio/LICENSE"
cp "$PROJECT_ROOT/packaging/licenses/"* "$STAGE/Folio/licenses/"
cp "$PROJECT_ROOT/packaging/manifest.toml" "$STAGE/.arospkg/manifest.toml"

( cd "$STAGE" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | sort |
  while read -r f; do shasum -a 256 "$f"; done > Folio/SHA256SUMS )
# Amiga convention: the archive holds the drawer, its icon and .arospkg at the
# root, not an extra wrapper directory. lh5, header level 1 (-o5 is rejected
# by this writer for level 2 archives with long names).
( cd "$STAGE" && rm -f "$OUT/$NAME.lha" && "$LHA_WRITER" aq "$OUT/$NAME.lha" Folio Folio.info .arospkg )

# Source: the repository at HEAD plus the pinned MuPDF with the series
# applied (work/ is git-ignored, so git archive alone would omit it).
SRC="Folio-$VER-source"
rm -rf "$OUT/$SRC" "$OUT/$SRC.zip"; mkdir -p "$OUT/$SRC"
git -C "$PROJECT_ROOT" archive --format=tar HEAD | tar -x -C "$OUT/$SRC"
mkdir -p "$OUT/$SRC/work"
git -C "$WORK_DIR" archive --format=tar --prefix=mupdf/ HEAD | tar -x -C "$OUT/$SRC/work"
# submodule trees are not in git archive; copy the ones the build uses
for sub in $(git -C "$WORK_DIR" config -f .gitmodules --get-regexp '\.path$' | awk '{print $2}'); do
  case "$sub" in thirdparty/freetype|thirdparty/libjpeg|thirdparty/lcms2|thirdparty/zlib|thirdparty/jbig2dec|thirdparty/openjpeg)
    rm -rf "$OUT/$SRC/work/mupdf/$sub"; mkdir -p "$OUT/$SRC/work/mupdf/$sub"
    git -C "$WORK_DIR/$sub" archive --format=tar HEAD | tar -x -C "$OUT/$SRC/work/mupdf/$sub" ;;
  esac
done
( cd "$OUT" && zip -q -r "$SRC.zip" "$SRC" && rm -rf "$SRC" )

echo "archives:"; ( cd "$OUT" && ls -l "$NAME.lha" "$SRC.zip" && shasum -a 256 "$NAME.lha" "$SRC.zip" )
echo; echo "contents:"; ( cd "$OUT" && "$LHA_WRITER" l "$NAME.lha" )
