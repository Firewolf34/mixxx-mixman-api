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
        [[ "${TEST_RUNNING:-no}" == yes ]] && echo org.mixxx.Mixxx
        ;;
    info)
        if [[ "$*" == *--show-commit* ]]; then
            echo "${TEST_INSTALLED_COMMIT}"
        else
            echo "Subject: Built from ${TEST_INSTALLED_SOURCE}"
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
        echo update >>"${TEST_COMMAND_LOG}"
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
export TEST_INSTALLED_COMMIT="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
export TEST_AVAILABLE_COMMIT="${TEST_INSTALLED_COMMIT}"
export TEST_INSTALLED_SOURCE="1111111111111111111111111111111111111111"
export TEST_AVAILABLE_SOURCE="${TEST_INSTALLED_SOURCE}"
export TEST_AC_POWER=yes
export TEST_RUNNING=no

"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "up-to-date"' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ ! -e "${TEST_COMMAND_LOG}" ]]

export TEST_AVAILABLE_COMMIT="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
export TEST_AVAILABLE_SOURCE="2222222222222222222222222222222222222222"
export TEST_AC_POWER=no
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "deferred-battery" and .pending_commit != ""' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ ! -e "${TEST_COMMAND_LOG}" ]]

export TEST_AC_POWER=yes
export TEST_RUNNING=yes
"${SCRIPT_DIR}/deck_flatpak_auto_update.sh" auto-update
jq -e '.result == "deferred-running" and .pending_commit != ""' \
    "${XDG_STATE_HOME}/mixxx-deck/auto-update-status.json" >/dev/null
[[ ! -e "${TEST_COMMAND_LOG}" ]]

echo "Deck automatic-update state tests passed."
