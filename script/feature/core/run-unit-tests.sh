#!/usr/bin/env bash
set -euo pipefail

# The library core - error codes, address vocabulary, descriptors, allocator, task, buffer pool,
# channel - is built into every configuration, so its tests are not gated on a feature flag. They still
# need a runner: Catch2 selects by tag, and a tag no script names never runs.
#
# The tag list is explicit rather than an inverted "everything but the feature tags" filter, so that a
# new feature tag cannot silently pull its tests into the core run.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"
# [fault] drives the branches that only run when a system call fails, through the seam in
# aio/detail/syscalls.hpp. Those cases exist only in a build made with project.enable_fault_injection,
# and Catch2 is told a filter matching nothing is not a failure, so naming the tag here is safe either
# way rather than something the runner has to detect.
run_catch_tests timeout 180s "$test_bin" \
    "[core],[buffer_pool],[channel],[allocation],[task],[async_poll],[fault],[branch]" "~[integration]"
