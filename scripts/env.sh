# SPDX-License-Identifier: AGPL-3.0-or-later
# Single place for machine-local paths and upstream pins. Override with
# environment variables; optional ignored local.env; scripts must not
# hardcode paths elsewhere.
#
# Precedence: existing environment, then local.env, then the defaults below.
# Relative values in local.env resolve against the directory that contains it.
# Supported shell: bash.

# --- project root and local.env --------------------------------------------
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -f "$PROJECT_ROOT/local.env" ]; then
  eval "$(python3 - "$PROJECT_ROOT/local.env" <<'PY'
import os, shlex, sys
from pathlib import Path
path = Path(sys.argv[1])
base = path.parent
for raw in path.read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, val = line.split("=", 1)
    key = key.strip()
    val = val.strip()
    if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
        val = val[1:-1]
    if not key or key in os.environ:
        continue
    # Only path-like values are resolved; a bare word such as AROS_TARGET=one
    # is a setting, not a file.
    candidate = Path(val)
    if val and ("/" in val or val.startswith(".")) and not candidate.is_absolute():
        val = str((base / candidate).resolve())
    print(f"export {key}={shlex.quote(val)}")
PY
)"
fi

# --- AROS target selection -------------------------------------------------
# one | mainline. Default is ABIv11 / AROS One. Set the toolchain paths in
# local.env (see local.env.example); the defaults below are only a guess.
AROS_TARGET="${AROS_TARGET:-one}"

case "$AROS_TARGET" in
  mainline)
    AROS_GCC_ROOT="${AROS_GCC_ROOT:-/opt/aros/mainline/bin}"
    AROS_SDK="${AROS_SDK:-/opt/aros/mainline/Developer}"
    ;;
  one)
    AROS_GCC_ROOT="${AROS_GCC_ROOT:-/opt/aros/abiv11/bin}"
    AROS_SDK="${AROS_SDK:-/opt/aros/abiv11/Developer}"
    ;;
  *)
    echo "scripts/env.sh: unknown AROS_TARGET '$AROS_TARGET' (mainline|one)" >&2
    return 1 2>/dev/null || exit 1
    ;;
esac

AROS_CC="${AROS_CC:-$AROS_GCC_ROOT/x86_64-aros-gcc}"
AROS_CXX="${AROS_CXX:-$AROS_GCC_ROOT/x86_64-aros-g++}"
AROS_AR="${AROS_AR:-$AROS_GCC_ROOT/x86_64-aros-ar}"
AROS_STRIP="${AROS_STRIP:-$AROS_GCC_ROOT/x86_64-aros-strip}"

# Some AROS cross toolchains keep the linker path hardcoded to the build
# tree they were made in; if yours does, that tree must be present.
AROS_BUILD_VOLUME="${AROS_BUILD_VOLUME:-$AROS_GCC_ROOT}"

# --- Upstream pin ----------------------------------------------------------
# URL and commit come from upstreams.json; REPO_ID selects the entry.
REPO_ID="${REPO_ID:-mupdf}"

# --- Project layout --------------------------------------------------------
UPSTREAM_DIR="$PROJECT_ROOT/upstream/$REPO_ID"
WORK_DIR="$PROJECT_ROOT/work/$REPO_ID"
PATCHES_DIR="$PROJECT_ROOT/patches/$REPO_ID"
BUILD_DIR="$PROJECT_ROOT/build/$AROS_TARGET"
