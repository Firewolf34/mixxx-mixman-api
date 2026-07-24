#!/bin/bash
# Abort the build if host-wide memory stalls become severe and sustained.

set -euo pipefail

PRESSURE_FILE="${MIXXX_BUILD_PRESSURE_FILE:-/proc/pressure/memory}"
MAX_SOME_AVG10="${MIXXX_BUILD_ABORT_PSI_SOME_AVG10:-60.00}"
MAX_FULL_AVG10="${MIXXX_BUILD_ABORT_PSI_FULL_AVG10:-20.00}"
INTERVAL_SECONDS="${MIXXX_BUILD_PSI_INTERVAL_SECONDS:-10}"
CONSECUTIVE_LIMIT="${MIXXX_BUILD_PSI_CONSECUTIVE_LIMIT:-6}"

die() {
    echo "Error: $*" >&2
    exit 1
}

[[ $# -gt 0 ]] || die "A command is required."
[[ "${PRESSURE_FILE}" == /* && -r "${PRESSURE_FILE}" ]] ||
    die "MIXXX_BUILD_PRESSURE_FILE must name a readable absolute file."
for value_name in MAX_SOME_AVG10 MAX_FULL_AVG10; do
    value="${!value_name}"
    [[ "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
        die "${value_name} must be a non-negative number."
done
for value_name in INTERVAL_SECONDS CONSECUTIVE_LIMIT; do
    value="${!value_name}"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] ||
        die "${value_name} must be a positive integer."
done
command -v setsid >/dev/null 2>&1 || die "setsid is required."

read_avg10() {
    local category="$1"
    awk -v category="${category}" '$1 == category {
        for (i = 1; i <= NF; ++i) {
            if ($i ~ /^avg10=/) {
                sub(/^avg10=/, "", $i)
                print $i
                exit
            }
        }
    }' "${PRESSURE_FILE}"
}

exceeds() {
    awk -v actual="$1" -v maximum="$2" \
        'BEGIN { exit !(actual > maximum) }'
}

setsid --wait "$@" &
session_pid=$!
violations=0

terminate_session() {
    if kill -0 "${session_pid}" 2>/dev/null; then
        kill -TERM -- "-${session_pid}" 2>/dev/null || true
        for _ in 1 2 3 4 5 6; do
            kill -0 "${session_pid}" 2>/dev/null || return
            sleep 5
        done
        kill -KILL -- "-${session_pid}" 2>/dev/null || true
    fi
}
trap terminate_session EXIT INT TERM

while kill -0 "${session_pid}" 2>/dev/null; do
    sleep "${INTERVAL_SECONDS}"
    kill -0 "${session_pid}" 2>/dev/null || break

    some_avg10="$(read_avg10 some)"
    full_avg10="$(read_avg10 full)"
    [[ "${some_avg10}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
        die "Could not read memory PSI some avg10."
    [[ "${full_avg10}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
        die "Could not read memory PSI full avg10."

    if exceeds "${some_avg10}" "${MAX_SOME_AVG10}" ||
            exceeds "${full_avg10}" "${MAX_FULL_AVG10}"; then
        violations=$((violations + 1))
        echo "Memory pressure warning ${violations}/${CONSECUTIVE_LIMIT}: some avg10=${some_avg10}% full avg10=${full_avg10}%" >&2
    else
        violations=0
    fi

    if ((violations >= CONSECUTIVE_LIMIT)); then
        echo "Error: sustained host memory pressure exceeded the build safety envelope; terminating the build." >&2
        terminate_session
        wait "${session_pid}" 2>/dev/null || true
        trap - EXIT
        exit 75
    fi
done

set +e
wait "${session_pid}"
status=$?
set -e
trap - EXIT
exit "${status}"
