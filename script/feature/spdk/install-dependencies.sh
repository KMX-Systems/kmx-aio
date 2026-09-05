#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SPDK_BASE="${ROOT_DIR}/output/spdk-local"
SPDK_SRC_DIR="${SPDK_BASE}/src"
SPDK_INSTALL_DIR="${SPDK_BASE}/install-local"
SPDK_REF="${SPDK_REF:-v24.09}"
JOBS="${JOBS:-$(nproc)}"

# shellcheck source=../apt.sh
source "${ROOT_DIR}/script/feature/apt.sh"

if [[ -f "${SPDK_INSTALL_DIR}/lib/pkgconfig/spdk_nvme.pc" ]]; then
	echo "[spdk] already installed locally: ${SPDK_INSTALL_DIR}"
	exit 0
fi

# Through apt_install_missing rather than a plain apt-get: source.qbs runs this from a Probe, in the
# middle of "qbs resolve", where there is no terminal for a sudo password. An unconditional "apt-get
# update" fails there even on a machine that has every one of these packages, and it fails with "sudo: a
# password is required" - which names neither SPDK nor anything else the reader could act on. The
# short-circuit above only covers a tree where SPDK has already been built.
echo "[spdk] Checking build dependencies (Ubuntu/Debian)..."
apt_install_missing spdk \
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

# SPDK leaves ISA-L out of its own shared libraries and does not dependably install it, so the
# prefix has to be reconciled against the build tree before anything links against it.
bash "${ROOT_DIR}/script/feature/spdk/install-isal.sh" "${SPDK_SRC_DIR}" "${SPDK_INSTALL_DIR}"

echo "[spdk] Installed local prefix: ${SPDK_INSTALL_DIR}"
echo "[spdk] Verify with: pkg-config --modversion spdk_nvme"
echo "[spdk] Resolve with: qbs resolve -f source/source.qbs config:debug project.enable_spdk:true project.spdk_prefix:\"${SPDK_INSTALL_DIR}\""
echo "[spdk] Build with:   qbs build -f source/source.qbs config:debug project.enable_spdk:true project.spdk_prefix:\"${SPDK_INSTALL_DIR}\""
