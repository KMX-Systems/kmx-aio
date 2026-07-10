#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[deprecated] use script/feature/quic/install-dependencies.sh"
exec bash "${ROOT_DIR}/script/feature/quic/install-dependencies.sh" "$@"
