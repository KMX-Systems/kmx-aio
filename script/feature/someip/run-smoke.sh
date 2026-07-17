#!/usr/bin/env bash
set -euo pipefail

# Build and run SOME/IP-focused tests, with optional sample smoke execution.

usage() {
    cat <<'USAGE'
Run SOME/IP tests and optional sample smoke.

Usage:
    script/feature/someip/run-smoke.sh [options]

Options:
  --skip-build      Do not run qbs resolve/build.
  --skip-samples    Do not run SOME/IP sample server/client smoke.
  -h, --help        Show this help.
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
source_dir="$repo_root/source"
run_build=true
run_samples=true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            run_build=false
            ;;
        --skip-samples)
            run_samples=false
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
    shift
done

if [[ "$run_build" == "true" ]]; then
    echo "==> Resolving/building SOME/IP targets"
    (
        cd "$source_dir"
        qbs resolve -f source.qbs "${qbs_build_dir_args[@]}" config:debug project.enable_someip:true
        qbs build -f source.qbs "${qbs_build_dir_args[@]}" config:debug project.enable_someip:true -j"$(nproc)"
    )
fi

test_bin="$(find "$qbs_build_root/debug" -type f -name kmx-aio-test | head -n 1 || true)"
if [[ -z "$test_bin" ]]; then
    echo "kmx-aio-test binary not found" >&2
    exit 1
fi

libstdcpp_path="$(g++ -print-file-name=libstdc++.so)"
libstdcpp_dir="$(dirname "$libstdcpp_path")"

echo "==> Running SOME/IP tests"
LD_LIBRARY_PATH="$libstdcpp_dir:${LD_LIBRARY_PATH:-}" "$test_bin" "[someip]"

if [[ "$run_samples" == "true" ]]; then
    echo "==> Running SOME/IP sample smoke"

    sample_server="$(find "$qbs_build_root/debug" -type f -name sample-someip-echo-server | head -n 1 || true)"
    sample_client="$(find "$qbs_build_root/debug" -type f -name sample-someip-echo-client | head -n 1 || true)"
    sample_publisher="$(find "$qbs_build_root/debug" -type f -name sample-someip-event-publisher | head -n 1 || true)"
    sample_subscriber="$(find "$qbs_build_root/debug" -type f -name sample-someip-event-subscriber | head -n 1 || true)"
    sample_diagnostics="$(find "$qbs_build_root/debug" -type f -name sample-someip-diagnostics | head -n 1 || true)"

    if [[ -z "$sample_server" || -z "$sample_client" || -z "$sample_publisher" || -z "$sample_subscriber" || -z "$sample_diagnostics" ]]; then
        echo "SOME/IP sample binaries not found (echo/pubsub/diagnostics)" >&2
        exit 1
    fi

    server_log="/tmp/kmx_someip_smoke_server.log"
    client_log="/tmp/kmx_someip_smoke_client.log"
    publisher_log="/tmp/kmx_someip_smoke_publisher.log"
    subscriber_log="/tmp/kmx_someip_smoke_subscriber.log"
    diagnostics_log="/tmp/kmx_someip_smoke_diagnostics.log"

    LD_LIBRARY_PATH="$libstdcpp_dir:${LD_LIBRARY_PATH:-}" bash -lc "'$sample_server' > '$server_log' 2>&1 & \
srv=\$!; \
sleep 1; \
timeout 5s '$sample_client' > '$client_log' 2>&1; \
rc=\$?; \
kill \"\$srv\" >/dev/null 2>&1 || true; \
wait \"\$srv\" >/dev/null 2>&1 || true; \
exit \"\$rc\""

    grep -E "SOMEIP_ECHO_CLIENT_START|SOMEIP_ECHO_CLIENT_DONE" "$client_log" >/dev/null
    grep -E "SOMEIP_ECHO_SERVER_START|SOMEIP_ECHO_SERVER_STOP" "$server_log" >/dev/null

    LD_LIBRARY_PATH="$libstdcpp_dir:${LD_LIBRARY_PATH:-}" bash -lc "'$sample_publisher' > '$publisher_log' 2>&1 & \
srv=\$!; \
sleep 1; \
timeout 8s '$sample_subscriber' > '$subscriber_log' 2>&1; \
rc=\$?; \
kill \"\$srv\" >/dev/null 2>&1 || true; \
wait \"\$srv\" >/dev/null 2>&1 || true; \
exit \"\$rc\""

    grep -E "SOMEIP_EVENT_PUBLISHER_START|SOMEIP_EVENT_PUBLISHER_STOP" "$publisher_log" >/dev/null
    grep -E "SOMEIP_EVENT_SUBSCRIBER_START|SOMEIP_EVENT_SUBSCRIBER_DONE" "$subscriber_log" >/dev/null

    LD_LIBRARY_PATH="$libstdcpp_dir:${LD_LIBRARY_PATH:-}" timeout 8s "$sample_diagnostics" > "$diagnostics_log" 2>&1
    grep -E "SOMEIP_DIAGNOSTICS_DONE" "$diagnostics_log" >/dev/null
fi

echo "==> SOME/IP tests completed successfully"
