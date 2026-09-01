#!/usr/bin/env bash
# Builds every translation unit in the tree with one toolchain.
#
# There is no single set of feature flags that compiles the whole project. source/source.qbs offers a
# project.full aggregate, but turning it on puts QUIC next to SPDK and OPC UA in one binary, and those
# two are linked against the system OpenSSL while QUIC brings BoringSSL - the same symbol names over
# different layouts, which links without a diagnostic and reads an SSL_CTX at the wrong offsets at run
# time. source.qbs warns about exactly that combination. A whole-tree build is therefore several builds:
# a handful of feature sets that are each internally consistent and that together leave no .cpp
# uncompiled.
#
# Every set gets its own build root under output/full-<toolchain>/<set>, so the passes cannot overwrite
# one another's artifacts and rebuilding one set does not throw the others away.
#
# The toolchain is not chosen here: script/clang_full_build.sh and script/gcc_full_build.sh are the
# entry points, and each names the profile and the extra properties its compiler needs.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Brings in repo_root, source_dir, the feature list and its KMX_ENABLE_*/qbs property translation
# (build_qbs_feature_args), the instrumentation properties and, through script/qbs-profile.sh, the
# profile selection. Nothing about feature naming is repeated here - the test runners and this script
# have to agree on what "modbus is on" means.
source "$script_dir/feature/common.sh"

# The passes, in the order they are built.
#
# Read together they name every entry of feature_list in script/feature/common.sh. readiness and
# openonload appear in all of them: neither conflicts with anything, and both change what the other
# features compile (readiness carries the epoll backend the modbus and v4l2 code sits on, openonload is
# a define that switches an alternative socket path on in every product).
full_build_sets=(quic storage avb)

set_features() {
    case "$1" in
        # BoringSSL lives here, so nothing that carries the system OpenSSL may join this set.
        quic)    echo "readiness openonload http2 http3 quic modbus cuda" ;;
        # The OpenSSL half: SPDK and open62541 are prebuilt against it, vsomeip sits next to them.
        storage) echo "readiness openonload af_xdp spdk opc_ua someip modbus" ;;
        # AVB pulls in the gPTP/SRP tree, which nothing else compiles.
        avb)     echo "readiness openonload af_xdp avb modbus v4l2" ;;
        *)       return 1 ;;
    esac
}

config="debug"
jobs="$(nproc)"
clean=false
keep_going=false
requested_sets=()
extra_properties=()
build_root_override=""

# What the wrapper scripts pass in: a tag that names the toolchain in the build root, and a list of
# profiles to try in order. QBS_PROFILE still wins over both, so a one-off build against some other
# profile needs no change here.
toolchain_tag="${KMX_FULL_BUILD_TAG:-build}"

usage() {
    cat <<'USAGE'
Builds the whole tree, one pass per feature set.

Usage:
    script/full-build.sh [options] [-- <extra qbs properties>]

Options:
  --config <name>        Qbs configuration to build (default: debug).
  --set <name>           Build only this set; repeatable. Default: all of them.
  --list-sets            Print the sets and the features they enable, then exit.
  --jobs <n>             Parallel jobs (default: nproc).
  --clean                Delete each set's build root before building it.
  --keep-going           Build the remaining sets after one fails, and fail at the end.
  --build-root <dir>     Parent directory for the per-set build roots.
  --qbs-property <k:v>   Extra property for every qbs invocation; repeatable.
  -h, --help             This text.

Environment:
  QBS_PROFILE            Overrides the profile the wrapper script picked.
  KMX_BUILD_ROOT         Same as --build-root.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) shift; config="${1:?--config needs a value}" ;;
        --set) shift; requested_sets+=("${1:?--set needs a value}") ;;
        --jobs|-j) shift; jobs="${1:?--jobs needs a value}" ;;
        --build-root) shift; build_root_override="${1:?--build-root needs a value}" ;;
        --qbs-property) shift; extra_properties+=("${1:?--qbs-property needs a value}") ;;
        --clean) clean=true ;;
        --keep-going) keep_going=true ;;
        --list-sets)
            for set_name in "${full_build_sets[@]}"; do
                printf '%-10s %s\n' "$set_name" "$(set_features "$set_name")"
            done
            exit 0
            ;;
        --help|-h) usage; exit 0 ;;
        --) shift; extra_properties+=("$@"); break ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
    shift
done

if [[ ${#requested_sets[@]} -gt 0 ]]; then
    for set_name in "${requested_sets[@]}"; do
        if ! set_features "$set_name" >/dev/null; then
            echo "ERROR: unknown feature set '$set_name'; known sets: ${full_build_sets[*]}" >&2
            exit 1
        fi
    done
    full_build_sets=("${requested_sets[@]}")
fi

# script/qbs-profile.sh has already had its say - it honours QBS_PROFILE and otherwise prefers a GCC 16
# profile, which is the right answer for the test runners and the wrong one for a clang build. When the
# wrapper offered candidates of its own, the first installed one replaces that choice.
select_full_build_profile() {
    [[ -z "${QBS_PROFILE:-}" ]] || return 0
    [[ -n "${KMX_FULL_BUILD_PROFILES:-}" ]] || return 0

    local -a candidates=()
    read -r -a candidates <<< "$KMX_FULL_BUILD_PROFILES"

    local profile
    for profile in "${candidates[@]}"; do
        if qbs_profile_is_usable "$profile"; then
            qbs_profile_args=("profile:$profile")
            echo "==> Using qbs profile $profile"
            return 0
        fi
    done

    {
        echo "ERROR: none of these qbs profiles selects an installed C++ compiler: ${candidates[*]}"
        echo "       Name another one with QBS_PROFILE=<name>, or create one with 'qbs setup-toolchains'."
        echo "       Profiles configured on this machine:"
        qbs_configured_profiles | sed 's/^/           /'
    } >&2
    exit 1
}

select_full_build_profile

full_build_root="${build_root_override:-${KMX_BUILD_ROOT:-$repo_root/output/full-${toolchain_tag}}}"

build_set() {
    local set_name="$1"
    local build_dir="$full_build_root/$set_name"

    local -a features=()
    read -r -a features <<< "$(set_features "$set_name")"

    # Every feature is spelled out, on and off alike, and exported before build_qbs_feature_args reads
    # them: a feature left at its default here would be carried over from the previous pass's exports.
    # core and completion are on throughout - core is the library itself, and completion is the io_uring
    # backend the rest of the tree is written against.
    local feature
    for feature in "${feature_list[@]}"; do
        export "$(feature_env_name "$feature")=false"
    done
    for feature in core completion "${features[@]}"; do
        export "$(feature_env_name "$feature")=true"
    done

    local -a qbs_feature_args=()
    mapfile -t qbs_feature_args < <(build_qbs_feature_args)

    if [[ "$clean" == "true" && -d "$build_dir" ]]; then
        echo "==> [$set_name] removing $build_dir"
        rm -rf "$build_dir"
    fi

    echo "==> [$set_name] building ${features[*]}"
    echo "==> [$set_name] build root $build_dir"

    (
        cd "$source_dir"
        qbs resolve -f source.qbs -d "$build_dir" "${qbs_profile_args[@]}" "config:$config" \
            "${qbs_feature_args[@]}" "${qbs_instrumentation_args[@]}" "${extra_properties[@]}"
        qbs build -f source.qbs -d "$build_dir" "${qbs_profile_args[@]}" "config:$config" \
            -j"$jobs" "${qbs_feature_args[@]}" "${qbs_instrumentation_args[@]}" "${extra_properties[@]}"
    )
}

echo "==> Full build: config $config, ${#full_build_sets[@]} set(s), $jobs job(s)"
[[ ${#extra_properties[@]} -eq 0 ]] || echo "==> Extra qbs properties: ${extra_properties[*]}"

results=()
failed=false

for set_name in "${full_build_sets[@]}"; do
    started="$SECONDS"
    if build_set "$set_name"; then
        results+=("$(printf 'ok      %-10s %ds' "$set_name" "$((SECONDS - started))")")
    else
        failed=true
        results+=("$(printf 'FAILED  %-10s %ds' "$set_name" "$((SECONDS - started))")")
        if [[ "$keep_going" != "true" ]]; then
            break
        fi
    fi
done

echo "==> Full build summary"
printf '    %s\n' "${results[@]}"

if [[ "$failed" == "true" ]]; then
    echo "==> Full build failed" >&2
    exit 1
fi

echo "==> Full build completed successfully"
