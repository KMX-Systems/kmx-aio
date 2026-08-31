#!/usr/bin/env bash
# Chooses the qbs profile the build scripts run under, exposing it as qbs_profile_args.
#
# A qbs command that names no profile falls back to the machine-wide defaultProfile, a setting this
# repository does not control. When that entry points at a compiler that is not installed - a profile
# created for a toolchain that was later renamed or removed - every build fails with "Could not find
# selected C++ compiler", which says nothing about the project and does not name the profile at fault.
#
# So the profile is chosen here. QBS_PROFILE wins when set. Otherwise the first preferred profile whose
# C++ compiler is actually present is used: the test runners put /opt/gcc-16/lib64 on LD_LIBRARY_PATH,
# so a GCC 16 profile is the toolchain the rest of these scripts already assume. Failing that the
# machine default is kept - which is what CI images want - but it is checked first, so a broken default
# is reported here with the list of profiles that would work instead.

qbs_preferred_profiles=(gcc16 gcc-16)
qbs_profile_args=()

qbs_profile_setting() {
    local raw
    raw="$(qbs config --list "profiles.$1.$2" 2>/dev/null | head -n 1)"
    [[ -n "$raw" ]] || return 0
    sed -e 's/^[^:]*:[[:space:]]*//' -e 's/^"//' -e 's/"$//' <<< "$raw"
}

# Prints the C++ compiler a profile resolves to, the same way qbs derives it: an explicit
# cpp.cxxCompilerName if the profile sets one, otherwise the toolchain type's default name, looked up
# under cpp.toolchainInstallPath when that is set and in PATH when it is not.
qbs_profile_cxx_compiler() {
    local profile="$1"
    local compiler_name install_path

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

    install_path="$(qbs_profile_setting "$profile" cpp.toolchainInstallPath)"
    if [[ -n "$install_path" ]]; then
        echo "${install_path}/${compiler_name}"
    else
        command -v "$compiler_name" || true
    fi
}

qbs_profile_is_usable() {
    local compiler
    compiler="$(qbs_profile_cxx_compiler "$1")"
    [[ -n "$compiler" && -x "$compiler" ]]
}

qbs_configured_profiles() {
    qbs config --list profiles 2>/dev/null | sed -n 's/^profiles\.\([^.]*\)\..*/\1/p' | sort -u
}

select_qbs_profile() {
    local profile

    if [[ -n "${QBS_PROFILE:-}" ]]; then
        qbs_profile_args=("profile:${QBS_PROFILE}")
        echo "==> Using qbs profile ${QBS_PROFILE} (from QBS_PROFILE)"
        return 0
    fi

    for profile in "${qbs_preferred_profiles[@]}"; do
        if qbs_profile_is_usable "$profile"; then
            qbs_profile_args=("profile:${profile}")
            echo "==> Using qbs profile ${profile}"
            return 0
        fi
    done

    local default_profile
    default_profile="$(qbs config defaultProfile 2>/dev/null | sed -e 's/^[^:]*:[[:space:]]*//' -e 's/^"//' -e 's/"$//')"

    if [[ -n "$default_profile" ]] && ! qbs_profile_is_usable "$default_profile"; then
        {
            echo "ERROR: the qbs default profile '${default_profile}' selects a C++ compiler that is not installed:"
            echo "           $(qbs_profile_cxx_compiler "$default_profile")"
            echo "       Choose another one with QBS_PROFILE=<name>, or repair the profile itself."
            echo "       Profiles configured on this machine:"
            qbs_configured_profiles | sed 's/^/           /'
        } >&2
        exit 1
    fi

    qbs_profile_args=()
}

select_qbs_profile
