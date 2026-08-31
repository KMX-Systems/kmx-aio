#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../apt.sh"

echo "[af_xdp] Checking AF_XDP toolchain dependencies..."
apt_install_missing af_xdp \
	libbpf-dev \
	libxdp-dev \
	libelf-dev \
	zlib1g-dev \
	clang \
	llvm

echo "[af_xdp] Verifying installed toolchain..."
pkg-config --modversion libbpf
pkg-config --modversion libxdp
command -v clang >/dev/null 2>&1
command -v llvm-config >/dev/null 2>&1

echo "[af_xdp] Done."
