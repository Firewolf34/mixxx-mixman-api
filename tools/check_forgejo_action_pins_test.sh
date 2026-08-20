#!/bin/bash
# Focused fixtures for the Forgejo action-reference policy.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CHECKER="${SCRIPT_DIR}/check_forgejo_action_pins.sh"
TEST_ROOT="$(mktemp -d)"

cleanup() {
    rm -rf -- "${TEST_ROOT}"
}
trap cleanup EXIT

write_workflow() {
    local directory="$1"
    local reference="$2"
    mkdir -p -- "${directory}"
    printf 'jobs:\n  build:\n    steps:\n      - uses: %s\n' "${reference}" \
        >"${directory}/test.yml"
}

expect_rejected() {
    local name="$1"
    local reference="$2"
    local directory="${TEST_ROOT}/${name}"
    write_workflow "${directory}" "${reference}"
    if "${CHECKER}" "${directory}" >"${directory}/output" 2>&1; then
        echo "Error: accepted mutable action reference: ${reference}" >&2
        exit 1
    fi
    grep -Fq 'mutable or non-reviewable action reference' "${directory}/output"
}

valid_directory="${TEST_ROOT}/valid"
mkdir -p -- "${valid_directory}"
printf '%s\n' \
    'jobs:' \
    '  build:' \
    '    steps:' \
    '      - uses: ./local-action' \
    '      - uses: https://data.forgejo.org/actions/checkout@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' \
    '      - uses: "owner/action@bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" # reviewed release' \
    '      - uses: docker://alpine@sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc' \
    >"${valid_directory}/valid.yaml"
"${CHECKER}" "${valid_directory}" >/dev/null

expect_rejected tag 'https://data.forgejo.org/actions/checkout@v6'
expect_rejected branch 'owner/action@main'
expect_rejected short_sha 'owner/action@abcdef123456'
expect_rejected uppercase_sha 'owner/action@AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
expect_rejected expression 'owner/action@${{ forgejo.sha }}'
expect_rejected empty ''
expect_rejected folded_scalar '>'
expect_rejected mutable_container 'docker://alpine:3.22'

echo "Forgejo action pin policy tests passed."
