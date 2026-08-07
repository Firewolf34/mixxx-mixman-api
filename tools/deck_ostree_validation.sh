#!/bin/bash
# Shared OSTree commit-subject validation for Deck Flatpak bundles.

deck_ostree_commit_subject_contains_source() {
    if [[ "$#" -ne 3 ]]; then
        echo "Error: expected OSTree repository, commit, and source SHA." >&2
        return 2
    fi

    local repository="$1"
    local commit="$2"
    local source_sha="$3"
    local commit_details
    local commit_subject=""
    local line
    local saw_date=false

    if [[ ! "${source_sha}" =~ ^[0-9a-f]{40}$ ]]; then
        echo "Error: source SHA must be exactly 40 lowercase hexadecimal characters." >&2
        return 2
    fi

    if ! commit_details="$(LC_ALL=C ostree --repo="${repository}" show "${commit}")"; then
        echo "Error: could not read OSTree commit ${commit}." >&2
        return 1
    fi

    while IFS= read -r line; do
        if [[ "${line}" == Date:* ]]; then
            saw_date=true
            continue
        fi
        if [[ "${saw_date}" == true && "${line}" == "(no subject)" ]]; then
            break
        fi
        if [[ "${saw_date}" == true && "${line}" == "    "* ]]; then
            commit_subject="${line#    }"
            break
        fi
    done <<<"${commit_details}"

    if [[ -z "${commit_subject}" ]]; then
        echo "Error: OSTree commit ${commit} has no readable subject." >&2
        return 1
    fi
    if [[ "${commit_subject}" != *"${source_sha}"* ]]; then
        echo "Error: bundle commit subject does not identify source SHA ${source_sha}." >&2
        return 1
    fi
}
