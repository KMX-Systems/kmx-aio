#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
interop_dir="$repo_root/documentation/features/knx/interoperability"
evidence_file="$interop_dir/evidence.tsv"
matrix_file="$interop_dir/matrix.md"

required_profiles=(
    "SEARCH / DESCRIPTION"
    "Tunnelling, no Secure"
    "Routing indication/control"
    "Client <-> in-tree server"
    "IP Secure"
    "Data Secure"
)

usage() {
    cat <<'EOF'
Usage:
  interoperability-matrix.sh verify
  interoperability-matrix.sh render
  interoperability-matrix.sh record --profile <value> --peer <value> --transport <value> --result <pending|passing|failing|skipped> [--capture <value>] [--notes <value>]
    interoperability-matrix.sh import --file <path> [--require-capture-files] [--dry-run]
EOF
}

escape_markdown() {
    local value="${1:-}"
    value="${value//$'\t'/ }"
    value="${value//$'\n'/ }"
    value="${value//|/\\|}"
    echo "$value"
}

normalize_field() {
    local value="${1:-}"
    value="${value//$'\t'/ }"
    value="${value//$'\n'/ }"
    echo "$value"
}

is_valid_result() {
    local result="${1:-}"
    case "$result" in
        pending|passing|failing|skipped) return 0 ;;
        *) return 1 ;;
    esac
}

ensure_inputs() {
    if [[ ! -f "$evidence_file" ]]; then
        echo "Missing evidence file: $evidence_file" >&2
        exit 1
    fi
}

capture_exists() {
    local capture="$1"
    local resolved="$capture"

    [[ -n "$resolved" ]] || return 1
    if [[ "$resolved" != /* ]]; then
        resolved="$repo_root/$resolved"
    fi

    [[ -f "$resolved" ]]
}

verify_matrix() {
    ensure_inputs

    declare -A seen_profiles=()
    local line_number=1
    local profile peer transport result capture notes updated_utc

    while IFS=$'\x1f' read -r profile peer transport result capture notes updated_utc; do
        ((line_number += 1))

        if [[ -z "$profile" || -z "$transport" || -z "$result" || -z "$updated_utc" ]]; then
            echo "Invalid row $line_number: missing required fields" >&2
            exit 1
        fi

        if ! is_valid_result "$result"; then
            echo "Invalid row $line_number: result '$result' is not supported" >&2
            exit 1
        fi

        if [[ "$result" != "pending" && "$result" != "skipped" && -z "$capture" ]]; then
            echo "Invalid row $line_number: capture is required for result '$result'" >&2
            exit 1
        fi

        seen_profiles["$profile"]=1
    done < <(awk -F '\t' 'NR > 1 { print $1 "\037" $2 "\037" $3 "\037" $4 "\037" $5 "\037" $6 "\037" $7 }' "$evidence_file")

    local required
    for required in "${required_profiles[@]}"; do
        if [[ -z "${seen_profiles[$required]:-}" ]]; then
            echo "Missing required interoperability profile row: $required" >&2
            exit 1
        fi
    done

    echo "Interoperability evidence verification passed"
}

render_matrix() {
    ensure_inputs

    {
        echo "# KNX Interoperability Matrix"
        echo
        echo "Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
        echo
        echo "| Profile | Peer and version | Transport | Result | Capture | Notes | Last updated |"
        echo "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |"

        local line_number=1
        local profile peer transport result capture notes updated_utc
        while IFS=$'\x1f' read -r profile peer transport result capture notes updated_utc; do
            ((line_number += 1))

            printf '| %s | %s | %s | %s | %s | %s | %s |\n' \
                "$(escape_markdown "$profile")" \
                "$(escape_markdown "$peer")" \
                "$(escape_markdown "$transport")" \
                "$(escape_markdown "$result")" \
                "$(escape_markdown "$capture")" \
                "$(escape_markdown "$notes")" \
                "$(escape_markdown "$updated_utc")"
        done < <(awk -F '\t' 'NR > 1 { print $1 "\037" $2 "\037" $3 "\037" $4 "\037" $5 "\037" $6 "\037" $7 }' "$evidence_file")
    } > "$matrix_file"

    echo "Rendered interoperability matrix to $matrix_file"
}

record_row() {
    ensure_inputs

    local profile=""
    local peer=""
    local transport=""
    local result=""
    local capture=""
    local notes=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --profile)
                profile="${2:-}"
                shift 2
                ;;
            --peer)
                peer="${2:-}"
                shift 2
                ;;
            --transport)
                transport="${2:-}"
                shift 2
                ;;
            --result)
                result="${2:-}"
                shift 2
                ;;
            --capture)
                capture="${2:-}"
                shift 2
                ;;
            --notes)
                notes="${2:-}"
                shift 2
                ;;
            *)
                echo "Unknown argument: $1" >&2
                usage
                exit 1
                ;;
        esac
    done

    if [[ -z "$profile" || -z "$transport" || -z "$result" ]]; then
        echo "Missing required record arguments" >&2
        usage
        exit 1
    fi

    if ! is_valid_result "$result"; then
        echo "Unsupported result '$result'" >&2
        exit 1
    fi

    if [[ "$result" != "pending" && "$result" != "skipped" && -z "$capture" ]]; then
        echo "Capture is required for result '$result'" >&2
        exit 1
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$(normalize_field "$profile")" \
        "$(normalize_field "$peer")" \
        "$(normalize_field "$transport")" \
        "$(normalize_field "$result")" \
        "$(normalize_field "$capture")" \
        "$(normalize_field "$notes")" \
        "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" >> "$evidence_file"

    verify_matrix
    render_matrix
}

import_rows() {
    ensure_inputs

    local import_file=""
    local require_capture_files="false"
    local dry_run="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --file)
                import_file="${2:-}"
                shift 2
                ;;
            --require-capture-files)
                require_capture_files="true"
                shift
                ;;
            --dry-run)
                dry_run="true"
                shift
                ;;
            *)
                echo "Unknown argument: $1" >&2
                usage
                exit 1
                ;;
        esac
    done

    if [[ -z "$import_file" ]]; then
        echo "Import requires --file" >&2
        usage
        exit 1
    fi

    if [[ ! -f "$import_file" ]]; then
        echo "Import file does not exist: $import_file" >&2
        exit 1
    fi

    local imported_rows=0
    local line_number=1
    local profile peer transport result capture notes updated_utc
    while IFS=$'\x1f' read -r profile peer transport result capture notes updated_utc; do
        ((line_number += 1))

        profile="$(normalize_field "$profile")"
        peer="$(normalize_field "$peer")"
        transport="$(normalize_field "$transport")"
        result="$(normalize_field "$result")"
        capture="$(normalize_field "$capture")"
        notes="$(normalize_field "$notes")"
        updated_utc="$(normalize_field "$updated_utc")"

        if [[ -z "$profile" || -z "$transport" || -z "$result" ]]; then
            echo "Invalid import row $line_number: missing required fields" >&2
            exit 1
        fi

        if ! is_valid_result "$result"; then
            echo "Invalid import row $line_number: unsupported result '$result'" >&2
            exit 1
        fi

        if [[ "$result" != "pending" && "$result" != "skipped" && -z "$capture" ]]; then
            echo "Invalid import row $line_number: capture is required for result '$result'" >&2
            exit 1
        fi

        if [[ "$require_capture_files" == "true" && "$result" != "pending" && "$result" != "skipped" ]]; then
            if ! capture_exists "$capture"; then
                echo "Invalid import row $line_number: capture file not found '$capture'" >&2
                exit 1
            fi
        fi

        if [[ "$dry_run" == "false" ]]; then
            if [[ -z "$updated_utc" ]]; then
                updated_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
            fi

            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$profile" \
                "$peer" \
                "$transport" \
                "$result" \
                "$capture" \
                "$notes" \
                "$updated_utc" >> "$evidence_file"
        fi

        ((imported_rows += 1))
    done < <(awk -F '\t' 'NR > 1 { print $1 "\037" $2 "\037" $3 "\037" $4 "\037" $5 "\037" $6 "\037" $7 }' "$import_file")

    if ((imported_rows == 0)); then
        echo "Import file has no data rows: $import_file" >&2
        exit 1
    fi

    if [[ "$dry_run" == "true" ]]; then
        echo "Validated $imported_rows interoperability import rows"
        return 0
    fi

    verify_matrix
    render_matrix
    echo "Imported $imported_rows interoperability rows"
}

command="${1:-}"
case "$command" in
    verify)
        verify_matrix
        ;;
    render)
        render_matrix
        ;;
    record)
        shift
        record_row "$@"
        ;;
    import)
        shift
        import_rows "$@"
        ;;
    *)
        usage
        exit 1
        ;;
esac
