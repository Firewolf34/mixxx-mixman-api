#!/bin/bash
# Lightweight stage/activate/rollback client for a resource-constrained DJ deck.

set -euo pipefail

APP_ID="org.mixxx.Mixxx"
EXPECTED_ARCH="x86_64"
EXPECTED_REF="app/${APP_ID}/${EXPECTED_ARCH}/master"
DEFAULT_MANIFEST_URL="https://forge.polinaria.world/artifacts/latest.json"
MANIFEST_URL="${MIXXX_DECK_MANIFEST_URL:-${DEFAULT_MANIFEST_URL}}"
GITHUB_API_BASE="${MIXXX_GITHUB_API_BASE:-https://api.github.com}"
GITHUB_OWNER="${MIXXX_GITHUB_OWNER:-Firewolf34}"
GITHUB_REPO="${MIXXX_GITHUB_REPO:-mixxx-mixman-api}"
GITHUB_WORKFLOW="${MIXXX_GITHUB_WORKFLOW:-github-deck-candidate.yml}"
GITHUB_ARTIFACT_NAME="Mixxx-flatpak-x86_64"
CACHE_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/mixxx-deck"
STATE_ROOT="${XDG_STATE_HOME:-${HOME}/.local/state}/mixxx-deck"
CONFIG_ROOT="${XDG_CONFIG_HOME:-${HOME}/.config}/mixxx-deck"
GITHUB_TOKEN_FILE="${MIXXX_GITHUB_TOKEN_FILE:-${CONFIG_ROOT}/github-actions-token}"
LOCK_FILE="${STATE_ROOT}/deploy.lock"
CURRENT_STATE="${STATE_ROOT}/current-source-sha"
PREVIOUS_STATE="${STATE_ROOT}/previous-source-sha"
STAGED_STATE="${STATE_ROOT}/staged-source-sha"
KEEP_LOCAL_BUILDS="${MIXXX_DECK_KEEP_LOCAL_BUILDS:-3}"
FLATHUB_REPO_URL="https://flathub.org/repo/flathub.flatpakrepo"
UDEV_RULE_SOURCE="res/linux/mixxx-usb-uaccess.rules"
UDEV_RULE_TARGET="/etc/udev/rules.d/69-mixxx-usb-uaccess.rules"
GITHUB_AUTH_CONFIG=""

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
OSTREE_VALIDATION_HELPER="${SCRIPT_DIR}/deck_ostree_validation.sh"
if [[ ! -r "${OSTREE_VALIDATION_HELPER}" ]]; then
    echo "Error: missing ${OSTREE_VALIDATION_HELPER}. Re-run setup from the repository." >&2
    exit 1
fi
# shellcheck source=tools/deck_ostree_validation.sh
source "${OSTREE_VALIDATION_HELPER}"

usage() {
    cat <<'EOF'
Usage:
  mixxx-deck setup
  mixxx-deck github-configure
  mixxx-deck check
  mixxx-deck stage [auto|forgejo|github|forgejo:<sha>|github:<sha>]
  mixxx-deck activate [auto|forgejo|github|forgejo:<sha>|github:<sha>]
  mixxx-deck deploy [auto|forgejo|github|forgejo:<sha>|github:<sha>]
  mixxx-deck rollback
  mixxx-deck status
  mixxx-deck run [mixxx-args...]

auto (also accepted as latest) compares verified Forgejo and GitHub candidates by
completion time. Forgejo wins an exact tie. GitHub needs a fine-grained token
with Actions:read for only the fallback repository; github-configure stores it
outside this checkout with mode 0600.

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

require_sha() {
    [[ "${1:-}" =~ ^[0-9a-f]{40}$ ]] ||
        die "Source SHA must be exactly 40 lowercase hexadecimal characters."
}

require_provider() {
    case "${1:-}" in
        forgejo|github|local|legacy) ;;
        *) die "Unknown build provider: ${1:-}" ;;
    esac
}

source_key() {
    local provider="$1"
    local sha="$2"
    require_provider "${provider}"
    require_sha "${sha}"
    printf '%s:%s\n' "${provider}" "${sha}"
}

split_source_key() {
    local key="$1"
    local provider sha
    provider="${key%%:*}"
    sha="${key#*:}"
    [[ "${key}" == *:* && "${sha}" != "${key}" ]] ||
        die "Invalid cached build key: ${key}"
    require_provider "${provider}"
    require_sha "${sha}"
    BUILD_PROVIDER="${provider}"
    BUILD_SHA="${sha}"
}

normalize_state_key() {
    local value="$1"
    if [[ "${value}" =~ ^[0-9a-f]{40}$ ]]; then
        source_key legacy "${value}"
    else
        split_source_key "${value}"
        source_key "${BUILD_PROVIDER}" "${BUILD_SHA}"
    fi
}

build_dir_for_key() {
    local key="$1"
    split_source_key "${key}"
    if [[ "${BUILD_PROVIDER}" == legacy ]]; then
        printf '%s/builds/%s\n' "${CACHE_ROOT}" "${BUILD_SHA}"
    else
        printf '%s/builds/%s/%s\n' "${CACHE_ROOT}" "${BUILD_PROVIDER}" "${BUILD_SHA}"
    fi
}

read_state_key() {
    local state_file="$1"
    [[ -f "${state_file}" ]] || return 1
    normalize_state_key "$(<"${state_file}")"
}

ensure_directories() {
    mkdir -p "${CACHE_ROOT}/builds/forgejo" \
        "${CACHE_ROOT}/builds/github" \
        "${CACHE_ROOT}/builds/local" \
        "${STATE_ROOT}" "${CONFIG_ROOT}"
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

release_github_auth() {
    if [[ -n "${GITHUB_AUTH_CONFIG}" ]]; then
        rm -f -- "${GITHUB_AUTH_CONFIG}"
        GITHUB_AUTH_CONFIG=""
    fi
}

github_token_configured() {
    [[ -f "${GITHUB_TOKEN_FILE}" ]] || return 1
    local mode
    mode="$(stat -c '%a' "${GITHUB_TOKEN_FILE}")"
    (( (8#${mode} & 077) == 0 ))
}

prepare_github_auth() {
    github_token_configured ||
        die "GitHub token file is missing or too broadly readable: ${GITHUB_TOKEN_FILE}"

    local token
    IFS= read -r token <"${GITHUB_TOKEN_FILE}"
    [[ "${token}" =~ ^[^[:space:]]{20,}$ ]] ||
        die "GitHub token file does not contain a plausible single-line token."

    GITHUB_AUTH_CONFIG="$(mktemp)"
    chmod 600 "${GITHUB_AUTH_CONFIG}"
    printf 'header = "Authorization: Bearer %s"\n' "${token}" >"${GITHUB_AUTH_CONFIG}"
    unset token
}

github_api_request() {
    local method="$1"
    local path="$2"
    [[ -n "${GITHUB_AUTH_CONFIG}" ]] || die "GitHub API authentication is not prepared."
    curl --config "${GITHUB_AUTH_CONFIG}" \
        --silent --show-error --fail --connect-timeout 15 \
        --request "${method}" \
        --header 'Accept: application/vnd.github+json' \
        --header 'X-GitHub-Api-Version: 2026-03-10' \
        "${GITHUB_API_BASE}${path}"
}

github_configure() {
    local token token_dir
    token_dir="${GITHUB_TOKEN_FILE%/*}"
    mkdir -p -- "${token_dir}"
    chmod 700 "${token_dir}"

    echo "Paste the GitHub fine-grained Actions:read token, then press Enter." >&2
    IFS= read -r -s token </dev/tty
    echo >&2
    [[ "${token}" =~ ^[^[:space:]]{20,}$ ]] ||
        die "token does not have the expected format"
    (
        umask 077
        printf '%s\n' "${token}" >"${GITHUB_TOKEN_FILE}"
    )
    unset token
    chmod 600 "${GITHUB_TOKEN_FILE}"

    prepare_github_auth
    if ! github_api_request GET "/repos/${GITHUB_OWNER}/${GITHUB_REPO}" >/dev/null; then
        release_github_auth
        rm -f -- "${GITHUB_TOKEN_FILE}"
        die "GitHub rejected the token; the local token file was removed."
    fi
    release_github_auth
    echo "GitHub Actions authentication succeeded for ${GITHUB_OWNER}/${GITHUB_REPO}."
}

manifest_url_for_sha() {
    local sha="$1"
    require_sha "${sha}"
    printf '%s/builds/%s/manifest.json\n' "${MANIFEST_URL%/latest.json}" "${sha}"
}

fetch_forgejo_manifest() {
    local target="$1"
    local destination="$2"
    local url
    if [[ "${target}" == latest ]]; then
        url="${MANIFEST_URL}"
    else
        url="$(manifest_url_for_sha "${target}")"
    fi
    curl --fail --location --silent --show-error --retry 3 \
        --connect-timeout 10 --output "${destination}" "${url}"
}

validate_forgejo_manifest() {
    local manifest="$1"
    local requested_sha="$2"
    local publication_root="${MANIFEST_URL%/latest.json}"
    jq -e \
        --arg app_id "${APP_ID}" \
        --arg arch "${EXPECTED_ARCH}" \
        --arg publication_root "${publication_root}" '
        .schema_version == 1 and
        .channel == "deck-candidate" and
        .app_id == $app_id and
        .arch == $arch and
        .source_ref == "refs/heads/deck/candidate" and
        (.source_sha | type == "string" and test("^[0-9a-f]{40}$")) and
        (.built_at | type == "string" and test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")) and
        (.source_sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
        (.sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
        (.size_bytes | type == "number" and . > 0 and floor == .) and
        (.bundle_url == ($publication_root + "/builds/" + .source_sha + "/Mixxx.flatpak")) and
        (.source_url == ($publication_root + "/builds/" + .source_sha + "/source.tar.zst"))
        ' "${manifest}" >/dev/null || die "Forgejo manifest validation failed."

    if [[ "${requested_sha}" != latest ]]; then
        [[ "$(jq -r '.source_sha' "${manifest}")" == "${requested_sha}" ]] ||
            die "Forgejo returned a manifest for a different source SHA."
    fi
}

forgejo_descriptor_for_sha() (
    local requested_sha="${1:-latest}"
    local manifest
    manifest="$(mktemp)"
    trap 'rm -f -- "${manifest}"' EXIT
    fetch_forgejo_manifest "${requested_sha}" "${manifest}"
    validate_forgejo_manifest "${manifest}" "${requested_sha}"
    jq -c '{provider: "forgejo", source_sha, timestamp: .built_at}' "${manifest}"
)

github_artifact_descriptor() {
    local run_json="$1"
    local run_id artifacts artifact
    run_id="$(jq -r '.id' <<<"${run_json}")"
    artifacts="$(github_api_request GET "/repos/${GITHUB_OWNER}/${GITHUB_REPO}/actions/runs/${run_id}/artifacts?per_page=100")"
    artifact="$(jq -ce --arg name "${GITHUB_ARTIFACT_NAME}" '
        [.artifacts[]
         | select(.name == $name and (.expired | not))
         | select(.id | type == "number")
         | select(.size_in_bytes | type == "number" and . > 0)
         | select(.digest | type == "string" and test("^sha256:[0-9a-f]{64}$"))]
        | if length == 0 then error("no valid candidate artifact") else max_by(.created_at) end
        ' <<<"${artifacts}")"
    jq -cn \
        --arg provider github \
        --arg source_sha "$(jq -r '.head_sha' <<<"${run_json}")" \
        --arg timestamp "$(jq -r '.updated_at' <<<"${run_json}")" \
        --argjson workflow_run_id "${run_id}" \
        --argjson artifact_id "$(jq -r '.id' <<<"${artifact}")" \
        --arg archive_sha256 "$(jq -r '.digest | ltrimstr("sha256:")' <<<"${artifact}")" \
        '{provider: $provider, source_sha: $source_sha, timestamp: $timestamp,
          workflow_run_id: $workflow_run_id, artifact_id: $artifact_id,
          archive_sha256: $archive_sha256}'
}

github_descriptors_for_sha() {
    local requested_sha="${1:-}"
    local runs_json run
    if [[ -n "${requested_sha}" ]]; then
        require_sha "${requested_sha}"
    fi
    runs_json="$(github_api_request GET "/repos/${GITHUB_OWNER}/${GITHUB_REPO}/actions/workflows/${GITHUB_WORKFLOW}/runs?branch=github%2Fcandidate&status=completed&per_page=100")"
    while IFS= read -r run; do
        [[ -n "${run}" ]] || continue
        if [[ -n "${requested_sha}" ]] &&
                [[ "$(jq -r '.head_sha' <<<"${run}")" != "${requested_sha}" ]]; then
            continue
        fi
        github_artifact_descriptor "${run}"
    done < <(jq -c '
        .workflow_runs[]
        | select(.status == "completed" and .conclusion == "success")
        | select(.head_branch == "github/candidate")
        | select(.head_sha | type == "string" and test("^[0-9a-f]{40}$"))
        | select(.updated_at | type == "string" and test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$"))
        ' <<<"${runs_json}")
}

newest_descriptor() {
    local descriptors="$1"
    [[ -n "${descriptors}" ]] || return 1
    jq -sc 'sort_by(.timestamp) | last' <<<"${descriptors}"
}

github_descriptor_for_sha() (
    prepare_github_auth
    trap release_github_auth EXIT
    local descriptors
    descriptors="$(github_descriptors_for_sha "${1:-}")"
    newest_descriptor "${descriptors}" ||
        die "No successful, unexpired GitHub candidate artifact is available."
)

auto_descriptors() (
    local temporary descriptor forgejo_error github_error
    temporary="$(mktemp)"
    trap 'rm -f -- "${temporary}" "${temporary}.forgejo" "${temporary}.github"' EXIT

    if descriptor="$(forgejo_descriptor_for_sha latest 2>"${temporary}.forgejo")"; then
        printf '%s\n' "${descriptor}" >>"${temporary}"
    else
        forgejo_error="$(<"${temporary}.forgejo")"
        echo "Forgejo candidate is unavailable: ${forgejo_error}" >&2
    fi
    rm -f -- "${temporary}.forgejo"

    if github_token_configured; then
        if descriptor="$(github_descriptor_for_sha 2>"${temporary}.github")"; then
            printf '%s\n' "${descriptor}" >>"${temporary}"
        else
            github_error="$(<"${temporary}.github")"
            echo "GitHub fallback is unavailable: ${github_error}" >&2
        fi
        rm -f -- "${temporary}.github"
    else
        echo "GitHub fallback is not configured; using Forgejo when available." >&2
    fi

    [[ -s "${temporary}" ]] || die "No verified Forgejo or GitHub candidate is available."
    jq -sc '
        sort_by([.timestamp, (if .provider == "forgejo" then 1 else 0 end)])
        | reverse | .[]
        ' "${temporary}"
)

descriptors_for_target() {
    local target="${1:-auto}"
    local provider sha
    case "${target}" in
        auto|latest)
            auto_descriptors
            ;;
        forgejo)
            forgejo_descriptor_for_sha latest
            ;;
        github)
            github_descriptor_for_sha
            ;;
        forgejo:*|github:*)
            provider="${target%%:*}"
            sha="${target#*:}"
            require_sha "${sha}"
            if [[ "${provider}" == forgejo ]]; then
                forgejo_descriptor_for_sha "${sha}"
            else
                github_descriptor_for_sha "${sha}"
            fi
            ;;
        local:*|legacy:*)
            split_source_key "${target}"
            printf '{"provider":"%s","source_sha":"%s"}\n' \
                "${BUILD_PROVIDER}" "${BUILD_SHA}"
            ;;
        *)
            require_sha "${target}"
            forgejo_descriptor_for_sha "${target}"
            ;;
    esac
}

verify_bundle_provenance() (
    local bundle_path="$1"
    local source_sha="$2"
    local validation_repo flatpak_commit
    require_sha "${source_sha}"
    validation_repo="$(mktemp -d)"
    trap 'rm -rf -- "${validation_repo}"' EXIT

    ostree init --repo="${validation_repo}" --mode=archive-z2
    flatpak build-import-bundle "${validation_repo}" "${bundle_path}"
    ostree --repo="${validation_repo}" fsck
    ostree --repo="${validation_repo}" refs | grep -Fxq "${EXPECTED_REF}" ||
        die "Bundle does not contain ${EXPECTED_REF}."
    flatpak_commit="$(ostree --repo="${validation_repo}" rev-parse "${EXPECTED_REF}")"
    deck_ostree_commit_subject_contains_source \
        "${validation_repo}" "${flatpak_commit}" "${source_sha}"
)

write_staged_state() {
    local provider="$1"
    local source_sha="$2"
    source_key "${provider}" "${source_sha}" >"${STAGED_STATE}"
}

cache_bundle_matches() {
    local build_dir="$1"
    local expected_size="$2"
    local expected_sha="$3"
    [[ -s "${build_dir}/Mixxx.flatpak" ]] &&
        [[ "$(stat -c '%s' "${build_dir}/Mixxx.flatpak")" == "${expected_size}" ]] &&
        [[ "$(sha256sum "${build_dir}/Mixxx.flatpak" | awk '{print $1}')" == "${expected_sha}" ]]
}

stage_forgejo_descriptor() (
    local descriptor="$1"
    local source_sha manifest build_dir bundle_path bundle_part expected_size expected_sha
    manifest="$(mktemp "${STATE_ROOT}/forgejo-manifest.XXXXXX")"
    trap 'rm -f -- "${manifest}" "${bundle_part:-}"' EXIT
    source_sha="$(jq -r '.source_sha' <<<"${descriptor}")"
    fetch_forgejo_manifest "${source_sha}" "${manifest}"
    validate_forgejo_manifest "${manifest}" "${source_sha}"
    [[ "$(jq -r '.source_sha' "${manifest}")" == "${source_sha}" ]] ||
        die "Forgejo candidate changed while it was being staged."

    build_dir="$(build_dir_for_key "forgejo:${source_sha}")"
    bundle_path="${build_dir}/Mixxx.flatpak"
    bundle_part="${bundle_path}.part"
    expected_size="$(jq -r '.size_bytes' "${manifest}")"
    expected_sha="$(jq -r '.sha256' "${manifest}")"
    mkdir -p "${build_dir}"

    if cache_bundle_matches "${build_dir}" "${expected_size}" "${expected_sha}"; then
        echo "Forgejo build ${source_sha} is already staged and verified."
    else
        curl --fail --location --silent --show-error --retry 3 --connect-timeout 10 \
            --output "${bundle_part}" "$(jq -r '.bundle_url' "${manifest}")"
        [[ "$(stat -c '%s' "${bundle_part}")" == "${expected_size}" ]] ||
            die "Downloaded Forgejo bundle size does not match its manifest."
        [[ "$(sha256sum "${bundle_part}" | awk '{print $1}')" == "${expected_sha}" ]] ||
            die "Downloaded Forgejo bundle checksum does not match its manifest."
        mv -f -- "${bundle_part}" "${bundle_path}"
    fi
    install -m 0644 "${manifest}" "${build_dir}/manifest.json"
    printf '%s\n' "${expected_sha}" >"${build_dir}/Mixxx.flatpak.sha256"
    verify_bundle_provenance "${bundle_path}" "${source_sha}"
    write_staged_state forgejo "${source_sha}"
    echo "Staged verified Forgejo build ${source_sha}."
    source_key forgejo "${source_sha}"
)

validate_github_artifact_manifest() {
    local manifest="$1"
    local descriptor="$2"
    local source_sha run_id
    source_sha="$(jq -r '.source_sha' <<<"${descriptor}")"
    run_id="$(jq -r '.workflow_run_id' <<<"${descriptor}")"
    jq -e \
        --arg app_id "${APP_ID}" \
        --arg arch "${EXPECTED_ARCH}" \
        --arg source_sha "${source_sha}" \
        --argjson run_id "${run_id}" '
        .schema_version == 1 and
        .provider == "github-actions" and
        .channel == "github-candidate" and
        .app_id == $app_id and
        .arch == $arch and
        .source_sha == $source_sha and
        .source_ref == "refs/heads/github/candidate" and
        (.built_at | type == "string" and test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")) and
        .workflow_run_id == ($run_id | tostring) and
        .bundle_filename == "Mixxx.flatpak" and
        (.sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
        (.size_bytes | type == "number" and . > 0 and floor == .)
        ' "${manifest}" >/dev/null || die "GitHub artifact metadata validation failed."
}

stage_github_descriptor() (
    local descriptor="$1"
    local temporary archive archive_part manifest manifest_part bundle_part
    local source_sha artifact_id archive_sha256 build_dir bundle_path expected_size expected_sha
    prepare_github_auth
    temporary="$(mktemp -d "${STATE_ROOT}/github-artifact.XXXXXX")"
    trap 'release_github_auth; rm -rf -- "${temporary}"' EXIT
    archive="${temporary}/artifact.zip"
    archive_part="${archive}.part"
    manifest="${temporary}/github-candidate-manifest.json"
    manifest_part="${manifest}.part"
    source_sha="$(jq -r '.source_sha' <<<"${descriptor}")"
    artifact_id="$(jq -r '.artifact_id' <<<"${descriptor}")"
    archive_sha256="$(jq -r '.archive_sha256' <<<"${descriptor}")"
    require_sha "${source_sha}"
    [[ "${artifact_id}" =~ ^[1-9][0-9]*$ ]] || die "Invalid GitHub artifact identifier."
    [[ "${archive_sha256}" =~ ^[0-9a-f]{64}$ ]] || die "Invalid GitHub artifact checksum."

    curl --config "${GITHUB_AUTH_CONFIG}" --fail --location --silent --show-error \
        --connect-timeout 15 --retry 3 \
        --header 'Accept: application/vnd.github+json' \
        --header 'X-GitHub-Api-Version: 2026-03-10' \
        --output "${archive_part}" \
        "${GITHUB_API_BASE}/repos/${GITHUB_OWNER}/${GITHUB_REPO}/actions/artifacts/${artifact_id}/zip"
    [[ "$(sha256sum "${archive_part}" | awk '{print $1}')" == "${archive_sha256}" ]] ||
        die "GitHub artifact archive checksum does not match the Actions API digest."
    mv -f -- "${archive_part}" "${archive}"
    unzip -tq "${archive}" >/dev/null
    mapfile -t archive_entries < <(unzip -Z1 "${archive}" | LC_ALL=C sort)
    [[ "${#archive_entries[@]}" -eq 2 ]] &&
        [[ "${archive_entries[0]}" == Mixxx.flatpak ]] &&
        [[ "${archive_entries[1]}" == github-candidate-manifest.json ]] ||
        die "GitHub artifact has an unexpected file layout."
    unzip -p "${archive}" github-candidate-manifest.json >"${manifest_part}"
    mv -f -- "${manifest_part}" "${manifest}"
    validate_github_artifact_manifest "${manifest}" "${descriptor}"

    build_dir="$(build_dir_for_key "github:${source_sha}")"
    bundle_path="${build_dir}/Mixxx.flatpak"
    bundle_part="${bundle_path}.part"
    expected_size="$(jq -r '.size_bytes' "${manifest}")"
    expected_sha="$(jq -r '.sha256' "${manifest}")"
    mkdir -p "${build_dir}"
    if cache_bundle_matches "${build_dir}" "${expected_size}" "${expected_sha}"; then
        echo "GitHub build ${source_sha} is already staged and verified."
    else
        unzip -p "${archive}" Mixxx.flatpak >"${bundle_part}"
        [[ "$(stat -c '%s' "${bundle_part}")" == "${expected_size}" ]] ||
            die "GitHub bundle size does not match its artifact metadata."
        [[ "$(sha256sum "${bundle_part}" | awk '{print $1}')" == "${expected_sha}" ]] ||
            die "GitHub bundle checksum does not match its artifact metadata."
        mv -f -- "${bundle_part}" "${bundle_path}"
    fi
    install -m 0644 "${manifest}" "${build_dir}/manifest.json"
    printf '%s\n' "${expected_sha}" >"${build_dir}/Mixxx.flatpak.sha256"
    verify_bundle_provenance "${bundle_path}" "${source_sha}"
    write_staged_state github "${source_sha}"
    echo "Staged verified GitHub build ${source_sha}."
    source_key github "${source_sha}"
)

stage_descriptor() {
    local descriptor="$1"
    case "$(jq -r '.provider' <<<"${descriptor}")" in
        forgejo) stage_forgejo_descriptor "${descriptor}" ;;
        github) stage_github_descriptor "${descriptor}" ;;
        local|legacy)
            local provider source_sha key bundle_path
            provider="$(jq -r '.provider' <<<"${descriptor}")"
            source_sha="$(jq -r '.source_sha' <<<"${descriptor}")"
            key="$(source_key "${provider}" "${source_sha}")"
            bundle_path="$(build_dir_for_key "${key}")/Mixxx.flatpak"
            [[ -s "${bundle_path}" ]] || die "No cached ${provider} build exists for ${source_sha}."
            verify_cached_bundle "${key}"
            write_staged_state "${provider}" "${source_sha}"
            echo "Selected cached ${provider} build ${source_sha}."
            printf '%s\n' "${key}"
            ;;
        *) die "Candidate descriptor has an unknown provider." ;;
    esac
}

stage_build() (
    local target="${1:-auto}"
    local descriptors descriptor stage_output last_error=""
    require_command curl
    require_command jq
    require_command sha256sum
    require_command stat
    require_command unzip
    require_command flatpak
    require_command ostree
    ensure_directories
    descriptors="$(descriptors_for_target "${target}")"
    [[ -n "${descriptors}" ]] || die "No candidate matches ${target}."
    while IFS= read -r descriptor; do
        [[ -n "${descriptor}" ]] || continue
        if stage_output="$(stage_descriptor "${descriptor}")"; then
            printf '%s\n' "${stage_output}"
            return 0
        fi
        last_error="${stage_output}"
        if [[ "${target}" == auto || "${target}" == latest ]]; then
            echo "Candidate staging failed; trying the next verified provider." >&2
        else
            break
        fi
    done <<<"${descriptors}"
    [[ -z "${last_error}" ]] || echo "${last_error}" >&2
    die "Unable to stage a verified candidate."
)

verify_cached_bundle() {
    local key="$1"
    local build_dir bundle_path checksum_path expected_sha
    split_source_key "${key}"
    build_dir="$(build_dir_for_key "${key}")"
    bundle_path="${build_dir}/Mixxx.flatpak"
    checksum_path="${build_dir}/Mixxx.flatpak.sha256"
    [[ -s "${bundle_path}" ]] || die "Staged bundle is missing for ${key}."
    [[ -f "${checksum_path}" ]] || die "Cached bundle checksum is missing for ${key}; stage it again."
    expected_sha="$(<"${checksum_path}")"
    [[ "${expected_sha}" =~ ^[0-9a-f]{64}$ ]] ||
        die "Cached bundle checksum is invalid for ${key}."
    [[ "$(sha256sum "${bundle_path}" | awk '{print $1}')" == "${expected_sha}" ]] ||
        die "Cached bundle checksum failed for ${key}; stage it again."
    verify_bundle_provenance "${bundle_path}" "${BUILD_SHA}"
}

snapshot_installed_build() {
    local source_sha="$1"
    local user_repo="${XDG_DATA_HOME:-${HOME}/.local/share}/flatpak/repo"
    local key build_dir bundle_path bundle_part checksum_path
    require_sha "${source_sha}"
    [[ -d "${user_repo}" ]] || die "The user Flatpak repository is missing: ${user_repo}"
    key="$(source_key local "${source_sha}")"
    build_dir="$(build_dir_for_key "${key}")"
    bundle_path="${build_dir}/Mixxx.flatpak"
    bundle_part="${bundle_path}.part"
    checksum_path="${build_dir}/Mixxx.flatpak.sha256"
    if [[ -s "${bundle_path}" && -f "${checksum_path}" ]] &&
            [[ "$(sha256sum "${bundle_path}" | awk '{print $1}')" == "$(<"${checksum_path}")" ]]; then
        printf '%s\n' "${key}"
        return
    fi

    echo "Saving installed build ${source_sha} for rollback..."
    mkdir -p "${build_dir}"
    rm -f -- "${bundle_path}" "${bundle_part}" "${checksum_path}"
    flatpak build-bundle --arch="${EXPECTED_ARCH}" \
        --runtime-repo="${FLATHUB_REPO_URL}" "${user_repo}" "${bundle_part}" \
        "${APP_ID}" master
    [[ -s "${bundle_part}" ]] || die "Failed to save the installed rollback build."
    mv -f -- "${bundle_part}" "${bundle_path}"
    sha256sum "${bundle_path}" | awk '{print $1}' >"${checksum_path}"
    verify_bundle_provenance "${bundle_path}" "${source_sha}"
    printf '%s\n' "${key}"
}

target_cache_key() {
    local target="$1"
    local provider sha
    case "${target}" in
        forgejo:*|github:*|local:*|legacy:*)
            provider="${target%%:*}"
            sha="${target#*:}"
            source_key "${provider}" "${sha}"
            ;;
        *)
            require_sha "${target}"
            source_key forgejo "${target}"
            ;;
    esac
}

resolve_target_key() {
    local target="${1:-auto}"
    local key build_dir
    if [[ "${target}" == auto || "${target}" == latest ]]; then
        if key="$(read_state_key "${STAGED_STATE}")"; then
            build_dir="$(build_dir_for_key "${key}")"
            if [[ -s "${build_dir}/Mixxx.flatpak" ]]; then
                printf '%s\n' "${key}"
                return
            fi
        fi
        stage_build auto | tail -n 1
        return
    fi
    key="$(target_cache_key "${target}")"
    build_dir="$(build_dir_for_key "${key}")"
    if [[ -s "${build_dir}/Mixxx.flatpak" ]]; then
        printf '%s\n' "${key}"
    elif [[ "${key}" == local:* || "${key}" == legacy:* ]]; then
        die "No cached build exists for ${key}."
    else
        stage_build "${target}" | tail -n 1
    fi
}

activate_build() {
    local target="${1:-auto}"
    local key build_dir bundle_path old_sha old_key="" verified_sha
    require_command flock
    ensure_flatpak
    ensure_directories
    exec 9>"${LOCK_FILE}"
    flock 9
    is_mixxx_running && die "Mixxx is running; stop it before activating a build."

    key="$(resolve_target_key "${target}")"
    split_source_key "${key}"
    build_dir="$(build_dir_for_key "${key}")"
    bundle_path="${build_dir}/Mixxx.flatpak"
    verify_cached_bundle "${key}"
    old_sha="$(installed_source_sha || true)"
    if [[ -n "${old_sha}" && "${old_sha}" != "${BUILD_SHA}" ]]; then
        old_key="$(snapshot_installed_build "${old_sha}")"
    fi

    flatpak install --user --bundle --reinstall --noninteractive -y "${bundle_path}"
    verified_sha="$(installed_source_sha || true)"
    [[ "${verified_sha}" == "${BUILD_SHA}" ]] ||
        die "Installed Flatpak does not report expected source SHA ${BUILD_SHA}."
    if [[ -n "${old_key}" ]]; then
        printf '%s\n' "${old_key}" >"${PREVIOUS_STATE}"
    fi
    printf '%s\n' "${key}" >"${CURRENT_STATE}"
    prune_local_builds
    echo "Activated ${key}."
}

prune_local_builds() {
    local current_key="" previous_key="" staged_key="" path relative provider sha key kept=0
    current_key="$(read_state_key "${CURRENT_STATE}" 2>/dev/null || true)"
    previous_key="$(read_state_key "${PREVIOUS_STATE}" 2>/dev/null || true)"
    staged_key="$(read_state_key "${STAGED_STATE}" 2>/dev/null || true)"
    while IFS= read -r path; do
        relative="${path#"${CACHE_ROOT}/builds/"}"
        provider="${relative%%/*}"
        sha="${relative#*/}"
        key="$(source_key "${provider}" "${sha}")"
        if [[ "${key}" == "${current_key}" || "${key}" == "${previous_key}" ||
                "${key}" == "${staged_key}" || "${kept}" -lt "${KEEP_LOCAL_BUILDS}" ]]; then
            kept=$((kept + 1))
            continue
        fi
        [[ "${path}" == "${CACHE_ROOT}/builds/"*/* ]] ||
            die "Refusing to prune unexpected path: ${path}"
        rm -rf -- "${path}"
    done < <(
        find "${CACHE_ROOT}/builds" -mindepth 2 -maxdepth 2 -type d \
            -printf '%T@ %p\n' 2>/dev/null | sort -rn | cut -d' ' -f2-
    )
}

rollback_build() {
    local previous_key
    previous_key="$(read_state_key "${PREVIOUS_STATE}")" ||
        die "No cached rollback build is recorded."
    activate_build "${previous_key}"
}

check_build() (
    local descriptors descriptor available_sha provider installed_sha
    require_command curl
    require_command jq
    ensure_directories
    descriptors="$(descriptors_for_target auto)"
    descriptor="$(head -n 1 <<<"${descriptors}")"
    available_sha="$(jq -r '.source_sha' <<<"${descriptor}")"
    provider="$(jq -r '.provider' <<<"${descriptor}")"
    installed_sha="$(installed_source_sha || true)"
    echo "Installed: ${installed_sha:-unknown}"
    echo "Available: ${provider}:${available_sha}"
    if [[ "${installed_sha}" == "${available_sha}" ]]; then
        echo "Deck is up to date."
    else
        echo "An update is available."
    fi
)

print_status() {
    local installed_sha staged_key previous_key current_key
    installed_sha="$(installed_source_sha || true)"
    staged_key="$(read_state_key "${STAGED_STATE}" 2>/dev/null || echo none)"
    previous_key="$(read_state_key "${PREVIOUS_STATE}" 2>/dev/null || echo none)"
    current_key="$(read_state_key "${CURRENT_STATE}" 2>/dev/null || echo none)"
    echo "Installed source: ${installed_sha:-unknown}"
    echo "Current build: ${current_key}"
    echo "Staged build: ${staged_key}"
    echo "Previous build: ${previous_key}"
    echo "Forgejo manifest URL: ${MANIFEST_URL}"
    echo "GitHub fallback: $(github_token_configured && echo configured || echo not-configured)"
    echo "Mixxx running: $(is_mixxx_running && echo yes || echo no)"
    flatpak info --user "${APP_ID}" 2>/dev/null | sed -n '1,12p' || true
}

setup_client() {
    local installed_helper="${HOME}/.local/bin/deck_ostree_validation.sh"
    ensure_flatpak
    require_command curl
    require_command jq
    require_command sha256sum
    require_command unzip
    require_command ostree
    ensure_directories
    install_udev_rules
    mkdir -p "${HOME}/.local/bin"
    if [[ ! "${OSTREE_VALIDATION_HELPER}" -ef "${installed_helper}" ]]; then
        install -m 0644 "${OSTREE_VALIDATION_HELPER}" "${installed_helper}"
    fi
    install -m 0755 "${BASH_SOURCE[0]}" "${HOME}/.local/bin/mixxx-deck"
    echo "Installed ${HOME}/.local/bin/mixxx-deck and its OSTree validator"
}

[[ "${MANIFEST_URL}" =~ ^https://[^/]+/.+/latest\.json$ ]] ||
    die "MIXXX_DECK_MANIFEST_URL must be an HTTPS latest.json URL."
[[ "${GITHUB_API_BASE}" =~ ^https://[^/]+$ ]] ||
    die "MIXXX_GITHUB_API_BASE must be an HTTPS API root URL."
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
    github-configure)
        [[ $# -eq 0 ]] || die "github-configure takes no arguments."
        require_command curl
        github_configure
        ;;
    check)
        [[ $# -eq 0 ]] || die "check takes no arguments."
        check_build
        ;;
    stage)
        [[ $# -le 1 ]] || die "stage accepts at most one target."
        stage_build "${1:-auto}" >/dev/stdout
        ;;
    activate)
        [[ $# -le 1 ]] || die "activate accepts at most one target."
        activate_build "${1:-auto}"
        ;;
    deploy)
        [[ $# -le 1 ]] || die "deploy accepts at most one target."
        target="${1:-auto}"
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
        usage >&2
        exit 2
        ;;
esac
