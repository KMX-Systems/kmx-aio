#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

# Through apt_install_missing rather than a plain apt-get, for the reason spelled out at the top of
# script/feature/apt.sh: source.qbs runs this from a Probe, in the middle of "qbs resolve", where there
# is no terminal for a sudo password. This installer builds nothing into output/, so it has no artifact
# to short-circuit on - what is already installed is the only thing it can check, and that is exactly
# what these helpers do.
# shellcheck source=../apt.sh
source "${ROOT_DIR}/script/feature/apt.sh"

echo "[v4l2] Checking optional V4L2 tooling (Ubuntu/Debian)..."
apt_install_missing v4l2 v4l-utils

echo "[v4l2] Verifying tools..."
command -v v4l2-ctl >/dev/null 2>&1

echo "[v4l2] Done."