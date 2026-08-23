#!/bin/bash
# Shared byte and free-space limits for deck downloads and rollback snapshots.

DECK_MAX_ARTIFACT_BYTES="${MIXXX_DECK_MAX_ARTIFACT_BYTES:-2147483648}"
DECK_MIN_FREE_RESERVE_BYTES="${MIXXX_DECK_MIN_FREE_RESERVE_BYTES:-1073741824}"
DECK_MAX_METADATA_BYTES="${MIXXX_DECK_MAX_METADATA_BYTES:-1048576}"

deck_storage_die() {
    echo "Error: $*" >&2
    return 1
}

deck_require_nonnegative_integer() {
    local value="$1"
    local label="$2"
    [[ "${value}" =~ ^(0|[1-9][0-9]{0,17})$ ]] ||
        deck_storage_die "${label} must be a non-negative integer number of bytes."
}

deck_validate_storage_limits() {
    deck_require_nonnegative_integer "${DECK_MAX_ARTIFACT_BYTES}" \
        MIXXX_DECK_MAX_ARTIFACT_BYTES || return 1
    if ((DECK_MAX_ARTIFACT_BYTES == 0)); then
        deck_storage_die "MIXXX_DECK_MAX_ARTIFACT_BYTES must be greater than zero."
        return 1
    fi
    deck_require_nonnegative_integer "${DECK_MIN_FREE_RESERVE_BYTES}" \
        MIXXX_DECK_MIN_FREE_RESERVE_BYTES || return 1
    deck_require_nonnegative_integer "${DECK_MAX_METADATA_BYTES}" \
        MIXXX_DECK_MAX_METADATA_BYTES || return 1
    if ((DECK_MAX_METADATA_BYTES == 0 ||
            DECK_MAX_METADATA_BYTES > DECK_MAX_ARTIFACT_BYTES)); then
        deck_storage_die \
            "MIXXX_DECK_MAX_METADATA_BYTES must be positive and no larger than the artifact limit."
        return 1
    fi
}

deck_snapshot_work_bytes() {
    printf '%s\n' "$((DECK_MAX_ARTIFACT_BYTES * 2))"
}

deck_require_size_within_budget() {
    local size_bytes="$1"
    local label="$2"
    deck_require_nonnegative_integer "${size_bytes}" "${label} size" || return 1
    if ((size_bytes == 0)); then
        deck_storage_die "${label} must not be empty."
        return 1
    fi
    if ((size_bytes > DECK_MAX_ARTIFACT_BYTES)); then
        deck_storage_die \
            "${label} is ${size_bytes} bytes, above the ${DECK_MAX_ARTIFACT_BYTES}-byte deck limit."
        return 1
    fi
}

deck_available_bytes() {
    local existing_path="$1"
    local available_kib
    if [[ ! -e "${existing_path}" ]]; then
        deck_storage_die "Storage-budget path does not exist: ${existing_path}"
        return 1
    fi
    available_kib="$(df -Pk -- "${existing_path}" | awk 'NR == 2 {print $4}')"
    if [[ ! "${available_kib}" =~ ^[0-9]+$ ]]; then
        deck_storage_die "Could not determine free space for ${existing_path}."
        return 1
    fi
    printf '%s\n' "$((available_kib * 1024))"
}

deck_require_free_space() {
    local existing_path="$1"
    local output_bytes="$2"
    local label="$3"
    local available_bytes required_bytes
    deck_require_nonnegative_integer "${output_bytes}" "${label} output" || return 1
    available_bytes="$(deck_available_bytes "${existing_path}")" || return 1
    required_bytes=$((output_bytes + DECK_MIN_FREE_RESERVE_BYTES))
    if ((available_bytes < required_bytes)); then
        deck_storage_die \
            "${label} needs ${required_bytes} free bytes including reserve; only ${available_bytes} are available."
        return 1
    fi
}

deck_run_with_artifact_limit() (
    local limit_kib
    limit_kib=$(((DECK_MAX_ARTIFACT_BYTES + 1023) / 1024))
    ulimit -f "${limit_kib}"
    "$@"
)

deck_verify_bounded_file() {
    local path="$1"
    local label="$2"
    local size_bytes
    if [[ ! -s "${path}" ]]; then
        deck_storage_die "${label} is missing or empty."
        return 1
    fi
    size_bytes="$(stat -c '%s' -- "${path}")"
    deck_require_size_within_budget "${size_bytes}" "${label}"
}
