#!/usr/bin/env bash
set -euo pipefail

# Runs KNXnet/IP tunnelling over TCP between an in-tree endpoint and an external peer on the loopback interface.
#
#   bash script/feature/knx/interop/run-tcp-tunnelling-interop.sh calimero
#       the in-tree tunnelling client against calimero-server: connect, a switch-on to 1/2/3 and its confirmation,
#       a heartbeat, a disconnect
#   bash script/feature/knx/interop/run-tcp-tunnelling-interop.sh xknx
#       an xknx TCP tunnel against the in-tree server: xknx sends a switch-on to 1/2/3, the server confirms it and
#       answers with a switch-on to 1/2/4, xknx disconnects
#
# The peer's log - calimero-server's output, or xknx's debug log - is kept under output/interop/captures/ as the
# capture the interoperability matrix names, and the in-tree side's output is appended to it.
#
# Needs a built kmx-aio-test with KNX enabled and the pinned peers under output/interop/: Calimero 3.0-M2 with
# JDK 21, or xknx 3.20.0 in xknx-venv.
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

binary="$(find "$build_root" -type f -name kmx-aio-test -not -path '*/install-root/*' -printf '%T@ %p\n' 2>/dev/null |
    sort -nr | head -1 | cut -d' ' -f2-)"
if [[ -z "$binary" || ! -x "$binary" ]]; then
    echo "No kmx-aio-test under $build_root; build one with KNX enabled first." >&2
    exit 1
fi

mkdir -p "$captures"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
capture="$captures/tcp-tunnelling-$peer-$stamp.log"
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

# Runs the in-tree side of the exchange with the port and timeout it reads.
run_in_tree() {
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib64}" KMX_KNX_INTEROP_PORT="$port" KMX_KNX_INTEROP_TIMEOUT="$timeout_s" \
        "$binary" "$1" --success
}

in_tree_status=0
peer_status=0
case "$peer" in
    calimero)
        config="$captures/tcp-tunnelling-calimero-$stamp.xml"
        sed "s/@PORT@/$port/" "$script_dir/calimero-tcp-server.xml" > "$config"
        jars="$interop/calimero"
        "$interop/jdk-21/bin/java" -cp "$jars/calimero-server-3.0-M2.jar:$jars/calimero-core-3.0-M2.jar:$jars/calimero-device-3.0-M2.jar" \
            io.calimero.server.Launcher "$config" > "$capture" 2>&1 &
        peer_pid=$!
        trap 'kill "$peer_pid" 2>/dev/null || true' EXIT
        wait_for_listener
        run_in_tree "[knx][tcp][client][interop]" >> "$capture.in-tree" 2>&1 || in_tree_status=$?
        # calimero-server serves until told to stop; give it a moment to log the disconnect first.
        sleep 1
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
        trap - EXIT
        ;;
    xknx)
        run_in_tree "[knx][tcp][server][interop]" > "$capture.in-tree" 2>&1 &
        in_tree_pid=$!
        trap 'kill "$in_tree_pid" 2>/dev/null || true' EXIT
        wait_for_listener
        "$interop/xknx-venv/bin/python" "$script_dir/xknx_tcp_tunnel_peer.py" --port "$port" --timeout "$timeout_s" \
            --log "$capture" || peer_status=$?
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
