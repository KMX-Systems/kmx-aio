#!/usr/bin/env bash
# Makes a C++26-capable compiler the machine's default, and points qbs at it.
#
# The build scripts under script/ compile with whatever "c++" and "cc" resolve to; see the comment at
# the top of script/qbs-profile.sh. That leaves CI with one job - to make the default a compiler that
# can build this project - instead of naming a compiler in every qbs command, which is what the
# workflow used to do and what left four copies of "g++-14" to update by hand.
#
# Two version numbers are written down here, and they are the only ones in the repository's build
# scripts: the library uses C++26 that arrived in GCC 16 and in Clang 23, and no runner image ships
# either. Ubuntu 24.04's own lists stop at GCC 14 and Clang 20 - its stock default is GCC 13.3, which
# does not recognise -std=c++26 at all - so the newest compiler apt offers is not enough on its own, and
# an extra source is added for whichever family can meet the floor.
#
# GCC is tried first, and not only because the workflow has always been a GCC build: Ubuntu's GCC passes
# --as-needed to the linker by default and the compilers usually installed alongside this project do not,
# which is what makes CI catch link-order failures that cannot be reproduced locally at all (see
# documentation/build.md). A source is added only once its family has been found wanting, so an image new
# enough to carry its own GCC 16 pulls in no third-party repository.
#
# KMX_CI_CXX / KMX_CI_CC name a compiler to use instead, for a runner that already has one. The floor is
# checked at the end against whichever compiler the qbs profile ends up resolving to, so it holds for
# that route too.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

sudo_prefix=""
if [[ "${EUID}" -ne 0 ]]; then
    sudo_prefix="sudo"
fi

# The oldest release of each family that implements the C++26 the library is written against; see the
# Platform Scope section of documentation/known-limitations.md.
minimum_version_gcc=16
minimum_version_clang=23

# The highest N among the packages apt lists for a family: g++-N for GCC, clang-N for Clang, whose one
# package carries clang++-N beside the C driver. Prints nothing when apt lists none.
newest_available_version() {
    local pattern
    case "$1" in
        gcc) pattern='^g\+\+-[0-9]+$' ;;
        clang) pattern='^clang-[0-9]+$' ;;
        *) return 1 ;;
    esac

    # One expression for both families: the greedy [^ ]* stops at the last "-" that a number follows.
    apt-cache search --names-only "$pattern" 2>/dev/null |
        sed -n 's/^[^ ]*-\([0-9][0-9]*\) .*/\1/p' |
        sort -n | tail -n 1
}

# The extra apt source carrying a new enough compiler of a family, for an image whose own lists do not.
# Each is the upstream's own: the PPA Ubuntu has served newer GCCs from for a decade, and llvm.sh, which
# LLVM supports as its installer and which wants the version named rather than discovered - so it is
# handed the floor, the oldest Clang that can build this project at all.
add_family_source() {
    local installer

    case "$1" in
        gcc)
            echo "==> Adding ppa:ubuntu-toolchain-r/test"
            ${sudo_prefix} apt-get install -y software-properties-common
            ${sudo_prefix} add-apt-repository -y ppa:ubuntu-toolchain-r/test
            ${sudo_prefix} apt-get update
            ;;
        clang)
            echo "==> Adding apt.llvm.org for Clang ${minimum_version_clang}"
            ${sudo_prefix} apt-get install -y curl lsb-release software-properties-common gnupg
            installer="$(mktemp)"
            curl -fsSL https://apt.llvm.org/llvm.sh -o "$installer"
            ${sudo_prefix} bash "$installer" "${minimum_version_clang}"
            rm -f "$installer"
            ;;
    esac
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
    for family in gcc clang; do
        floor_name="minimum_version_${family}"
        floor="${!floor_name}"

        # "|| true" and then the emptiness check, not the bare pipeline: this script runs under "set -e",
        # and a command substitution passes its pipeline's status straight out, so a host without
        # apt-cache would abort here with no output at all rather than reaching the diagnostics below.
        version="$(newest_available_version "$family" || true)"

        # Both tests short-circuit on the empty string before it can reach an arithmetic comparison, which
        # would abort the script rather than move on to the next family.
        if [[ -z "$version" || "$version" -lt "$floor" ]]; then
            echo "==> apt offers ${family} ${version:-nothing}, short of the ${floor} this project needs"
            add_family_source "$family"
            version="$(newest_available_version "$family" || true)"
        fi

        [[ -n "$version" && "$version" -ge "$floor" ]] || continue

        echo "==> Installing ${family} ${version}"
        case "$family" in
            gcc)
                ${sudo_prefix} apt-get install -y "gcc-${version}" "g++-${version}"
                cxx="/usr/bin/g++-${version}"
                cc="/usr/bin/gcc-${version}"
                ;;
            clang)
                ${sudo_prefix} apt-get install -y "clang-${version}"
                cxx="/usr/bin/clang++-${version}"
                cc="/usr/bin/clang-${version}"
                ;;
        esac
        break
    done

    if [[ -z "$cxx" ]]; then
        {
            echo "ERROR: no compiler this project can build with could be installed. Its C++26 needs"
            echo "       GCC ${minimum_version_gcc} or Clang ${minimum_version_clang}, and neither apt nor the sources added above"
            echo "       offered one."
            echo "       KMX_CI_CXX / KMX_CI_CC name a compiler to use instead."
        } >&2
        exit 1
    fi
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

# What the checks below are asked of: the compiler this profile resolves to, and not "c++". A profile is
# what qbs builds through, and the two have already differed once - qbs assembles the command as
# installPath + toolchainPrefix + compilerName, so a profile made from a Debian driver can name a
# different binary than the alternative it was derived from. Checking "c++" then passes while every
# translation unit fails on "unrecognized command-line option '-std=c++26'", naming a compiler nothing in
# CI had asked for.
profile_cxx="$(qbs_profile_cxx_compiler "$profile")"
if [[ -z "$profile_cxx" || ! -x "$profile_cxx" ]]; then
    {
        echo "ERROR: qbs profile '$profile' resolves to no usable C++ compiler${profile_cxx:+ ('$profile_cxx')}."
        echo "       Name a compiler with KMX_CI_CXX / KMX_CI_CC, or a profile with QBS_PROFILE."
    } >&2
    exit 1
fi

echo "==> Profile C++ compiler: $profile_cxx ($("$profile_cxx" --version | head -n 1))"

# The floor again, and this time against the compiler that will actually build. Everything above chose a
# compiler; this asks the profile what came of that choice, which is the only question the rest of CI
# depends on - KMX_CI_CXX reaches here having been checked by nothing, and so does an image whose own
# lists already offered a GCC 16.
#
# "-dumpversion" rather than the --version banner, because every family answers it with the version and
# nothing else. The leading integer is all that is compared: GCC prints "16" or "16.2.0" depending on the
# release, and a Clang snapshot prints "23.1.1" or "24.0.0git".
profile_type="$(qbs_compiler_toolchain_type "$profile_cxx")"
floor_name="minimum_version_${profile_type}"
floor="${!floor_name:-0}"
profile_version="$("$profile_cxx" -dumpversion 2>/dev/null | sed -n 's/^\([0-9][0-9]*\).*/\1/p')"

if [[ -z "$profile_version" || "$profile_version" -lt "$floor" ]]; then
    {
        echo "ERROR: qbs profile '$profile' builds with ${profile_type} ${profile_version:-of no readable version},"
        echo "       short of the ${profile_type} ${floor} this project's C++26 needs."
        echo "       $profile_cxx: $("$profile_cxx" --version | head -n 1)"
        echo "       Name a newer one with KMX_CI_CXX / KMX_CI_CC."
    } >&2
    exit 1
fi

# Version numbers say what a release implements, not that this copy of it works: a compiler installed
# without its C++ headers, or one whose driver cannot find its own backend, passes the check above and
# then fails every product. One translation unit settles it.
if ! echo 'int main() {}' | "$profile_cxx" -std=c++26 -x c++ -fsyntax-only - 2>/dev/null; then
    {
        echo "ERROR: ${profile_type} ${profile_version} at '$profile_cxx' is new enough for this project"
        echo "       but cannot compile an empty program at -std=c++26, so its installation is incomplete."
        echo "       $("$profile_cxx" --version | head -n 1)"
    } >&2
    exit 1
fi
