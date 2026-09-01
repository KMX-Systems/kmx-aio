#!/usr/bin/env bash
# Shared position-independent-code helper for the per-feature dependency builders.
#
# Every dependency this project builds into output/ is a static library, and every one of them ends up
# inside kmx-aio-test, a sample or a benchmark - executables that the toolchains here link as PIE by
# default. Code compiled without -fPIC carries absolute relocations (R_X86_64_32 and R_X86_64_32S),
# which a position-independent image cannot resolve, so such a link fails with
#
#     relocation R_X86_64_32 against `.rodata.str1.8' can not be used when making a PIE object
#
# naming only the first archive of however many are at fault. That is why the builders all configure
# with -DCMAKE_POSITION_INDEPENDENT_CODE=ON.
#
# The flag alone is not enough, though: each builder short-circuits on the artifact it produced last
# time, so a tree built before the flag was set would keep its non-PIC libraries forever - the files are
# there and look complete, and nothing else distinguishes them. The check below is that missing signal.

# Reports whether a built library still has to be rebuilt to be position-independent.
# @param 1 Path to the static archive (or shared object) to inspect.
# @return 0 when the file carries absolute relocations, 1 when it is position-independent, unreadable
#         or readelf is unavailable - the caller then keeps whatever it already has.
library_needs_pic_rebuild() {
	local library="$1"

	if [[ ! -f "${library}" ]] || ! command -v readelf >/dev/null 2>&1; then
		return 1
	fi

	# R_X86_64_32 and R_X86_64_32S are both absolute and both rejected in a PIE link; the shared prefix
	# matches either. An archive of LTO objects reports no relocations at all and is left alone here -
	# it has its own incompatibilities, which are not this check's business (the OPC UA builder makes
	# that check for itself).
	#
	# The output is captured rather than piped into grep: "grep -q" stops at the first match and closes
	# the pipe, readelf dies of SIGPIPE, and under the "set -o pipefail" every caller of this file runs
	# with, the pipeline then reports 141 - so a large archive that does need rebuilding would be read
	# as "no match" and kept.
	local relocations
	relocations="$(readelf --relocs "${library}" 2>/dev/null || true)"

	grep -q 'R_X86_64_32' <<< "${relocations}"
}
