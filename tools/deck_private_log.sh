#!/bin/bash
# Persist the most recent failed deck workflow step in private runner state.

set -euo pipefail

LOG_DIR="${MIXXX_DECK_PRIVATE_LOG_DIR:-/data/logs}"
MAX_LOG_BYTES="${MIXXX_DECK_PRIVATE_LOG_MAX_BYTES:-2097152}"

usage() {
    cat <<'EOF'
Usage: deck_private_log.sh <safe-step-name> -- <command> [args...]

Run a deck workflow command, streaming its output normally. If it fails, retain
the trailing bounded output in the runner-only latest.log file.
EOF
}

die() {
    echo "Error: $*" >&2
    exit 1
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

[[ $# -ge 3 ]] || {
    usage >&2
    exit 2
}

step_name="$1"
shift
[[ "$1" == "--" ]] || die "Expected -- after the step name."
shift

[[ "${step_name}" =~ ^[a-z0-9][a-z0-9_-]{0,63}$ ]] ||
    die "Step name must contain only lowercase letters, digits, underscores, and hyphens."
[[ "${LOG_DIR}" == /data/* && "${LOG_DIR}" != *'/../'* && "${LOG_DIR}" != */.. ]] ||
    die "MIXXX_DECK_PRIVATE_LOG_DIR must be a private directory below /data."
[[ "${MAX_LOG_BYTES}" =~ ^[1-9][0-9]*$ ]] ||
    die "MIXXX_DECK_PRIVATE_LOG_MAX_BYTES must be a positive integer."
((MAX_LOG_BYTES <= 4194304)) ||
    die "MIXXX_DECK_PRIVATE_LOG_MAX_BYTES must not exceed 4194304."

umask 077
install -d -m 0700 "${LOG_DIR}"

temporary_log="$(mktemp "${LOG_DIR}/.${step_name}.XXXXXX")"
replacement_log=""

cleanup() {
    rm -f -- "${temporary_log:-}" "${replacement_log:-}"
}
trap cleanup EXIT

set +e
"$@" 2>&1 | tee "${temporary_log}"
pipeline_statuses=("${PIPESTATUS[@]}")
set -e

command_status="${pipeline_statuses[0]}"
tee_status="${pipeline_statuses[1]}"
((tee_status == 0)) || die "Could not record private workflow output."

if ((command_status == 0)); then
    exit 0
fi

replacement_log="$(mktemp "${LOG_DIR}/.latest.XXXXXX")"
{
    printf 'Deck Flatpak workflow failure\n'
    printf 'Step: %s\n' "${step_name}"
    printf 'Recorded: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'Exit status: %s\n\n' "${command_status}"
    tail -c "${MAX_LOG_BYTES}" "${temporary_log}"
} >"${replacement_log}"
chmod 0600 "${replacement_log}"
mv -f -- "${replacement_log}" "${LOG_DIR}/latest.log"
replacement_log=""

echo "Saved the failed ${step_name} output to private runner log ${LOG_DIR}/latest.log." >&2
exit "${command_status}"
