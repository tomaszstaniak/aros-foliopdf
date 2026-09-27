#!/bin/sh
# Build and run the host-side page column geometry test.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Wno-unused-function -o build/host/layout_test tests/layout_test.c
build/host/layout_test
