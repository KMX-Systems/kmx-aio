#!/usr/bin/env bash
set -euo pipefail

# Runs the in-tree KNX IP Secure router against an external peer on the loopback interface, one telegram each
# way: the router sends a switch-on to 1/2/3, and the peer answers with one to 1/2/4.
#
#   bash script/feature/knx/interop/run-secure-routing-interop.sh xknx
#   bash script/feature/knx/interop/run-secure-routing-interop.sh calimero
#
# Both sides key themselves from the vendored keyring.knxkeys. The peer's log is kept under
# output/interop/captures/ as the capture the interoperability matrix names.
#
# Needs a built kmx-aio-test with KNX enabled and the pinned peers under output/interop/: xknx 3.20.0 in
# xknx-venv, or Calimero 3.0-M2 with JDK 21. Calimero's secure routing link has no port parameter, so that peer
# runs on the standard port 3671.
#
# Environment:
#   KMX_BUILD_ROOT            where to find kmx-aio-test; default output/knx-release-gates
#   KMX_KNX_INTEROP_TIMEOUT   seconds either side waits; default 40

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
peer="${1:-xknx}"
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-release-gates}"
interop="$repo_root/output/interop"
captures="$interop/captures"
keyring="$repo_root/documentation/features/knx/conformance/keyrings/keyring.knxkeys"
timeout_s="${KMX_KNX_INTEROP_TIMEOUT:-40}"

binary="$(find "$build_root" -type f -name kmx-aio-test -not -path '*/install-root/*' -printf '%T@ %p\n' 2>/dev/null |
    sort -nr | head -1 | cut -d' ' -f2-)"
if [[ -z "$binary" || ! -x "$binary" ]]; then
    echo "No kmx-aio-test under $build_root; build one with KNX enabled first." >&2
    exit 1
fi

mkdir -p "$captures"
capture="$captures/secure-routing-$peer-$(date -u +%Y%m%dT%H%M%SZ).log"

case "$peer" in
    xknx)
        python="$interop/xknx-venv/bin/python"
        port="$("$python" -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
        "$python" "$script_dir/xknx_secure_routing_peer.py" --port "$port" --keyring "$keyring" --password pwd \
            --timeout "$timeout_s" --log "$capture" &
        ;;
    calimero)
        port=3671
        "$interop/jdk-21/bin/java" -cp "$interop/calimero/calimero-core-3.0-M2.jar" "$script_dir/CalimeroSecureRoutingPeer.java" \
            --keyring "$keyring" --password pwd --timeout "$timeout_s" > "$capture" 2>&1 &
        ;;
    *)
        echo "Unknown peer '$peer': use xknx or calimero." >&2
        exit 2
        ;;
esac
peer_pid=$!
trap 'kill "$peer_pid" 2>/dev/null || true' EXIT

# Give the peer its head start: it joins the group and begins synchronising its timer before the router speaks.
sleep 2

router_status=0
LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib64}" KMX_KNX_INTEROP_PORT="$port" KMX_KNX_INTEROP_KEYRING="$keyring" \
    KMX_KNX_INTEROP_PASSWORD=pwd KMX_KNX_INTEROP_TIMEOUT="$timeout_s" \
    "$binary" "[knx][secure][routing][interop]" || router_status=$?

peer_status=0
wait "$peer_pid" || peer_status=$?
trap - EXIT

echo "==> in-tree router exit $router_status, $peer peer exit $peer_status; capture $capture"
[[ "$router_status" -eq 0 && "$peer_status" -eq 0 ]]
