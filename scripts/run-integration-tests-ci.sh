#!/usr/bin/env bash
set -euo pipefail

# Run all integration tests using CI pipeline (full rebuild)
# This script rebuilds all dependencies and tests every configuration.
# Use this for CI/CD or when you need to verify everything from scratch.
#
# For quick local testing on pre-built binaries, use: bash scripts/run-integration-tests.sh

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Running all integration tests (CI pipeline with full rebuilds)"
bash "$repo_root/scripts/ci/run-ci-avb-local.sh" --only all

echo "==> Integration tests completed successfully"
