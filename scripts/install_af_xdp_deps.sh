#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script currently supports Ubuntu/Debian only (apt-get required)." >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

echo "[af_xdp] Installing AF_XDP toolchain dependencies (Ubuntu/Debian)..."
${SUDO} apt-get update
${SUDO} apt-get install -y \
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
