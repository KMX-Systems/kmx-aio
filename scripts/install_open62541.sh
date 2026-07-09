#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build/open62541"
SRC_DIR="$BUILD_DIR/src"
INSTALL_DIR="$BUILD_DIR/install-local"

mkdir -p "$BUILD_DIR"

if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --depth 1 --branch v1.4.10 https://github.com/open62541/open62541.git "$SRC_DIR"
else
    git -C "$SRC_DIR" fetch --depth 1 origin v1.4.10
    git -C "$SRC_DIR" checkout -f v1.4.10
fi

cmake -S "$SRC_DIR" -B "$BUILD_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DUA_ENABLE_ENCRYPTION=OPENSSL \
    -DUA_ENABLE_AMALGAMATION=OFF \
    -DUA_BUILD_EXAMPLES=OFF \
    -DUA_BUILD_TESTS=OFF \
    -DUA_BUILD_TOOLS=OFF

cmake --build "$BUILD_DIR/build" -j"$(nproc)"
cmake --install "$BUILD_DIR/build"

echo "open62541 installed into: $INSTALL_DIR"
