#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="$ROOT_DIR/output/open62541"
SRC_DIR="$BUILD_DIR/src"
INSTALL_DIR="$BUILD_DIR/install-local"

# shellcheck source=../pic.sh
source "$(dirname "${BASH_SOURCE[0]}")/../pic.sh"

mkdir -p "$BUILD_DIR"

# Reports whether the installed archive holds GCC LTO objects rather than machine code. See the comment
# on -DCMAKE_INTERPROCEDURAL_OPTIMIZATION below for why such an archive has to be thrown away; the check
# is here for the same reason library_needs_pic_rebuild is - a tree built before the flag was set looks
# complete and would otherwise be kept forever.
# @param 1 Path to the static archive to inspect.
# @return 0 when any member carries .gnu.lto_* sections, 1 otherwise or when the file cannot be read.
library_needs_lto_rebuild() {
	local library="$1"

	if [[ ! -f "${library}" ]] || ! command -v readelf >/dev/null 2>&1; then
		return 1
	fi

	# readelf walks every member of an archive on its own, so there is nothing to unpack first. Its
	# output is captured rather than piped into grep: "grep -q" stops at the first match and closes the
	# pipe, readelf dies of SIGPIPE, and under "set -o pipefail" the whole pipeline then reports 141 -
	# so a match would be read as "no match".
	local sections
	sections="$(readelf -S "${library}" 2>/dev/null || true)"

	grep -q '\.gnu\.lto_' <<< "${sections}"
}

if [[ -f "$INSTALL_DIR/lib/libopen62541.a" ]]; then
	if library_needs_pic_rebuild "$INSTALL_DIR/lib/libopen62541.a"; then
		echo "open62541 in $INSTALL_DIR is not position-independent; rebuilding"
		rm -rf "$BUILD_DIR/build" "$INSTALL_DIR"
	elif library_needs_lto_rebuild "$INSTALL_DIR/lib/libopen62541.a"; then
		echo "open62541 in $INSTALL_DIR is a GCC LTO archive; rebuilding"
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
#
# Link-time optimization off, which open62541 would otherwise switch on by itself: its CMakeLists turns
# CMAKE_INTERPROCEDURAL_OPTIMIZATION on for a Release build without unit tests unless the variable is
# already defined. Built by GCC that fills the archive with GCC LTO IR instead of machine code, and only
# the GCC driver knows to hand /usr/bin/ld the matching -plugin liblto_plugin.so. Linking the same
# archive through clang++ therefore fails with
#
#     libopen62541.a(ua_types.c.o): plugin needed to handle lto object
#
# followed by an undefined reference for every open62541 symbol the build uses. One archive has to serve
# both toolchains - script/clang_full_build.sh and script/gcc_full_build.sh share output/open62541 - so
# it is built as plain object code.
cmake -S "$SRC_DIR" -B "$BUILD_DIR/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DUA_ENABLE_ENCRYPTION=OPENSSL \
	-DUA_ENABLE_AMALGAMATION=OFF \
	-DUA_BUILD_EXAMPLES=OFF \
	-DUA_BUILD_TESTS=OFF \
	-DUA_BUILD_TOOLS=OFF

cmake --build "$BUILD_DIR/build" -j"$(nproc)"
cmake --install "$BUILD_DIR/build"

echo "open62541 installed into: $INSTALL_DIR"
