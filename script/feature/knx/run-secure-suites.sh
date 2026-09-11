#!/usr/bin/env bash
set -euo pipefail

# Builds kmx-aio-test with KNX and both executor pillars against one TLS backend, and runs the KNX Secure suites on it.
#
#   bash script/feature/knx/run-secure-suites.sh openssl
#   bash script/feature/knx/run-secure-suites.sh boringssl   # QUIC's backend; bootstraps BoringSSL when it is missing
#
# The test binary is looked for after the build, and its absence fails the run: a failed BoringSSL bootstrap or a stale
# build graph still lets qbs exit 0 with nothing built.
#
# Environment:
#   KMX_BUILD_ROOT   where to build; default output/knx-secure-<backend>

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
backend="${1:-openssl}"
case "$backend" in
    openssl) quic=false ;;
    boringssl) quic=true ;;
    *)
        echo "Unknown backend '$backend': use openssl or boringssl." >&2
        exit 2
        ;;
esac
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-secure-$backend}"
# qbs runs from source/ below, so a relative root would land under source/ rather than under output/.
[[ "$build_root" = /* ]] || build_root="$repo_root/$build_root"
source "$repo_root/script/qbs-profile.sh"

if [[ "$backend" == boringssl ]]; then
    bash "$repo_root/script/bootstrap_optional_deps.sh" --quic
fi

features=(project.enable_knx:true project.enable_readiness:true project.enable_completion:true "project.enable_quic:$quic")
cd "$repo_root/source"
qbs resolve -d "$build_root" -f source.qbs "${qbs_profile_args[@]}" config:debug "${features[@]}"
qbs build -d "$build_root" -f source.qbs "${qbs_profile_args[@]}" config:debug "${features[@]}" --products kmx-aio-test

cd "$repo_root"
bin="$(find "$build_root/debug" -type f -name kmx-aio-test -not -path '*/install-root/*' -print -quit)"
if [[ -z "$bin" || ! -x "$bin" ]]; then
    echo "No kmx-aio-test was built under $build_root; the build did not produce the binary it reported." >&2
    exit 1
fi

source "$repo_root/script/feature/common.sh"
for tags in "[knx][secure]" "[knx][keyring]" "[knx][tcp]" "[knx][data_secure]"; do
    run_catch_tests timeout 600s "$bin" "$tags"
done
echo "KNX Secure suites passed on $backend."
