#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Bootstrap optional feature dependencies for this repository.

Usage:
  scripts/bootstrap_optional_deps.sh [options]

Options:
  --all           Install/check all optional feature dependencies.
  --accelerators  Install shared optional accelerator/system dependencies.
  --af-xdp        Install AF_XDP dependencies.
  --avb           Install AVB/PTP host tooling.
  --spdk          Install and build local SPDK prefix under build/spdk-local.
  --v4l2          Install V4L2 host tooling.
  --quic          Install/build BoringSSL + lsquic.
  --opc-ua        Install/build open62541 local prefix.
  --cuda-check    Run CUDA environment preflight check.
  -h, --help      Show this help.

Examples:
  bash scripts/bootstrap_optional_deps.sh --all
  bash scripts/bootstrap_optional_deps.sh --af-xdp --quic --opc-ua
USAGE
}

run_script() {
    local script_name="$1"
    shift || true
    local script_path
    script_path="${ROOT_DIR}/scripts/${script_name}"

    if [[ ! -x "${script_path}" ]]; then
        echo "Missing executable script: ${script_path}" >&2
        exit 1
    fi

    echo "[bootstrap] running ${script_name}"
    bash "${script_path}" "$@"
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_af_xdp=false
run_avb=false
run_spdk=false
run_v4l2=false
run_quic=false
run_opc_ua=false
run_cuda_check=false
run_accelerators=false

if [[ $# -eq 0 ]]; then
    usage
    exit 1
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            run_accelerators=true
            run_af_xdp=true
            run_avb=true
            run_spdk=true
            run_v4l2=true
            run_quic=true
            run_opc_ua=true
            run_cuda_check=true
            ;;
        --accelerators)
            run_accelerators=true
            ;;
        --af-xdp)
            run_af_xdp=true
            ;;
        --avb)
            run_avb=true
            ;;
        --spdk)
            run_spdk=true
            ;;
        --v4l2)
            run_v4l2=true
            ;;
        --quic)
            run_quic=true
            ;;
        --opc-ua)
            run_opc_ua=true
            ;;
        --cuda-check)
            run_cuda_check=true
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

if [[ "${run_af_xdp}" == "false" && \
    "${run_accelerators}" == "false" && \
      "${run_avb}" == "false" && \
      "${run_spdk}" == "false" && \
      "${run_v4l2}" == "false" && \
      "${run_quic}" == "false" && \
      "${run_opc_ua}" == "false" && \
      "${run_cuda_check}" == "false" ]]; then
    echo "No actions selected." >&2
    usage
    exit 1
fi

if [[ "${run_accelerators}" == "true" ]]; then
    run_script install_optional_accelerator_deps.sh
fi

if [[ "${run_af_xdp}" == "true" ]]; then
    run_script install_af_xdp_deps.sh
fi

if [[ "${run_avb}" == "true" ]]; then
    run_script install_avb_deps.sh
fi

if [[ "${run_spdk}" == "true" ]]; then
    run_script install_spdk_local.sh
fi

if [[ "${run_v4l2}" == "true" ]]; then
    run_script install_v4l2_deps.sh
fi

if [[ "${run_quic}" == "true" ]]; then
    run_script install_lsquic.sh
fi

if [[ "${run_opc_ua}" == "true" ]]; then
    run_script install_open62541.sh
fi

if [[ "${run_cuda_check}" == "true" ]]; then
    run_script check_cuda_env.sh
fi

echo "[bootstrap] done"
