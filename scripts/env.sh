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

# The target is taken from the environment, else from local.env, else `one`.
# Then `local.<target>.env` (optional, per-target toolchain and SDK) is read
# before `local.env`, so its values win over local.env's while the
# environment still wins over both. The ignored local.env.example documents
# the keys.
if [ -z "${AROS_TARGET:-}" ] && [ -f "$PROJECT_ROOT/local.env" ]; then
  AROS_TARGET="$(sed -n 's/^[[:space:]]*AROS_TARGET[[:space:]]*=[[:space:]]*["'\'']\{0,1\}\([a-z0-9_-]*\).*/\1/p' "$PROJECT_ROOT/local.env" | head -1)"
fi
AROS_TARGET="${AROS_TARGET:-one}"
export AROS_TARGET

for _local_env in "$PROJECT_ROOT/local.$AROS_TARGET.env" "$PROJECT_ROOT/local.env"; do
  [ -f "$_local_env" ] || continue
  eval "$(python3 - "$_local_env" <<'PY'
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
done
unset _local_env

# --- AROS target selection -------------------------------------------------
# one | mainline | raspi. Default is ABIv11 / AROS One. Set the toolchain
# paths in local.env (see local.env.example); the defaults below are only a
# guess. The target names select directories and defaults:
#   one       x86_64 ABIv11 (AROS One), GCC, build/one
#   mainline  x86_64 ABIv1 (mainline nightlies), GCC, build/mainline
#   raspi     aarch64 ABIv1 (Raspberry Pi nightlies), Clang wrapper, build/raspi
# The three ABIs never share objects or a binary.
# AROS_SYSROOT, when set, is passed to the compiler as --sysroot so the
# build uses exactly that SDK rather than the compiler's built-in default.
# Target `one` keeps the compiler default unless local.env sets it (the
# builds up to 0.5 used the default, see docs/development.md).
case "$AROS_TARGET" in
  mainline)
    AROS_GCC_ROOT="${AROS_GCC_ROOT:-/opt/aros/mainline/bin}"
    AROS_SDK="${AROS_SDK:-/opt/aros/mainline/Developer}"
    AROS_SYSROOT="${AROS_SYSROOT:-$AROS_SDK}"
    AROS_TOOL_PREFIX="${AROS_TOOL_PREFIX:-x86_64-aros-}"
    AROS_CC="${AROS_CC:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}gcc}"
    AROS_CXX="${AROS_CXX:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}g++}"
    ;;
  raspi)
    AROS_GCC_ROOT="${AROS_GCC_ROOT:-/opt/aros/raspi/bin}"
    AROS_SDK="${AROS_SDK:-/opt/aros/raspi/Developer}"
    AROS_SYSROOT="${AROS_SYSROOT:-$AROS_SDK}"
    AROS_TOOL_PREFIX="${AROS_TOOL_PREFIX:-aarch64-aros-}"
    AROS_CC="${AROS_CC:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}clang}"
    AROS_CXX="${AROS_CXX:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}clang++}"
    ;;
  one)
    AROS_GCC_ROOT="${AROS_GCC_ROOT:-/opt/aros/abiv11/bin}"
    AROS_SDK="${AROS_SDK:-/opt/aros/abiv11/Developer}"
    AROS_SYSROOT="${AROS_SYSROOT:-}"
    AROS_TOOL_PREFIX="${AROS_TOOL_PREFIX:-x86_64-aros-}"
    AROS_CC="${AROS_CC:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}gcc}"
    AROS_CXX="${AROS_CXX:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}g++}"
    ;;
  *)
    echo "scripts/env.sh: unknown AROS_TARGET '$AROS_TARGET' (mainline|one|raspi)" >&2
    return 1 2>/dev/null || exit 1
    ;;
esac

AROS_AR="${AROS_AR:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}ar}"
AROS_STRIP="${AROS_STRIP:-$AROS_GCC_ROOT/${AROS_TOOL_PREFIX}strip}"
# The compiler command with its sysroot, for make variables and links.
AROS_CC_CMD="$AROS_CC${AROS_SYSROOT:+ --sysroot=$AROS_SYSROOT}"
AROS_CXX_CMD="$AROS_CXX${AROS_SYSROOT:+ --sysroot=$AROS_SYSROOT}"

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
