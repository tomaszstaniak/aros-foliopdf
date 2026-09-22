#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Reproduction check: apply the complete patch series to the pinned base in
# an isolated temporary directory, without touching upstream/ or work/.
#
#   scripts/check-reproduction.sh         clean up the temp dir on exit
#   scripts/check-reproduction.sh KEEP    keep the temp dir for inspection
#
# Limitation: clones from the local upstream/ checkout. It proves the series
# applies to the pin — it does not re-prove the manifest URL is fetchable.
# Populated submodule working trees are copied from upstream/ so nested
# diffs can apply.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"
# shellcheck source=workflow-lib.sh
source "$SCRIPT_DIR/workflow-lib.sh"

MANIFEST="$PROJECT_ROOT/upstreams.json"
SERIES="$PATCHES_DIR/series"

[ -d "$UPSTREAM_DIR/.git" ] || { echo "repro: no upstream checkout; run scripts/bootstrap.sh first" >&2; exit 1; }
[ -n "$(git -C "$UPSTREAM_DIR" status --porcelain)" ] && { echo "repro: upstream checkout is dirty" >&2; exit 1; }

PINNED_COMMIT="$(python3 -c '
import json, sys
with open(sys.argv[1]) as f:
    m = json.load(f)
print(m["repositories"][sys.argv[2]]["commit"])
' "$MANIFEST" "$REPO_ID")"

TMPDIR_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/aros-foliopdf-repro.XXXXXX")"
if [ "${1:-}" = "KEEP" ]; then
  echo "repro: temp dir is $TMPDIR_ROOT/repro (kept for inspection)"
else
  trap 'rm -rf "$TMPDIR_ROOT"' EXIT
fi

git clone --quiet --no-hardlinks "$UPSTREAM_DIR" "$TMPDIR_ROOT/repro"
git -C "$TMPDIR_ROOT/repro" checkout --quiet --detach "$PINNED_COMMIT"
# Nested checkouts are gitlinks. Copy populated trees from upstream/
# so thirdparty/* patches have files to apply to. The list comes from the
# pin's own .gitmodules, so a base update cannot leave it stale.
if [ -f "$UPSTREAM_DIR/.gitmodules" ]; then
  git -C "$UPSTREAM_DIR" config -f .gitmodules --get-regexp '\.path$' |
  while read -r _ sub; do
    if [ -e "$UPSTREAM_DIR/$sub/.git" ]; then
      rm -rf "$TMPDIR_ROOT/repro/$sub"
      mkdir -p "$(dirname "$TMPDIR_ROOT/repro/$sub")"
      cp -a "$UPSTREAM_DIR/$sub" "$TMPDIR_ROOT/repro/$sub"
    fi
  done
fi
echo "repro: cloned pinned base $PINNED_COMMIT into $TMPDIR_ROOT/repro"

COUNT=0
if [ -f "$SERIES" ]; then
  while IFS= read -r patch; do
    case "$patch" in ''|\#*) continue ;; esac
    COUNT=$((COUNT+1))
    apply_checkout_patch "$TMPDIR_ROOT/repro" "$PATCHES_DIR/$patch"
    if ! git -C "$TMPDIR_ROOT/repro" diff --cached --quiet; then
      git -C "$TMPDIR_ROOT/repro" commit --quiet -m "$patch"
    fi
    echo "repro: applied $patch"
  done < "$SERIES"
fi

echo "repro: PASS — series of $COUNT patch(es) applies cleanly to the pinned base"
