#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/4D-Plugin-SDK/4D Plugin API"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/pdfa-ownership.XXXXXX")"
trap 'status=$?; rm -f "$BUILD/sdk.o" "$BUILD/status-ownership"; rm -rf "$BUILD/status-ownership.dSYM"; rmdir "$BUILD"; exit "$status"' EXIT

# Compile the complete pinned SDK without changing or substituting its wrappers.
clang -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wno-int-conversion -Wno-multichar -I"$SDK" \
    -c "$SDK/4DPluginAPI.c" -o "$BUILD/sdk.o"
SOURCE_ARGS=()
if [ "$#" -gt 0 ]; then
    SOURCE_ARGS+=("-DPDFA_SOURCE=\"$1\"")
fi
clang++ -std=c++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$SDK" ${SOURCE_ARGS[@]+"${SOURCE_ARGS[@]}"} "$ROOT/tests/status_ownership.cpp" "$BUILD/sdk.o" \
    -framework CoreFoundation -framework CoreGraphics -framework Cocoa \
    -o "$BUILD/status-ownership"
"$BUILD/status-ownership"
