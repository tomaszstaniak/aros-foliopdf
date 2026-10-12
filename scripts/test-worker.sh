#!/bin/sh
# Build and run the host-side render worker test, plainly and under
# ThreadSanitizer (skipped where the compiler cannot build with it).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-function -pthread \
   -o build/host/worker_test tests/worker_test.c
build/host/worker_test
if cc -std=c99 -D_POSIX_C_SOURCE=200809L -g -fsanitize=thread -Wno-unused-function -pthread \
      -o build/host/worker_test_tsan tests/worker_test.c 2>/dev/null; then
    build/host/worker_test_tsan
else
    echo "test-worker: ThreadSanitizer not available, skipped"
fi
