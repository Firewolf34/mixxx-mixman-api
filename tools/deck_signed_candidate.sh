#!/bin/bash
# Resolve and verify source-qualified commits from a configured signed OSTree remote.

deck_signed_candidate_die() {
    echo "Error: $*" >&2
    return 1
}

deck_signed_history_commit() {
    local user_repo="$1"
    local remote_name="$2"
    local source_sha="$3"
    local history_ref remote_ref listing commit
    if [[ ! -d "${user_repo}" ]]; then
        deck_signed_candidate_die \
            "The user Flatpak repository is unavailable for signed candidate verification."
        return 1
    fi
    if [[ ! "${remote_name}" =~ ^[A-Za-z0-9._-]+$ ]]; then
        deck_signed_candidate_die "The signed repository name is invalid."
        return 1
    fi
    if [[ ! "${source_sha}" =~ ^[0-9a-f]{40}$ ]]; then
        deck_signed_candidate_die "The signed candidate source SHA is invalid."
        return 1
    fi
    history_ref="mixxx/history/${source_sha}"
    remote_ref="${remote_name}:${history_ref}"
    if ! listing="$(ostree --repo="${user_repo}" remote refs --revision \
            "${remote_name}")"; then
        deck_signed_candidate_die "Could not read the configured signed Mixxx repository."
        return 1
    fi
    commit="$(awk -v ref="${remote_ref}" '
        $1 == ref && $2 ~ /^[0-9a-f]{64}$/ {print $2}
        $2 == ref && $1 ~ /^[0-9a-f]{64}$/ {print $1}
    ' <<<"${listing}")"
    if [[ ! "${commit}" =~ ^[0-9a-f]{64}$ ]]; then
        deck_signed_candidate_die \
            "Signed repository history does not contain ${source_sha}."
        return 1
    fi
    if ! ostree --repo="${user_repo}" pull --depth=0 --commit-metadata-only \
            "${remote_name}" "${history_ref}" >/dev/null; then
        deck_signed_candidate_die "Could not fetch signed metadata for ${source_sha}."
        return 1
    fi
    if [[ "$(ostree --repo="${user_repo}" rev-parse "${remote_ref}")" != \
            "${commit}" ]]; then
        deck_signed_candidate_die \
            "Signed repository history changed during verification."
        return 1
    fi
    if ! ostree --repo="${user_repo}" show --gpg-verify-remote="${remote_name}" \
            "${commit}" >/dev/null; then
        deck_signed_candidate_die \
            "Candidate commit is not authenticated by the configured signing key."
        return 1
    fi
    printf '%s\n' "${commit}"
}

deck_require_signed_bundle_commit() {
    local user_repo="$1"
    local remote_name="$2"
    local source_sha="$3"
    local bundle_commit="$4"
    local signed_commit
    if [[ ! "${bundle_commit}" =~ ^[0-9a-f]{64}$ ]]; then
        deck_signed_candidate_die "Candidate bundle commit is invalid."
        return 1
    fi
    signed_commit="$(deck_signed_history_commit \
        "${user_repo}" "${remote_name}" "${source_sha}")" || return 1
    if [[ "${bundle_commit}" != "${signed_commit}" ]]; then
        deck_signed_candidate_die \
            "Candidate bundle differs from the GPG-authenticated repository commit."
        return 1
    fi
}
