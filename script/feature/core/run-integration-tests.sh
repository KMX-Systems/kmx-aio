#!/usr/bin/env bash
set -euo pipefail

# The counterpart to run-unit-tests.sh. Core is enabled in every configuration (feature_default_enabled
# in common.sh), so the global orchestrator asks every feature - core included - for a runner of this
# name, and run_feature_script_if_enabled treats a missing one as a hard error rather than a skip. That
# error is worth keeping: it catches a feature whose runner was never written. So core needs the file
# even though the core library currently carries no [integration]-tagged cases.
#
# The tag list mirrors run-unit-tests.sh and is ANDed with [integration] rather than inverted, so a new
# feature tag cannot silently pull its integration tests into the core run. run_catch_tests passes
# --allow-running-no-tests, so an empty selection is a pass today and this starts running on its own the
# moment core gains integration coverage.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"
run_catch_tests timeout 180s "$test_bin" \
    "[core][integration],[buffer_pool][integration],[channel][integration],[allocation][integration],[task][integration],[async_poll][integration],[branch][integration]"
