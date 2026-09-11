#!/usr/bin/env bash
set -euo pipefail

# Runs KNX Data Secure group communication between the in-tree stack and xknx on the loopback interface. Both ends key
# themselves from the vendored keyring.knxkeys, whose one group key secures 1/1/1; the in-tree side is 1.1.12 and xknx is
# 1.1.1, which the keyring lists as each other's senders.
#
#   bash script/feature/knx/interop/run-data-secure-interop.sh routing [xknx|calimero]
#       the in-tree router and the peer over KNX IP Secure routing: the router sends a secured switch-on to 1/1/1, and the
#       peer answers with a secured switch-off. Calimero's secure routing link has no port parameter, so that peer runs on
#       the standard port 3671; its Data Secure is Calimero's own SecureApplicationLayer
#   bash script/feature/knx/interop/run-data-secure-interop.sh tunnel
#       an xknx TCP tunnel through the in-tree server, which passes Data Secure through untouched, to an in-tree device:
#       xknx sends a secured switch-on to 1/1/1, and the device opens it, confirms it, and answers with a secured
#       switch-off
#
# xknx's debug log is kept under output/interop/captures/ as the capture the interoperability matrix names, with the
# in-tree side's output appended to it.
#
# Needs a built kmx-aio-test with KNX enabled, and xknx 3.20.0 in output/interop/xknx-venv.
#
# Environment:
#   KMX_BUILD_ROOT            where to find kmx-aio-test; default output/knx-release-gates
#   KMX_KNX_INTEROP_TIMEOUT   seconds either side waits; default 40

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
mode="${1:-routing}"
# The peer: xknx or Calimero over routing; the tunnel runs against xknx alone.
peer="${2:-xknx}"
[[ "$mode" == routing ]] || peer=xknx
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-release-gates}"
interop="$repo_root/output/interop"
captures="$interop/captures"
keyring="$repo_root/documentation/features/knx/conformance/keyrings/keyring.knxkeys"
timeout_s="${KMX_KNX_INTEROP_TIMEOUT:-40}"
python="$interop/xknx-venv/bin/python"

binary="$(find "$build_root" -type f -name kmx-aio-test -not -path '*/install-root/*' -printf '%T@ %p\n' 2>/dev/null |
    sort -nr | head -1 | cut -d' ' -f2-)"
if [[ -z "$binary" || ! -x "$binary" ]]; then
    echo "No kmx-aio-test under $build_root; build one with KNX enabled first." >&2
    exit 1
fi

mkdir -p "$captures"
capture="$captures/data-secure-$mode-$peer-$(date -u +%Y%m%dT%H%M%SZ).log"

# Runs one in-tree case with the port, keyring and timeout it reads.
run_in_tree() {
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib64}" KMX_KNX_INTEROP_PORT="$port" KMX_KNX_INTEROP_KEYRING="$keyring" \
        KMX_KNX_INTEROP_PASSWORD=pwd KMX_KNX_INTEROP_TIMEOUT="$timeout_s" "$binary" "$1" --success
}

in_tree_status=0
peer_status=0
case "$mode" in
    routing)
        if [[ "$peer" == calimero ]]; then
            port=3671
            "$interop/jdk-21/bin/java" -cp "$interop/calimero/calimero-core-3.0-M2.jar" "$script_dir/CalimeroDataSecurePeer.java" \
                --keyring "$keyring" --password pwd --timeout "$timeout_s" > "$capture" 2>&1 &
        else
            port="$("$python" -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
            "$python" "$script_dir/xknx_secure_routing_peer.py" --port "$port" --keyring "$keyring" --password pwd --timeout "$timeout_s" \
                --log "$capture" --individual-address 1.1.1 --expected-group 1/1/1 --expected-value 1 --answer-group 1/1/1 \
                --answer-value 0 --require-data-secure &
        fi
        peer_pid=$!
        trap 'kill "$peer_pid" 2>/dev/null || true' EXIT
        # The peer joins the group and begins synchronising its timer before the router speaks.
        sleep 2
        run_in_tree "[knx][data_secure][routing][interop]" > "$capture.in-tree" 2>&1 || in_tree_status=$?
        wait "$peer_pid" || peer_status=$?
        trap - EXIT
        ;;
    tunnel)
        port="$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
        run_in_tree "[knx][data_secure][tcp][interop]" > "$capture.in-tree" 2>&1 &
        in_tree_pid=$!
        trap 'kill "$in_tree_pid" 2>/dev/null || true' EXIT
        for _ in $(seq 1 $((timeout_s * 10))); do
            if ss -ltn "sport = :$port" | grep -q LISTEN; then
                break
            fi
            sleep 0.1
        done
        "$python" "$script_dir/xknx_tcp_tunnel_peer.py" --port "$port" --timeout "$timeout_s" --log "$capture" \
            --data-secure-keyring "$keyring" --data-secure-password pwd --request-group 1/1/1 --answer-group 1/1/1 --answer-value 0 \
            --require-data-secure || peer_status=$?
        wait "$in_tree_pid" || in_tree_status=$?
        trap - EXIT
        ;;
    *)
        echo "Unknown mode '$mode': use routing or tunnel." >&2
        exit 2
        ;;
esac

{
    echo
    echo "==== in-tree side ($binary) ===="
    cat "$capture.in-tree"
} >> "$capture"
rm -f "$capture.in-tree"

echo "==> in-tree side exit $in_tree_status, $peer exit $peer_status; capture $capture"
[[ "$in_tree_status" -eq 0 && "$peer_status" -eq 0 ]]
