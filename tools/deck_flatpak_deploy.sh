#!/bin/bash
# Lightweight stage/activate/rollback client for a resource-constrained DJ deck.

set -euo pipefail

APP_ID="org.mixxx.Mixxx"
EXPECTED_ARCH="x86_64"
DEFAULT_MANIFEST_URL="https://forge.polinaria.world/artifacts/latest.json"
MANIFEST_URL="${MIXXX_DECK_MANIFEST_URL:-${DEFAULT_MANIFEST_URL}}"
CACHE_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/mixxx-deck"
STATE_ROOT="${XDG_STATE_HOME:-${HOME}/.local/state}/mixxx-deck"
CONFIG_ROOT="${XDG_CONFIG_HOME:-${HOME}/.config}/mixxx-deck"
LOCK_FILE="${STATE_ROOT}/deploy.lock"
CURRENT_STATE="${STATE_ROOT}/current-source-sha"
PREVIOUS_STATE="${STATE_ROOT}/previous-source-sha"
STAGED_STATE="${STATE_ROOT}/staged-source-sha"
KEEP_LOCAL_BUILDS="${MIXXX_DECK_KEEP_LOCAL_BUILDS:-3}"
FLATHUB_REPO_URL="https://flathub.org/repo/flathub.flatpakrepo"
UDEV_RULE_SOURCE="res/linux/mixxx-usb-uaccess.rules"
UDEV_RULE_TARGET="/etc/udev/rules.d/69-mixxx-usb-uaccess.rules"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

usage() {
    cat <<EOF
Usage:
  mixxx-deck setup
  mixxx-deck check
  mixxx-deck stage [latest|<source-sha>]
  mixxx-deck activate [latest|<source-sha>]
  mixxx-deck deploy [latest|<source-sha>]
  mixxx-deck rollback
  mixxx-deck status
  mixxx-deck run [mixxx-args...]

The client may download a build while Mixxx is running, but activate, deploy,
and rollback always refuse to modify the installed Flatpak until Mixxx stops.
EOF
}

die() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command '$1' is missing."
}

ensure_directories() {
    mkdir -p "${CACHE_ROOT}/builds" "${STATE_ROOT}" "${CONFIG_ROOT}"
}

ensure_flatpak() {
    require_command flatpak
    flatpak remote-add --user --if-not-exists flathub "${FLATHUB_REPO_URL}"
}

install_udev_rules() {
    local source_path="${REPO_ROOT}/${UDEV_RULE_SOURCE}"
    if [[ ! -f "${source_path}" ]]; then
        echo "Skipping udev rule install; source checkout is not available at ${source_path}."
        return
    fi
    sudo install -Dm644 "${source_path}" "${UDEV_RULE_TARGET}"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "Installed ${UDEV_RULE_TARGET}; reconnect controllers before testing."
}

is_mixxx_running() {
    flatpak ps --columns=application 2>/dev/null | grep -Fxq "${APP_ID}"
}

installed_source_sha() {
    flatpak info --user "${APP_ID}" 2>/dev/null |
        sed -nE 's/^[[:space:]]*Subject:[[:space:]]*Built from ([0-9a-f]{40}).*/\1/p' |
        head -n 1
}

snapshot_installed_build() {
    local source_sha="$1"
    local user_repo="${XDG_DATA_HOME:-${HOME}/.local/share}/flatpak/repo"
    local build_dir="${CACHE_ROOT}/builds/${source_sha}"
    local bundle_path="${build_dir}/Mixxx.flatpak"
    local bundle_part="${bundle_path}.part"
    local checksum_path="${build_dir}/Mixxx.flatpak.sha256"

    [[ "${source_sha}" =~ ^[0-9a-f]{40}$ ]] ||
        die "Cannot snapshot an installed build without a valid source SHA."
    [[ -d "${user_repo}" ]] ||
        die "The user Flatpak repository is missing: ${user_repo}"
    if [[ -s "${bundle_path}" && -f "${checksum_path}" ]] &&
            [[ "$(sha256sum "${bundle_path}" | awk '{print $1}')" == "$(<"${checksum_path}")" ]]; then
        return
    fi

    echo "Saving installed build ${source_sha} for rollback..."
    mkdir -p "${build_dir}"
    rm -f -- "${bundle_path}" "${bundle_part}" "${checksum_path}"
    flatpak build-bundle \
        --arch="${EXPECTED_ARCH}" \
        --runtime-repo="${FLATHUB_REPO_URL}" \
        "${user_repo}" \
        "${bundle_part}" \
        "${APP_ID}" \
        master
    [[ -s "${bundle_part}" ]] || die "Failed to save the installed rollback build."
    mv -f -- "${bundle_part}" "${bundle_path}"
    sha256sum "${bundle_path}" | awk '{print $1}' >"${checksum_path}"
}

verify_cached_bundle() {
    local source_sha="$1"
    local build_dir="${CACHE_ROOT}/builds/${source_sha}"
    local bundle_path="${build_dir}/Mixxx.flatpak"
    local checksum_path="${build_dir}/Mixxx.flatpak.sha256"
    local expected_sha

    [[ -s "${bundle_path}" ]] || die "Staged bundle is missing for ${source_sha}."
    [[ -f "${checksum_path}" ]] ||
        die "Cached bundle checksum is missing for ${source_sha}; stage it again."
    expected_sha="$(<"${checksum_path}")"
    [[ "${expected_sha}" =~ ^[0-9a-f]{64}$ ]] ||
        die "Cached bundle checksum is invalid for ${source_sha}."
    [[ "$(sha256sum "${bundle_path}" | awk '{print $1}')" == "${expected_sha}" ]] ||
        die "Cached bundle checksum failed for ${source_sha}; stage it again."
}

manifest_url_for_target() {
    local target="$1"
    if [[ "${target}" == "latest" ]]; then
        printf '%s\n' "${MANIFEST_URL}"
        return
    fi
    [[ "${target}" =~ ^[0-9a-f]{40}$ ]] || die "Invalid source SHA: ${target}"
    printf '%s/builds/%s/manifest.json\n' "${MANIFEST_URL%/latest.json}" "${target}"
}

fetch_manifest() {
    local target="$1"
    local destination="$2"
    local url
    url="$(manifest_url_for_target "${target}")"
    curl --fail --location --silent --show-error --retry 3 \
        --connect-timeout 10 \
        --output "${destination}" \
        "${url}"
}

validate_manifest() {
    local manifest="$1"
    local requested_target="$2"
    local publication_root="${MANIFEST_URL%/latest.json}"
    jq -e \
        --arg app_id "${APP_ID}" \
        --arg arch "${EXPECTED_ARCH}" \
        --arg publication_root "${publication_root}" \
        '
        .schema_version == 1 and
        .channel == "deck-candidate" and
        .app_id == $app_id and
        .arch == $arch and
        .source_ref == "refs/heads/deck/candidate" and
        (.source_sha | type == "string" and test("^[0-9a-f]{40}$")) and
        (.source_sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
        (.sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
        (.size_bytes | type == "number" and . > 0 and floor == .) and
        (.bundle_url == ($publication_root + "/builds/" + .source_sha + "/Mixxx.flatpak")) and
        (.source_url == ($publication_root + "/builds/" + .source_sha + "/source.tar.zst"))
        ' "${manifest}" >/dev/null || die "Manifest validation failed."

    if [[ "${requested_target}" != "latest" ]]; then
        local actual_sha
        actual_sha="$(jq -r '.source_sha' "${manifest}")"
        [[ "${actual_sha}" == "${requested_target}" ]] ||
            die "Server returned ${actual_sha}, expected ${requested_target}."
    fi
}

stage_build() (
    local target="${1:-latest}"
    local temp_manifest
    local source_sha
    local build_dir
    local bundle_path
    local expected_size
    local expected_sha

    require_command curl
    require_command jq
    require_command sha256sum
    ensure_directories
    temp_manifest="$(mktemp "${STATE_ROOT}/manifest.XXXXXX")"
    trap 'rm -f -- "${temp_manifest:-}" "${bundle_part:-}"' EXIT

    fetch_manifest "${target}" "${temp_manifest}"
    validate_manifest "${temp_manifest}" "${target}"
    source_sha="$(jq -r '.source_sha' "${temp_manifest}")"
    build_dir="${CACHE_ROOT}/builds/${source_sha}"
    bundle_path="${build_dir}/Mixxx.flatpak"
    bundle_part="${bundle_path}.part"
    expected_size="$(jq -r '.size_bytes' "${temp_manifest}")"
    expected_sha="$(jq -r '.sha256' "${temp_manifest}")"
    mkdir -p "${build_dir}"

    if [[ -f "${bundle_path}" ]] &&
            [[ "$(stat -c '%s' "${bundle_path}")" == "${expected_size}" ]] &&
            [[ "$(sha256sum "${bundle_path}" | awk '{print $1}')" == "${expected_sha}" ]]; then
        echo "Build ${source_sha} is already staged and verified."
    else
        curl --fail --location --silent --show-error --retry 3 \
            --connect-timeout 10 \
            --output "${bundle_part}" \
            "$(jq -r '.bundle_url' "${temp_manifest}")"
        [[ "$(stat -c '%s' "${bundle_part}")" == "${expected_size}" ]] ||
            die "Downloaded bundle size does not match the manifest."
        [[ "$(sha256sum "${bundle_part}" | awk '{print $1}')" == "${expected_sha}" ]] ||
            die "Downloaded bundle checksum does not match the manifest."
        mv -f -- "${bundle_part}" "${bundle_path}"
        install -m 0644 "${temp_manifest}" "${build_dir}/manifest.json"
        echo "Staged and verified ${source_sha}."
    fi
    install -m 0644 "${temp_manifest}" "${build_dir}/manifest.json"
    printf '%s\n' "${expected_sha}" >"${build_dir}/Mixxx.flatpak.sha256"

    printf '%s\n' "${source_sha}" >"${STAGED_STATE}"
    printf '%s\n' "${source_sha}"
)

resolve_target_sha() {
    local target="${1:-latest}"
    local build_sha
    if [[ "${target}" == "latest" ]]; then
        if [[ -f "${STAGED_STATE}" ]]; then
            build_sha="$(<"${STAGED_STATE}")"
        else
            build_sha="$(stage_build latest | tail -n 1)"
        fi
    else
        [[ "${target}" =~ ^[0-9a-f]{40}$ ]] || die "Invalid source SHA: ${target}"
        build_sha="${target}"
        if [[ ! -f "${CACHE_ROOT}/builds/${build_sha}/Mixxx.flatpak" ]]; then
            build_sha="$(stage_build "${target}" | tail -n 1)"
        fi
    fi
    printf '%s\n' "${build_sha}"
}

activate_build() {
    local target="${1:-latest}"
    local source_sha
    local bundle_path
    local old_sha
    local verified_sha

    require_command flock
    ensure_flatpak
    ensure_directories
    exec 9>"${LOCK_FILE}"
    flock 9

    is_mixxx_running && die "Mixxx is running; stop it before activating a build."
    source_sha="$(resolve_target_sha "${target}")"
    bundle_path="${CACHE_ROOT}/builds/${source_sha}/Mixxx.flatpak"
    verify_cached_bundle "${source_sha}"
    old_sha="$(installed_source_sha || true)"
    if [[ -n "${old_sha}" && "${old_sha}" != "${source_sha}" ]]; then
        snapshot_installed_build "${old_sha}"
    fi

    flatpak install --user --bundle --reinstall --noninteractive -y "${bundle_path}"
    verified_sha="$(installed_source_sha || true)"
    [[ "${verified_sha}" == "${source_sha}" ]] ||
        die "Installed Flatpak does not report expected source SHA ${source_sha}."

    if [[ -n "${old_sha}" && "${old_sha}" != "${source_sha}" &&
            -f "${CACHE_ROOT}/builds/${old_sha}/Mixxx.flatpak" ]]; then
        printf '%s\n' "${old_sha}" >"${PREVIOUS_STATE}"
    fi
    printf '%s\n' "${source_sha}" >"${CURRENT_STATE}"
    prune_local_builds
    echo "Activated ${source_sha}."
}

prune_local_builds() {
    local current_sha=""
    local previous_sha=""
    local staged_sha=""
    local kept=0
    local path
    local sha

    [[ -f "${CURRENT_STATE}" ]] && current_sha="$(<"${CURRENT_STATE}")"
    [[ -f "${PREVIOUS_STATE}" ]] && previous_sha="$(<"${PREVIOUS_STATE}")"
    [[ -f "${STAGED_STATE}" ]] && staged_sha="$(<"${STAGED_STATE}")"

    while IFS= read -r path; do
        sha="${path##*/}"
        if [[ "${sha}" == "${current_sha}" || "${sha}" == "${previous_sha}" ||
                "${sha}" == "${staged_sha}" || "${kept}" -lt "${KEEP_LOCAL_BUILDS}" ]]; then
            kept=$((kept + 1))
            continue
        fi
        [[ "${path}" == "${CACHE_ROOT}/builds/"* ]] ||
            die "Refusing to prune unexpected path: ${path}"
        rm -rf -- "${path}"
    done < <(
        find "${CACHE_ROOT}/builds" -mindepth 1 -maxdepth 1 -type d \
            -printf '%T@ %p\n' 2>/dev/null |
            sort -rn |
            cut -d' ' -f2-
    )
}

rollback_build() {
    local previous_sha
    [[ -f "${PREVIOUS_STATE}" ]] || die "No cached rollback build is recorded."
    previous_sha="$(<"${PREVIOUS_STATE}")"
    activate_build "${previous_sha}"
}

check_build() (
    local temp_manifest
    local available_sha
    local installed_sha
    require_command curl
    require_command jq
    ensure_directories
    temp_manifest="$(mktemp "${STATE_ROOT}/manifest.XXXXXX")"
    trap 'rm -f -- "${temp_manifest:-}"' EXIT
    fetch_manifest latest "${temp_manifest}"
    validate_manifest "${temp_manifest}" latest
    available_sha="$(jq -r '.source_sha' "${temp_manifest}")"
    installed_sha="$(installed_source_sha || true)"
    echo "Installed: ${installed_sha:-unknown}"
    echo "Available: ${available_sha}"
    if [[ "${installed_sha}" == "${available_sha}" ]]; then
        echo "Deck is up to date."
    else
        echo "An update is available."
    fi
)

print_status() {
    local installed_sha
    installed_sha="$(installed_source_sha || true)"
    echo "Installed source: ${installed_sha:-unknown}"
    echo "Staged source: $([[ -f "${STAGED_STATE}" ]] && cat "${STAGED_STATE}" || echo none)"
    echo "Previous source: $([[ -f "${PREVIOUS_STATE}" ]] && cat "${PREVIOUS_STATE}" || echo none)"
    echo "Manifest URL: ${MANIFEST_URL}"
    echo "Mixxx running: $(is_mixxx_running && echo yes || echo no)"
    flatpak info --user "${APP_ID}" 2>/dev/null | sed -n '1,12p' || true
}

setup_client() {
    ensure_flatpak
    require_command curl
    require_command jq
    require_command sha256sum
    ensure_directories
    install_udev_rules
    mkdir -p "${HOME}/.local/bin"
    install -m 0755 "${BASH_SOURCE[0]}" "${HOME}/.local/bin/mixxx-deck"
    echo "Installed ${HOME}/.local/bin/mixxx-deck"
}

[[ "${MANIFEST_URL}" =~ ^https://[^/]+/.+/latest\.json$ ]] ||
    die "MIXXX_DECK_MANIFEST_URL must be an HTTPS latest.json URL."
[[ "${KEEP_LOCAL_BUILDS}" =~ ^[1-9][0-9]*$ ]] ||
    die "MIXXX_DECK_KEEP_LOCAL_BUILDS must be a positive integer."

if [[ $# -lt 1 ]]; then
    usage
    exit 2
fi

command_name="$1"
shift

case "${command_name}" in
    setup)
        [[ $# -eq 0 ]] || die "setup takes no arguments."
        setup_client
        ;;
    check)
        [[ $# -eq 0 ]] || die "check takes no arguments."
        check_build
        ;;
    stage)
        [[ $# -le 1 ]] || die "stage accepts at most one target."
        stage_build "${1:-latest}" >/dev/stdout
        ;;
    activate)
        [[ $# -le 1 ]] || die "activate accepts at most one target."
        activate_build "${1:-latest}"
        ;;
    deploy)
        [[ $# -le 1 ]] || die "deploy accepts at most one target."
        target="${1:-latest}"
        stage_build "${target}" >/dev/stdout
        activate_build "${target}"
        ;;
    rollback)
        [[ $# -eq 0 ]] || die "rollback takes no arguments."
        rollback_build
        ;;
    status)
        [[ $# -eq 0 ]] || die "status takes no arguments."
        print_status
        ;;
    run)
        ensure_flatpak
        flatpak run "${APP_ID}" "$@"
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
