#!/usr/bin/env bash
set -euo pipefail

resolve_repo_root() {
    local dir
    dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

    while [[ "$dir" != "/" ]]; do
        if [[ -f "$dir/kmx-aio.qbs" && -d "$dir/source" ]]; then
            echo "$dir"
            return 0
        fi
        dir="$(dirname "$dir")"
    done

    echo "Could not locate repository root from script path" >&2
    exit 1
}

repo_root="$(resolve_repo_root)"
consumer_roots=(
    "$repo_root/source/sample"
    "$repo_root/source/library-test"
)
sample_root="$repo_root/source/sample"

if command -v rg >/dev/null 2>&1; then
    mapfile -t umbrella_matches < <(rg -n "Depends \\{ name: ['\"]kmx-aio-lib['\"] \\}" "${consumer_roots[@]}" --glob '*.qbs')
else
    mapfile -t umbrella_matches < <(grep -R -n -E --include='*.qbs' "Depends \{ name: ['\"]kmx-aio-lib['\"] \}" "${consumer_roots[@]}" || true)
fi

if [[ ${#umbrella_matches[@]} -gt 0 ]]; then
    echo "Artifact boundary check failed: these consumer QBS products still depend on kmx-aio-lib:" >&2
    printf '  %s\n' "${umbrella_matches[@]}" >&2
    exit 1
fi

feature_rules=(
    'kmx-aio-spdk|project.enable_spdk'
    'kmx-aio-xdp|project.enable_af_xdp'
    'kmx-aio-quic|project.enable_quic'
    'kmx-aio-avb|project.enable_avb'
    'kmx-aio-gpu|project.enable_cuda'
    'kmx-aio-opcua|project.enable_opc_ua'
    'kmx-aio-someip|project.enable_someip'
)

missing_condition_matches=()
for rule in "${feature_rules[@]}"; do
    artifact_name="${rule%%|*}"
    condition_name="${rule##*|}"

    while IFS= read -r qbs_file; do
        [[ -z "$qbs_file" ]] && continue

        if ! grep -Eq "condition:[[:space:]]*${condition_name}\b" "$qbs_file"; then
            missing_condition_matches+=("${qbs_file}: depends on ${artifact_name} without condition: ${condition_name}")
        fi
    done < <(grep -R -l --include='*.qbs' "Depends { name: \"${artifact_name}\" }" "$sample_root" || true)
done

if [[ ${#missing_condition_matches[@]} -gt 0 ]]; then
    echo "Artifact boundary check failed: these sample products depend on feature-gated artifacts without matching conditions:" >&2
    printf '  %s\n' "${missing_condition_matches[@]}" >&2
    exit 1
fi

echo "Artifact boundary check passed."