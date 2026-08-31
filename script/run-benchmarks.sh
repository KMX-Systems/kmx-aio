#!/usr/bin/env bash
# Builds and runs the kmx-aio micro-benchmarks.
#
# The benchmarks are built in release configuration on purpose, and the product forces optimization on
# regardless: a debug build measures the debug build's checks, not the library. Arguments after the
# script name are passed through to the binary, so
#
#     script/run-benchmarks.sh --filter readiness --repeats 5
#
# runs only the readiness cases, five times each, keeping the fastest run of each.
#
# A desktop is a noisy place to measure microseconds. The runner pins the process to a set of distinct
# physical cores when it can work out which those are - hyperthread siblings sharing one core turn a
# ping-pong benchmark into a measurement of that core's contention - and reports what it pinned to, so
# a number can be read together with the conditions it was taken under.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/feature/common.sh"

benchmark_features=(project.enable_readiness:true project.enable_completion:true)

echo "==> Building benchmarks (release)"
(
    cd "$source_dir"
    qbs resolve -f source.qbs "${qbs_build_dir_args[@]}" "${qbs_profile_args[@]}" config:release "${benchmark_features[@]}"
    qbs build -f source.qbs "${qbs_build_dir_args[@]}" "${qbs_profile_args[@]}" config:release -j"$(nproc)" \
        -p kmx-aio-benchmark "${benchmark_features[@]}"
)

benchmark_bin="$(find "$qbs_build_root/release" -name kmx-aio-benchmark -type f -print -quit 2>/dev/null || true)"
if [[ -z "$benchmark_bin" ]]; then
    echo "ERROR: kmx-aio-benchmark binary not found under $qbs_build_root/release" >&2
    exit 1
fi

# One CPU per physical core, so no two benchmark threads land on the same core's two hyperthreads.
first_cpu_of_each_core() {
    local -A seen=()
    local -a cpus=()
    local cpu core
    while read -r cpu core; do
        [[ "$cpu" =~ ^[0-9]+$ ]] || continue
        if [[ -z "${seen[$core]:-}" ]]; then
            seen[$core]=1
            cpus+=("$cpu")
        fi
    done < <(lscpu -e=CPU,CORE 2>/dev/null | tail -n +2)

    (IFS=,; echo "${cpus[*]}")
}

cpu_list="$(first_cpu_of_each_core)"
governor="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"

echo "==> CPU governor: $governor"
if [[ "$governor" != "performance" ]]; then
    echo "    (measurements taken under a scaling governor vary with whatever else the machine is doing;"
    echo "     compare figures from one run, not across runs)"
fi

if [[ -n "$cpu_list" ]] && command -v taskset >/dev/null 2>&1; then
    echo "==> Running on CPUs $cpu_list"
    run_with_local_gcc_runtime taskset -c "$cpu_list" "$benchmark_bin" "$@"
else
    echo "==> Running unpinned"
    run_with_local_gcc_runtime "$benchmark_bin" "$@"
fi
