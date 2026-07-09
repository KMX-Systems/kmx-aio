#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script currently supports Ubuntu/Debian only (apt-get required)." >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

echo "[avb] Installing AVB/PTP runtime dependencies (Ubuntu/Debian)..."
${SUDO} apt-get update
${SUDO} apt-get install -y \
    linuxptp \
    ethtool \
    iproute2

echo "[avb] Verifying tools..."
command -v ptp4l >/dev/null 2>&1
command -v phc2sys >/dev/null 2>&1
command -v ethtool >/dev/null 2>&1

echo "[avb] Done. Note: You still need CAP_NET_RAW and a PTP-capable NIC/driver."
