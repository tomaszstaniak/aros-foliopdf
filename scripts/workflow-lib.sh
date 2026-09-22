# SPDX-License-Identifier: AGPL-3.0-or-later
# Shared functions for the patch-workflow scripts (bootstrap, save-patch).
# Sourced, not executed; expects env.sh to be sourced first and the work
# copy to exist where a function requires it.
#
# The in-sync
# definition used everywhere: work HEAD tree == reference tree
# reconstructed from the manifest pin + complete series. Commit subjects and
# the recorded base are provenance hints for diagnostics, never proof of
# consistency.

manifest_pin() {
  python3 -c '
import json, sys
with open(sys.argv[1]) as f:
    m = json.load(f)
print(m["repositories"][sys.argv[2]]["commit"])
' "$PROJECT_ROOT/upstreams.json" "$REPO_ID"
}

manifest_url() {
  python3 -c '
import json, sys
with open(sys.argv[1]) as f:
    m = json.load(f)
print(m["repositories"][sys.argv[2]]["url"])
' "$PROJECT_ROOT/upstreams.json" "$REPO_ID"
}

series_entries() {
  grep -ve '^[[:space:]]*\#' "$PATCHES_DIR/series" 2>/dev/null | sed -e '/^[[:space:]]*$/d' || true
}

reference_tree() {
  local pin="$1" idx out
  idx="$(mktemp "${TMPDIR:-/tmp}/aros-foliopdf-ref.XXXXXX")"
  out="$(
    export GIT_DIR="$UPSTREAM_DIR/.git" GIT_INDEX_FILE="$idx"
    git read-tree "$pin" || exit 1
    while IFS= read -r patch; do
      # Nested-checkout diffs cannot enter the superproject index.
      if git apply --cached --check "$PATCHES_DIR/$patch" >/dev/null 2>&1; then
        git apply --cached "$PATCHES_DIR/$patch" || exit 1
      fi
    done < <(series_entries)
    git write-tree
  )"
  local rc=$?
  rm -f "$idx"
  printf '%s\n' "$out"
  return "$rc"
}

work_head_tree() {
  git -C "$WORK_DIR" rev-parse 'HEAD^{tree}'
}

work_tree_dirty() {
  [ -n "$(git -C "$WORK_DIR" status --porcelain)" ]
}

work_chain() {
  local base
  base="$(git -C "$WORK_DIR" config project.base || true)"
  [ -n "$base" ] || return 1
  git -C "$WORK_DIR" log --format=%s "$base..HEAD" | tail -r
}

# Unified diff body of a project patch file (skips the required header).
patch_unified_diff() {
  python3 -c '
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
i = text.find("diff --git ")
if i < 0:
    sys.exit("apply-patch: no diff --git in " + sys.argv[1])
sys.stdout.write(text[i:])
' "$1"
}

# Apply one series patch to a checkout. Superproject paths use git apply
# --index. Files inside nested checkouts (gitlinks: MuPDF's thirdparty/*)
# are not in the superproject index, so those patches fall back to patch(1)
# on the working tree. Nested apply does not change the superproject tree.
apply_checkout_patch() {
  local dest="$1" patch="$2"
  [ -f "$patch" ] || { echo "apply-patch: missing $patch" >&2; return 1; }
  if git -C "$dest" apply --index --check "$patch" >/dev/null 2>&1; then
    git -C "$dest" apply --index "$patch"
    return 0
  fi
  patch_unified_diff "$patch" | (cd "$dest" && patch -p1)
}

series_drop_last() {
  local tmp
  tmp="$(mktemp "${TMPDIR:-/tmp}/aros-foliopdf-series.XXXXXX")"
  cp "$PATCHES_DIR/series" "$tmp"
  python3 - "$tmp" <<'PYEOF'
import sys
lines = open(sys.argv[1]).readlines()
for i in range(len(lines) - 1, -1, -1):
    if lines[i].strip() and not lines[i].lstrip().startswith("#"):
        del lines[i]
        break
open(sys.argv[1], "w").writelines(lines)
PYEOF
  cat "$tmp" > "$PATCHES_DIR/series"
  rm -f "$tmp"
}
