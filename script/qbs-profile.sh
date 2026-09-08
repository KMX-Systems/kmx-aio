#!/usr/bin/env bash
# Chooses the qbs profile the build scripts run under, exposing it as qbs_profile_args.
#
# The toolchain is not named here, and no version of one appears anywhere in this repository's build
# scripts. What gets used is the machine's default C++ compiler - whatever "c++" and "cc" resolve to,
# which on a Debian-family system is what update-alternatives points them at. Change the alternative and
# the next build follows it, with nothing to edit here.
#
# Qbs cannot be pointed at a bare compiler, only at a profile, and the machine-wide defaultProfile is a
# setting this repository does not control: it routinely names a toolchain that was since renamed,
# removed or demoted, and then every build fails with "Could not find selected C++ compiler" - or, worse,
# quietly builds with a compiler that is no longer the default. So a profile is found or made for the
# compiler that is actually wanted:
#
#   1. QBS_PROFILE names a profile outright, and wins over everything below.
#   2. Otherwise the wanted compiler is KMX_CXX (default "c++") with KMX_CC (default "cc") beside it, and
#      this script keeps a profile of its own named after that command - kmx-cxx for the default "c++",
#      kmx-gxx for "g++", and so on. It is created on first use and rewritten whenever the command starts
#      resolving to a different toolchain, so following an alternative to another compiler needs no
#      further action.
#
# Profiles that were not created here are never picked up on their own, however well they happen to match
# the compiler: a profile is a bundle of build settings and not just a compiler path, and adopting one on
# the strength of its compiler alone would quietly apply the rest of it. QBS_PROFILE is how a hand-made
# profile gets used.
#
# Three traps shape how the profiles below are written, and each cost an afternoon to find.
#
# Clang picks C or C++ driver mode from the name it was invoked under. A profile that names the resolved
# "clang-24" binary compiles C++ sources happily and then fails every link with undefined references to
# std::cout, because a driver named "clang" links no C++ standard library - so the name written into a
# profile always contains "++".
#
# And qbs joins cpp.cxxCompilerName onto cpp.toolchainInstallPath without noticing that the name might
# already be absolute, which turns "/usr/bin/c++" into "/usr/lib/llvm-24/bin//usr/bin/c++" and resolves
# no products at all - so the name written into a profile is always bare, and the resolved binary is used
# only to find the directory it lives in.
#
# And a Debian-style driver such as "/usr/bin/g++-14" - which is what "c++" resolves to once
# update-alternatives is pointed at a newer GCC - makes setup-toolchains record a cpp.toolchainPrefix of
# "x86_64-linux-gnu-" and drop the version altogether. Leaving cpp.cxxCompilerName out then does not mean
# "the compiler this profile was made from": qbs assembles installPath + prefix + its own default name
# and runs "/usr/bin/x86_64-linux-gnu-g++", the triplet symlink the distro still points at the stock GCC.
# So the driver's own name is always written down, even when the directory already answers to it under
# the default name - the prefix is not known until setup-toolchains has run, so there is no moment at
# which omitting the name can be shown to be safe.

qbs_profile_args=()

# "gcc" or "clang" for whichever profile was selected, so a caller can apply a workaround that belongs to
# one compiler without knowing which profile it ended up with. Set by select_qbs_profile below.
qbs_profile_toolchain_type=""

qbs_profile_setting() {
    local raw
    raw="$(qbs config --list "profiles.$1.$2" 2>/dev/null | head -n 1)"
    [[ -n "$raw" ]] || return 0
    sed -e 's/^[^:]*:[[:space:]]*//' -e 's/^"//' -e 's/"$//' <<< "$raw"
}

# Prints the C++ compiler a profile resolves to, the same way qbs derives it: an explicit
# cpp.cxxCompilerName if the profile sets one, otherwise the toolchain type's default name - either way
# behind cpp.toolchainPrefix, looked up under cpp.toolchainInstallPath when that is set and in PATH when
# it is not.
qbs_profile_cxx_compiler() {
    local profile="$1"
    local compiler_name install_path prefix

    compiler_name="$(qbs_profile_setting "$profile" cpp.cxxCompilerName)"
    if [[ -z "$compiler_name" ]]; then
        case "$(qbs_profile_setting "$profile" qbs.toolchainType)" in
            clang) compiler_name="clang++" ;;
            gcc) compiler_name="g++" ;;
            *) return 0 ;;
        esac
    fi

    if [[ "$compiler_name" == */* ]]; then
        echo "$compiler_name"
        return 0
    fi

    # The prefix goes in front of a stored name as much as in front of a default one: the name is written
    # without it precisely because qbs puts it back. Dropping it here would report the wrong binary for
    # exactly the profiles where the two differ.
    prefix="$(qbs_profile_setting "$profile" cpp.toolchainPrefix)"
    compiler_name="${prefix}${compiler_name}"

    install_path="$(qbs_profile_setting "$profile" cpp.toolchainInstallPath)"
    if [[ -n "$install_path" ]]; then
        echo "${install_path}/${compiler_name}"
    else
        command -v "$compiler_name" || true
    fi
}

# The compiler family a profile selects. Profiles written by "qbs setup-toolchains" say so in
# qbs.toolchainType; older ones, and the ones Qt Creator writes, only carry the qbs.toolchain list.
qbs_profile_type() {
    local profile="$1" type

    type="$(qbs_profile_setting "$profile" qbs.toolchainType)"
    if [[ -z "$type" ]]; then
        case "$(qbs_profile_setting "$profile" qbs.toolchain)" in
            *clang*) type="clang" ;;
            *gcc*) type="gcc" ;;
        esac
    fi

    echo "$type"
}

# The newest versioned spelling of a compiler command, for a machine that has no unversioned one.
#
# Debian ships versioned drivers such as /usr/bin/clang++-24 and /usr/bin/g++-17, and leaves the plain
# "clang++" to the "clang" metapackage, which an installation that pulled in only the base clang package
# does not necessarily have. Naming a compiler family rather than a version, which is what the wrapper
# scripts do, therefore has to reach the versioned driver: without this, script/clang_full_build.sh
# cannot run at all on such a machine unless the caller happens to know to set KMX_CXX.
qbs_newest_versioned_compiler() {
    local base="$1"
    local -a dirs=()
    local dir path name version best="" best_version=-1

    IFS=: read -r -a dirs <<< "${PATH:-}"
    for dir in "${dirs[@]}"; do
        [[ -n "$dir" && -d "$dir" ]] || continue

        for path in "$dir/$base"-*; do
            [[ -f "$path" && -x "$path" ]] || continue

            name="$(basename "$path")"
            version="${name#"$base"-}"
            # The version and nothing else, so "clang++-cpp" and the like are not mistaken for one.
            [[ "$version" =~ ^[0-9]+$ ]] || continue

            if ((version > best_version)); then
                best_version="$version"
                best="$path"
            fi
        done
    done

    [[ -n "$best" ]] || return 1
    echo "$best"
}

# The absolute path of a compiler command, with its own name kept: see the driver-mode note at the top
# of this file for why the symlink is not followed here.
qbs_compiler_path() {
    local command_name="$1" path

    if [[ "$command_name" == */* ]]; then
        path="$command_name"
    else
        path="$(command -v "$command_name" 2>/dev/null || true)"
        # Nothing under the bare name, so the newest versioned spelling of it stands in.
        [[ -n "$path" ]] || path="$(qbs_newest_versioned_compiler "$command_name" || true)"
    fi

    [[ -n "$path" && -x "$path" ]] || return 1
    echo "$path"
}

# gcc or clang, asked of the compiler rather than guessed from its name: "c++" and "cc" say nothing, and
# a machine where they point at clang is exactly the case this has to get right.
#
# The output is captured and then matched, rather than piped into grep. "grep -q" exits at the first
# match and closes the pipe, whatever is writing ahead of it dies of SIGPIPE with status 141, and every
# script that sources this file runs under "set -o pipefail" - where that status becomes the pipeline's
# own and the test reads as "no match". A clang would be reported as gcc, and the compiler-specific
# workarounds that key on the answer would silently stop applying. script/feature/pic.sh notes the same
# trap for readelf.
qbs_compiler_toolchain_type() {
    local version
    version="$("$1" --version 2>/dev/null)" || version=""
    version="${version%%$'\n'*}"

    if [[ "${version,,}" == *clang* ]]; then
        echo "clang"
    else
        echo "gcc"
    fi
}

# The profile this script maintains for a given compiler command. Named after the command - "c++" gives
# kmx-cxx, "g++" gives kmx-gxx - and not after the compiler it currently resolves to, so that following
# an alternative to a new version rewrites one profile instead of leaving a trail of them, and so that
# two scripts asking for the same compiler under different names do not fight over one profile.
qbs_generated_profile_name() {
    local name
    name="$(basename "$1")"
    echo "kmx-$(sed -e 's/++/xx/g' -e 's/[^A-Za-z0-9]/-/g' <<< "$name")"
}

# The C++ driver of a toolchain, named as it is spelled inside the toolchain's own directory.
#
# Clang picks C or C++ driver mode from the name it was invoked under, so a toolchain whose real binary
# is "clang-24" has to be called through its "clang++" alias: invoked as "clang" it compiles C++ sources
# but links no C++ standard library, and every link ends in undefined references to std::cout. GCC has no
# such rule - "g++-17" is already the C++ driver - so its own basename is the answer there.
qbs_toolchain_cxx_name() {
    local install_dir="$1" toolchain_type="$2" name="$3"
    local candidate

    if [[ "$toolchain_type" != "clang" || "$name" == *++* ]]; then
        echo "$name"
        return 0
    fi

    # clang-24 -> clang++-24, clang -> clang++.
    candidate="${name/clang/clang++}"
    for candidate in "$candidate" "clang++"; do
        if [[ -x "$install_dir/$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

# The C driver beside a given C++ driver, preferring the compiler the caller actually named and falling
# back to the conventional spelling of the same version: clang++-24 goes with clang-24, g++-17 with
# gcc-17. Prints nothing when neither is there, which leaves qbs to its own default for the toolchain.
qbs_toolchain_cc_name() {
    local install_dir="$1" toolchain_type="$2" cxx_name="$3" cc="$4"
    local candidate cc_real
    local -a candidates=()

    # The compiler the caller named, accepted only when it belongs beside the C++ driver in every sense:
    # same directory, not itself a C++ driver, and the same family. "cc" and "c++" are separate
    # alternatives and KMX_CC and KMX_CXX are separate variables, so a GCC C driver arriving alongside a
    # clang C++ one is a configuration reached by accident rather than on purpose - and qbs would then
    # compile the C sources with GCC under clang-derived flags without saying anything.
    if [[ -n "$cc" ]]; then
        cc_real="$(readlink -f "$cc")"
        if [[ "$(dirname "$cc_real")" == "$install_dir" ]]; then
            candidate="$(basename "$cc_real")"
            if [[ "$candidate" != *++* && "$(qbs_compiler_toolchain_type "$cc_real")" == "$toolchain_type" ]]; then
                echo "$candidate"
                return 0
            fi
        fi
    fi

    # Per family, because the two substitutions are not interchangeable: applying the clang one to
    # "g++-17" leaves it untouched, and a C++ driver named as the C compiler drags libstdc++ into
    # every C-only product that links through it.
    if [[ "$toolchain_type" == "clang" ]]; then
        candidates=("${cxx_name/clang++/clang}" clang)
    else
        candidates=("${cxx_name/g++/gcc}" gcc)
    fi

    for candidate in "${candidates[@]}"; do
        # Still a C++ driver: the substitution found nothing to replace, so this is not the C compiler.
        [[ "$candidate" == *++* ]] && continue

        if [[ -n "$candidate" && -x "$install_dir/$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    return 0
}

# Creates or refreshes the profile for a compiler, and prints its name.
#
# setup-toolchains is what fills in the archiver, assembler and nm that belong to this toolchain rather
# than to whatever happens to be first in PATH, so it is handed the compiler's real binary and the
# directory that comes with it.
#
# The compiler names written afterwards are always written down, always bare and always contain "++"; see
# the three traps at the top of this file for what each of those avoids.
qbs_ensure_profile_for_compiler() {
    local cxx="$1" cc="$2"
    local profile toolchain_type cxx_real install_dir cxx_name cc_name

    profile="$(qbs_generated_profile_name "$cxx")"
    toolchain_type="$(qbs_compiler_toolchain_type "$cxx")"
    cxx_real="$(readlink -f "$cxx")"
    install_dir="$(dirname "$cxx_real")"

    # Named outright, and never left to qbs even when this directory's "g++" or "clang++" already is this
    # very compiler: what qbs falls back on is prefix + default name, and the prefix is setup-toolchains'
    # to choose further down. See the third trap at the top of this file for what the omission cost.
    if ! cxx_name="$(qbs_toolchain_cxx_name "$install_dir" "$toolchain_type" "$(basename "$cxx_real")")"; then
        {
            echo "ERROR: '$cxx' resolves to '$cxx_real', and no C++ driver sits beside it in"
            echo "       '$install_dir'. A clang invoked under a name without '++' links no C++ standard"
            echo "       library, so this toolchain cannot be used as it stands."
        } >&2
        return 1
    fi

    cc_name="$(qbs_toolchain_cc_name "$install_dir" "$toolchain_type" "$cxx_name" "$cc")"

    # Already in step with the compiler, so leave it alone: rewriting it on every build would re-resolve
    # the project each time. The install path is what moves when the command starts resolving to another
    # toolchain, and it is where the archiver and nm come from; a new version of the same compiler in the
    # same directory needs no rewrite. The stored name is put back together with its prefix first, for
    # the reason spelled out where it is written below.
    local stored_cxx_name
    stored_cxx_name="$(qbs_profile_setting "$profile" cpp.cxxCompilerName)"
    if [[ -n "$stored_cxx_name" ]]; then
        stored_cxx_name="$(qbs_profile_setting "$profile" cpp.toolchainPrefix)${stored_cxx_name}"
    fi

    if [[ "$stored_cxx_name" == "$cxx_name" &&
          "$(qbs_profile_setting "$profile" qbs.toolchainType)" == "$toolchain_type" &&
          "$(qbs_profile_setting "$profile" cpp.toolchainInstallPath)" == "$install_dir" ]]; then
        echo "$profile"
        return 0
    fi

    # Dropped rather than overwritten: setup-toolchains adds keys and removes none, so a profile that
    # carried a compiler name for the previous toolchain would keep it here.
    qbs config --unset "profiles.${profile}" >/dev/null 2>&1 || true

    if ! qbs setup-toolchains --type "$toolchain_type" "$cxx_real" "$profile" >/dev/null 2>&1; then
        {
            echo "ERROR: could not create a qbs profile for '$cxx' (resolved to '$cxx_real')."
            echo "       Create one by hand and name it with QBS_PROFILE=<name>."
        } >&2
        return 1
    fi

    if [[ -n "$cxx_name" ]]; then
        # Written without the toolchain prefix. A cross-style compiler makes setup-toolchains set
        # cpp.toolchainPrefix - "x86_64-linux-gnu-" for the Debian g++-17 - and qbs then builds the
        # command as installPath + prefix + compilerName. Writing the full basename here leaves it
        # looking for "/usr/bin/x86_64-linux-gnu-x86_64-linux-gnu-g++-17".
        local prefix
        prefix="$(qbs_profile_setting "$profile" cpp.toolchainPrefix)"

        qbs config "profiles.${profile}.cpp.cxxCompilerName" "${cxx_name#"$prefix"}" >/dev/null
        [[ -z "$cc_name" ]] ||
            qbs config "profiles.${profile}.cpp.cCompilerName" "${cc_name#"$prefix"}" >/dev/null
    fi

    echo "$profile"
}

select_qbs_profile() {
    if [[ -n "${QBS_PROFILE:-}" ]]; then
        local profile_cxx

        qbs_profile_args=("profile:${QBS_PROFILE}")
        qbs_profile_toolchain_type="$(qbs_profile_type "$QBS_PROFILE")"

        # A hand-written profile, or one Qt Creator wrote, can carry neither qbs.toolchainType nor
        # qbs.toolchain. An empty answer there does not mean "no compiler", it means "not recorded" - and
        # a caller keying a compiler-specific workaround on this would quietly skip it for a profile that
        # needs it. So the profile's own compiler is asked, exactly as it is for a generated profile.
        if [[ -z "$qbs_profile_toolchain_type" ]]; then
            profile_cxx="$(qbs_profile_cxx_compiler "$QBS_PROFILE")"
            if [[ -n "$profile_cxx" && -x "$profile_cxx" ]]; then
                qbs_profile_toolchain_type="$(qbs_compiler_toolchain_type "$profile_cxx")"
            fi
        fi

        echo "==> Using qbs profile ${QBS_PROFILE} (from QBS_PROFILE)"
        return 0
    fi

    local cxx_command="${KMX_CXX:-c++}"
    local cc_command="${KMX_CC:-cc}"
    local cxx cc profile

    if ! cxx="$(qbs_compiler_path "$cxx_command")"; then
        {
            echo "ERROR: no C++ compiler '${cxx_command}' on PATH, under that name or a versioned one."
            if [[ "$cxx_command" == "c++" ]]; then
                echo "       This is the machine's default C++ compiler; install one, or point"
                echo "       update-alternatives --config c++ at the compiler you want to build with."
            fi
            echo "       KMX_CXX names another compiler, QBS_PROFILE another qbs profile."
        } >&2
        exit 1
    fi

    cc="$(qbs_compiler_path "$cc_command" || true)"

    if ! profile="$(qbs_ensure_profile_for_compiler "$cxx" "$cc")"; then
        exit 1
    fi

    qbs_profile_args=("profile:${profile}")
    qbs_profile_toolchain_type="$(qbs_compiler_toolchain_type "$cxx")"
    echo "==> Using qbs profile ${profile} (${cxx_command} -> $(readlink -f "$cxx"))"
}

select_qbs_profile
