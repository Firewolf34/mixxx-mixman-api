#!/bin/bash
# Build, validate, and atomically publish a deck-candidate Flatpak.

set -euo pipefail

APP_ID="org.mixxx.Mixxx"
EXPECTED_ARCH="x86_64"
EXPECTED_REF="app/${APP_ID}/${EXPECTED_ARCH}/master"
CHANNEL="deck-candidate"
KEEP_BUILDS="${MIXXX_DECK_KEEP_BUILDS:-10}"
PUBLISH_ROOT="${MIXXX_DECK_PUBLISH_ROOT:-}"
PUBLIC_BASE_URL="${MIXXX_DECK_PUBLIC_BASE_URL:-https://polinaria.world/mixxx-deck}"
SOURCE_REF="${MIXXX_DECK_SOURCE_REF:-refs/heads/deck/candidate}"
EVENT_SHA="${MIXXX_DECK_EVENT_SHA:-}"
LOCK_FILE="${MIXXX_DECK_LOCK_FILE:-/data/locks/mixxx-deck-build.lock}"
BUILDER_JOBS="${FLATPAK_BUILDER_JOBS:-}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

die() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command '$1' is missing."
}

if [[ -z "${PUBLISH_ROOT}" || "${PUBLISH_ROOT}" != /* ]]; then
    die "MIXXX_DECK_PUBLISH_ROOT must be an absolute path."
fi
if [[ "${SOURCE_REF}" != "refs/heads/deck/candidate" ]]; then
    die "Refusing to publish non-candidate ref ${SOURCE_REF}."
fi
if [[ ! "${KEEP_BUILDS}" =~ ^[1-9][0-9]*$ ]]; then
    die "MIXXX_DECK_KEEP_BUILDS must be a positive integer."
fi
if [[ "$(uname -m)" != "${EXPECTED_ARCH}" ]]; then
    die "This workflow must run on ${EXPECTED_ARCH}."
fi
if [[ "${BUILDER_JOBS}" != "1" ]]; then
    die "FLATPAK_BUILDER_JOBS must be exactly 1 on the 2 GiB VPS."
fi

for command_name in \
        ccache \
        flatpak \
        flatpak-builder \
        flock \
        git \
        jq \
        ostree \
        sha256sum \
        tar \
        timeout \
        zstd; do
    require_command "${command_name}"
done

cd "${REPO_ROOT}"
tools/deck_build_preflight.sh
SOURCE_SHA="$(git rev-parse --verify HEAD)"
if [[ -n "${EVENT_SHA}" && "${SOURCE_SHA}" != "${EVENT_SHA}" ]]; then
    die "Checked-out SHA ${SOURCE_SHA} does not match event SHA ${EVENT_SHA}."
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    die "The build checkout contains tracked modifications."
fi

mkdir -p "$(dirname -- "${LOCK_FILE}")" "${PUBLISH_ROOT}/builds"
exec 9>"${LOCK_FILE}"
flock 9

echo "Building ${APP_ID} from ${SOURCE_SHA}..."
packaging/flatpak/flatpak_build.sh bundle

BUNDLE_PATH="${REPO_ROOT}/Mixxx.flatpak"
[[ -s "${BUNDLE_PATH}" ]] || die "Flatpak bundle was not created."

TEMP_DIR="$(mktemp -d)"
cleanup() {
    rm -rf -- "${TEMP_DIR}"
}
trap cleanup EXIT

VALIDATION_REPO="${TEMP_DIR}/validation-repo"
ostree init --repo="${VALIDATION_REPO}" --mode=archive-z2
flatpak build-import-bundle "${VALIDATION_REPO}" "${BUNDLE_PATH}"
ostree --repo="${VALIDATION_REPO}" fsck

if ! ostree --repo="${VALIDATION_REPO}" refs | grep -Fxq "${EXPECTED_REF}"; then
    die "Bundle does not contain ${EXPECTED_REF}."
fi

FLATPAK_COMMIT="$(ostree --repo="${VALIDATION_REPO}" rev-parse "${EXPECTED_REF}")"
COMMIT_SUBJECT="$(ostree --repo="${VALIDATION_REPO}" show --print-detached-metadata-key=ostree.commit.subject "${FLATPAK_COMMIT}" 2>/dev/null || true)"
if [[ -z "${COMMIT_SUBJECT}" ]]; then
    COMMIT_SUBJECT="$(ostree --repo="${VALIDATION_REPO}" show -s "${FLATPAK_COMMIT}")"
fi
if [[ "${COMMIT_SUBJECT}" != *"${SOURCE_SHA}"* ]]; then
    die "Bundle commit subject does not identify source SHA ${SOURCE_SHA}."
fi

echo "Running headless Mixxx version smoke test..."
timeout 30 flatpak build \
    --env=QT_QPA_PLATFORM=offscreen \
    build_flatpak \
    /app/bin/mixxx --version

SOURCE_ARCHIVE="${TEMP_DIR}/source.tar.zst"
git archive --format=tar --prefix="mixxx-${SOURCE_SHA}/" HEAD |
    zstd -T0 -19 -o "${SOURCE_ARCHIVE}"

BUNDLE_SHA256="$(sha256sum "${BUNDLE_PATH}" | awk '{print $1}')"
SOURCE_SHA256="$(sha256sum "${SOURCE_ARCHIVE}" | awk '{print $1}')"
BUNDLE_SIZE="$(stat -c '%s' "${BUNDLE_PATH}")"
BUILT_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_URL="${PUBLIC_BASE_URL}/builds/${SOURCE_SHA}"
MANIFEST_PATH="${TEMP_DIR}/manifest.json"

jq -n \
    --argjson schema_version 1 \
    --arg channel "${CHANNEL}" \
    --arg app_id "${APP_ID}" \
    --arg arch "${EXPECTED_ARCH}" \
    --arg source_sha "${SOURCE_SHA}" \
    --arg source_ref "${SOURCE_REF}" \
    --arg built_at "${BUILT_AT}" \
    --arg bundle_url "${BUILD_URL}/Mixxx.flatpak" \
    --arg source_url "${BUILD_URL}/source.tar.zst" \
    --arg sha256 "${BUNDLE_SHA256}" \
    --arg source_sha256 "${SOURCE_SHA256}" \
    --argjson size_bytes "${BUNDLE_SIZE}" \
    '{
        schema_version: $schema_version,
        channel: $channel,
        app_id: $app_id,
        arch: $arch,
        source_sha: $source_sha,
        source_ref: $source_ref,
        built_at: $built_at,
        bundle_url: $bundle_url,
        source_url: $source_url,
        sha256: $sha256,
        source_sha256: $source_sha256,
        size_bytes: $size_bytes
    }' >"${MANIFEST_PATH}"

STAGING_DIR="${PUBLISH_ROOT}/.staging-${SOURCE_SHA}-$$"
FINAL_DIR="${PUBLISH_ROOT}/builds/${SOURCE_SHA}"
if [[ -e "${FINAL_DIR}" ]]; then
    if [[ "$(sha256sum "${FINAL_DIR}/Mixxx.flatpak" | awk '{print $1}')" != "${BUNDLE_SHA256}" ]]; then
        die "Existing immutable build does not match ${SOURCE_SHA}."
    fi
    echo "Verified existing immutable build ${FINAL_DIR}."
else
    mkdir -m 0755 "${STAGING_DIR}"
    install -m 0644 "${BUNDLE_PATH}" "${STAGING_DIR}/Mixxx.flatpak"
    install -m 0644 "${SOURCE_ARCHIVE}" "${STAGING_DIR}/source.tar.zst"
    install -m 0644 "${MANIFEST_PATH}" "${STAGING_DIR}/manifest.json"
    mv -- "${STAGING_DIR}" "${FINAL_DIR}"
fi

REMOTE_SHA="$(git ls-remote origin "${SOURCE_REF}" | awk 'NR == 1 {print $1}')"
if [[ -n "${REMOTE_SHA}" && "${REMOTE_SHA}" != "${SOURCE_SHA}" ]]; then
    echo "A newer ${SOURCE_REF} commit exists; keeping this build without promoting latest.json."
    exit 0
fi

LATEST_TEMP="${PUBLISH_ROOT}/.latest-${SOURCE_SHA}-$$.json"
install -m 0644 "${FINAL_DIR}/manifest.json" "${LATEST_TEMP}"
mv -f -- "${LATEST_TEMP}" "${PUBLISH_ROOT}/latest.json"

mapfile -t EXPIRED_BUILDS < <(
    find "${PUBLISH_ROOT}/builds" -mindepth 1 -maxdepth 1 -type d \
        -printf '%T@ %p\n' |
        sort -rn |
        tail -n "+$((KEEP_BUILDS + 1))" |
        cut -d' ' -f2-
)
for expired_build in "${EXPIRED_BUILDS[@]}"; do
    [[ "${expired_build}" == "${PUBLISH_ROOT}/builds/"* ]] ||
        die "Refusing to prune unexpected path: ${expired_build}"
    rm -rf -- "${expired_build}"
done

echo "Published ${SOURCE_SHA} to ${BUILD_URL}"
