#!/bin/sh
# Build and run the host-side page entry validation test.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -o build/host/pageinput_test tests/pageinput_test.c
build/host/pageinput_test
