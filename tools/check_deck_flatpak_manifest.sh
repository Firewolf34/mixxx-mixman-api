#!/bin/bash
# Verify that the deck manifest differs only by documented memory-saving knobs.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
NORMAL_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.yaml"
DECK_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.deck.yaml"
NORMALIZED_NORMAL_MANIFEST="$(mktemp)"
NORMALIZED_MANIFEST="$(mktemp)"

cleanup() {
    rm -f -- "${NORMALIZED_NORMAL_MANIFEST}" "${NORMALIZED_MANIFEST}"
}
trap cleanup EXIT

awk '
    $0 == "  - name: mixxx" {
        in_mixxx_module = 1
        print
        next
    }
    /^  - name:/ {
        in_mixxx_module = 0
        print
        next
    }
    in_mixxx_module && $0 == "    run-tests: true" {
        next
    }
    in_mixxx_module && $0 == "    build-options:" {
        in_mixxx_build_options = 1
        next
    }
    in_mixxx_build_options {
        if ($0 == "    config-opts:") {
            in_mixxx_build_options = 0
            print
        }
        next
    }
    in_mixxx_module && $0 == "      - -DBUILD_TESTING=ON" {
        print "      - -DBUILD_TESTING=OFF"
        next
    }
    {
        print
    }
' "${NORMAL_MANIFEST}" >"${NORMALIZED_NORMAL_MANIFEST}"

awk '
    BEGIN {
        started = 0
        in_top_build_options = 0
        modules_seen = 0
    }
    !started {
        if ($0 ~ /^app-id:/) {
            started = 1
        } else {
            next
        }
    }
    !modules_seen && $0 == "build-options:" {
        in_top_build_options = 1
        print
        next
    }
    in_top_build_options {
        if ($0 == "cleanup:") {
            in_top_build_options = 0
            print
        } else if ($0 == "  no-debuginfo: true" || $0 == "  strip: true") {
            next
        } else {
            print
        }
        next
    }
    $0 == "modules:" {
        modules_seen = 1
        print
        next
    }
    $0 == "      - -DCMAKE_BUILD_TYPE=Release" {
        print "      - -DCMAKE_BUILD_TYPE=RelWithDebInfo"
        next
    }
    $0 == "      - \"-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG\"" ||
    $0 == "      - \"-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG\"" ||
    $0 == "      - \"-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-fuse-ld=bfd -Wl,--no-keep-memory,--reduce-memory-overheads\"" ||
    $0 == "      - \"-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=-fuse-ld=bfd -Wl,--no-keep-memory,--reduce-memory-overheads\"" ||
    $0 == "      - -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF" {
        next
    }
    {
        print
    }
' "${DECK_MANIFEST}" >"${NORMALIZED_MANIFEST}"

if ! diff -u "${NORMALIZED_NORMAL_MANIFEST}" "${NORMALIZED_MANIFEST}"; then
    echo "Error: deck and normal Flatpak manifests have undocumented drift." >&2
    exit 1
fi

echo "Deck Flatpak manifest is synchronized with the normal manifest."
