#!/bin/bash
# Audit and materialize the exact tracked Git tree used for a deck build.

deck_tracked_source_die() {
    echo "Error: $*" >&2
    return 1
}

deck_require_pristine_build_checkout() {
    local repository="$1"
    local contamination
    contamination="$(git -C "${repository}" status \
        --porcelain=v1 --untracked-files=all --ignored=matching)"
    if [[ -n "${contamination}" ]]; then
        printf '%s\n' "${contamination}" >&2
        deck_tracked_source_die \
            "The build checkout contains modified, untracked, or ignored input."
        return 1
    fi
}

deck_export_tracked_source() {
    local repository="$1"
    local revision="$2"
    local destination="$3"
    if [[ ! "${revision}" =~ ^[0-9a-f]{40}$ ]]; then
        deck_tracked_source_die "Tracked source revision is invalid."
        return 1
    fi
    if [[ -e "${destination}" ]]; then
        deck_tracked_source_die \
            "Tracked source destination already exists: ${destination}"
        return 1
    fi
    if ! mkdir -m 0700 -- "${destination}"; then
        deck_tracked_source_die \
            "Could not create tracked source destination: ${destination}"
        return 1
    fi
    if ! git -C "${repository}" archive --format=tar "${revision}" |
            tar -x -C "${destination}"; then
        rm -rf -- "${destination}"
        deck_tracked_source_die "Could not materialize the tracked source tree."
    fi
}
