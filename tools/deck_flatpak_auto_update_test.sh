#!/bin/bash
# Lightweight state-machine tests for the idle-only deck updater.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEMP_ROOT="$(mktemp -d)"
trap 'rm -rf -- "${TEMP_ROOT}"' EXIT

mkdir -p "${TEMP_ROOT}/bin" "${TEMP_ROOT}/state" "${TEMP_ROOT}/cache" \
    "${TEMP_ROOT}/data/flatpak/repo"

cat >"${TEMP_ROOT}/bin/flatpak" <<'EOF'
#!/bin/bash
set -euo pipefail
case "$1" in
    ps)
        case "${TEST_PROCESS_STATE:-idle}" in
            live)
                printf '%s\t%s\n' "${TEST_LIVE_PID}" org.mixxx.Mixxx
                ;;
            stale)
                printf '%s\t%s\n' 999999999 org.mixxx.Mixxx
                ;;
            malformed)
                printf '%s\t%s\n' invalid org.mixxx.Mixxx
                ;;
            error)
                exit 2
                ;;
            idle)
                ;;
            *)
                echo "unexpected TEST_PROCESS_STATE" >&2
                exit 2
                ;;
        esac
        ;;
    info)
        if [[ "$*" == *--show-commit* ]]; then
            cat "${TEST_INSTALLED_COMMIT_FILE}"
        else
            printf 'Subject: Built from '
            cat "${TEST_INSTALLED_SOURCE_FILE}"
        fi
        ;;
    remote-info)
        if [[ "$*" == *--show-commit* ]]; then
            echo "${TEST_AVAILABLE_COMMIT}"
        else
            echo "Subject: Built from ${TEST_AVAILABLE_SOURCE}"
        fi
        ;;
    update)
        echo "update $*" >>"${TEST_COMMAND_LOG}"
        if [[ "$*" == *--commit=* ]]; then
            case "${TEST_ROLLBACK_UPDATE:-noop}" in
                restore)
                    printf '%s\n' "${TEST_ROLLBACK_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
                    printf '%s\n' "${TEST_ROLLBACK_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
                    ;;
                noop)
                    echo "Nothing to do."
                    ;;
                fail)
                    exit 1
                    ;;
                *)
                    exit 2
                    ;;
            esac
        elif [[ "$*" == *--no-pull* ]]; then
            printf '%s\n' "${TEST_AVAILABLE_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
            printf '%s\n' "${TEST_AVAILABLE_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
        fi
        ;;
    build-bundle)
        for argument in "$@"; do
            if [[ "${argument}" == *.part ]]; then
                printf 'automatic rollback bundle\n' >"${argument}"
                exit 0
            fi
        done
        exit 2
        ;;
    run)
        ;;
    install)
        echo "install $*" >>"${TEST_COMMAND_LOG}"
        case "${TEST_BUNDLE_INSTALL:-restore}" in
            restore)
                printf '%s\n' "${TEST_ROLLBACK_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
                printf '%s\n' "${TEST_ROLLBACK_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
                ;;
            noop)
                ;;
            fail)
                exit 1
                ;;
            *)
                exit 2
                ;;
        esac
        ;;
    *)
        echo "unexpected flatpak command: $*" >&2
        exit 2
        ;;
esac
EOF
cat >"${TEMP_ROOT}/bin/on_ac_power" <<'EOF'
#!/bin/bash
[[ "${TEST_AC_POWER:-yes}" == yes ]]
EOF
chmod 0755 "${TEMP_ROOT}/bin/flatpak" "${TEMP_ROOT}/bin/on_ac_power"

export PATH="${TEMP_ROOT}/bin:/usr/bin:/bin"
export HOME="${TEMP_ROOT}"
export XDG_STATE_HOME="${TEMP_ROOT}/state"
export XDG_CACHE_HOME="${TEMP_ROOT}/cache"
export XDG_DATA_HOME="${TEMP_ROOT}/data"
export TEST_COMMAND_LOG="${TEMP_ROOT}/commands.log"
export TEST_INSTALLED_COMMIT_FILE="${TEMP_ROOT}/installed-commit"
export TEST_INSTALLED_SOURCE_FILE="${TEMP_ROOT}/installed-source"
export TEST_INSTALLED_COMMIT="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
export TEST_AVAILABLE_COMMIT="${TEST_INSTALLED_COMMIT}"
export TEST_INSTALLED_SOURCE="1111111111111111111111111111111111111111"
export TEST_AVAILABLE_SOURCE="${TEST_INSTALLED_SOURCE}"
export TEST_AC_POWER=yes
export TEST_PROCESS_STATE=idle
printf '%s\n' "${TEST_INSTALLED_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
printf '%s\n' "${TEST_INSTALLED_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"

TEST_PROCESS_STATE=live bash -c \
    'export TEST_LIVE_PID=$$; source "$1"; is_mixxx_running' bash \
    "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"
TEST_PROCESS_STATE=stale bash -c \
    'source "$1"; is_host_pid_namespace() { return 0; }; ! is_mixxx_running' bash \
    "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"
TEST_PROCESS_STATE=stale bash -c \
    'source "$1"; is_host_pid_namespace() { return 1; }; is_mixxx_running' bash \
    "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"
TEST_PROCESS_STATE=malformed bash -c \
    'source "$1"; is_mixxx_running' bash \
    "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"
TEST_PROCESS_STATE=error bash -c \
    'source "$1"; is_mixxx_running' bash \
    "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"

"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "up-to-date"' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/current-source-sha")" == \
    "repo:${TEST_INSTALLED_SOURCE}" ]]
[[ ! -e "${TEST_COMMAND_LOG}" ]]

migration_source=9999999999999999999999999999999999999999
mkdir -p "${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/${migration_source}"
printf '%s\n' legacy:3333333333333333333333333333333333333333 \
    >"${XDG_STATE_HOME}/mixxx-deck/current-source-sha"
printf '%s\n' legacy:4444444444444444444444444444444444444444 \
    >"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha"
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/current-source-sha")" == \
    "repo:${TEST_INSTALLED_SOURCE}" ]]
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha")" == \
    "repo:${migration_source}" ]]
rm -rf -- "${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/${migration_source}"

export TEST_AVAILABLE_COMMIT="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
export TEST_AVAILABLE_SOURCE="2222222222222222222222222222222222222222"
export TEST_AC_POWER=no
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "deferred-battery" and .pending_commit != ""' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ ! -e "${TEST_COMMAND_LOG}" ]]

export TEST_AC_POWER=yes
export TEST_PROCESS_STATE=live
export TEST_LIVE_PID=$$
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "deferred-running" and .pending_commit != ""' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ ! -e "${TEST_COMMAND_LOG}" ]]

export TEST_PROCESS_STATE=idle
for index in 1 2 3 4; do
    old_dir="${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/000000000000000000000000000000000000000${index}"
    mkdir -p "${old_dir}"
    touch -d "2020-01-0${index} 00:00:00 UTC" "${old_dir}"
done
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "updated"' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/current-source-sha")" == \
    "repo:${TEST_AVAILABLE_SOURCE}" ]]
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha")" == \
    "repo:${TEST_INSTALLED_SOURCE}" ]]
[[ "$(find "${XDG_CACHE_HOME}/mixxx-deck/repo-rollback" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 3 ]]
[[ -s "${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/${TEST_INSTALLED_SOURCE}/Mixxx.flatpak" ]]
[[ "$(<"${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/${TEST_INSTALLED_SOURCE}/ostree-commit")" == \
    "${TEST_INSTALLED_COMMIT}" ]]

source "${SCRIPT_DIR}/deck_flatpak_auto_update.sh"
export TEST_ROLLBACK_COMMIT="${TEST_INSTALLED_COMMIT}"
export TEST_ROLLBACK_SOURCE="${TEST_INSTALLED_SOURCE}"
rollback_dir="${TEMP_ROOT}/rollback/${TEST_ROLLBACK_SOURCE}"
mkdir -p "${rollback_dir}"
printf 'verified rollback bundle\n' >"${rollback_dir}/Mixxx.flatpak"
sha256sum "${rollback_dir}/Mixxx.flatpak" | awk '{print $1}' > \
    "${rollback_dir}/Mixxx.flatpak.sha256"

printf '%s\n' "${TEST_AVAILABLE_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
printf '%s\n' "${TEST_AVAILABLE_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
export TEST_ROLLBACK_UPDATE=noop
export TEST_BUNDLE_INSTALL=restore
rollback_commit "${TEST_ROLLBACK_COMMIT}" "${TEST_ROLLBACK_SOURCE}" "${rollback_dir}"
[[ "$(<"${TEST_INSTALLED_COMMIT_FILE}")" == "${TEST_ROLLBACK_COMMIT}" ]]
[[ "$(<"${TEST_INSTALLED_SOURCE_FILE}")" == "${TEST_ROLLBACK_SOURCE}" ]]
grep -q '^install ' "${TEST_COMMAND_LOG}"

printf '%s\n' "${TEST_AVAILABLE_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
printf '%s\n' "${TEST_AVAILABLE_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
export TEST_BUNDLE_INSTALL=noop
if rollback_commit "${TEST_ROLLBACK_COMMIT}" "${TEST_ROLLBACK_SOURCE}" "${rollback_dir}"; then
    echo "rollback unexpectedly succeeded without restoring the old build" >&2
    exit 1
fi

jq -n \
    --arg installed "${TEST_AVAILABLE_COMMIT}" \
    --arg available "${TEST_AVAILABLE_COMMIT}" \
    '{schema_version: 1, result: "rollback-failed",
      installed_commit: $installed, available_commit: $available}' > \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json"
export TEST_AVAILABLE_COMMIT
export TEST_AVAILABLE_SOURCE
if "${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update; then
    echo "rollback-failed state was incorrectly overwritten as up to date" >&2
    exit 1
fi
jq -e '.result == "rollback-failed"' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null

jq -n \
    --arg installed "${TEST_ROLLBACK_COMMIT}" \
    '{schema_version: 1, result: "rollback-failed",
      installed_commit: $installed, available_commit: $installed}' > \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json"
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "up-to-date"' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null

echo "Deck automatic-update state tests passed."
