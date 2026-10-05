#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Stage the release archives. Nothing here is uploaded: publication is a
# separate decision.
#
#   APKG_PACK=/path/to/apkg-pack.pyz scripts/make-release.sh
#       -> dist/Folio-<ver>.x86_64-aros-v11.zip   (the AROS package, made and
#                                                  checked by apkg-pack)
#          dist/Folio-<ver>-source.zip            (corresponding source)
#
# The package description is packaging/catalogue/folio.x86_64.v11.toml,
# kept by hand; bump its version (or revision) before a release. It goes
# into the archive as .arospkg/manifest.toml, which apkg-pack submit reads.
#
# APKG_PACK is apkg-pack.pyz from arospkg, or an apkg-pack command; default
# apkg-pack on PATH. Optional: OUT (default dist/), and LHA_WRITER, a
# create-capable classic lha (LHa for UNIX 1.14i or similar), to make the
# same contents as an .lha as well; Homebrew's Lhasa only extracts.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"

VER="${VER:-0.4.1}"
MANIFEST="$PROJECT_ROOT/packaging/catalogue/folio.x86_64.v11.toml"
APKG_PACK="${APKG_PACK:-apkg-pack}"
apkg_pack() {
  case "$APKG_PACK" in
    *.pyz) python3 "$APKG_PACK" "$@" ;;
    *) "$APKG_PACK" "$@" ;;
  esac
}
apkg_pack --version >/dev/null 2>&1 || { echo "make-release: set APKG_PACK to apkg-pack.pyz (see arospkg docs/guide/authoring.md)" >&2; exit 1; }
# The description is not rewritten to match: a stale version would publish
# this build under the last release's name.
grep -q "^version *= \"$VER\"$" "$MANIFEST" || { echo "make-release: $MANIFEST does not say version \"$VER\"" >&2; exit 1; }
if [ -n "${LHA_WRITER:-}" ]; then
  "$LHA_WRITER" --help 2>&1 | grep -q "a   Add" || { echo "make-release: $LHA_WRITER cannot create archives" >&2; exit 1; }
fi
[ "$AROS_TARGET" = one ] || { echo "make-release: this release is ABIv11 only (AROS_TARGET=one)" >&2; exit 1; }
BIN="$BUILD_DIR/Folio"
[ -f "$BIN" ] || { echo "make-release: build first (scripts/build-folio.sh)" >&2; exit 1; }

OUT="${OUT:-$PROJECT_ROOT/dist}"
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
# apkg-pack reads the description beside the drawer and embeds it as
# .arospkg/manifest.toml; the staged copy is only for SHA256SUMS and the .lha.
cp "$MANIFEST" "$STAGE/Folio.arospkg.toml"
cp "$MANIFEST" "$STAGE/.arospkg/manifest.toml"

( cd "$STAGE" && find . -type f ! -name SHA256SUMS ! -name Folio.arospkg.toml | sed 's|^\./||' | sort |
  while read -r f; do shasum -a 256 "$f"; done > Folio/SHA256SUMS )

# The archive holds the drawer, its icon and .arospkg at the root, not an
# extra wrapper directory. apkg-pack checks the description against the
# drawer before writing and the archive after.
rm -f "$OUT/$NAME.zip"
apkg_pack check "$STAGE/Folio"
apkg_pack build "$STAGE/Folio" --output "$OUT/$NAME.zip"
apkg_pack check "$OUT/$NAME.zip"
# Packing must not change a byte of what was staged.
python3 - "$STAGE" "$OUT/$NAME.zip" <<'PY'
import sys, zipfile
from pathlib import Path
stage, z = Path(sys.argv[1]), zipfile.ZipFile(sys.argv[2])
files = [n for n in z.namelist() if not n.endswith("/")]
bad = [n for n in files if z.read(n) != (stage / n).read_bytes()]
staged = {p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()} - {"Folio.arospkg.toml"}
if bad or staged != set(files):
    sys.exit(f"make-release: archive differs from the stage: {bad or sorted(staged ^ set(files))}")
print(f"make-release: {len(files)} files in the archive, byte-identical to the stage")
PY
if [ -n "${LHA_WRITER:-}" ]; then
  # lh5, header level 1 (-o5 is rejected by this writer for level 2 archives
  # with long names).
  ( cd "$STAGE" && rm -f "$OUT/$NAME.lha" && "$LHA_WRITER" aq "$OUT/$NAME.lha" Folio Folio.info .arospkg )
fi

# Source: this repository at HEAD. It pins MuPDF by commit in upstreams.json
# and carries the patches, so scripts/bootstrap.sh reconstructs the exact
# tree that was built; the MuPDF sources themselves are not duplicated here.
SRC="Folio-$VER-source"
rm -rf "$OUT/$SRC" "$OUT/$SRC.zip"; mkdir -p "$OUT/$SRC"
git -C "$PROJECT_ROOT" archive --format=tar HEAD | tar -x -C "$OUT/$SRC"
( cd "$OUT" && zip -q -r "$SRC.zip" "$SRC" && rm -rf "$SRC" )

echo "archives:"; ( cd "$OUT" && ls -l "$NAME".* "$SRC.zip" && shasum -a 256 "$NAME".* "$SRC.zip" )
echo; echo "contents:"; unzip -l "$OUT/$NAME.zip"
echo; echo "Upload $NAME.zip, then: apkg-pack submit <its https URL> --pr"
