#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../apt.sh"

echo "[avb] Checking AVB/PTP runtime dependencies..."
apt_install_missing avb \
	linuxptp \
	ethtool \
	iproute2

echo "[avb] Verifying tools..."
command -v ptp4l >/dev/null 2>&1
command -v phc2sys >/dev/null 2>&1
command -v ethtool >/dev/null 2>&1

echo "[avb] Done. Note: You still need CAP_NET_RAW and a PTP-capable NIC/driver."
