#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUTPUT_DIR="${ROOT_DIR}/output"
BORINGSSL_DIR="${OUTPUT_DIR}/boringssl"
BORINGSSL_BUILD_DIR="${BORINGSSL_DIR}/build"
LSQUIC_DIR="${OUTPUT_DIR}/lsquic"
LSQUIC_BUILD_DIR="${LSQUIC_DIR}/build"

# What the build actually consumes. Written on every run so it always describes the current decision,
# and read back by source.qbs, which must compile and link against whichever copy won here.
CONFIG_FILE="${OUTPUT_DIR}/quic-dependencies.json"

JOBS="${JOBS:-$(nproc)}"

# NOTE: BoringSSL has no ABI/API stability guarantee between commits. lsquic's
# own README pins a specific known-good BoringSSL tag; floating to "master" here
# can silently drift ahead of what lsquic was validated against and causes QUIC
# Initial/Handshake AEAD key derivation mismatches (symptom: handshake never
# completes, "BAD_DECRYPT" in lsquic debug logs). Default to that pinned tag;
# override via BORINGSSL_REF only if you know what you're doing.
BORINGSSL_REF="${BORINGSSL_REF:-0.20250807.0}"
LSQUIC_REF="${LSQUIC_REF:-}"

# Lowest versions this project is known to build against, used to judge an already installed copy.
#
# BoringSSL releases are dated (0.20250807.0), so their leading component says nothing about API
# breaks; BORINGSSL_API_VERSION in <openssl/base.h> is the number BoringSSL itself bumps when its API
# moves, and 36 is what the pinned tag above carries. lsquic uses ordinary semantic versioning, so its
# major version is the meaningful gate - the pinned tag builds as 4.x.
BORINGSSL_MIN_API_VERSION="${BORINGSSL_MIN_API_VERSION:-36}"
LSQUIC_MIN_MAJOR_VERSION="${LSQUIC_MIN_MAJOR_VERSION:-4}"

# Set to 1 to ignore anything installed system-wide and always build the vendored copies (CI uses this
# when it wants the pinned versions regardless of what the image happens to ship).
FORCE_VENDORED="$(
    case "${KMX_QUIC_FORCE_VENDORED:-0}" in
        1|true|yes|on) echo 1 ;;
        *) echo 0 ;;
    esac
)"

ensure_tool() {
	local tool="$1"
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "[bootstrap] missing required tool: ${tool}" >&2
		exit 1
	fi
}

resolve_ref() {
	local repo_dir="$1"
	local requested="$2"

	if [[ -n "${requested}" ]]; then
		echo "${requested}"
		return
	fi

	if git -C "${repo_dir}" rev-parse --verify --quiet origin/master >/dev/null; then
		echo "master"
		return
	fi

	echo "main"
}

# A CMake build tree records the absolute path it was configured in. Moving the dependency trees (as the
# switch from build/ to output/ did) leaves those caches pointing at directories that no longer exist, and
# every later cmake run in them fails instead of reconfiguring. Throw such a cache away and start clean.
drop_relocated_cmake_cache() {
	local build_dir="$1"
	local cache="${build_dir}/CMakeCache.txt"

	if [[ ! -f "${cache}" ]]; then
		return
	fi

	local cached_binary_dir
	cached_binary_dir="$(sed -n 's|^CMAKE_CACHEFILE_DIR:INTERNAL=||p' "${cache}" | head -n 1)"
	if [[ -n "${cached_binary_dir}" && "${cached_binary_dir}" != "${build_dir}" ]]; then
		echo "[bootstrap] ${build_dir} was configured as ${cached_binary_dir}; reconfiguring from scratch"
		rm -rf "${build_dir}"
	fi
}

read_integer_define() {
	local header="$1"
	local macro="$2"
	sed -n "s/^[[:space:]]*#[[:space:]]*define[[:space:]]\\+${macro}[[:space:]]\\+\\([0-9]\\+\\).*/\\1/p" \
		"${header}" | head -n 1
}

library_directories_of_prefix() {
	local prefix="$1"
	printf '%s\n' "${prefix}/lib" "${prefix}/lib64"

	# Debian/Ubuntu put shared libraries in a triple-qualified directory; ask the compiler for the
	# triple rather than guessing it from uname.
	local triple
	triple="$( { cc -dumpmachine || gcc -dumpmachine; } 2>/dev/null || true)"
	if [[ -n "${triple}" ]]; then
		printf '%s\n' "${prefix}/lib/${triple}"
	fi
}

# Prints the path of lib<name>.a, or lib<name>.so when no static archive is installed. Static wins
# because that is how the vendored build links, and because a BoringSSL shared object sitting next to
# the system OpenSSL is the configuration most likely to load the wrong soname at run time.
find_library_in_prefix() {
	local prefix="$1"
	local name="$2"
	local directory candidate

	while read -r directory; do
		for candidate in "${directory}/lib${name}.a" "${directory}/lib${name}.so"; do
			if [[ -f "${candidate}" ]]; then
				echo "${candidate}"
				return 0
			fi
		done
	done < <(library_directories_of_prefix "${prefix}")

	return 1
}

boringssl_origin=""
boringssl_include_dir=""
boringssl_ssl_library=""
boringssl_crypto_library=""
boringssl_version=""

lsquic_origin=""
lsquic_include_dir=""
lsquic_library=""
lsquic_version=""

# An installed BoringSSL is only accepted when it identifies itself as BoringSSL and its API version is
# at least the one the pinned tag carries. OpenSSL - any version of it - is not a substitute: lsquic's
# BoringSSL backend and this project's ALPN selection callback both use BoringSSL-only entry points, and
# <openssl/base.h> is a file OpenSSL does not ship at all, so its presence is the first thing checked.
detect_system_boringssl() {
	local prefix header api ssl_library crypto_library

	for prefix in ${BORINGSSL_PREFIX:-} /usr/local /usr; do
		header="${prefix}/include/openssl/base.h"
		[[ -f "${header}" ]] || continue
		grep -q "define[[:space:]]\+OPENSSL_IS_BORINGSSL" "${header}" || continue

		api="$(read_integer_define "${header}" BORINGSSL_API_VERSION)"
		if [[ -z "${api}" ]]; then
			echo "[bootstrap] boringssl headers in ${prefix} carry no BORINGSSL_API_VERSION; ignoring them"
			continue
		fi

		if (( api < BORINGSSL_MIN_API_VERSION )); then
			echo "[bootstrap] boringssl in ${prefix} has API version ${api}, below the tested ${BORINGSSL_MIN_API_VERSION}; building the pinned copy instead"
			continue
		fi

		ssl_library="$(find_library_in_prefix "${prefix}" ssl)" || continue
		crypto_library="$(find_library_in_prefix "${prefix}" crypto)" || continue

		boringssl_origin="system"
		boringssl_include_dir="${prefix}/include"
		boringssl_ssl_library="${ssl_library}"
		boringssl_crypto_library="${crypto_library}"
		boringssl_version="API ${api}"
		echo "[bootstrap] using installed boringssl from ${prefix} (API version ${api})"
		return 0
	done

	return 1
}

detect_system_lsquic() {
	local prefix header major minor patch library

	for prefix in ${LSQUIC_PREFIX:-} /usr/local /usr; do
		header="${prefix}/include/lsquic.h"
		[[ -f "${header}" ]] || continue

		major="$(read_integer_define "${header}" LSQUIC_MAJOR_VERSION)"
		if [[ -z "${major}" ]]; then
			echo "[bootstrap] lsquic header in ${prefix} carries no LSQUIC_MAJOR_VERSION; ignoring it"
			continue
		fi

		if (( major < LSQUIC_MIN_MAJOR_VERSION )); then
			echo "[bootstrap] lsquic in ${prefix} is version ${major}.x, below the tested ${LSQUIC_MIN_MAJOR_VERSION}.x; building the pinned copy instead"
			continue
		fi

		library="$(find_library_in_prefix "${prefix}" lsquic)" || continue

		minor="$(read_integer_define "${header}" LSQUIC_MINOR_VERSION)"
		patch="$(read_integer_define "${header}" LSQUIC_PATCH_VERSION)"

		lsquic_origin="system"
		lsquic_include_dir="${prefix}/include"
		lsquic_library="${library}"
		lsquic_version="${major}.${minor:-0}.${patch:-0}"
		echo "[bootstrap] using installed lsquic from ${prefix} (version ${lsquic_version})"
		return 0
	done

	return 1
}

use_vendored_boringssl() {
	boringssl_origin="vendored"
	boringssl_include_dir="${BORINGSSL_DIR}/include"
	boringssl_ssl_library="${BORINGSSL_BUILD_DIR}/libssl.a"
	boringssl_crypto_library="${BORINGSSL_BUILD_DIR}/libcrypto.a"
	boringssl_version="${BORINGSSL_REF}"
}

use_vendored_lsquic() {
	lsquic_origin="vendored"
	lsquic_include_dir="${LSQUIC_DIR}/include"
	lsquic_library="${LSQUIC_BUILD_DIR}/src/liblsquic/liblsquic.a"
	lsquic_version=""
}

# Clones when absent, then checks out the requested ref. The ref that was actually used is reported in
# the global resolved_ref rather than on stdout, so the progress lines here stay progress lines.
resolved_ref=""
clone_and_checkout() {
	local repo_dir="$1"
	local url="$2"
	local ref="$3"
	local name="$4"

	if [[ ! -d "${repo_dir}/.git" ]]; then
		echo "[bootstrap] cloning ${name}"
		git clone "${url}" "${repo_dir}"
	fi

	ref="$(resolve_ref "${repo_dir}" "${ref}")"
	echo "[bootstrap] updating ${name} (${ref})"
	git -C "${repo_dir}" fetch --all --tags --prune
	git -C "${repo_dir}" checkout "${ref}"
	resolved_ref="${ref}"
}

build_vendored_boringssl() {
	ensure_tool git
	ensure_tool cmake
	mkdir -p "${OUTPUT_DIR}"

	clone_and_checkout "${BORINGSSL_DIR}" \
		"https://boringssl.googlesource.com/boringssl" "${BORINGSSL_REF}" boringssl
	boringssl_version="${resolved_ref}"

	drop_relocated_cmake_cache "${BORINGSSL_BUILD_DIR}"
	mkdir -p "${BORINGSSL_BUILD_DIR}"
	cmake -S "${BORINGSSL_DIR}" -B "${BORINGSSL_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
	cmake --build "${BORINGSSL_BUILD_DIR}" --parallel "${JOBS}"

	if [[ ! -f "${boringssl_ssl_library}" || ! -f "${boringssl_crypto_library}" ]]; then
		echo "[bootstrap] missing expected boringssl static libraries under ${BORINGSSL_BUILD_DIR}" >&2
		exit 1
	fi
}

# Builds lsquic against whichever BoringSSL was settled on above - the installed one when it was good
# enough, the vendored one otherwise. lsquic compiles its crypto backend against those exact headers,
# so the two decisions cannot be made independently once this point is reached.
build_vendored_lsquic() {
	ensure_tool git
	ensure_tool cmake
	mkdir -p "${OUTPUT_DIR}"

	clone_and_checkout "${LSQUIC_DIR}" \
		"https://github.com/litespeedtech/lsquic.git" "${LSQUIC_REF}" lsquic
	git -C "${LSQUIC_DIR}" submodule update --init --recursive

	drop_relocated_cmake_cache "${LSQUIC_BUILD_DIR}"
	mkdir -p "${LSQUIC_BUILD_DIR}"
	cmake -S "${LSQUIC_DIR}" -B "${LSQUIC_BUILD_DIR}" \
		-DCMAKE_BUILD_TYPE=Release \
		-DBORINGSSL_DIR="$(dirname "${boringssl_ssl_library}")" \
		-DBORINGSSL_INCLUDE="${boringssl_include_dir}" \
		-DSSLLIB_INCLUDE="${boringssl_include_dir}" \
		-DLIBSSL_LIB_ssl="${boringssl_ssl_library}" \
		-DLIBSSL_LIB_crypto="${boringssl_crypto_library}" \
		-DLSQUIC_LIBSSL="BORINGSSL" \
		-DLSQUIC_BIN=OFF \
		-DLSQUIC_TESTS=OFF
	cmake --build "${LSQUIC_BUILD_DIR}" --parallel "${JOBS}"

	if [[ ! -f "${lsquic_library}" ]]; then
		echo "[bootstrap] missing expected lsquic static lib at ${lsquic_library}" >&2
		exit 1
	fi

	lsquic_version="$(read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_MAJOR_VERSION).$(
		read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_MINOR_VERSION).$(
		read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_PATCH_VERSION)"
}

write_config_file() {
	mkdir -p "${OUTPUT_DIR}"
	cat > "${CONFIG_FILE}" <<JSON
{
    "comment": "Written by script/feature/quic/install-dependencies.sh; read by source/source.qbs.",
    "boringssl": {
        "origin": "${boringssl_origin}",
        "version": "${boringssl_version}",
        "include_dir": "${boringssl_include_dir}",
        "ssl_library": "${boringssl_ssl_library}",
        "crypto_library": "${boringssl_crypto_library}"
    },
    "lsquic": {
        "origin": "${lsquic_origin}",
        "version": "${lsquic_version}",
        "include_dir": "${lsquic_include_dir}",
        "library": "${lsquic_library}"
    }
}
JSON
}

if [[ "${FORCE_VENDORED}" == "1" ]]; then
	echo "[bootstrap] KMX_QUIC_FORCE_VENDORED is set; skipping system detection"
else
	detect_system_boringssl || true

	# An installed lsquic is only usable together with an installed BoringSSL: lsquic exposes BoringSSL
	# types through <lsquic.h> and this project links both into one binary, so a prebuilt lsquic that
	# was compiled against some other crypto library (or against BoringSSL headers we do not have)
	# cannot be paired with the vendored BoringSSL. When BoringSSL has to be built here, lsquic does too.
	if [[ "${boringssl_origin}" == "system" ]]; then
		detect_system_lsquic || true
	else
		echo "[bootstrap] no usable installed boringssl, so an installed lsquic cannot be used either"
	fi
fi

if [[ "${boringssl_origin}" != "system" ]]; then
	use_vendored_boringssl
	if [[ -f "${boringssl_ssl_library}" && -f "${boringssl_crypto_library}" ]]; then
		echo "[bootstrap] boringssl already built under ${BORINGSSL_BUILD_DIR}"
	else
		build_vendored_boringssl
	fi
fi

if [[ "${lsquic_origin}" != "system" ]]; then
	use_vendored_lsquic
	if [[ -f "${lsquic_library}" ]]; then
		echo "[bootstrap] lsquic already built under ${LSQUIC_BUILD_DIR}"
		lsquic_version="$(read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_MAJOR_VERSION).$(
			read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_MINOR_VERSION).$(
			read_integer_define "${LSQUIC_DIR}/include/lsquic.h" LSQUIC_PATCH_VERSION)"
	else
		build_vendored_lsquic
	fi
fi

write_config_file

printf '\n[bootstrap] done\n'
printf '[bootstrap] boringssl (%s, %s): %s\n' \
	"${boringssl_origin}" "${boringssl_version}" "${boringssl_ssl_library}"
printf '[bootstrap] lsquic (%s, %s): %s\n' \
	"${lsquic_origin}" "${lsquic_version}" "${lsquic_library}"
printf '[bootstrap] build configuration written to %s\n' "${CONFIG_FILE}"
