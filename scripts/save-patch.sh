#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Save selected changes from work/ as a numbered patch with the required
# header, and append it to the series. A patch is a working file until
# committed to the project repository.
#
# Usage:
#   scripts/save-patch.sh <NNNN-short-name> \
#     --problem "..." --solution "..." --scope "..." \
#     [--status candidate|local-only|submitted|accepted]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "$SCRIPT_DIR/env.sh"
# shellcheck source=workflow-lib.sh
source "$SCRIPT_DIR/workflow-lib.sh"

SERIES="$PATCHES_DIR/series"
STATUS="candidate"
PROBLEM="" SOLUTION="" SCOPE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --status)   STATUS="$2"; shift 2 ;;
    --problem)  PROBLEM="$2"; shift 2 ;;
    --solution) SOLUTION="$2"; shift 2 ;;
    --scope)    SCOPE="$2"; shift 2 ;;
    -*) echo "save-patch: unknown option $1" >&2; exit 1 ;;
    *)  NAME="$1"; shift ;;
  esac
done
[ -n "${NAME:-}" ] || { echo "usage: save-patch.sh <NNNN-short-name> --problem ... --solution ... --scope ..." >&2; exit 1; }
[ -n "$PROBLEM" ] && [ -n "$SOLUTION" ] && [ -n "$SCOPE" ] || { echo "save-patch: --problem, --solution and --scope are required (the why is always required)" >&2; exit 1; }

[ -d "$WORK_DIR/.git" ] || { echo "save-patch: no work copy; run scripts/bootstrap.sh first" >&2; exit 1; }
[ -f "$SERIES" ] || touch "$SERIES"

if grep -qx "$NAME.patch" "$SERIES"; then
  echo "save-patch: $NAME.patch already exists in series; saving over an existing patch requires removing it from series first" >&2
  exit 1
fi
[ -f "$PATCHES_DIR/$NAME.patch" ] && { echo "save-patch: $PATCHES_DIR/$NAME.patch exists but is not in series (leftover from an interrupted run?); resolve it first" >&2; exit 1; }

PINNED_COMMIT="$(manifest_pin)"
[ -n "$PINNED_COMMIT" ] || { echo "save-patch: no pinned commit for '$REPO_ID' in upstreams.json" >&2; exit 1; }
WORK_BASE="$(git -C "$WORK_DIR" config project.base || true)"
[ "$WORK_BASE" = "$PINNED_COMMIT" ] || {
  echo "save-patch: work base ($WORK_BASE) != manifest pin ($PINNED_COMMIT)." >&2
  echo "  Reconcile first (scripts/bootstrap.sh, then --recreate if it reports divergence)," >&2
  echo "  then re-run save-patch. Refusing: the diff would not reproduce from the new pin." >&2
  exit 1
}
if ! REF_TREE="$(reference_tree "$PINNED_COMMIT")"; then
  echo "save-patch: cannot reconstruct pin+series at $PINNED_COMMIT — the series may not apply to the pin, or upstream/ has not been fetched at it (run scripts/bootstrap.sh first)" >&2
  exit 1
fi
WORK_TREE="$(work_head_tree)"
[ "$WORK_TREE" = "$REF_TREE" ] || {
  echo "save-patch: work HEAD tree $WORK_TREE != pin+series tree $REF_TREE." >&2
  echo "  The base is stale (amended commit, edited patch, diverged series)." >&2
  echo "  Reconcile with scripts/bootstrap.sh first; refusing to save an unreproducible diff." >&2
  exit 1
}

STAGED="$(git -C "$WORK_DIR" diff --cached)"
[ -n "$STAGED" ] || { echo "save-patch: nothing staged in $WORK_DIR — git -C work/$REPO_ID add your changes first" >&2; exit 1; }

REQUIRES="$(series_entries | tail -n 1 || true)"
[ -n "$REQUIRES" ] || REQUIRES="none"

DATE="$(date +%F)"
PATCH_FILE="$PATCHES_DIR/$NAME.patch"

PATCH_TMP="$(mktemp "${TMPDIR:-/tmp}/aros-foliopdf-patch.XXXXXX")"
{
  echo "Title: $NAME"
  echo "Repository: $REPO_ID"
  echo "Base-Commit: $PINNED_COMMIT"
  echo "Requires: $REQUIRES"
  echo "Author: aros-foliopdf project"
  echo "Date: $DATE"
  echo "Upstream-Status: $STATUS"
  echo "Scope: $SCOPE"
  echo
  echo "Problem:"
  echo "  $PROBLEM"
  echo "Solution:"
  echo "  $SOLUTION"
  echo "Validation:"
  echo "  not-run"
  echo "Removal:"
  echo "  Fill in when this is a workaround; permanent fixes say so here."
  echo
  git -C "$WORK_DIR" diff --cached --binary
} > "$PATCH_TMP"

if ! cp "$PATCH_TMP" "$PATCH_FILE"; then
  rm -f "$PATCH_TMP"
  echo "save-patch: cannot write $PATCH_FILE" >&2
  exit 1
fi
rm -f "$PATCH_TMP"

if ! printf '%s\n' "$NAME.patch" >> "$SERIES"; then
  rm -f "$PATCH_FILE"
  echo "save-patch: cannot update $SERIES — rolled back, work/ untouched" >&2
  exit 1
fi

if ! git -C "$WORK_DIR" commit --quiet -m "$NAME.patch"; then
  series_drop_last
  rm -f "$PATCH_FILE"
  echo "save-patch: committing the baseline in work/ failed — patch and series entry rolled back" >&2
  exit 1
fi

echo "save-patch: wrote $PATCH_FILE (base $PINNED_COMMIT, requires $REQUIRES) and appended it to series"
echo "save-patch: commit the patch and series to the project repository to make them durable"
