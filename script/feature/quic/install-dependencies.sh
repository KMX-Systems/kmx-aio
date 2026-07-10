#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BORINGSSL_DIR="${BUILD_DIR}/boringssl"
BORINGSSL_BUILD_DIR="${BORINGSSL_DIR}/build"
LSQUIC_DIR="${BUILD_DIR}/lsquic"
LSQUIC_BUILD_DIR="${LSQUIC_DIR}/build"

JOBS="${JOBS:-$(nproc)}"
BORINGSSL_REF="${BORINGSSL_REF:-}"
LSQUIC_REF="${LSQUIC_REF:-}"

ensure_tool() {
	local tool="$1"
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "[bootstrap] missing required tool: ${tool}" >&2
		exit 1
	fi
}

resolve_ref() {
	local repo_dir="$1"
	local requested_ref="$2"

	if [[ -n "${requested_ref}" ]]; then
		echo "${requested_ref}"
		return
	fi

	if git -C "${repo_dir}" show-ref --verify --quiet refs/heads/master || \
	   git -C "${repo_dir}" show-ref --verify --quiet refs/remotes/origin/master; then
		echo "master"
		return
	fi

	echo "main"
}

if [[ -f "${LSQUIC_BUILD_DIR}/src/liblsquic/liblsquic.a" && \
	  -f "${BORINGSSL_BUILD_DIR}/libssl.a" && \
	  -f "${BORINGSSL_BUILD_DIR}/libcrypto.a" ]]; then
	echo "[bootstrap] lsquic/boringssl already built"
	exit 0
fi

ensure_tool git
ensure_tool cmake

mkdir -p "${BUILD_DIR}"

if [[ ! -d "${BORINGSSL_DIR}/.git" ]]; then
	echo "[bootstrap] cloning boringssl"
	git clone https://boringssl.googlesource.com/boringssl "${BORINGSSL_DIR}"
fi

if [[ ! -d "${LSQUIC_DIR}/.git" ]]; then
	echo "[bootstrap] cloning lsquic"
	git clone https://github.com/litespeedtech/lsquic.git "${LSQUIC_DIR}"
fi

BORINGSSL_REF="$(resolve_ref "${BORINGSSL_DIR}" "${BORINGSSL_REF}")"
LSQUIC_REF="$(resolve_ref "${LSQUIC_DIR}" "${LSQUIC_REF}")"

echo "[bootstrap] updating boringssl (${BORINGSSL_REF})"
git -C "${BORINGSSL_DIR}" fetch --all --tags --prune
git -C "${BORINGSSL_DIR}" checkout "${BORINGSSL_REF}"

echo "[bootstrap] updating lsquic (${LSQUIC_REF})"
git -C "${LSQUIC_DIR}" fetch --all --tags --prune
git -C "${LSQUIC_DIR}" checkout "${LSQUIC_REF}"
git -C "${LSQUIC_DIR}" submodule update --init --recursive

mkdir -p "${BORINGSSL_BUILD_DIR}"
cmake -S "${BORINGSSL_DIR}" -B "${BORINGSSL_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BORINGSSL_BUILD_DIR}" --parallel "${JOBS}"

mkdir -p "${LSQUIC_BUILD_DIR}"
cmake -S "${LSQUIC_DIR}" -B "${LSQUIC_BUILD_DIR}" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBORINGSSL_DIR="${BORINGSSL_BUILD_DIR}" \
	-DBORINGSSL_INCLUDE="${BORINGSSL_DIR}/include" \
	-DSSLLIB_INCLUDE="${BORINGSSL_DIR}/include" \
	-DLIBSSL_LIB_ssl="${BORINGSSL_BUILD_DIR}/libssl.a" \
	-DLIBSSL_LIB_crypto="${BORINGSSL_BUILD_DIR}/libcrypto.a" \
	-DLSQUIC_LIBSSL="BORINGSSL" \
	-DLSQUIC_BIN=OFF \
	-DLSQUIC_TESTS=OFF
cmake --build "${LSQUIC_BUILD_DIR}" --parallel "${JOBS}"

if [[ ! -f "${LSQUIC_BUILD_DIR}/src/liblsquic/liblsquic.a" ]]; then
	echo "[bootstrap] missing expected lsquic static lib at ${LSQUIC_BUILD_DIR}/src/liblsquic/liblsquic.a" >&2
	exit 1
fi

if [[ ! -f "${BORINGSSL_BUILD_DIR}/libssl.a" || ! -f "${BORINGSSL_BUILD_DIR}/libcrypto.a" ]]; then
	echo "[bootstrap] missing expected boringssl static libraries under ${BORINGSSL_BUILD_DIR}" >&2
	exit 1
fi

printf '\n[bootstrap] done\n'
printf '[bootstrap] lsquic: %s\n' "${LSQUIC_BUILD_DIR}/src/liblsquic/liblsquic.a"
printf '[bootstrap] boringssl ssl: %s\n' "${BORINGSSL_BUILD_DIR}/libssl.a"
printf '[bootstrap] boringssl crypto: %s\n' "${BORINGSSL_BUILD_DIR}/libcrypto.a"
