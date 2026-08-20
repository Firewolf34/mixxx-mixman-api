#!/bin/bash
# Idle-only updater for the signed Polinaria Mixxx Flatpak repository.

set -euo pipefail

APP_ID="${MIXXX_DECK_APP_ID:-org.mixxx.Mixxx}"
EXPECTED_ARCH="${MIXXX_DECK_ARCH:-x86_64}"
REMOTE_NAME="${MIXXX_DECK_REMOTE_NAME:-polinaria-mixxx}"
REMOTE_DESCRIPTOR_URL="${MIXXX_DECK_REMOTE_DESCRIPTOR_URL:-https://forge.polinaria.world/artifacts/flatpak/mixxx.flatpakrepo}"
STATE_ROOT="${XDG_STATE_HOME:-${HOME}/.local/state}/mixxx-deck"
CACHE_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/mixxx-deck"
STATUS_FILE="${STATE_ROOT}/auto-update-status.json"
CURRENT_STATE="${STATE_ROOT}/current-source-sha"
PREVIOUS_STATE="${STATE_ROOT}/previous-source-sha"
UPDATE_LOCK="${STATE_ROOT}/auto-update.lock"
DEPLOY_LOCK="${STATE_ROOT}/deploy.lock"
ROLLBACK_ROOT="${CACHE_ROOT}/repo-rollback"
USER_REPO="${XDG_DATA_HOME:-${HOME}/.local/share}/flatpak/repo"
SMOKE_TIMEOUT_SECONDS="${MIXXX_DECK_SMOKE_TIMEOUT_SECONDS:-90}"

die() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command '$1' is missing."
}

is_host_pid_namespace() {
    local pid_one_comm
    [[ -r /proc/1/comm ]] || return 1
    IFS= read -r pid_one_comm </proc/1/comm || return 1
    [[ "${pid_one_comm}" == systemd || "${pid_one_comm}" == init ]]
}

is_mixxx_running() {
    local processes pid application
    if ! processes="$(flatpak ps --columns=pid,application 2>/dev/null)"; then
        echo "Could not verify whether Mixxx is running; deferring safely." >&2
        return 0
    fi
    while read -r pid application; do
        [[ "${application:-}" == "${APP_ID}" ]] || continue
        if [[ ! "${pid:-}" =~ ^[1-9][0-9]*$ ]]; then
            echo "Flatpak reported Mixxx with an invalid PID; deferring safely." >&2
            return 0
        fi
        [[ -d "/proc/${pid}" ]] && return 0
        if ! is_host_pid_namespace; then
            echo "Flatpak reported Mixxx, but its host PID is not visible in this PID namespace; deferring safely." >&2
            return 0
        fi
    done <<<"${processes}"
    return 1
}

installed_commit() {
    flatpak info --user --show-commit "${APP_ID}" 2>/dev/null
}

installed_source_sha() {
    flatpak info --user "${APP_ID}" 2>/dev/null |
        sed -nE 's/^[[:space:]]*Subject:[[:space:]]*Built from ([0-9a-f]{40}).*$/\1/p' |
        head -n 1
}

installed_build_matches() {
    local expected_commit="$1"
    local expected_source="$2"
    [[ "$(installed_commit || true)" == "${expected_commit}" ]] &&
        [[ "$(installed_source_sha || true)" == "${expected_source}" ]]
}

previous_status_blocks_commit() {
    local commit="$1"
    [[ -r "${STATUS_FILE}" ]] || return 1
    jq -e --arg commit "${commit}" '
        .schema_version == 1 and
        .result == "rollback-failed" and
        .installed_commit == $commit and
        .available_commit == $commit
    ' "${STATUS_FILE}" >/dev/null 2>&1
}

available_commit() {
    flatpak remote-info --user --show-commit "${REMOTE_NAME}" "${APP_ID}"
}

available_source_sha() {
    flatpak remote-info --user "${REMOTE_NAME}" "${APP_ID}" |
        sed -nE 's/^[[:space:]]*Subject:[[:space:]]*Built from ([0-9a-f]{40}).*$/\1/p' |
        head -n 1
}

write_status() {
    local result="$1"
    local message="$2"
    local installed="${3:-}"
    local available="${4:-}"
    local pending="${5:-}"
    local temporary
    temporary="$(mktemp "${STATE_ROOT}/status.XXXXXX")"
    jq -n \
        --arg checked_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        --arg result "${result}" \
        --arg message "${message}" \
        --arg installed_commit "${installed}" \
        --arg available_commit "${available}" \
        --arg pending_commit "${pending}" \
        '{
            schema_version: 1,
            checked_at: $checked_at,
            result: $result,
            message: $message,
            installed_commit: $installed_commit,
            available_commit: $available_commit,
            pending_commit: $pending_commit
        }' >"${temporary}"
    mv -f -- "${temporary}" "${STATUS_FILE}"
}

write_state_key() {
    local state_file="$1"
    local source_sha="$2"
    local temporary
    [[ "${source_sha}" =~ ^[0-9a-f]{40}$ ]] ||
        die "Cannot record an invalid source SHA in shared rollback state."
    temporary="$(mktemp "${STATE_ROOT}/state.XXXXXX")"
    printf 'repo:%s\n' "${source_sha}" >"${temporary}"
    mv -f -- "${temporary}" "${state_file}"
}

state_source_sha() {
    local state_file="$1"
    local value
    [[ -r "${state_file}" ]] || return 1
    value="$(<"${state_file}")"
    value="${value##*:}"
    [[ "${value}" =~ ^[0-9a-f]{40}$ ]] || return 1
    printf '%s\n' "${value}"
}

newest_rollback_source() {
    local excluded_source="$1"
    local path source_sha
    while IFS= read -r path; do
        source_sha="${path##*/}"
        [[ "${source_sha}" =~ ^[0-9a-f]{40}$ ]] || continue
        [[ "${source_sha}" != "${excluded_source}" ]] || continue
        printf '%s\n' "${source_sha}"
        return 0
    done < <(
        find "${ROLLBACK_ROOT}" -mindepth 1 -maxdepth 1 -type d \
            -printf '%T@ %p\n' 2>/dev/null | sort -rn | cut -d' ' -f2-
    )
    return 1
}

reconcile_current_state() {
    local installed_source="$1"
    local recorded_source="" rollback_source=""
    recorded_source="$(state_source_sha "${CURRENT_STATE}" 2>/dev/null || true)"
    if [[ "${recorded_source}" != "${installed_source}" ]]; then
        rollback_source="$(newest_rollback_source "${installed_source}" 2>/dev/null || true)"
        if [[ -n "${rollback_source}" ]]; then
            write_state_key "${PREVIOUS_STATE}" "${rollback_source}"
        fi
    fi
    write_state_key "${CURRENT_STATE}" "${installed_source}"
}

remote_is_configured() {
    flatpak remote-info --user --show-commit "${REMOTE_NAME}" "${APP_ID}" >/dev/null 2>&1
}

configure_remote() {
    flatpak remote-add --user --if-not-exists --from \
        "${REMOTE_NAME}" "${REMOTE_DESCRIPTOR_URL}"
    remote_is_configured ||
        die "Signed remote ${REMOTE_NAME} does not expose ${APP_ID}."
}

snapshot_installed_commit() {
    local source_sha="$1"
    local commit="$2"
    local snapshot_dir bundle part checksum
    [[ "${source_sha}" =~ ^[0-9a-f]{40}$ ]] ||
        die "Installed build has no valid source SHA for rollback."
    [[ "${commit}" =~ ^[0-9a-f]{64}$ ]] ||
        die "Installed build has no valid OSTree commit for rollback."
    snapshot_dir="${ROLLBACK_ROOT}/${source_sha}"
    bundle="${snapshot_dir}/Mixxx.flatpak"
    part="${bundle}.part"
    checksum="${snapshot_dir}/Mixxx.flatpak.sha256"
    mkdir -p "${snapshot_dir}"
    if [[ -s "${bundle}" && -s "${checksum}" ]] &&
            [[ "$(sha256sum "${bundle}" | awk '{print $1}')" == "$(<"${checksum}")" ]]; then
        printf '%s\n' "${commit}" >"${snapshot_dir}/ostree-commit"
        touch "${snapshot_dir}"
        printf '%s\n' "${snapshot_dir}"
        return
    fi
    rm -f -- "${part}"
    flatpak build-bundle --arch="${EXPECTED_ARCH}" \
        --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
        "${USER_REPO}" "${part}" "${APP_ID}" master
    [[ -s "${part}" ]] || die "Could not export the installed rollback build."
    mv -f -- "${part}" "${bundle}"
    sha256sum "${bundle}" | awk '{print $1}' >"${checksum}"
    printf '%s\n' "${commit}" >"${snapshot_dir}/ostree-commit"
    printf '%s\n' "${snapshot_dir}"
}

rollback_commit() {
    local old_commit="$1"
    local old_source="$2"
    local snapshot_dir="$3"
    local bundle="${snapshot_dir}/Mixxx.flatpak"
    local checksum="${snapshot_dir}/Mixxx.flatpak.sha256"

    echo "Automatic update failed; restoring ${old_commit}." >&2
    flatpak update --user --app --no-pull --commit="${old_commit}" \
        --noninteractive -y "${APP_ID}" || true
    if installed_build_matches "${old_commit}" "${old_source}"; then
        echo "Restored previous Mixxx commit from the local Flatpak repository." >&2
        return 0
    fi

    echo "Commit rollback did not restore the previous build; using the cached bundle." >&2
    if [[ ! -s "${bundle}" || ! -s "${checksum}" ]] ||
            [[ "$(sha256sum "${bundle}" | awk '{print $1}')" != "$(<"${checksum}")" ]]; then
        echo "Cached rollback bundle is missing or failed checksum verification." >&2
        return 1
    fi
    flatpak install --user --bundle --no-pull --reinstall --noninteractive -y \
        "${bundle}" || true
    if installed_build_matches "${old_commit}" "${old_source}"; then
        echo "Restored and verified the previous Mixxx build from its cached bundle." >&2
        return 0
    fi

    echo "Rollback verification failed; the previous Mixxx build was not restored." >&2
    return 1
}

record_failed_update() {
    local reason="$1"
    local old_commit="$2"
    local old_source="$3"
    local new_commit="$4"
    local snapshot_dir="$5"
    local current_commit current_source

    if rollback_commit "${old_commit}" "${old_source}" "${snapshot_dir}"; then
        write_state_key "${CURRENT_STATE}" "${old_source}"
        write_status rolled-back "${reason} The previous build was restored and verified." \
            "${old_commit}" "${new_commit}" "${new_commit}"
        return 0
    fi

    current_commit="$(installed_commit || true)"
    current_source="$(installed_source_sha || true)"
    write_state_key "${PREVIOUS_STATE}" "${old_source}"
    if [[ "${current_source}" =~ ^[0-9a-f]{40}$ ]]; then
        write_state_key "${CURRENT_STATE}" "${current_source}"
    fi
    write_status rollback-failed \
        "${reason} Rollback verification failed; do not launch this build automatically." \
        "${current_commit}" "${new_commit}" "${new_commit}"
}

smoke_installed_build() (
    local smoke_home
    smoke_home="$(mktemp -d)"
    trap 'rm -rf -- "${smoke_home}"' EXIT
    timeout "${SMOKE_TIMEOUT_SECONDS}" flatpak run \
        --command=mixxx \
        --env=QT_QPA_PLATFORM=offscreen \
        --env="HOME=${smoke_home}" \
        "${APP_ID}" --version
)

prune_rollback_snapshots() {
    local path kept=0
    while IFS= read -r path; do
        kept=$((kept + 1))
        ((kept <= 3)) && continue
        [[ "${path}" == "${ROLLBACK_ROOT}/"* ]] ||
            die "Refusing to prune unexpected rollback path ${path}."
        rm -rf -- "${path}"
    done < <(
        find "${ROLLBACK_ROOT}" -mindepth 1 -maxdepth 1 -type d \
            -printf '%T@ %p\n' 2>/dev/null | sort -rn | cut -d' ' -f2-
    )
}

auto_update() {
    local old_commit new_commit old_source new_source snapshot_dir
    require_command flatpak
    require_command flock
    require_command jq
    require_command on_ac_power
    require_command sha256sum
    require_command timeout
    [[ "${SMOKE_TIMEOUT_SECONDS}" =~ ^[1-9][0-9]*$ ]] ||
        die "MIXXX_DECK_SMOKE_TIMEOUT_SECONDS must be a positive integer."
    mkdir -p "${STATE_ROOT}" "${ROLLBACK_ROOT}"

    exec 8>"${UPDATE_LOCK}"
    if ! flock -n 8; then
        echo "Another Mixxx update check is already running."
        return 0
    fi

    remote_is_configured ||
        die "Signed remote ${REMOTE_NAME} is not configured; run mixxx-deck setup."
    old_commit="$(installed_commit)"
    new_commit="$(available_commit)"
    old_source="$(installed_source_sha || true)"
    new_source="$(available_source_sha || true)"
    [[ "${old_commit}" =~ ^[0-9a-f]{64}$ ]] || die "Installed OSTree commit is invalid."
    [[ "${new_commit}" =~ ^[0-9a-f]{64}$ ]] || die "Available OSTree commit is invalid."
    [[ "${new_source}" =~ ^[0-9a-f]{40}$ ]] || die "Available source provenance is invalid."

    if [[ "${old_commit}" == "${new_commit}" ]]; then
        if previous_status_blocks_commit "${old_commit}"; then
            echo "Error: the installed build previously failed validation and rollback; refusing to mark it up to date." >&2
            return 1
        fi
        reconcile_current_state "${new_source}"
        write_status up-to-date "Installed build is current." \
            "${old_commit}" "${new_commit}"
        echo "Mixxx is up to date at ${new_source}."
        return 0
    fi
    if ! on_ac_power; then
        write_status deferred-battery "Update is pending until AC power is available." \
            "${old_commit}" "${new_commit}" "${new_commit}"
        echo "Mixxx update ${new_source} is pending; Coal is on battery."
        return 0
    fi
    if is_mixxx_running; then
        write_status deferred-running "Update is pending because Mixxx is running." \
            "${old_commit}" "${new_commit}" "${new_commit}"
        echo "Mixxx update ${new_source} is pending; Mixxx is running."
        return 0
    fi

    echo "Downloading signed Mixxx update ${new_source} without deploying it..."
    flatpak update --user --app --no-deploy --noninteractive -y "${APP_ID}"

    exec 9>"${DEPLOY_LOCK}"
    if ! flock -n 9; then
        write_status deferred-running "Update was downloaded but the launch lock is active." \
            "${old_commit}" "${new_commit}" "${new_commit}"
        echo "Mixxx update was downloaded and will activate after Mixxx exits."
        return 0
    fi
    if is_mixxx_running; then
        write_status deferred-running "Update was downloaded but Mixxx started." \
            "${old_commit}" "${new_commit}" "${new_commit}"
        echo "Mixxx started during download; activation is deferred."
        return 0
    fi

    snapshot_dir="$(snapshot_installed_commit "${old_source}" "${old_commit}")"
    if ! flatpak update --user --app --no-pull --noninteractive -y "${APP_ID}"; then
        record_failed_update "Deployment failed." "${old_commit}" "${old_source}" \
            "${new_commit}" "${snapshot_dir}"
        return 1
    fi
    if ! installed_build_matches "${new_commit}" "${new_source}"; then
        record_failed_update "Installed identity validation failed." \
            "${old_commit}" "${old_source}" "${new_commit}" "${snapshot_dir}"
        return 1
    fi
    if ! smoke_installed_build; then
        record_failed_update "Headless smoke validation failed." \
            "${old_commit}" "${old_source}" "${new_commit}" "${snapshot_dir}"
        return 1
    fi

    prune_rollback_snapshots
    write_state_key "${PREVIOUS_STATE}" "${old_source}"
    write_state_key "${CURRENT_STATE}" "${new_source}"
    write_status updated "Signed update installed and validated." \
        "${new_commit}" "${new_commit}"
    echo "Activated and validated Mixxx ${new_source}."
}

main() {
    case "${1:-auto-update}" in
        auto-update)
            [[ $# -eq 1 || $# -eq 0 ]] || die "auto-update takes no arguments."
            auto_update
            ;;
        configure-remote)
            [[ $# -eq 1 ]] || die "configure-remote takes no arguments."
            require_command flatpak
            configure_remote
            ;;
        *)
            die "Usage: $0 [auto-update|configure-remote]"
            ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
