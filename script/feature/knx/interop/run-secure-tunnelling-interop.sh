#!/usr/bin/env bash
set -euo pipefail

# Runs KNX IP Secure tunnelling between an in-tree endpoint and an external peer on the loopback interface.
#
#   bash script/feature/knx/interop/run-secure-tunnelling-interop.sh calimero
#       the in-tree secure client against calimero-server: a session with the device authentication code and user 2's
#       password, a switch-on to 1/2/3 and its confirmation, a heartbeat and a keep-alive, and a disconnect that closes
#       the session with SESSION_STATUS close
#   bash script/feature/knx/interop/run-secure-tunnelling-interop.sh xknx
#       an xknx secure TCP tunnel against the in-tree secure server, both keyed from the testcase.knxkeys fixture: xknx
#       reads the server's description over UDP, opens a session as the user of tunnel 1.0.1, sends a switch-on to 1/2/3,
#       receives its confirmation and a switch-on to 1/2/4 in answer, and disconnects
#
# The peer's log - calimero-server's output, or xknx's debug log - is kept under output/interop/captures/ as the capture
# the interoperability matrix names, with the in-tree side's output appended to it.
#
# Needs a built kmx-aio-test with KNX enabled and the pinned peers under output/interop/: Calimero 3.0-M2 with JDK 21,
# or xknx 3.20.0 in xknx-venv.
#
# Environment:
#   KMX_BUILD_ROOT            where to find kmx-aio-test; default output/knx-release-gates
#   KMX_KNX_INTEROP_TIMEOUT   seconds either side waits; default 40

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
peer="${1:-calimero}"
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-release-gates}"
interop="$repo_root/output/interop"
captures="$interop/captures"
timeout_s="${KMX_KNX_INTEROP_TIMEOUT:-40}"
keyring="$repo_root/documentation/features/knx/conformance/keyrings/testcase.knxkeys"

binary="$(find "$build_root" -type f -name kmx-aio-test -not -path '*/install-root/*' -printf '%T@ %p\n' 2>/dev/null |
    sort -nr | head -1 | cut -d' ' -f2-)"
if [[ -z "$binary" || ! -x "$binary" ]]; then
    echo "No kmx-aio-test under $build_root; build one with KNX enabled first." >&2
    exit 1
fi

mkdir -p "$captures"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
capture="$captures/secure-tunnelling-$peer-$stamp.log"
port="$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"

# Waits until something listens on the chosen TCP port, or the timeout passes.
wait_for_listener() {
    for _ in $(seq 1 $((timeout_s * 10))); do
        if ss -ltn "sport = :$port" | grep -q LISTEN; then
            return 0
        fi
        sleep 0.1
    done
    echo "Nothing listens on 127.0.0.1:$port after ${timeout_s} s." >&2
    return 1
}

in_tree_status=0
peer_status=0
case "$peer" in
    calimero)
        config="$captures/secure-tunnelling-calimero-$stamp.xml"
        keys="$captures/secure-tunnelling-calimero-$stamp.keys"
        cp "$script_dir/calimero-secure-tunnel.keys" "$keys"
        sed -e "s|@PORT@|$port|" -e "s|@KEYFILE@|$keys|" "$script_dir/calimero-secure-tcp-server.xml" > "$config"
        jars="$interop/calimero"
        "$interop/jdk-21/bin/java" -cp "$jars/calimero-server-3.0-M2.jar:$jars/calimero-core-3.0-M2.jar:$jars/calimero-device-3.0-M2.jar" \
            io.calimero.server.Launcher "$config" > "$capture" 2>&1 &
        peer_pid=$!
        trap 'kill "$peer_pid" 2>/dev/null || true' EXIT
        wait_for_listener
        LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib64}" KMX_KNX_INTEROP_PORT="$port" KMX_KNX_INTEROP_TIMEOUT="$timeout_s" \
            KMX_KNX_INTEROP_USER_ID=2 KMX_KNX_INTEROP_USER_PASSWORD=secret KMX_KNX_INTEROP_DEVICE_PASSWORD=trustme \
            "$binary" "[knx][secure][tunnelling][interop]" --success > "$capture.in-tree" 2>&1 || in_tree_status=$?
        # calimero-server serves until told to stop; give it a moment to log the session's end first.
        sleep 1
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
        trap - EXIT
        ;;
    xknx)
        LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib64}" KMX_KNX_INTEROP_PORT="$port" KMX_KNX_INTEROP_TIMEOUT="$timeout_s" \
            KMX_KNX_INTEROP_KEYRING="$keyring" KMX_KNX_INTEROP_KEYRING_PASSWORD=password \
            "$binary" "[knx][secure][server][interop]" --success > "$capture.in-tree" 2>&1 &
        in_tree_pid=$!
        trap 'kill "$in_tree_pid" 2>/dev/null || true' EXIT
        wait_for_listener
        "$interop/xknx-venv/bin/python" "$script_dir/xknx_tcp_tunnel_peer.py" --port "$port" --timeout "$timeout_s" \
            --keyring "$keyring" --keyring-password password --tunnel 1.0.1 --log "$capture" || peer_status=$?
        wait "$in_tree_pid" || in_tree_status=$?
        trap - EXIT
        ;;
    *)
        echo "Unknown peer '$peer': use calimero or xknx." >&2
        exit 2
        ;;
esac

{
    echo
    echo "==== in-tree side ($binary) ===="
    cat "$capture.in-tree"
} >> "$capture"
rm -f "$capture.in-tree"

echo "==> in-tree side exit $in_tree_status, $peer peer exit $peer_status; capture $capture"
[[ "$in_tree_status" -eq 0 && "$peer_status" -eq 0 ]]
