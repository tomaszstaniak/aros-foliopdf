#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Werror -o build/host/renderpolicy_test tests/renderpolicy_test.c
build/host/renderpolicy_test
python3 tests/test-render-tick.py
