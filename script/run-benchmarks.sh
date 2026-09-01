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
# Both execution models are always on. A scenario measured on both is what this suite is for, and a
# build with one model reports the other side of every comparison as "not run".
#
# Optional features are selected with --set, which names one of the same internally consistent feature
# sets script/full-build.sh uses. They exist because no single set links: QUIC brings BoringSSL and
# SPDK and open62541 bring system OpenSSL, and the two cannot share one image. A complete comparison
# across the whole feature matrix is therefore always assembled from more than one run - use
# --format json --output to write each run to a file, and merge the files afterwards.
#
#     script/run-benchmarks.sh --set quic          # TLS, HTTP/2, HTTP/3, QUIC, Modbus
#     script/run-benchmarks.sh --list-sets
#
# A desktop is a noisy place to measure microseconds. The runner pins the process to a set of distinct
# physical cores when it can work out which those are - hyperthread siblings sharing one core turn a
# ping-pong benchmark into a measurement of that core's contention - and reports what it pinned to, so
# a number can be read together with the conditions it was taken under.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/feature/common.sh"

# The same sets script/full-build.sh defines, for the same reason. "core" is the default: the two
# executors and nothing optional, which is every scenario that needs no dependency to run.
benchmark_sets=(core quic storage avb gpu)

set_features() {
    case "$1" in
        core)    echo "" ;;
        # The BoringSSL half: QUIC and everything carried over it.
        quic)    echo "http2 http3 quic modbus" ;;
        # The OpenSSL half: SPDK and open62541 are prebuilt against it, vsomeip sits next to them.
        storage) echo "af_xdp spdk opc_ua someip modbus" ;;
        # AVB pulls in the gPTP/SRP tree, which nothing else compiles.
        avb)     echo "af_xdp avb modbus v4l2" ;;
        # CUDA needs the toolkit and a device, so it is its own set rather than a default.
        gpu)     echo "cuda" ;;
        *)       return 1 ;;
    esac
}

set_name="core"
benchmark_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --set)
            set_name="${2:?--set needs a name}"
            shift 2
            ;;
        --list-sets)
            for name in "${benchmark_sets[@]}"; do
                printf '%-10s %s\n' "$name" "$(set_features "$name")"
            done
            exit 0
            ;;
        *)
            benchmark_args+=("$1")
            shift
            ;;
    esac
done

if ! set_features "$set_name" >/dev/null; then
    echo "ERROR: unknown feature set '$set_name'; known sets: ${benchmark_sets[*]}" >&2
    exit 1
fi

# Every feature is named explicitly, off first and then on for the set, so the build cannot inherit a
# gate from the environment and quietly measure something other than what --set asked for.
for feature in "${feature_list[@]}"; do
    export "KMX_ENABLE_$(echo "$feature" | tr '[:lower:]' '[:upper:]')=false"
done
for feature in core completion readiness $(set_features "$set_name"); do
    export "KMX_ENABLE_$(echo "$feature" | tr '[:lower:]' '[:upper:]')=true"
done

readarray -t benchmark_features < <(build_qbs_feature_args)

echo "==> Feature set: $set_name ($(set_features "$set_name" | sed "s/^$/no optional features/"))"
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
    run_with_local_gcc_runtime taskset -c "$cpu_list" "$benchmark_bin" "${benchmark_args[@]+"${benchmark_args[@]}"}"
else
    echo "==> Running unpinned"
    run_with_local_gcc_runtime "$benchmark_bin" "${benchmark_args[@]+"${benchmark_args[@]}"}"
fi
