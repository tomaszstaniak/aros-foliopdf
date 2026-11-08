#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Host test of src/prefs.h (the preferences file format).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Werror -o build/host/prefs_test tests/prefs_test.c
build/host/prefs_test
