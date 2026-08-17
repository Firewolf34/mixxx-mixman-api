#!/bin/bash
# Focused local contract tests for the public mixxx-deck-ci command.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
client="${script_dir}/mixxx_deck_ci.sh"
test_root="$(mktemp -d)"

cleanup() {
    rm -rf -- "${test_root}"
}
trap cleanup EXIT

help_output="$("${client}" --help)"
grep -Fq "mixxx-deck-ci status" <<<"${help_output}"
grep -Fq "Use mixxx-deck for signed artifact" <<<"${help_output}"
grep -Fq "Use forgejo-issues for Forgejo issue tickets" <<<"${help_output}"

if HOME="${test_root}" "${client}" runs >"${test_root}/stdout" 2>"${test_root}/stderr"; then
    echo "Expected a missing-token failure." >&2
    exit 1
fi
grep -Fq "${test_root}/.config/mixxx-deck/forgejo-actions-token" "${test_root}/stderr"

install -d -m 0700 "${test_root}/.config/mixxx-deck"
printf '%s\n' "test-token-value-1234567890" > \
    "${test_root}/.config/mixxx-deck/forgejo-actions-token"
chmod 0644 "${test_root}/.config/mixxx-deck/forgejo-actions-token"
if HOME="${test_root}" "${client}" runs >"${test_root}/stdout" 2>"${test_root}/stderr"; then
    echo "Expected an unsafe-token-mode failure." >&2
    exit 1
fi
grep -Fq "must not be accessible to group or others" "${test_root}/stderr"

echo "mixxx-deck-ci contract tests passed."
