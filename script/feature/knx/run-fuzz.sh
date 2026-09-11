#!/usr/bin/env bash
set -euo pipefail

# Builds the KNX fuzz targets with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer, and runs each
# against its own corpus, all of them at once, one core each.
#
#   bash script/feature/knx/run-fuzz.sh                      # every target, KMX_FUZZ_SECONDS each
#   bash script/feature/knx/run-fuzz.sh keyring datagram     # the targets named
#   KMX_FUZZ_SECONDS=60 bash script/feature/knx/run-fuzz.sh  # a smoke run
#
# The targets are compiled straight from the sources they cover rather than through qbs. libFuzzer needs
# clang's -fsanitize=fuzzer, which no qbs configuration of this project provides, and the code under test
# needs nothing but the library headers and the TLS backend's libcrypto.
#
#   xml_reader, keyring   the ETS keyring reader and loader, seeded from the vendored keyrings
#   datagram              every KNXnet/IP codec behind decode_datagram, with the wrapper, TIMER_NOTIFY and session checks
#   reassembler           the KNXnet/IP over TCP frame reassembler
#   data_secure           the S-A_Data codec and the Data Secure context
#
# Corpora live under the build root and are seeded on first use - from the vendored ETS keyrings, or from the
# generated vectors by source/fuzz/knx/seed_corpus.py - so a later run resumes from what an earlier one found. A
# finding is written to <build root>/findings/<target>/ and fails the script.
#
# Environment:
#   KMX_FUZZ_CXX       the compiler; default clang++
#   KMX_FUZZ_SECONDS   seconds per target; default 3600, the one CPU-hour per corpus the release plan asks for
#   KMX_BUILD_ROOT     where binaries, corpora, findings and logs go; default output/fuzz/knx

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
cxx="${KMX_FUZZ_CXX:-clang++}"
seconds="${KMX_FUZZ_SECONDS:-3600}"
build_root="${KMX_BUILD_ROOT:-$repo_root/output/fuzz/knx}"
library="$repo_root/source/library"
fuzz_dir="$repo_root/source/fuzz/knx"
fixtures="$repo_root/documentation/features/knx/conformance/keyrings"

targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
    targets=(xml_reader keyring datagram reassembler data_secure)
fi

# What every target links: the error category and the crypto adapter.
common_sources=(
    "$library/src/kmx/aio/net_parse.cpp"
    "$library/src/kmx/aio/knx/error.cpp"
    "$library/src/kmx/aio/knx/secure/ccm.cpp"
    "$library/src/kmx/aio/knx/secure/crypto.cpp"
    "$library/src/kmx/aio/knx/secure/key.cpp"
)

# The library sources one target covers, beyond the common ones.
target_sources() {
    local knx="$library/src/kmx/aio/knx"
    case "$1" in
        xml_reader | keyring)
            echo "$knx/keyring.cpp $knx/secure/keyring_format.cpp $knx/secure/xml_reader.cpp"
            ;;
        datagram)
            # The routing codec shares its translation unit with the routing client, which brings in the core coroutine,
            # allocator and socket support, and the Data Secure context with the keyring behind it.
            local core="$library/src/kmx/aio"
            echo "$core/task.cpp $core/async_mutex.cpp $core/allocator/slab.cpp $core/allocator/counter.cpp" \
                "$core/allocator/statistics.cpp $core/allocator/detail/thread_state.cpp $core/file_descriptor.cpp" \
                "$core/error_code.cpp $core/exception.cpp $core/basic_types.cpp $core/detail/syscalls.cpp" \
                "$knx/address.cpp $knx/datagram.cpp $knx/frame.cpp $knx/connection.cpp $knx/discovery.cpp $knx/dib.cpp" \
                "$knx/routing.cpp $knx/data_secure.cpp $knx/keyring.cpp $knx/secure/keyring_format.cpp $knx/secure/xml_reader.cpp" \
                "$knx/secure/routing_timer_state.cpp $knx/secure/session.cpp $knx/secure/wrapper.cpp $knx/secure/timer_notify.cpp"
            ;;
        reassembler)
            echo "$knx/frame.cpp $knx/frame_reassembler.cpp"
            ;;
        data_secure)
            echo "$knx/address.cpp $knx/data_secure.cpp $knx/keyring.cpp $knx/secure/keyring_format.cpp $knx/secure/xml_reader.cpp"
            ;;
    esac
}

# The feature header qbs would generate, reduced to the one feature these sources need.
write_config() {
    mkdir -p "$build_root/include/kmx/aio"
    printf '#pragma once\n#ifndef KMX_AIO_FEATURE_KNX\n    #define KMX_AIO_FEATURE_KNX 1\n#endif\n' \
        > "$build_root/include/kmx/aio/config.hpp"
}

build_target() {
    local target="$1"
    # Run against the libstdc++ the compiler linked, not whichever older one the loader finds first: the core sources a
    # target may pull in use symbols only a current libstdc++ has.
    local runtime_dir
    runtime_dir="$(dirname "$(readlink -f "$("$cxx" -print-file-name=libstdc++.so)")")"
    echo "==> building $target"
    # shellcheck disable=SC2046 # the source list is meant to split into words
    "$cxx" -std=c++2c -O1 -g -fno-omit-frame-pointer \
        -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
        -I "$build_root/include" -I "$library/api" -I "$library/inc" \
        "${common_sources[@]}" $(target_sources "$target") "$fuzz_dir/${target}_fuzz.cpp" -lcrypto \
        -Wl,-rpath,"$runtime_dir" -o "$build_root/$target"
}

seed_corpus() {
    local target="$1"
    local corpus="$2"
    case "$target" in
        xml_reader | keyring) cp "$fixtures"/*.knxkeys "$corpus/" ;;
        *) python3 "$fuzz_dir/seed_corpus.py" "$target" "$corpus" > /dev/null ;;
    esac
}

run_target() {
    local target="$1"
    local corpus="$build_root/corpus/$target"
    local findings="$build_root/findings/$target/"
    local dictionary=()
    mkdir -p "$corpus" "$findings"
    if [[ -z "$(ls -A "$corpus")" ]]; then
        seed_corpus "$target" "$corpus"
    fi
    case "$target" in
        xml_reader | keyring) dictionary=(-dict="$fuzz_dir/keyring.dict") ;;
    esac
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
        "$build_root/$target" "$corpus" "${dictionary[@]}" -max_total_time="$seconds" \
        -max_len=16384 -timeout=10 -rss_limit_mb=2048 -artifact_prefix="$findings" -print_final_stats=1 \
        > "$build_root/$target.log" 2>&1
}

for target in "${targets[@]}"; do
    if [[ ! -f "$fuzz_dir/${target}_fuzz.cpp" ]]; then
        echo "unknown fuzz target: $target" >&2
        exit 2
    fi
done

write_config
for target in "${targets[@]}"; do
    build_target "$target"
done

declare -A pids=()
for target in "${targets[@]}"; do
    echo "==> fuzzing $target for ${seconds}s, log $build_root/$target.log"
    run_target "$target" &
    pids[$target]=$!
done

status=0
for target in "${targets[@]}"; do
    if wait "${pids[$target]}"; then
        echo "==> $target: no finding; $(grep -E '^Done [0-9]+ runs' "$build_root/$target.log" | tail -1)"
    else
        echo "==> $target: FAILED; see $build_root/$target.log and $build_root/findings/$target/" >&2
        status=1
    fi
done
exit "$status"
