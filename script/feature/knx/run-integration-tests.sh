#!/usr/bin/env bash
set -euo pipefail

# Every KNX integration test drives an injected in-memory transport, so none of them needs a network
# interface, a privileged socket or a peer. They are tagged [integration] because they exercise the
# client, session and codec together rather than because they leave the process.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"
run_catch_tests timeout 25s "$test_bin" "[knx][integration]"
