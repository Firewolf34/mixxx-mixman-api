#!/bin/bash
# Bound runner cache growth without touching registration state or artifacts.

set -euo pipefail

MODE="${1:---finalize}"
DATA_PATH="${MIXXX_BUILD_DATA_PATH:-/data}"
RETAINED_CACHE_MAX_KIB="${MIXXX_BUILD_RETAINED_CACHE_MAX_KIB:-5242880}"
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

die() {
    echo "Error: $*" >&2
    exit 1
}

[[ "${MODE}" == "--prepare" || "${MODE}" == "--finalize" ]] ||
    die "Usage: $0 [--prepare|--finalize]"
[[ "${DATA_PATH}" == /* && -d "${DATA_PATH}" ]] ||
    die "MIXXX_BUILD_DATA_PATH must name an existing absolute directory."
[[ "${RETAINED_CACHE_MAX_KIB}" =~ ^[1-9][0-9]*$ ]] ||
    die "MIXXX_BUILD_RETAINED_CACHE_MAX_KIB must be a positive integer."
[[ "${REPO_ROOT}" != / && -d "${REPO_ROOT}" ]] ||
    die "Could not determine a safe repository root."

empty_children() {
    local path="$1"
    [[ "${path}" == "${DATA_PATH}"/* && -d "${path}" ]] ||
        die "Refusing to clean unexpected path: ${path}"
    find "${path}" -mindepth 1 -maxdepth 1 -xdev -exec rm -rf -- {} +
}

retained_cache_kib() {
    local total=0
    local path
    for path in "${DATA_PATH}/.local/share/flatpak" "${DATA_PATH}/ccache"; do
        if [[ -e "${path}" ]]; then
            total=$((total + $(du -sk "${path}" | awk '{print $1}')))
        fi
    done
    printf '%s\n' "${total}"
}

install -d -m 0750 "${DATA_PATH}/tmp" "${DATA_PATH}/cache" "${DATA_PATH}/ccache"
empty_children "${DATA_PATH}/tmp"
empty_children "${DATA_PATH}/cache"

if [[ "${MODE}" == "--finalize" ]]; then
    rm -rf -- "${REPO_ROOT}/build_flatpak" "${REPO_ROOT}/flatpak_repo" \
        "${REPO_ROOT}/Mixxx.flatpak" "${REPO_ROOT}/.flatpak-builder"
fi

current_kib="$(retained_cache_kib)"
if ((current_kib > RETAINED_CACHE_MAX_KIB)); then
    echo "Retained runner cache is ${current_kib} KiB; clearing ccache to enforce ${RETAINED_CACHE_MAX_KIB} KiB."
    empty_children "${DATA_PATH}/ccache"
    current_kib="$(retained_cache_kib)"
fi
if ((current_kib > RETAINED_CACHE_MAX_KIB)); then
    echo "Flatpak SDK cache alone exceeds the retained-cache cap; clearing it for a cold next build."
    empty_children "${DATA_PATH}/.local/share/flatpak"
    current_kib="$(retained_cache_kib)"
fi

echo "Retained Mixxx runner cache: ${current_kib} KiB (cap ${RETAINED_CACHE_MAX_KIB} KiB)"
