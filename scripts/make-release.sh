#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Stage the release archives. Nothing here is uploaded: publication is a
# separate decision.
#
#   scripts/make-release.sh            -> dist/Folio-<ver>.x86_64-aros-v11.zip
#                                         dist/Folio-<ver>-source.zip
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

VER="${VER:-0.1}"
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
( cd "$OUT" && rm -f "$NAME.zip" && zip -q -r "$NAME.zip" "$NAME" )

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

echo "archives:"; ( cd "$OUT" && ls -l "$NAME.zip" "$SRC.zip" && shasum -a 256 "$NAME.zip" "$SRC.zip" )
echo; echo "contents:"; ( cd "$OUT" && unzip -l "$NAME.zip" | sed -n '4,$p' )
