#!/usr/bin/env bash
# Builds the whole tree with GCC. See script/full-build.sh for what "the whole tree" means here and for
# the options this forwards; everything below is the part that is specific to this compiler.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The compiler family, and nothing narrower: whichever GCC "g++" resolves to on this machine is the one
# that gets used, and script/qbs-profile.sh makes it a profile. No version appears here, so a machine
# that moves to the next GCC needs no edit; to build with a particular one, name it - KMX_CXX=g++-17
# script/gcc_full_build.sh - or point QBS_PROFILE at a profile of your own. A machine carrying only the
# versioned drivers is covered as well; see the note in script/clang_full_build.sh.
export KMX_CXX="${KMX_CXX:-g++}"
export KMX_CC="${KMX_CC:-gcc}"

# Keeps the GCC artifacts in output/full-gcc/, next to and not on top of the other toolchains'.
export KMX_FULL_BUILD_TAG="gcc"

exec "$script_dir/full-build.sh" "$@"
