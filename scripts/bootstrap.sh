#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Bootstrap: clean pinned upstream checkout + editable work copy with the
# full patch series applied, verified by CONTENT against the manifest.
#
#   scripts/bootstrap.sh              create or verify (in-sync: no-op)
#   scripts/bootstrap.sh --recreate   rebuild work/ after a pin/series change
#
# MuPDF keeps its dependencies as git submodules
# under thirdparty/. The work copy is duplicated from upstream/ with
# `cp -a` so those checkouts stay populated without a second network fetch.
#
# "In sync" means: the work copy's recorded base equals the manifest pin,
# AND its HEAD tree equals the reconstructed (pin + complete series) tree.
# Commit subjects are not proof of anything.
#
# Safety rules:
#   - A dirty upstream or a dirty work tree is never touched.
#   - --recreate never deletes the previous work copy: it is moved aside
#     with its full .git (including any stash) and reported.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"
# shellcheck source=workflow-lib.sh
source "$SCRIPT_DIR/workflow-lib.sh"

SERIES="$PATCHES_DIR/series"
MANIFEST="$PROJECT_ROOT/upstreams.json"
RECREATE=0
[ "${1:-}" = "--recreate" ] && RECREATE=1
[ $# -gt 1 ] && { echo "usage: bootstrap.sh [--recreate]" >&2; exit 1; }

[ -f "$MANIFEST" ] || { echo "bootstrap: missing $MANIFEST — create it first (see upstreams.example.json)" >&2; exit 1; }
[ -f "$SERIES" ] || { echo "bootstrap: missing $SERIES — an empty series file is required" >&2; exit 1; }

PINNED_URL="$(manifest_url)"
PINNED_COMMIT="$(manifest_pin)"
[ -n "$PINNED_COMMIT" ] || { echo "bootstrap: no pinned commit for '$REPO_ID' in upstreams.json" >&2; exit 1; }

# --- upstream/: clean checkout of the pinned commit, never edited ----------
if [ -d "$UPSTREAM_DIR/.git" ]; then
  if [ -n "$(git -C "$UPSTREAM_DIR" status --porcelain)" ]; then
    echo "bootstrap: $UPSTREAM_DIR is dirty — upstream must stay clean. Inspect manually." >&2
    exit 1
  fi
else
  echo "bootstrap: cloning $PINNED_URL into $UPSTREAM_DIR"
  mkdir -p "$(dirname "$UPSTREAM_DIR")"
  git clone "$PINNED_URL" "$UPSTREAM_DIR"
fi

if ! git -C "$UPSTREAM_DIR" fetch --all --quiet; then
  if git -C "$UPSTREAM_DIR" cat-file -e "${PINNED_COMMIT}^{commit}"; then
    echo "bootstrap: fetch failed; using already-present pin $PINNED_COMMIT"
  else
    echo "bootstrap: fetch failed and pin $PINNED_COMMIT is not in this clone" >&2
    exit 1
  fi
fi
git -C "$UPSTREAM_DIR" checkout --quiet --detach "$PINNED_COMMIT"
if [ -f "$UPSTREAM_DIR/.gitmodules" ]; then
  echo "bootstrap: populating nested submodules"
  git -C "$UPSTREAM_DIR" submodule update --init --recursive
fi
ACTUAL="$(git -C "$UPSTREAM_DIR" rev-parse HEAD)"
[ "$ACTUAL" = "$PINNED_COMMIT" ] || { echo "bootstrap: upstream HEAD $ACTUAL != manifest $PINNED_COMMIT" >&2; exit 1; }
echo "bootstrap: upstream $REPO_ID at $PINNED_COMMIT (clean)"

create_work() {
  echo "bootstrap: creating $WORK_DIR from $PINNED_COMMIT (+ $(series_entries | wc -l | tr -d ' ') patch(es))"
  mkdir -p "$(dirname "$WORK_DIR")"
  # Full copy so nested submodule working trees (thirdparty/*) remain
  # populated. git clone of the superproject alone leaves those empty.
  cp -a "$UPSTREAM_DIR" "$WORK_DIR"
  git -C "$WORK_DIR" checkout --quiet --detach "$PINNED_COMMIT"
  git -C "$WORK_DIR" remote remove origin >/dev/null 2>&1 || true
  git -C "$WORK_DIR" config project.base "$PINNED_COMMIT"
  series_entries | while IFS= read -r patch; do
    echo "bootstrap: applying $patch"
    apply_checkout_patch "$WORK_DIR" "$PATCHES_DIR/$patch"
    if ! git -C "$WORK_DIR" diff --cached --quiet; then
      git -C "$WORK_DIR" commit --quiet -m "$patch"
    fi
  done
}

# --- work/: verify or create ------------------------------------------------
if [ ! -d "$WORK_DIR/.git" ]; then
  create_work
  echo "bootstrap: work copy ready at $WORK_DIR"
  exit 0
fi

if work_tree_dirty; then
  echo "bootstrap: $WORK_DIR has uncommitted changes; refusing to touch it." >&2
  echo "  Save them with scripts/save-patch.sh, or git -C '$WORK_DIR' stash/reset by hand." >&2
  exit 1
fi

if ! REF_TREE="$(reference_tree "$PINNED_COMMIT")"; then
  echo "bootstrap: the series does not apply to pinned commit $PINNED_COMMIT — fix patches/series first" >&2
  exit 1
fi
WORK_TREE="$(work_head_tree)"
WORK_BASE="$(git -C "$WORK_DIR" config project.base || true)"

if [ -n "$WORK_BASE" ] && [ "$WORK_BASE" = "$PINNED_COMMIT" ] && [ "$WORK_TREE" = "$REF_TREE" ]; then
  echo "bootstrap: work copy in sync with pin $PINNED_COMMIT and series ($(series_entries | wc -l | tr -d ' ') patch(es)) — verified by content"
  exit 0
fi

echo "bootstrap: work copy does NOT match pin + series:" >&2
if [ -z "$WORK_BASE" ]; then
  echo "  work copy has no recorded base (created by an older tool?) — rebuild with --recreate" >&2
elif [ "$WORK_BASE" != "$PINNED_COMMIT" ] && [ "$WORK_TREE" = "$REF_TREE" ]; then
  echo "  pin changed ($WORK_BASE -> $PINNED_COMMIT) but the tree content is identical;" >&2
  echo "  the recorded base must be updated — re-run with --recreate" >&2
else
  echo "  work HEAD tree:    $WORK_TREE" >&2
  echo "  pin + series tree: $REF_TREE" >&2
  [ "$WORK_BASE" = "$PINNED_COMMIT" ] || echo "  hint: recorded base $WORK_BASE != manifest pin $PINNED_COMMIT (pin changed?)" >&2
  if ! diff <(work_chain 2>/dev/null) <(series_entries) >/dev/null 2>&1; then
    echo "  hint: local commit subjects differ from the series (patch added/removed/renamed?)" >&2
  fi
  echo "  A patch file may have been edited, or work/ has commits beyond the series." >&2
fi

if [ "$RECREATE" = 0 ]; then
  echo "bootstrap: refusing to proceed. After protecting unsaved work, re-run:" >&2
  echo "  scripts/bootstrap.sh --recreate" >&2
  exit 1
fi

BACKUP="$WORK_DIR.pre-recreate.$(date +%Y%m%d-%H%M%S)"
N=0
while [ -e "$BACKUP" ]; do
  N=$((N + 1))
  BACKUP="$WORK_DIR.pre-recreate.$(date +%Y%m%d-%H%M%S).$N"
done
echo "bootstrap: --recreate: preserving previous copy at $BACKUP"
mv "$WORK_DIR" "$BACKUP"
create_work
if [ "$(git -C "$WORK_DIR" rev-parse 'HEAD^{tree}')" = "$WORK_TREE" ]; then
  echo "bootstrap: rebuilt content is identical (pin-only change); previous copy kept at" >&2
else
  echo "bootstrap: rebuilt work differs from the preserved copy; previous copy kept at" >&2
fi
echo "  $BACKUP" >&2
echo "  Inspect and delete it manually once you have what you need from it." >&2
echo "bootstrap: work copy ready at $WORK_DIR"
