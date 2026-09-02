#!/usr/bin/env bash
# Reconciles the ISA-L libraries in an SPDK install prefix with the ones SPDK actually built.
#
# SPDK compiles ISA-L from its own submodules, but links it only into its applications through
# SYS_LIBS - never into libspdk_util.so or libspdk_accel.so. Those ship with the ISA-L symbols
# still undefined, so every consumer of the shared SPDK libraries has to put -lisal/-lisal_crypto
# on its own link line. What ends up in the install prefix is not dependable: upstream leaves the
# dependency out of the install and points the generated spdk_syslibs.pc back into the build tree
# (spdk/spdk#2736, spdk/spdk#3143). A prefix carrying a libisal* file that resolves none of those
# symbols is indistinguishable from a good one until an unrelated product fails to link with a wall
# of "undefined reference to `isal_inflate'". Reconcile it here instead, once, at bootstrap time.
#
# Usage: install-isal.sh <spdk-source-dir> <install-prefix>
set -euo pipefail

SPDK_SRC_DIR="${1:?usage: install-isal.sh <spdk-source-dir> <install-prefix>}"
SPDK_INSTALL_DIR="${2:?usage: install-isal.sh <spdk-source-dir> <install-prefix>}"

lib_dir="${SPDK_INSTALL_DIR}/lib"

# Entry points that identify ISA-L in a symbol table. Only consulted when neither the prefix nor the
# build tree holds an ISA-L library, to tell "SPDK was built without ISA-L" apart from "ISA-L went
# missing"; every other decision below compares real symbol tables instead.
readonly ISAL_SYMBOL_PATTERN='^(isal_|crc16_t10dif|crc32_iscsi|crc32_gzip|crc64_|xor_gen|xor_check|pc_bswap|ec_|gf_)'

# Global symbols a library defines, one per line. Works for archives and shared objects alike.
defined_symbols() {
	local lib="$1"
	[[ -e "${lib}" ]] || return 0
	nm --defined-only --extern-only "${lib}" 2> /dev/null | awk '$2 ~ /^[TDWRB]$/ { print $3 }'
}

# The file `-l<name>` would actually pick out of a directory: a shared object shadows an archive.
selected_library() {
	local dir="$1" name="$2" candidate
	for candidate in "${dir}/lib${name}.so" "${dir}/lib${name}.a"; do
		if [[ -e "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
	done
	return 0
}

isal_symbols_from() {
	local dir="$1" name lib
	for name in isal isal_crypto; do
		lib="$(selected_library "${dir}" "${name}")"
		if [[ -n "${lib}" ]]; then
			defined_symbols "${lib}"
		fi
	done | sort -u
}

# Externals the installed SPDK shared libraries still expect somebody else to provide.
spdk_undefined_symbols() {
	local lib
	for lib in "${lib_dir}"/libspdk_*.so; do
		if [[ -e "${lib}" ]]; then
			nm --dynamic --undefined-only "${lib}" 2> /dev/null | awk '{ print $NF }'
		fi
	done | sort -u
}

# ISA-L symbols the prefix fails to supply. Intersecting the undefined set with a real ISA-L symbol
# table keeps unrelated externals - libc, OpenSSL, the other SPDK libraries - out of the comparison
# without hardcoding a symbol list that the next SPDK release would invalidate.
unresolved_isal_symbols() {
	local universe="$1"
	comm -12 <(spdk_undefined_symbols) <(echo "${universe}") \
		| comm -23 - <(isal_symbols_from "${lib_dir}")
}

build_tree_symbols="$(
	{
		isal_symbols_from "${SPDK_SRC_DIR}/isa-l/.libs"
		isal_symbols_from "${SPDK_SRC_DIR}/isa-l-crypto/.libs"
	} | sort -u
)"
isal_universe="$({ echo "${build_tree_symbols}"; isal_symbols_from "${lib_dir}"; } | sort -u | sed '/^$/d')"

if [[ -z "${isal_universe}" ]]; then
	# No ISA-L library anywhere to compare against, so fall back to naming its entry points.
	if spdk_undefined_symbols | grep -Eq "${ISAL_SYMBOL_PATTERN}"; then
		echo "[spdk] ERROR: the installed SPDK libraries need ISA-L, but neither ${lib_dir} nor" >&2
		echo "[spdk]        ${SPDK_SRC_DIR}/isa-l{,-crypto}/.libs holds an ISA-L library." >&2
		exit 1
	fi
	echo "[spdk] ISA-L is not in use (SPDK disables it without nasm 2.14+); nothing to reconcile."
	exit 0
fi

# Always on the record: a link that fails over ISA-L later should be answerable from this log alone.
report_state() {
	local name lib
	for name in isal isal_crypto; do
		lib="$(selected_library "${lib_dir}" "${name}")"
		if [[ -n "${lib}" ]]; then
			echo "[spdk]   -l${name} -> ${lib} ($(defined_symbols "${lib}" | wc -l) symbols)"
		else
			echo "[spdk]   -l${name} -> not present in ${lib_dir}"
		fi
	done
}

missing="$(unresolved_isal_symbols "${isal_universe}")"
if [[ -z "${missing}" ]]; then
	echo "[spdk] ISA-L in ${lib_dir} already satisfies the installed SPDK libraries."
	report_state
	exit 0
fi

echo "[spdk] ISA-L in ${lib_dir} does not satisfy the installed SPDK libraries; taking it from the build tree."
mkdir -p "${lib_dir}"
for artifact in "${SPDK_SRC_DIR}"/isa-l/.libs/libisal.{a,so,so.*} \
	"${SPDK_SRC_DIR}"/isa-l-crypto/.libs/libisal_crypto.{a,so,so.*}; do
	if [[ -e "${artifact}" ]]; then
		cp -a -- "${artifact}" "${lib_dir}/"
	fi
done

missing="$(unresolved_isal_symbols "${isal_universe}")"
if [[ -n "${missing}" ]]; then
	echo "[spdk] ERROR: the SPDK shared libraries reference ISA-L symbols nothing in the prefix defines:" >&2
	echo "${missing}" | sed 's/^/[spdk]   /' >&2
	report_state >&2
	exit 1
fi

echo "[spdk] ISA-L reconciled into ${lib_dir}."
report_state
