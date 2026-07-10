#!/usr/bin/env bash
set -euo pipefail

ok=true

if command -v nvidia-smi >/dev/null 2>&1; then
    echo "[cuda] NVIDIA runtime detected via nvidia-smi"
else
    echo "[cuda] Missing nvidia-smi (NVIDIA driver/runtime not detected)"
    ok=false
fi

if [[ -f /usr/include/cuda_runtime.h ]] || [[ -f /usr/local/cuda/include/cuda_runtime.h ]]; then
    echo "[cuda] CUDA headers detected"
else
    echo "[cuda] CUDA headers missing (/usr/include/cuda_runtime.h or /usr/local/cuda/include/cuda_runtime.h)"
    ok=false
fi

if command -v nvcc >/dev/null 2>&1; then
    echo "[cuda] nvcc detected"
else
    echo "[cuda] nvcc not found (optional for this repo if headers/runtime are present)"
fi

if [[ "${ok}" == "true" ]]; then
    echo "[cuda] Environment looks ready for project.enable_cuda:true"
    exit 0
fi

echo "[cuda] Environment not ready. Install NVIDIA driver + CUDA toolkit for your distro."
exit 1