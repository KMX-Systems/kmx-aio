#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/feature/common.sh"

echo "==> Installing dependencies for active features"
for feature in "${feature_list[@]}"; do
    run_feature_script_if_enabled "$feature" "install-dependencies.sh"
done

echo "==> Dependency installation completed"
