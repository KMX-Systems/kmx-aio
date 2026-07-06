#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
ld_path="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}"

ready_timeout_sec="${KMX_QUIC_READY_TIMEOUT_SEC:-2}"
communication_window_sec="${KMX_QUIC_COMMUNICATION_WINDOW_SEC:-0.5}"
concurrent_clients="${KMX_QUIC_CONCURRENT_CLIENTS:-3}"

tmp_dir="$(mktemp -d /tmp/kmx-quic-echo-XXXXXX)"
server_log="${tmp_dir}/server.log"
client_normal_log="${tmp_dir}/client-normal.log"

server_pid=""

wait_for_pattern() {
    local pattern="$1"
    local file="$2"
    local pid="$3"
    local timeout_seconds="$4"

    local ticks=$((timeout_seconds * 100))
    local i=0

    while ((i < ticks)); do
        if grep -q "${pattern}" "${file}" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "${pid}" 2>/dev/null; then
            return 1
        fi
        sleep 0.01
        i=$((i + 1))
    done

    return 1
}

cleanup() {
    if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

find_latest_binary() {
    local name="$1"
    find "${repo_root}/debug" -type f -name "${name}" -printf '%T@ %p\n' 2>/dev/null |
        sort -nr |
        awk 'NR == 1 {print $2}'
}

server_bin="$(find_latest_binary sample-quic-echo-server)"
client_bin="$(find_latest_binary sample-quic-echo-client)"

if [[ -z "${server_bin}" || -z "${client_bin}" ]]; then
    echo "FAIL: Missing QUIC echo binaries in ${repo_root}/debug"
    echo "Hint: build with qbs first."
    exit 1
fi

if [[ ! -f /tmp/quic_cert.pem || ! -f /tmp/quic_key.pem ]]; then
    openssl req -x509 -newkey rsa:2048 -keyout /tmp/quic_key.pem -out /tmp/quic_cert.pem -days 1 -nodes -subj "/CN=localhost" >/dev/null 2>&1
fi

echo "Using server: ${server_bin}"
echo "Using client: ${client_bin}"
echo "Logs: ${tmp_dir}"

env LD_LIBRARY_PATH="${ld_path}" stdbuf -oL -eL "${server_bin}" >"${server_log}" 2>&1 &
server_pid="$!"

if ! wait_for_pattern "listening" "${server_log}" "${server_pid}" "${ready_timeout_sec}"; then
    echo "FAIL: Server did not become ready"
    if kill -0 "${server_pid}" 2>/dev/null; then
        echo "Server process is still alive but readiness marker was not observed."
    fi
    cat "${server_log}" || true
    exit 1
fi

echo "Running ${concurrent_clients} concurrent normal clients for ${communication_window_sec}s"

declare -a client_pids=()
declare -a client_logs=()
declare -a client_statuses=()

for i in $(seq 1 "${concurrent_clients}"); do
    log_path="${tmp_dir}/client-normal-${i}.log"
    client_logs+=("${log_path}")
    timeout "${communication_window_sec}s" env KMX_QUIC_ECHO_CLIENT_CLOSE_AFTER_RESPONSES=0 LD_LIBRARY_PATH="${ld_path}" "${client_bin}" >"${log_path}" 2>&1 &
    client_pids+=("$!")
done

for i in $(seq 1 "${concurrent_clients}"); do
    idx=$((i - 1))
    pid="${client_pids[$idx]}"
    if wait "${pid}"; then
        client_statuses+=(0)
    else
        client_statuses+=($?)
    fi
done

for i in $(seq 1 "${concurrent_clients}"); do
    idx=$((i - 1))
    status="${client_statuses[$idx]}"
    log_path="${client_logs[$idx]}"

    if [[ "${status}" -ne 124 ]]; then
        echo "FAIL: Client ${i} did not run for full ${communication_window_sec}s window (exit=${status})"
        cat "${log_path}" || true
        exit 1
    fi

    grep -q "on_hsk_done" "${log_path}" || {
        echo "FAIL: Client ${i} did not complete handshake"
        cat "${log_path}" || true
        exit 1
    }

done

expected_stream_msgs=$((concurrent_clients * 2))
actual_stream_msgs=$(grep -c "Received QUIC stream data:" "${server_log}" 2>/dev/null || true)
if [[ "${actual_stream_msgs}" -lt "${expected_stream_msgs}" ]]; then
    echo "FAIL: Server observed insufficient stream traffic (${actual_stream_msgs}/${expected_stream_msgs})"
    cat "${server_log}" || true
    exit 1
fi

if ! kill -0 "${server_pid}" 2>/dev/null; then
    echo "FAIL: Server died during concurrent-client window"
    cat "${server_log}" || true
    exit 1
fi

echo "PASS: ${concurrent_clients} concurrent normal clients communicated for ${communication_window_sec}s"
echo "Artifacts:"
echo "  ${server_log}"
for log_path in "${client_logs[@]}"; do
    echo "  ${log_path}"
done
