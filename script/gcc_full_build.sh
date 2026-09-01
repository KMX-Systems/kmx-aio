#!/usr/bin/env bash
# Builds the whole tree with GCC. See script/full-build.sh for what "the whole tree" means here and for
# the options this forwards; everything below is the part that is specific to this compiler.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The first of these that resolves to an installed compiler is used; QBS_PROFILE still overrides. GCC 16
# comes first because the library is written against C++26 features the older releases do not implement,
# and because it is the toolchain the test runners already assume - a binary built here can be run by
# script/run-unit-tests.sh without rebuilding it.
export KMX_FULL_BUILD_PROFILES="${KMX_FULL_BUILD_PROFILES:-gcc16 gcc-16 gcc13 gcc}"

# Keeps the GCC artifacts in output/full-gcc/, next to and not on top of the clang ones.
export KMX_FULL_BUILD_TAG="gcc"

exec "$script_dir/full-build.sh" "$@"
