#!/bin/bash
# Validate a provider bundle and publish it to the signed Polinaria Flatpak repo.

set -euo pipefail

APP_ID="org.mixxx.Mixxx"
EXPECTED_ARCH="x86_64"
EXPECTED_REF="app/${APP_ID}/${EXPECTED_ARCH}/master"
PUBLIC_BASE_URL="${MIXXX_DECK_PUBLIC_BASE_URL:-https://forge.polinaria.world/artifacts}"
ARTIFACT_ROOT="${MIXXX_DECK_ARTIFACT_ROOT:-}"
REPOSITORY="${MIXXX_DECK_REPOSITORY:-}"
GPG_HOME="${MIXXX_DECK_GPG_HOME:-}"
GPG_KEY="${MIXXX_DECK_GPG_KEY:-}"
KEEP_BUILDS="${MIXXX_DECK_KEEP_BUILDS:-3}"
GENERATE_STATIC_DELTAS="${MIXXX_DECK_GENERATE_STATIC_DELTAS:-0}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/deck_ostree_validation.sh
source "${SCRIPT_DIR}/deck_ostree_validation.sh"

die() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command '$1' is missing."
}

[[ $# -eq 2 ]] || die "Usage: $0 BUNDLE MANIFEST"
BUNDLE="$1"
MANIFEST="$2"
[[ -s "${BUNDLE}" ]] || die "Bundle is missing or empty: ${BUNDLE}"
[[ -s "${MANIFEST}" ]] || die "Manifest is missing or empty: ${MANIFEST}"
[[ "${ARTIFACT_ROOT}" == /* ]] || die "MIXXX_DECK_ARTIFACT_ROOT must be absolute."
[[ "${REPOSITORY}" == /* ]] || die "MIXXX_DECK_REPOSITORY must be absolute."
[[ "${GPG_HOME}" == /* ]] || die "MIXXX_DECK_GPG_HOME must be absolute."
[[ "${GPG_KEY}" =~ ^[0-9A-F]{40}$ ]] || die "MIXXX_DECK_GPG_KEY must be a 40-character fingerprint."
[[ "${KEEP_BUILDS}" =~ ^[1-9][0-9]*$ ]] || die "MIXXX_DECK_KEEP_BUILDS must be positive."
[[ "${GENERATE_STATIC_DELTAS}" == 0 || "${GENERATE_STATIC_DELTAS}" == 1 ]] ||
    die "MIXXX_DECK_GENERATE_STATIC_DELTAS must be 0 or 1."

for command_name in flatpak flock jq ostree sha256sum stat; do
    require_command "${command_name}"
done

SOURCE_SHA="$(jq -er '.source_sha' "${MANIFEST}")"
SOURCE_REF="$(jq -er '.source_ref' "${MANIFEST}")"
BUILT_AT="$(jq -er '.built_at' "${MANIFEST}")"
BUNDLE_SHA="$(jq -er '.sha256' "${MANIFEST}")"
BUNDLE_SIZE="$(jq -er '.size_bytes' "${MANIFEST}")"
CHANNEL="$(jq -er '.channel' "${MANIFEST}")"
PROVIDER="$(jq -r '.provider // "forgejo-actions"' "${MANIFEST}")"

[[ "${SOURCE_SHA}" =~ ^[0-9a-f]{40}$ ]] || die "Manifest source SHA is invalid."
[[ "${BUILT_AT}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]] ||
    die "Manifest build timestamp is invalid."
[[ "${BUNDLE_SHA}" =~ ^[0-9a-f]{64}$ ]] || die "Manifest bundle checksum is invalid."
[[ "${BUNDLE_SIZE}" =~ ^[1-9][0-9]*$ ]] || die "Manifest bundle size is invalid."
jq -e --arg app "${APP_ID}" --arg arch "${EXPECTED_ARCH}" '
    .schema_version == 1 and .app_id == $app and .arch == $arch
' "${MANIFEST}" >/dev/null || die "Manifest application contract is invalid."

case "${PROVIDER}:${CHANNEL}:${SOURCE_REF}" in
    github-actions:github-candidate:refs/heads/github/candidate)
        jq -e '
            (.workflow_run_id | tostring | test("^[1-9][0-9]*$")) and
            .bundle_filename == "Mixxx.flatpak"
        ' "${MANIFEST}" >/dev/null ||
            die "GitHub workflow metadata is invalid."
        ;;
    forgejo-actions:deck-candidate:refs/heads/deck/candidate)
        jq -e '
            (.source_sha256 | type == "string" and test("^[0-9a-f]{64}$")) and
            (.source_url | type == "string")
        ' "${MANIFEST}" >/dev/null ||
            die "Forgejo publication metadata is invalid."
        ;;
    *)
        die "Manifest provider, channel, and source ref are not an authorized candidate."
        ;;
esac

[[ "$(stat -c '%s' "${BUNDLE}")" == "${BUNDLE_SIZE}" ]] ||
    die "Bundle size does not match the manifest."
[[ "$(sha256sum "${BUNDLE}" | awk '{print $1}')" == "${BUNDLE_SHA}" ]] ||
    die "Bundle checksum does not match the manifest."

mkdir -p "${ARTIFACT_ROOT}/builds" "$(dirname -- "${REPOSITORY}")"
exec 9>"${ARTIFACT_ROOT}/.mixxx-repo-publish.lock"
flock 9
cd "${ARTIFACT_ROOT}"

if [[ -s "${ARTIFACT_ROOT}/latest.json" ]]; then
    CURRENT_BUILT_AT="$(jq -r '.built_at // empty' "${ARTIFACT_ROOT}/latest.json")"
    CURRENT_SOURCE_SHA="$(jq -r '.source_sha // empty' "${ARTIFACT_ROOT}/latest.json")"
    if [[ -n "${CURRENT_BUILT_AT}" && "${BUILT_AT}" < "${CURRENT_BUILT_AT}" &&
            "${SOURCE_SHA}" != "${CURRENT_SOURCE_SHA}" ]]; then
        echo "Ignoring stale ${PROVIDER} candidate ${SOURCE_SHA} from ${BUILT_AT}."
        exit 0
    fi
fi

VALIDATION_ROOT="$(mktemp -d)"
STAGING_DIR="${ARTIFACT_ROOT}/.staging-${SOURCE_SHA}-$$"
LATEST_TEMP="${ARTIFACT_ROOT}/.latest-${SOURCE_SHA}-$$.json"
cleanup() {
    rm -rf -- "${VALIDATION_ROOT}" "${STAGING_DIR}"
    rm -f -- "${LATEST_TEMP}"
}
trap cleanup EXIT

VALIDATION_REPO="${VALIDATION_ROOT}/repo"
ostree init --repo="${VALIDATION_REPO}" --mode=archive-z2
flatpak build-import-bundle "${VALIDATION_REPO}" "${BUNDLE}"
ostree --repo="${VALIDATION_REPO}" fsck
ostree --repo="${VALIDATION_REPO}" refs | grep -Fxq "${EXPECTED_REF}" ||
    die "Bundle does not contain ${EXPECTED_REF}."
VALIDATED_COMMIT="$(ostree --repo="${VALIDATION_REPO}" rev-parse "${EXPECTED_REF}")"
deck_ostree_commit_subject_contains_source \
    "${VALIDATION_REPO}" "${VALIDATED_COMMIT}" "${SOURCE_SHA}"

FINAL_DIR="${ARTIFACT_ROOT}/builds/${SOURCE_SHA}"
if [[ -e "${FINAL_DIR}" ]]; then
    [[ -s "${FINAL_DIR}/Mixxx.flatpak" ]] ||
        die "Existing immutable build is incomplete."
    [[ "$(sha256sum "${FINAL_DIR}/Mixxx.flatpak" | awk '{print $1}')" == "${BUNDLE_SHA}" ]] ||
        die "Existing immutable build checksum differs for ${SOURCE_SHA}."
else
    mkdir -m 2775 "${STAGING_DIR}"
    install -m 0644 "${BUNDLE}" "${STAGING_DIR}/Mixxx.flatpak"
    install -m 0644 "${MANIFEST}" "${STAGING_DIR}/provider-manifest.json"
    mv -- "${STAGING_DIR}" "${FINAL_DIR}"
fi

if [[ ! -f "${REPOSITORY}/config" ]]; then
    ostree init --repo="${REPOSITORY}" --mode=archive-z2
fi
flatpak build-import-bundle \
    --gpg-sign="${GPG_KEY}" \
    --gpg-homedir="${GPG_HOME}" \
    --no-update-summary \
    "${REPOSITORY}" "${BUNDLE}"
PUBLISHED_COMMIT="$(ostree --repo="${REPOSITORY}" rev-parse "${EXPECTED_REF}")"
[[ "${PUBLISHED_COMMIT}" == "${VALIDATED_COMMIT}" ]] ||
    die "Published OSTree commit differs from the validated bundle."
ostree refs --repo="${REPOSITORY}" \
    --create="mixxx/history/${SOURCE_SHA}" "${PUBLISHED_COMMIT}"

jq \
    --arg provider "${PROVIDER}" \
    --arg bundle_url "${PUBLIC_BASE_URL}/builds/${SOURCE_SHA}/Mixxx.flatpak" \
    --arg ostree_commit "${PUBLISHED_COMMIT}" \
    --arg published_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    '. + {
        schema_version: 2,
        provider: $provider,
        bundle_url: $bundle_url,
        ostree_commit: $ostree_commit,
        published_at: $published_at
    }' "${MANIFEST}" >"${LATEST_TEMP}"
install -m 0644 "${LATEST_TEMP}" "${FINAL_DIR}/manifest.json"

mapfile -t EXPIRED_BUILDS < <(
    find "${ARTIFACT_ROOT}/builds" -mindepth 1 -maxdepth 1 -type d \
        -printf '%T@ %p\n' |
        sort -rn |
        tail -n "+$((KEEP_BUILDS + 1))" |
        cut -d' ' -f2-
)
for expired_build in "${EXPIRED_BUILDS[@]}"; do
    [[ "${expired_build}" == "${ARTIFACT_ROOT}/builds/"* ]] ||
        die "Refusing to prune unexpected path ${expired_build}."
    expired_sha="$(basename -- "${expired_build}")"
    [[ "${expired_sha}" =~ ^[0-9a-f]{40}$ ]] ||
        die "Refusing to prune invalid build directory ${expired_build}."
    ostree refs --repo="${REPOSITORY}" \
        --delete="mixxx/history/${expired_sha}" 2>/dev/null || true
    rm -rf -- "${expired_build}"
done

BUILD_UPDATE_ARGS=(
    --title="Polinaria Mixxx Deck"
    --comment="Verified Mixxx candidate builds for Coal"
    --default-branch=master
    --gpg-sign="${GPG_KEY}"
    --gpg-homedir="${GPG_HOME}"
    --prune
)
if [[ "${GENERATE_STATIC_DELTAS}" == 1 ]]; then
    BUILD_UPDATE_ARGS+=(--generate-static-deltas --static-delta-jobs=1)
fi
flatpak build-update-repo "${BUILD_UPDATE_ARGS[@]}" "${REPOSITORY}"
ostree --repo="${REPOSITORY}" fsck

mv -f -- "${LATEST_TEMP}" "${ARTIFACT_ROOT}/latest.json"
echo "Published signed ${PROVIDER} Mixxx candidate ${SOURCE_SHA} (${PUBLISHED_COMMIT})."
