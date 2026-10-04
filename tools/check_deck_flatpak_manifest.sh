#!/bin/bash
# Verify that the deck manifest differs only by documented memory-saving knobs.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
NORMAL_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.yaml"
DECK_MANIFEST="${REPO_ROOT}/packaging/flatpak/org.mixxx.Mixxx.deck.yaml"
NORMAL_PROTOBUF_MODULE="${REPO_ROOT}/packaging/flatpak/modules/protobuf.yaml"
DECK_PROTOBUF_MODULE="${REPO_ROOT}/packaging/flatpak/modules/protobuf.deck.yaml"
CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"
QML_CONTROLS_REGISTRATION_SOURCE="${REPO_ROOT}/src/qml/qmlcontrolsregistration.cpp"
DECK_DEPLOY_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_deploy.sh"
DECK_AUTO_UPDATE_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_auto_update.sh"
DECK_BREAK_GLASS_SCRIPT="${REPO_ROOT}/tools/mixxx_break_glass.sh"
DECK_REPO_PUBLISH_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_repo_publish.sh"
FLATPAK_BUILD_SCRIPT="${REPO_ROOT}/packaging/flatpak/flatpak_build.sh"
DECK_PUBLISH_SCRIPT="${REPO_ROOT}/tools/deck_flatpak_publish.sh"
DECK_STORAGE_HELPER="${REPO_ROOT}/tools/deck_storage_budget.sh"
DECK_HTTPS_HELPER="${REPO_ROOT}/tools/deck_https_fetch.sh"
DECK_SIGNED_HELPER="${REPO_ROOT}/tools/deck_signed_candidate.sh"
DECK_TRACKED_SOURCE_HELPER="${REPO_ROOT}/tools/deck_tracked_source.sh"
DECK_ARTIFACT_SAFETY_TEST="${REPO_ROOT}/tools/deck_artifact_safety_test.sh"
GITHUB_DECK_WORKFLOW="${REPO_ROOT}/.github/workflows/github-deck-candidate.yml"
FORGEJO_ACTION_PIN_CHECK="${SCRIPT_DIR}/check_forgejo_action_pins.sh"
DECK_UPDATE_SERVICE="${REPO_ROOT}/packaging/flatpak/systemd/mixxx-deck-update.service"
DECK_UPDATE_TIMER="${REPO_ROOT}/packaging/flatpak/systemd/mixxx-deck-update.timer"
NORMALIZED_NORMAL_MANIFEST="$(mktemp)"
NORMALIZED_MANIFEST="$(mktemp)"
NORMALIZED_DECK_PROTOBUF="$(mktemp)"

cleanup() {
    rm -f -- "${NORMALIZED_NORMAL_MANIFEST}" "${NORMALIZED_MANIFEST}" \
        "${NORMALIZED_DECK_PROTOBUF}"
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
        in_mixxx_module = 0
        in_mixxx_config_opts = 0
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
    $0 == "  - name: mixxx" {
        in_mixxx_module = 1
        print
        next
    }
    in_mixxx_module && /^  - name:/ {
        in_mixxx_module = 0
        in_mixxx_config_opts = 0
    }
    in_mixxx_module && $0 == "    config-opts:" {
        in_mixxx_config_opts = 1
        print
        next
    }
    in_mixxx_config_opts && $0 !~ /^      - / {
        in_mixxx_config_opts = 0
    }
    $0 == "  - modules/protobuf.deck.yaml" {
        print "  - modules/protobuf.yaml"
        next
    }
    $0 == "      - -DCMAKE_BUILD_TYPE=Release" {
        print "      - -DCMAKE_BUILD_TYPE=RelWithDebInfo"
        next
    }
    in_mixxx_config_opts && $0 == "      - -DMIXXX_DECK_LOW_MEMORY_QML_COMPILATION=ON" {
        next
    }
    in_mixxx_config_opts && $0 == "      - -DMIXXX_DECK_LOW_MEMORY_CONTROLLER_PREFERENCES_COMPILATION=ON" {
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

awk '
    $0 ~ /^  #/ { next }
    $0 == "name: protobuf-deck" {
        print "name: protobuf"
        next
    }
    $0 == "  - -DCMAKE_BUILD_TYPE=Release" {
        print "  - -DCMAKE_BUILD_TYPE=RelWithDebInfo"
        next
    }
    $0 == "  - \"-DCMAKE_C_FLAGS_RELEASE=-O1 -g0 -DNDEBUG\"" ||
    $0 == "  - \"-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g0 -DNDEBUG\"" {
        next
    }
    { print }
' "${DECK_PROTOBUF_MODULE}" >"${NORMALIZED_DECK_PROTOBUF}"
if ! diff -u "${NORMAL_PROTOBUF_MODULE}" "${NORMALIZED_DECK_PROTOBUF}"; then
    echo "Error: deck protobuf module has undocumented drift." >&2
    exit 1
fi

if ! grep -Fxq 'name: protobuf-deck' "${DECK_PROTOBUF_MODULE}"; then
    echo "Error: deck protobuf must use an isolated Flatpak Builder module name." >&2
    exit 1
fi

if ! grep -Fq 'set(MIXXX_QML_CONTROLS_OPTIONS)' "${CMAKE_FILE}" ||
    ! grep -Fq '${MIXXX_QML_CONTROLS_OPTIONS}' "${CMAKE_FILE}" ||
    ! grep -Fq 'src/qml/qmlcontrolsregistration.cpp' "${CMAKE_FILE}"; then
    echo "Error: the Flatpak Mixxx.Controls registration workaround is incomplete." >&2
    exit 1
fi

if ! awk '
    $0 == "  MIXXX_DECK_LOW_MEMORY_CONTROLLER_PREFERENCES_COMPILATION" {
        getline
        getline
        if ($0 == "  OFF") {
            default_off++
        }
    }
    END { exit default_off == 1 ? 0 : 1 }
' "${CMAKE_FILE}" ||
    ! awk '
        $0 == "if(MIXXX_DECK_LOW_MEMORY_CONTROLLER_PREFERENCES_COMPILATION)" {
            in_contract = 1
            next
        }
        in_contract && $0 == "  set_property(" {
            in_property = 1
            source = 0
            append = 0
            compile_options = 0
            flags = 0
            next
        }
        in_property && $0 == "    SOURCE src/controllers/dlgprefcontroller.cpp" {
            source = 1
            next
        }
        in_property && $0 == "    APPEND" { append = 1; next }
        in_property && $0 == "    PROPERTY COMPILE_OPTIONS" {
            compile_options = 1
            next
        }
        in_property && $0 == "      \"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O1;-g0>\"" {
            flags = 1
            next
        }
        in_property && $0 == "  )" {
            if (source && append && compile_options && flags) {
                exact_contract++
            }
            in_property = 0
            next
        }
        in_contract && $0 == "endif()" { in_contract = 0 }
        END { exit exact_contract == 1 ? 0 : 1 }
    ' "${CMAKE_FILE}" ||
    [ "$(grep -Fc -- '-DMIXXX_DECK_LOW_MEMORY_CONTROLLER_PREFERENCES_COMPILATION=ON' "${DECK_MANIFEST}")" -ne 1 ] ||
    grep -Fq -- '-DMIXXX_DECK_LOW_MEMORY_CONTROLLER_PREFERENCES_COMPILATION=ON' "${NORMAL_MANIFEST}"; then
    echo "Error: the deck-only controller-preferences low-memory compile contract is incomplete." >&2
    exit 1
fi

if ! awk '
    $0 == "  MIXXX_DECK_LOW_MEMORY_QML_COMPILATION" {
        getline
        getline
        if ($0 == "  OFF") {
            default_off++
        }
    }
    END { exit default_off == 1 ? 0 : 1 }
' "${CMAKE_FILE}" ||
    ! awk '
        $0 == "  if(MIXXX_DECK_LOW_MEMORY_QML_COMPILATION)" {
            in_contract = 1
            next
        }
        in_contract && $0 == "    target_compile_options(" {
            in_options = 1
            target = 0
            private_scope = 0
            flags = 0
            next
        }
        in_options && $0 == "      mixxx-qml-lib" { target = 1; next }
        in_options && $0 == "      PRIVATE" { private_scope = 1; next }
        in_options && $0 == "        \"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O1;-g0>\"" {
            flags = 1
            next
        }
        in_options && $0 == "    )" {
            if (target && private_scope && flags) {
                exact_contract++
            }
            in_options = 0
            next
        }
        in_contract && $0 == "  endif()" { in_contract = 0 }
        END { exit exact_contract == 1 ? 0 : 1 }
    ' "${CMAKE_FILE}" ||
    ! awk '
        $0 == "  - name: mixxx" { in_mixxx = 1; next }
        in_mixxx && /^  - name:/ { in_mixxx = 0; in_config = 0 }
        in_mixxx && $0 == "    config-opts:" { in_config = 1; next }
        in_config && $0 !~ /^      - / { in_config = 0 }
        in_config && $0 == "      - -DMIXXX_DECK_LOW_MEMORY_QML_COMPILATION=ON" {
            exact_option++
        }
        END { exit exact_option == 1 ? 0 : 1 }
    ' "${DECK_MANIFEST}" ||
    [ "$(grep -Fc -- '-DMIXXX_DECK_LOW_MEMORY_QML_COMPILATION=ON' "${DECK_MANIFEST}")" -ne 1 ] ||
    grep -Fq -- '-DMIXXX_DECK_LOW_MEMORY_QML_COMPILATION=ON' "${NORMAL_MANIFEST}"; then
    echo "Error: the deck-only Mixxx QML low-memory compile contract is incomplete." >&2
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
if ! grep -Fq 'install -m 0644 "${STORAGE_BUDGET_HELPER}" "${installed_storage_helper}"' \
        "${DECK_DEPLOY_SCRIPT}" ||
    ! grep -Fq 'install -m 0644 "${HTTPS_FETCH_HELPER}" "${installed_https_helper}"' \
        "${DECK_DEPLOY_SCRIPT}" ||
    ! grep -Fq 'install -m 0644 "${SIGNED_CANDIDATE_HELPER}" "${installed_signed_helper}"' \
        "${DECK_DEPLOY_SCRIPT}"; then
    echo "Error: mixxx-deck setup does not install its artifact safety helpers." >&2
    exit 1
fi
if ! grep -Fq 'install -m 0755 "${SCRIPT_DIR}/mixxx_break_glass.sh" "${BREAK_GLASS_CLIENT}"' \
        "${DECK_DEPLOY_SCRIPT}" ||
    ! grep -Fq 'flatpak install --user --bundle --no-pull' "${DECK_DEPLOY_SCRIPT}"; then
    echo "Error: offline manual rollback setup is incomplete." >&2
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
if ! grep -Fq 'deck_require_pristine_build_checkout "${REPO_ROOT}"' \
        "${DECK_PUBLISH_SCRIPT}" ||
    ! grep -Fq 'deck_export_tracked_source "${REPO_ROOT}" "${SOURCE_SHA}" "${SOURCE_ROOT}"' \
        "${DECK_PUBLISH_SCRIPT}"; then
    echo "Error: the Forgejo publisher does not use a fresh tracked-only source tree." >&2
    exit 1
fi

if ! grep -Fq '          build-dir: build_flatpak' "${GITHUB_DECK_WORKFLOW}" ||
    ! grep -Fq '          repo-dir: repo' "${GITHUB_DECK_WORKFLOW}" ||
    ! grep -Fq '          retention-days: 14' "${GITHUB_DECK_WORKFLOW}"; then
    echo "Error: the GitHub workflow does not pin its validated Flatpak directories." >&2
    exit 1
fi

bash "${FORGEJO_ACTION_PIN_CHECK}"

bash -n "${DECK_DEPLOY_SCRIPT}" "${DECK_AUTO_UPDATE_SCRIPT}" \
    "${DECK_BREAK_GLASS_SCRIPT}" "${DECK_REPO_PUBLISH_SCRIPT}" \
    "${DECK_PUBLISH_SCRIPT}" "${DECK_STORAGE_HELPER}" "${DECK_HTTPS_HELPER}" \
    "${DECK_SIGNED_HELPER}" "${DECK_TRACKED_SOURCE_HELPER}"
bash "${DECK_ARTIFACT_SAFETY_TEST}"
bash "${SCRIPT_DIR}/deck_flatpak_auto_update_test.sh"
bash "${SCRIPT_DIR}/deck_flatpak_deploy_test.sh"

bash "${SCRIPT_DIR}/deck_ostree_validation_test.sh"

echo "Deck Flatpak manifest is synchronized with the normal manifest."
echo "Flatpak Mixxx.Controls runtime registration is present."
