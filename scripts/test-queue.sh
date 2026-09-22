#!/bin/sh
# Build and run the host-side render queue test.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Wno-unused-function -o build/host/queue_test tests/queue_test.c
build/host/queue_test
