#!/usr/bin/env bash
# Builds the whole tree with Clang. See script/full-build.sh for what "the whole tree" means here and for
# the options this forwards; everything below is the part that is specific to this compiler.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The compiler family, and nothing narrower: whichever Clang "clang++" resolves to on this machine is the
# one that gets used, and script/qbs-profile.sh makes it a profile. No version appears here, so a machine
# that moves to the next Clang needs no edit; to build with a particular one, name it -
# KMX_CXX=clang++-24 script/clang_full_build.sh - or point QBS_PROFILE at a profile of your own.
#
# A machine with no unversioned "clang++" is covered too, and there are plenty: Debian ships the driver
# as clang++-24 and leaves the plain name to the "clang" metapackage. qbs_compiler_path falls back to the
# newest versioned spelling, so naming the family here is enough on those as well.
export KMX_CXX="${KMX_CXX:-clang++}"
export KMX_CC="${KMX_CC:-clang}"

# Keeps the Clang artifacts in output/full-clang/, next to and not on top of the other toolchains'.
export KMX_FULL_BUILD_TAG="clang"

# The non-PIE workaround for a non-PIC Catch2 used to live here, because clang was the only way to reach
# it. It moved into full-build.sh once the default compiler could be a clang too: the check belongs
# wherever the profile is known, not in one of the entry points that leads there.
exec "$script_dir/full-build.sh" "$@"
