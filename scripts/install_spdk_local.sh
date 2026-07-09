#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPDK_BASE="${ROOT_DIR}/build/spdk-local"
SPDK_SRC_DIR="${SPDK_BASE}/src"
SPDK_INSTALL_DIR="${SPDK_BASE}/install-local"
SPDK_REF="${SPDK_REF:-v24.09}"
JOBS="${JOBS:-$(nproc)}"

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script currently supports Ubuntu/Debian only (apt-get required)." >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

echo "[spdk] Installing build dependencies (Ubuntu/Debian)..."
${SUDO} apt-get update
${SUDO} apt-get install -y \
    build-essential pkg-config meson ninja-build git \
    python3 python3-jinja2 python3-pyelftools python3-tabulate \
    libaio-dev libnuma-dev uuid-dev libssl-dev libelf-dev libpcap-dev

mkdir -p "${SPDK_BASE}"

if [[ ! -d "${SPDK_SRC_DIR}/.git" ]]; then
    echo "[spdk] Cloning SPDK (${SPDK_REF})..."
    git clone --depth 1 --branch "${SPDK_REF}" https://github.com/spdk/spdk.git "${SPDK_SRC_DIR}"
else
    echo "[spdk] Updating SPDK (${SPDK_REF})..."
    git -C "${SPDK_SRC_DIR}" fetch --tags --prune origin
    git -C "${SPDK_SRC_DIR}" checkout -f "${SPDK_REF}"
fi

git -C "${SPDK_SRC_DIR}" submodule update --init --recursive

pushd "${SPDK_SRC_DIR}" >/dev/null
./configure --prefix="${SPDK_INSTALL_DIR}" --with-shared --disable-tests --disable-unit-tests --disable-apps --disable-examples \
    --without-fio --without-vhost --without-iscsi-initiator --without-rbd --without-xnvme --without-fc --without-rdma \
    --without-crypto --without-vfio-user --without-virtio --without-nvme-cuse
make -j"${JOBS}"
make install
popd >/dev/null

echo "[spdk] Installed local prefix: ${SPDK_INSTALL_DIR}"
echo "[spdk] Verify with: pkg-config --modversion spdk_nvme"
