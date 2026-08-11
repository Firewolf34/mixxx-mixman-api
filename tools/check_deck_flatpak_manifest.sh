#!/bin/bash
# Verify that the deck manifest differs only by documented memory-saving knobs.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
NORMAL_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.yaml"
DECK_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.deck.yaml"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"
QML_CONTROLS_REGISTRATION_SOURCE="${REPO_ROOT}/src/qml/qmlcontrolsregistration.cpp"
DECK_DEPLOY_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_deploy.sh"
DECK_AUTO_UPDATE_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_auto_update.sh"
DECK_REPO_PUBLISH_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_repo_publish.sh"
FLATPAK_BUILD_SCRIPT="${REPO_ROOT}/packaging/flatpak/flatpak_build.sh"
DECK_PUBLISH_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_publish.sh"
GITHUB_DECK_WORKFLOW="${REPO_ROOT}/.github/workflows/github-deck-candidate.yml"
DECK_UPDATE_SERVICE="${REPO_ROOT}/packaging/flatpak/systemd/mixxx-deck-update.service"
DECK_UPDATE_TIMER="${REPO_ROOT}/packaging/flatpak/systemd/mixxx-deck-update.timer"
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

if ! grep -Fq 'set(MIXXX_QML_CONTROLS_OPTIONS)' "${CMAKE_FILE}" ||
    ! grep -Fq '${MIXXX_QML_CONTROLS_OPTIONS}' "${CMAKE_FILE}" ||
    ! grep -Fq 'src/qml/qmlcontrolsregistration.cpp' "${CMAKE_FILE}"; then
    echo "Error: the Flatpak Mixxx.Controls registration workaround is incomplete." >&2
    exit 1
fi

if ! grep -Fq 'qml_register_types_Mixxx_Controls()' "${QML_CONTROLS_REGISTRATION_SOURCE}" ||
    ! grep -Fq 'qmlRegisterModule("Mixxx.Controls", 1, 0)' "${QML_CONTROLS_REGISTRATION_SOURCE}"; then
    echo "Error: the Mixxx.Controls static-plugin registration contract is missing." >&2
    exit 1
fi

if ! grep -Fq 'install -m 0644 "${OSTREE_VALIDATION_HELPER}" "${installed_helper}"' \
        "${DECK_DEPLOY_SCRIPT}"; then
    echo "Error: mixxx-deck setup does not install its OSTree validator." >&2
    exit 1
fi
if ! grep -Fq 'flock -s 8' "${DECK_DEPLOY_SCRIPT}" ||
    ! grep -Fq 'deck_flatpak_auto_update.sh' "${DECK_DEPLOY_SCRIPT}" ||
    ! grep -Fq 'OnUnitInactiveSec=4h' "${DECK_UPDATE_TIMER}" ||
    ! grep -Fq 'ExecStartPre=/usr/bin/nm-online -q --timeout=180' "${DECK_UPDATE_SERVICE}"; then
    echo "Error: idle-only automatic update and boot scheduling are incomplete." >&2
    exit 1
fi
if ! grep -Fq 'flatpak update --user --app --no-deploy' "${DECK_AUTO_UPDATE_SCRIPT}" ||
    ! grep -Fq 'flock -n 9' "${DECK_AUTO_UPDATE_SCRIPT}" ||
    ! grep -Fq 'on_ac_power' "${DECK_AUTO_UPDATE_SCRIPT}"; then
    echo "Error: automatic update safety checks are incomplete." >&2
    exit 1
fi
if ! grep -Fq -- '--gpg-sign="${GPG_KEY}"' "${DECK_REPO_PUBLISH_SCRIPT}" ||
    ! grep -Fq 'deck_ostree_commit_subject_contains_source' "${DECK_REPO_PUBLISH_SCRIPT}"; then
    echo "Error: signed repository publication checks are incomplete." >&2
    exit 1
fi
if ! grep -Fq 'mkdir -m 0775 "${STAGING_DIR}"' "${DECK_REPO_PUBLISH_SCRIPT}" ||
    grep -Fq 'mkdir -m 2775 "${STAGING_DIR}"' "${DECK_REPO_PUBLISH_SCRIPT}"; then
    echo "Error: repository staging must inherit setgid instead of requesting it." >&2
    exit 1
fi

if ! grep -Fq 'BUILD_OPTIONS+=("--subject=Built from ${FLATPAK_SOURCE_SHA}")' \
        "${FLATPAK_BUILD_SCRIPT}" ||
    ! grep -Fq 'MIXXX_FLATPAK_SOURCE_SHA="${SOURCE_SHA}"' \
        "${DECK_PUBLISH_SCRIPT}"; then
    echo "Error: the Forgejo publisher does not stamp source provenance." >&2
    exit 1
fi

if ! grep -Fq '          build-dir: build_flatpak' "${GITHUB_DECK_WORKFLOW}" ||
    ! grep -Fq '          repo-dir: repo' "${GITHUB_DECK_WORKFLOW}" ||
    ! grep -Fq '          retention-days: 14' "${GITHUB_DECK_WORKFLOW}"; then
    echo "Error: the GitHub workflow does not pin its validated Flatpak directories." >&2
    exit 1
fi

bash -n "${DECK_AUTO_UPDATE_SCRIPT}" "${DECK_REPO_PUBLISH_SCRIPT}"
bash "${SCRIPT_DIR}/deck_flatpak_auto_update_test.sh"

bash "${SCRIPT_DIR}/deck_ostree_validation_test.sh"

echo "Deck Flatpak manifest is synchronized with the normal manifest."
echo "Flatpak Mixxx.Controls runtime registration is present."
