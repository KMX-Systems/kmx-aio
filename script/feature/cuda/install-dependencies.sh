#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../apt.sh"

echo "[accelerators] Checking optional accelerator dependencies..."
apt_install_missing accelerators \
	libbpf-dev \
	libxdp-dev \
	libelf-dev \
	zlib1g-dev \
	clang \
	llvm \
	libaio-dev \
	libnuma-dev \
	uuid-dev \
	meson \
	ninja-build \
	libssl-dev

echo "[accelerators] Verifying key tooling..."
pkg-config --modversion libbpf
pkg-config --modversion libxdp
command -v clang >/dev/null 2>&1
command -v llvm-config >/dev/null 2>&1
command -v meson >/dev/null 2>&1
command -v ninja >/dev/null 2>&1

bash "$script_dir/check_env.sh"

echo "[accelerators] Done."
