#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="$ROOT_DIR/output/open62541"
SRC_DIR="$BUILD_DIR/src"
INSTALL_DIR="$BUILD_DIR/install-local"

# shellcheck source=../pic.sh
source "$(dirname "${BASH_SOURCE[0]}")/../pic.sh"

mkdir -p "$BUILD_DIR"

if [[ -f "$INSTALL_DIR/lib/libopen62541.a" ]]; then
	if library_needs_pic_rebuild "$INSTALL_DIR/lib/libopen62541.a"; then
		echo "open62541 in $INSTALL_DIR is not position-independent; rebuilding"
		rm -rf "$BUILD_DIR/build" "$INSTALL_DIR"
	else
		echo "open62541 already installed into: $INSTALL_DIR"
		exit 0
	fi
fi

if [[ ! -d "$SRC_DIR/.git" ]]; then
	git clone --depth 1 --branch v1.4.10 https://github.com/open62541/open62541.git "$SRC_DIR"
else
	git -C "$SRC_DIR" fetch --depth 1 origin v1.4.10
	git -C "$SRC_DIR" checkout -f v1.4.10
fi

# Position-independent code even though this is a static library: it is linked into kmx-aio-test and
# the samples, which the toolchains here link as PIE by default. A static archive built without -fPIC
# carries absolute relocations that a PIE link cannot resolve ("relocation R_X86_64_32 ... can not be
# used when making a PIE object"). Every vendored dependency is built this way for the same reason.
cmake -S "$SRC_DIR" -B "$BUILD_DIR/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DBUILD_SHARED_LIBS=OFF \
	-DUA_ENABLE_ENCRYPTION=OPENSSL \
	-DUA_ENABLE_AMALGAMATION=OFF \
	-DUA_BUILD_EXAMPLES=OFF \
	-DUA_BUILD_TESTS=OFF \
	-DUA_BUILD_TOOLS=OFF

cmake --build "$BUILD_DIR/build" -j"$(nproc)"
cmake --install "$BUILD_DIR/build"

echo "open62541 installed into: $INSTALL_DIR"
