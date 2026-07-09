#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[deprecated] use scripts/install_lsquic.sh"
exec bash "${ROOT_DIR}/scripts/install_lsquic.sh" "$@"
