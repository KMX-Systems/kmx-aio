#!/usr/bin/env bash
set -euo pipefail

# Builds the project under a sanitizer and runs the unit tests against it.
#
#   bash script/run-sanitizer-tests.sh                 # AddressSanitizer + UndefinedBehaviorSanitizer
#   bash script/run-sanitizer-tests.sh asan
#   bash script/run-sanitizer-tests.sh ubsan
#   bash script/run-sanitizer-tests.sh tsan
#   bash script/run-sanitizer-tests.sh asan+ubsan
#
# The build lands in its own tree (output/asan, output/ubsan, ...) rather than in output/debug, so an
# instrumented build never has to be undone before the next ordinary one, and no plain kmx-aio-test is
# left sitting where the instrumented one is expected. Feature selection works exactly as it does for
# script/run-unit-tests.sh: KMX_ENABLE_<FEATURE>=true/false, defaults otherwise.
#
# ASan and TSan cannot be combined - they ship mutually exclusive runtimes - and the build rejects the
# combination rather than producing something that half works. UBSan combines with either.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
    cat <<'USAGE'
usage: run-sanitizer-tests.sh [asan|ubsan|asan+ubsan|tsan|tsan+ubsan]

options:
  -h, --help      show this message

environment:
  KMX_ENABLE_<FEATURE>   true/false, as for script/run-unit-tests.sh
  ASAN_OPTIONS, UBSAN_OPTIONS, TSAN_OPTIONS, LSAN_OPTIONS
                         override the defaults the runner would otherwise set
USAGE
}

selection="asan+ubsan"
selection_given="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        asan|ubsan|tsan|asan+ubsan|ubsan+asan|tsan+ubsan|ubsan+tsan)
            if [[ "$selection_given" == "true" ]]; then
                # "asan tsan" reads as a request for both, and silently keeping the last one would
                # produce a run that looks like it covered the first.
                echo "ERROR: name one sanitizer selection; combine with '+', as in asan+ubsan" >&2
                exit 1
            fi
            selection="$1"
            selection_given="true"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unrecognized argument '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [[ "$selection" == *asan* && "$selection" == *tsan* ]]; then
    echo "ERROR: asan and tsan cannot be combined; run them as two separate invocations" >&2
    exit 1
fi

# The build tree is named after what is in it, so output/asan and output/tsan can coexist and neither
# disturbs the ordinary output/debug build.
build_tree="$(tr '+' '-' <<< "$selection")"

export KMX_SANITIZERS="$selection"
export KMX_BUILD_ROOT="$repo_root/output/$build_tree"

# QBS synthesizes the Linux target triple from its platform model. Clang packages can carry sanitizer
# runtimes under the driver's native triple instead of QBS's vendor spelling; fail before a long build
# if the selected runtime cannot be found under the target spelling the compiler/linker will request.
sanitizer_cxx="${KMX_CXX:-c++}"
if [[ "$sanitizer_cxx" == */* || -n "$(command -v "$sanitizer_cxx" 2>/dev/null || true)" ]]; then
    sanitizer_cxx_path="$(command -v "$sanitizer_cxx" 2>/dev/null || printf '%s' "$sanitizer_cxx")"
    if [[ "$("$sanitizer_cxx_path" --version 2>/dev/null | head -n 1)" == *clang* ]]; then
        sanitizer_resource_dir="$("$sanitizer_cxx_path" -print-resource-dir 2>/dev/null || true)"
        sanitizer_target_dir="$sanitizer_resource_dir/lib/x86_64-pc-linux-gnu"
        sanitizer_runtimes=()
        [[ "$selection" == *asan* ]] && sanitizer_runtimes+=(libclang_rt.asan_static.a)
        [[ "$selection" == *ubsan* ]] && sanitizer_runtimes+=(libclang_rt.ubsan_standalone.a)
        for sanitizer_runtime in "${sanitizer_runtimes[@]}"; do
            if [[ -n "$sanitizer_resource_dir" && ! -f "$sanitizer_target_dir/$sanitizer_runtime" ]]; then
                echo "ERROR: Clang sanitizer runtime '$sanitizer_runtime' is missing for QBS target x86_64-pc-linux-gnu." >&2
                echo "       Clang resource directory: $sanitizer_resource_dir" >&2
                echo "       Installed runtime candidates:" >&2
                candidate_found="false"
                candidate=""
                for candidate in "$sanitizer_resource_dir"/lib/*/"$sanitizer_runtime"; do
                    if [[ -f "$candidate" ]]; then
                        echo "       $candidate" >&2
                        candidate_found="true"
                    fi
                done
                [[ "$candidate_found" == "true" ]] || echo "       (none found)" >&2
                echo "       Install the matching compiler-rt target runtime or configure QBS to use the compiler's native target triple." >&2
                exit 1
            fi
        done
    fi
fi

echo "==> Sanitizers: $selection"
echo "==> Build tree: $KMX_BUILD_ROOT"

if [[ "$selection_given" == "false" ]]; then
    echo "==> (no sanitizer named; using the default)"
fi

# common.sh reads KMX_SANITIZERS when it is sourced, and turns it into both the qbs properties the build
# needs and the ASAN_OPTIONS/UBSAN_OPTIONS the binaries need. Sourcing it here only reports what those
# came out as; run-unit-tests.sh sources it again for the run itself.
source "$repo_root/script/feature/common.sh"

echo "==> qbs properties: ${qbs_instrumentation_args[*]}"

apply_sanitizer_runtime_options
for variable in ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS; do
    if [[ -n "${!variable:-}" ]]; then
        echo "==> $variable=${!variable}"
    fi
done

bash "$repo_root/script/run-unit-tests.sh"

echo "==> Sanitizer run completed with no findings ($selection)"
