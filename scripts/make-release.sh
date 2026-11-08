#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Stage the release archives. Nothing here is uploaded: publication is a
# separate decision.
#
#   APKG_PACK=/path/to/apkg-pack.pyz scripts/make-release.sh [target...]
#
# Targets are the build targets of scripts/env.sh; default all three:
#   one       -> Folio-<ver>.x86_64-aros-v11.zip    folio.x86_64.v11.toml
#   mainline  -> Folio-<ver>.x86_64-aros-v1.zip     folio.x86_64.v1.toml
#   raspi     -> Folio-<ver>.aarch64-aros-raspi.zip folio.aarch64.v1.toml
# plus Folio-<ver>-source.zip (the repository at HEAD) and
# SHA256SUMS-<ver>, all in OUT (default dist/<ver>/).
#
# Each package description in packaging/catalogue/ is kept by hand; bump
# its version (or revision) before a release. It goes into the archive as
# .arospkg/manifest.toml, which apkg-pack submit reads. The version is the
# one in src/folio.c's $VER string; VER, when set, must agree with it.
#
# The executables are not built here. Each must come from
# scripts/build-folio.sh at the current HEAD with no local changes in src/,
# as its build log records, and the tracked files must be committed, so the
# source archive is the source of every binary.
#
# APKG_PACK is apkg-pack.pyz from arospkg, or an apkg-pack command; default
# apkg-pack on PATH. Optional: LHA_WRITER, a create-capable classic lha
# (LHa for UNIX 1.14i or similar), to make the same contents as an .lha as
# well; Homebrew's Lhasa only extracts.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC_VER="$(sed -n 's/.*"\$VER: Folio \([0-9][0-9.]*\) (.*/\1/p' "$ROOT/src/folio.c" | head -1)"
[ -n "$SRC_VER" ] || { echo "make-release: no \$VER string in src/folio.c" >&2; exit 1; }
VER="${VER:-$SRC_VER}"
[ "$VER" = "$SRC_VER" ] || { echo "make-release: VER=$VER but src/folio.c says $SRC_VER" >&2; exit 1; }
for s in "Folio $VER log" "bFolio $VER"; do
  grep -q "$s" "$ROOT/src/folio.c" || { echo "make-release: src/folio.c lacks \"$s\"" >&2; exit 1; }
done
grep -q "^## $VER (" "$ROOT/CHANGELOG.md" || { echo "make-release: CHANGELOG.md has no \"## $VER (\" section" >&2; exit 1; }

APKG_PACK="${APKG_PACK:-apkg-pack}"
apkg_pack() {
  case "$APKG_PACK" in
    *.pyz) python3 "$APKG_PACK" "$@" ;;
    *) "$APKG_PACK" "$@" ;;
  esac
}
apkg_pack --version >/dev/null 2>&1 || { echo "make-release: set APKG_PACK to apkg-pack.pyz (see arospkg docs/guide/authoring.md)" >&2; exit 1; }
if [ -n "${LHA_WRITER:-}" ]; then
  "$LHA_WRITER" --help 2>&1 | grep -q "a   Add" || { echo "make-release: $LHA_WRITER cannot create archives" >&2; exit 1; }
fi
[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ] ||
  { echo "make-release: commit the tracked changes first; the source archive is git HEAD" >&2; exit 1; }
HEAD_SHORT="$(git -C "$ROOT" rev-parse --short HEAD)"

OUT="${OUT:-$ROOT/dist/$VER}"
mkdir -p "$OUT"
TARGETS=("$@")
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=(one mainline raspi)

for TARGET in "${TARGETS[@]}"; do
  case "$TARGET" in
    one)      NAME="Folio-$VER.x86_64-aros-v11";    MANIFEST=folio.x86_64.v11.toml; STRIP=yes ;;
    mainline) NAME="Folio-$VER.x86_64-aros-v1";     MANIFEST=folio.x86_64.v1.toml;  STRIP=yes ;;
    # Unstripped, as in 0.5: --strip-unneeded is proven safe for AROS
    # x86_64 executables only.
    raspi)    NAME="Folio-$VER.aarch64-aros-raspi"; MANIFEST=folio.aarch64.v1.toml; STRIP=no ;;
    *) echo "make-release: unknown target $TARGET (one|mainline|raspi)" >&2; exit 1 ;;
  esac
  MANIFEST="$ROOT/packaging/catalogue/$MANIFEST"
  # A subshell per target: env.sh sets the toolchain for AROS_TARGET.
  (
  export AROS_TARGET="$TARGET"
  # shellcheck source=env.sh
  source "$SCRIPT_DIR/env.sh"
  # The description is not rewritten to match: a stale version would
  # publish this build under the last release's name.
  grep -q "^version *= \"$VER\"$" "$MANIFEST" || { echo "make-release: $MANIFEST does not say version \"$VER\"" >&2; exit 1; }
  BIN="$BUILD_DIR/Folio"
  LOG="$BUILD_DIR/folio-build.log"
  [ -f "$BIN" ] && [ -f "$LOG" ] || { echo "make-release: build $TARGET first (scripts/build-folio.sh)" >&2; exit 1; }
  # The log is written before the link, the executable by it: an older
  # executable is left over from an earlier build.
  grep -q "^build-folio: source $HEAD_SHORT, dirty=0$" "$LOG" && [ "$BIN" -nt "$LOG" ] ||
    { echo "make-release: $BIN was not built from a clean $HEAD_SHORT (see $LOG)" >&2; exit 1; }

  STAGE="$OUT/$NAME"
  rm -rf "$STAGE"; mkdir -p "$STAGE/Folio/licenses" "$STAGE/.arospkg"
  if [ "$STRIP" = yes ]; then
    # --strip-unneeded only: a full strip breaks AROS x86_64 relocations.
    "$AROS_STRIP" --strip-unneeded --remove-section .comment -o "$STAGE/Folio/Folio" "$BIN"
  else
    cp "$BIN" "$STAGE/Folio/Folio"
  fi
  cp "$ROOT/packaging/Folio.info" "$STAGE/Folio/Folio.info"
  cp "$ROOT/packaging/Folio.info" "$STAGE/Folio.info"      # drawer icon, beside the drawer
  cp "$ROOT/packaging/README" "$STAGE/Folio/README"
  cp "$ROOT/CHANGELOG.md" "$STAGE/Folio/CHANGELOG"
  cp "$ROOT/LICENSE" "$STAGE/Folio/LICENSE"
  cp "$ROOT/packaging/licenses/"* "$STAGE/Folio/licenses/"
  # apkg-pack reads the description beside the drawer and embeds it as
  # .arospkg/manifest.toml; the staged copy is only for SHA256SUMS and the .lha.
  cp "$MANIFEST" "$STAGE/Folio.arospkg.toml"
  cp "$MANIFEST" "$STAGE/.arospkg/manifest.toml"

  ( cd "$STAGE" && find . -type f ! -name SHA256SUMS ! -name Folio.arospkg.toml | sed 's|^\./||' | sort |
    while read -r f; do shasum -a 256 "$f"; done > Folio/SHA256SUMS )

  # The archive holds the drawer, its icon and .arospkg at the root, not an
  # extra wrapper directory. apkg-pack checks the description against the
  # drawer (CPU, icon stack for [launch]) before writing and the archive after.
  rm -f "$OUT/$NAME.zip" "$OUT/$NAME.lha"
  apkg_pack check "$STAGE/Folio"
  apkg_pack build "$STAGE/Folio" --output "$OUT/$NAME.zip"
  apkg_pack check "$OUT/$NAME.zip"
  # Packing must not change a byte of what was staged.
  python3 - "$STAGE" "$OUT/$NAME.zip" <<'PY'
import sys, zipfile
from pathlib import Path
stage, z = Path(sys.argv[1]), zipfile.ZipFile(sys.argv[2])
# apkg-pack writes Folio/application.json from the description's [launch]
# table (arospkg 0.5); it is the one file that has no staged copy.
generated = {"Folio/application.json"}
files = [n for n in z.namelist() if not n.endswith("/") and n not in generated]
bad = [n for n in files if z.read(n) != (stage / n).read_bytes()]
staged = {p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()} - {"Folio.arospkg.toml"}
if bad or staged != set(files):
    sys.exit(f"make-release: archive differs from the stage: {bad or sorted(staged ^ set(files))}")
print(f"make-release: {len(files)} files in {Path(sys.argv[2]).name}, byte-identical to the stage")
PY
  if [ -n "${LHA_WRITER:-}" ]; then
    # lh5, header level 1 (-o5 is rejected by this writer for level 2
    # archives with long names). Same files, without application.json.
    ( cd "$STAGE" && "$LHA_WRITER" aq "$OUT/$NAME.lha" Folio Folio.info .arospkg )
  fi
  echo "make-release: $TARGET executable $(shasum -a 256 "$STAGE/Folio/Folio" | cut -d' ' -f1)"
  )
done

# Source: this repository at HEAD. It pins MuPDF by commit in upstreams.json
# and carries the patches, so scripts/bootstrap.sh reconstructs the exact
# tree that was built; the MuPDF sources themselves are not duplicated here.
SRC="Folio-$VER-source"
rm -rf "${OUT:?}/$SRC" "$OUT/$SRC.zip"; mkdir -p "$OUT/$SRC"
git -C "$ROOT" archive --format=tar HEAD | tar -x -C "$OUT/$SRC"
( cd "$OUT" && zip -q -X -r "$SRC.zip" "$SRC" && rm -rf "$SRC" )

( cd "$OUT" && find . -maxdepth 1 -type f \( -name 'Folio-*.zip' -o -name 'Folio-*.lha' \) | sed 's|^\./||' | sort |
  while read -r f; do shasum -a 256 "$f"; done > "SHA256SUMS-$VER" )
echo "make-release: source $HEAD_SHORT"
echo "archives in $OUT:"; ( cd "$OUT" && cat "SHA256SUMS-$VER" && find . -maxdepth 1 -type f -name 'Folio-*' -exec ls -l {} + )
echo; echo "Upload them, then for each package: apkg-pack submit <its https URL> --pr"
