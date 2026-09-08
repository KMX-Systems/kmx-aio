#!/usr/bin/env bash
# Makes a C++26-capable compiler the machine's default, and points qbs at it.
#
# The build scripts under script/ compile with whatever "c++" and "cc" resolve to; see the comment at
# the top of script/qbs-profile.sh. That leaves CI with one job - to make the default a compiler that
# can build this project - instead of naming a compiler in every qbs command, which is what the
# workflow used to do and what left four copies of "g++-14" to update by hand.
#
# No version is written down here either. A runner image's stock default is not new enough (Ubuntu
# 24.04 ships GCC 13.3, which does not recognise -std=c++26 at all), so the newest GCC the package
# lists offer is installed and the alternatives are pointed at it. The check at the end is what turns
# "the image moved on and its newest GCC is now too old" into one line rather than a compile error per
# translation unit.
#
# KMX_CI_CXX / KMX_CI_CC name a compiler to use instead, for a runner that already has one.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

sudo_prefix=""
if [[ "${EUID}" -ne 0 ]]; then
    sudo_prefix="sudo"
fi

# The highest N among the g++-N packages apt knows about.
newest_available_gcc_version() {
    apt-cache search --names-only '^g\+\+-[0-9]+$' 2>/dev/null |
        sed -n 's/^g++-\([0-9][0-9]*\) .*/\1/p' |
        sort -n | tail -n 1
}

cxx="${KMX_CI_CXX:-}"
cc="${KMX_CI_CC:-}"

# Either one on its own names the toolchain, and the other is derived from it: a runner that sets only
# KMX_CI_CXX is naming a compiler, not asking for a build with no C driver. Without this the validation
# loop below would reach an empty string and report "ERROR: '' is not installed", which names nothing the
# reader could act on, and update-alternatives would be handed an empty target.
# Clang is tested for first in both directions, because "clang++" contains "g++": substituting the GCC
# spelling into it would turn clang++-24 into clangcc-24.
if [[ -n "$cxx" && -z "$cc" ]]; then
    if [[ "$cxx" == *clang++* ]]; then
        cc="${cxx//clang++/clang}"
    else
        cc="${cxx//g++/gcc}"
    fi
elif [[ -z "$cxx" && -n "$cc" ]]; then
    if [[ "$cc" == *clang* ]]; then
        cxx="${cc//clang/clang++}"
    else
        cxx="${cc//gcc/g++}"
    fi
fi

if [[ -z "$cxx" ]]; then
    # "|| true" and then the emptiness check, not the bare pipeline: this script runs under "set -e", and
    # a command substitution passes its pipeline's status straight out, so a host without apt-cache would
    # abort here with no output at all rather than reaching the diagnostic below.
    version="$(newest_available_gcc_version || true)"
    if [[ -z "$version" ]]; then
        echo "ERROR: apt lists no g++-<version> package; run 'apt-get update' first." >&2
        echo "       KMX_CI_CXX / KMX_CI_CC name a compiler to use instead." >&2
        exit 1
    fi

    echo "==> Installing GCC $version (the newest apt offers)"
    ${sudo_prefix} apt-get install -y "gcc-${version}" "g++-${version}"

    cxx="/usr/bin/g++-${version}"
    cc="/usr/bin/gcc-${version}"
fi

# Resolved through PATH as well as taken as a path, so KMX_CI_CXX=g++-15 works as readily as
# KMX_CI_CXX=/usr/bin/g++-15 - update-alternatives needs the absolute path either way.
for name in cxx cc; do
    compiler="${!name}"
    if [[ "$compiler" != /* ]]; then
        compiler="$(command -v "$compiler" 2>/dev/null || true)"
    fi

    if [[ -z "$compiler" || ! -x "$compiler" ]]; then
        echo "ERROR: '${!name}' is not installed (as the ${name^^} compiler for this run)." >&2
        exit 1
    fi

    printf -v "$name" '%s' "$compiler"
done

# c++ and cc are the ones that matter - they are what the build scripts follow. g++ and gcc are set
# alongside them so that a hand-typed compiler command on the runner agrees with what CI built with.
set_alternative() {
    local link="$1" name="$2" target="$3"
    ${sudo_prefix} update-alternatives --install "$link" "$name" "$target" 100
    ${sudo_prefix} update-alternatives --set "$name" "$target"
}

set_alternative /usr/bin/c++ c++ "$cxx"
set_alternative /usr/bin/cc cc "$cc"
set_alternative /usr/bin/g++ g++ "$cxx"
set_alternative /usr/bin/gcc gcc "$cc"

echo "==> Default C++ compiler: $(c++ --version | head -n 1)"

# Every "qbs build" in the workflow names no profile and so uses the machine-wide defaultProfile. Point
# that at the profile script/qbs-profile.sh keeps for the default compiler, and the workflow needs to
# know nothing about toolchains at all.
# shellcheck source=../qbs-profile.sh
source "$repo_root/script/qbs-profile.sh"

profile="${qbs_profile_args[0]#profile:}"
qbs config defaultProfile "$profile"
echo "==> qbs defaultProfile: $profile"

# The library is written against C++26. A compiler that does not take the flag fails every product with
# "unrecognized command-line option", which says nothing about why CI chose it.
#
# Asked of the compiler the profile resolves to, and not of "c++": a profile is what qbs builds through,
# and the two have already differed once. qbs assembles the command as installPath + toolchainPrefix +
# compilerName, so a profile made from a Debian driver can name a different binary than the alternative
# it was derived from - and a check on "c++" then passes while every translation unit fails.
profile_cxx="$(qbs_profile_cxx_compiler "$profile")"
if [[ -z "$profile_cxx" || ! -x "$profile_cxx" ]]; then
    {
        echo "ERROR: qbs profile '$profile' resolves to no usable C++ compiler${profile_cxx:+ ('$profile_cxx')}."
        echo "       Name a compiler with KMX_CI_CXX / KMX_CI_CC, or a profile with QBS_PROFILE."
    } >&2
    exit 1
fi

echo "==> Profile C++ compiler: $profile_cxx ($("$profile_cxx" --version | head -n 1))"

if ! echo 'int main() {}' | "$profile_cxx" -std=c++26 -x c++ -fsyntax-only - 2>/dev/null; then
    {
        echo "ERROR: the compiler qbs profile '$profile' builds with does not accept -std=c++26, which"
        echo "       this project needs."
        echo "       $profile_cxx: $("$profile_cxx" --version | head -n 1)"
        echo "       Name a newer one with KMX_CI_CXX / KMX_CI_CC."
    } >&2
    exit 1
fi
