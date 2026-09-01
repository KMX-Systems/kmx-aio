#!/usr/bin/env bash
# Builds the whole tree with clang. See script/full-build.sh for what "the whole tree" means here and
# for the options this forwards; everything below is the part that is specific to this compiler.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The repository's own profile preference (script/qbs-profile.sh) is GCC, so a clang build has to name
# its profile. The first of these that resolves to an installed compiler is used; QBS_PROFILE still
# overrides. clang-20 and clang20 are the same settings under two names on the machines this was written
# for, and a plain "clang" profile is the sensible last resort.
export KMX_FULL_BUILD_PROFILES="${KMX_FULL_BUILD_PROFILES:-clang-20 clang20 clang}"

# Keeps the clang artifacts in output/full-clang/, next to and not on top of the GCC ones.
export KMX_FULL_BUILD_TAG="clang"

extra_properties=()

# Catch2 under /usr/local is commonly installed non-PIC, and clang links executables as PIE, so
# kmx-aio-test then fails with
#
#     relocation R_X86_64_32 against `.rodata.str1.8' can not be used when making a PIE object
#
# naming Catch2 and nothing else. Linking the executables non-PIE is the one-line way past it and costs
# only this binary's address-space randomisation; rebuilding Catch2 with
# -DCMAKE_POSITION_INDEPENDENT_CODE=ON is the real fix, and once that is done the check below stops
# adding the flag on its own. GCC is unaffected, which is why this lives here and not in full-build.sh.
source "$script_dir/feature/pic.sh"

catch2_needs_no_pie() {
    local library
    for library in /usr/local/lib/libCatch2.a /usr/local/lib/libCatch2Main.a \
                   /usr/lib/x86_64-linux-gnu/libCatch2.a /usr/lib/x86_64-linux-gnu/libCatch2Main.a; do
        if library_needs_pic_rebuild "$library"; then
            return 0
        fi
    done
    return 1
}

if catch2_needs_no_pie; then
    echo "==> Catch2 is not position-independent; linking with -no-pie"
    extra_properties+=(--qbs-property modules.cpp.driverLinkerFlags:-no-pie)
fi

exec "$script_dir/full-build.sh" "${extra_properties[@]}" "$@"
