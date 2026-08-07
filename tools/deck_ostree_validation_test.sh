#!/bin/bash
# Regression tests for portable OSTree commit-subject validation.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/deck_ostree_validation.sh
source "${SCRIPT_DIR}/deck_ostree_validation.sh"

SOURCE_SHA="70412b6b79fdc897701f48ea7dd64ded69f311bb"

ostree() {
    printf 'commit %s\nDate: 2026-08-06 00:00:00 +0000\nSubject: Built from %s\n' \
        "${SOURCE_SHA}" "${SOURCE_SHA}"
}

deck_ostree_commit_subject_contains_source test-repo test-commit "${SOURCE_SHA}"

ostree() {
    printf 'commit %s\nDate: 2026-08-06 00:00:00 +0000\nSubject: Built from another commit\n' \
        "${SOURCE_SHA}"
}

if deck_ostree_commit_subject_contains_source \
        test-repo test-commit "${SOURCE_SHA}" 2>/dev/null; then
    echo "Error: source SHA outside the commit subject was accepted." >&2
    exit 1
fi

ostree() {
    return 23
}

if deck_ostree_commit_subject_contains_source \
        test-repo test-commit "${SOURCE_SHA}" 2>/dev/null; then
    echo "Error: an OSTree read failure was accepted." >&2
    exit 1
fi

echo "OSTree commit-subject validation tests passed."
