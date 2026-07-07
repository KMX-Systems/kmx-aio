#!/usr/bin/env bash
set -euo pipefail

job="all"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)
            shift
            job="${1:-}"
            ;;
        --help|-h)
            cat <<'USAGE'
Run local equivalents for ci-avb workflow jobs.

Usage:
  scripts/ci/run-ci-avb-local.sh [--only <job>]

Jobs:
  all
  build-and-test
  quic-smoke
  gpu-smoke
USAGE
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
    shift
done

if [[ "$job" != "all" && "$job" != "build-and-test" && "$job" != "quic-smoke" && "$job" != "gpu-smoke" ]]; then
    echo "Invalid --only value: $job" >&2
    exit 1
fi

resolve_repo_root() {
    local dir
    dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

    while [[ "$dir" != "/" ]]; do
        if [[ -f "$dir/kmx-aio.qbs" && -d "$dir/source" ]]; then
            echo "$dir"
            return 0
        fi
        dir="$(dirname "$dir")"
    done

    echo "Could not locate repository root from script path" >&2
    exit 1
}

repo_root="$(resolve_repo_root)"
source_dir="$repo_root/source"

sync_script_into_build_dir() {
    local target_dir target_file
    target_dir="$repo_root/build"
    target_file="$target_dir/run-ci-avb-local.sh"

    mkdir -p "$target_dir"

    if [[ ! -f "$target_file" ]] || ! cmp -s "${BASH_SOURCE[0]}" "$target_file"; then
        cp "${BASH_SOURCE[0]}" "$target_file"
        chmod +x "$target_file"
    fi
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command missing: $1" >&2
        exit 1
    fi
}

find_test_bin() {
    local bin
    bin="$(find "$source_dir/debug" -type f -name kmx-aio-test | head -n 1 || true)"
    if [[ -z "$bin" ]]; then
        echo "kmx-aio-test binary not found" >&2
        exit 1
    fi
    echo "$bin"
}

run_build_and_test() {
    echo "==> build-and-test"
    (
        cd "$source_dir"
        qbs resolve -f source.qbs config:debug \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:false \
            project.enable_cuda:false

        qbs build -f source.qbs config:debug -j 2 \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:false \
            project.enable_cuda:false
    )

    local test_bin
    test_bin="$(find_test_bin)"
    timeout 90s "$test_bin"

    for i in $(seq 1 20); do
        echo "flake-guard run $i"
        timeout 20s "$test_bin" "channel wait_until_can_send unblocks when consumer pops from a full ring"
    done

    local talker_bin listener_bin
    talker_bin="$(find "$source_dir/debug" -type f -name sample-avb-talker | head -n 1 || true)"
    listener_bin="$(find "$source_dir/debug" -type f -name sample-avb-listener | head -n 1 || true)"
    if [[ -z "$talker_bin" || -z "$listener_bin" ]]; then
        echo "sample-avb binaries not found" >&2
        exit 1
    fi

    "$talker_bin" --help | head -n 15
    "$listener_bin" --help | head -n 15
    "$talker_bin" --period-us 0 && exit 1 || true
    "$listener_bin" --sync-timeout-s 0 && exit 1 || true
}

run_quic_smoke() {
    echo "==> quic-smoke"
    require_cmd openssl

    (
        cd "$repo_root"
        bash build/install_lsquic.sh
    )

    (
        cd "$source_dir"
        local products
        products="sample-quic-echo-readiness-server,sample-quic-echo-readiness-client,sample-quic-http3-server,sample-quic-http3-client,kmx-aio-test"
        qbs resolve -f source.qbs config:debug \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:true \
            project.enable_cuda:false

        qbs build -f source.qbs config:debug -j 2 \
            --products "$products" \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:true \
            project.enable_cuda:false
    )

    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout /tmp/quic_key.pem \
        -out /tmp/quic_cert.pem \
        -subj "/CN=localhost" \
        -days 1 >/dev/null 2>&1

    local test_bin
    test_bin="$(find_test_bin)"
    timeout 30s "$test_bin" "[quic][readiness][integration][smoke]"
    timeout 30s "$test_bin" "[quic][http3][integration][smoke]"
}

run_gpu_smoke() {
    echo "==> gpu-smoke"
    if ! command -v nvidia-smi >/dev/null 2>&1; then
        echo "No NVIDIA driver/runtime detected; skipping gpu-smoke."
        return 0
    fi

    if [[ ! -f /usr/include/cuda_runtime.h && ! -f /usr/local/cuda/include/cuda_runtime.h ]]; then
        echo "CUDA headers not detected; skipping gpu-smoke."
        return 0
    fi

    (
        cd "$source_dir"
        local products
        products="kmx-aio-test,sample-gpu-image-processing"
        qbs resolve -f source.qbs config:debug \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:false \
            project.enable_cuda:true

        qbs build -f source.qbs config:debug -j 2 \
            --products "$products" \
            project.enable_openonload:false \
            project.enable_af_xdp:false \
            project.enable_spdk:false \
            project.enable_quic:false \
            project.enable_cuda:true
    )

    local sample_bin test_bin
    sample_bin="$(find "$source_dir/debug" -type f -name sample-gpu-image-processing | head -n 1 || true)"
    if [[ -z "$sample_bin" ]]; then
        echo "sample-gpu-image-processing binary not found" >&2
        exit 1
    fi

    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 30s "$sample_bin" \
        --max-frames 1 --width 320 --height 240 --buffer-count 2 --gpu-device 0

    test_bin="$(find_test_bin)"
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 60s "$test_bin" "[gpu]"
}

require_cmd qbs
require_cmd timeout
sync_script_into_build_dir

case "$job" in
    all)
        run_build_and_test
        run_quic_smoke
        run_gpu_smoke
        ;;
    build-and-test)
        run_build_and_test
        ;;
    quic-smoke)
        run_quic_smoke
        ;;
    gpu-smoke)
        run_gpu_smoke
        ;;
esac

echo "Done: $job"
