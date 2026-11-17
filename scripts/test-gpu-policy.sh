#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Werror -o build/host/gpu-policy tests/gpu-policy.c
build/host/gpu-policy
printf 'gpu-policy: passed\n'
cc -std=c99 -Wall -Wextra -Werror -o build/host/gpu-tiles tests/gpu-tiles.c
build/host/gpu-tiles
printf 'gpu-tiles: passed\n'
