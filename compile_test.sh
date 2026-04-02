#!/bin/bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build-linux"
BIN="$ROOT_DIR/tulpar"

echo "Building Tulpar..."
cmake --build "$BUILD_DIR" -j2 >/dev/null

if [[ ! -x "$BIN" ]]; then
  BIN="$BUILD_DIR/tulpar"
fi

if [[ ! -x "$BIN" ]]; then
  echo "Error: tulpar binary not found (checked $ROOT_DIR/tulpar and $BUILD_DIR/tulpar)"
  exit 1
fi

echo "Running compile sanity checks..."
timeout 5 "$BIN" "$ROOT_DIR/examples/01_hello_world.tpr" <<< $'John\n10\n' >/dev/null
timeout 5 "$BIN" "$ROOT_DIR/examples/05_strings.tpr" <<< $'John\n10\n' >/dev/null

echo "Compile test successful."
