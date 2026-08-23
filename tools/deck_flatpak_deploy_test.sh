#!/bin/bash
# Lightweight mocked tests for offline manual rollback and state migration.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEMP_ROOT="$(mktemp -d)"
trap 'rm -rf -- "${TEMP_ROOT}"' EXIT

mkdir -p "${TEMP_ROOT}/bin" "${TEMP_ROOT}/state/mixxx-deck" \
    "${TEMP_ROOT}/cache/mixxx-deck/repo-rollback" \
    "${TEMP_ROOT}/data/flatpak/repo" "${TEMP_ROOT}/config"

cat >"${TEMP_ROOT}/bin/flatpak" <<'EOF'
#!/bin/bash
set -euo pipefail
echo "$*" >>"${TEST_COMMAND_LOG}"
case "$1" in
    ps)
        if [[ "${TEST_PROCESS_STATE:-idle}" == live ]]; then
            printf '%s\t%s\n' "${TEST_LIVE_PID}" org.mixxx.Mixxx
        fi
        ;;
    info)
        if [[ "$*" == *--show-commit* ]]; then
            cat "${TEST_INSTALLED_COMMIT_FILE}"
        else
            printf 'Subject: Built from %s\n' "$(<"${TEST_INSTALLED_SOURCE_FILE}")"
        fi
        ;;
    build-import-bundle)
        echo "Importing bundle objects..."
        ;;
    build-bundle)
        for argument in "$@"; do
            if [[ "${argument}" == *.part ]]; then
                printf 'snapshot bundle\n' >"${argument}"
                exit 0
            fi
        done
        exit 2
        ;;
    update)
        [[ "$*" == *--no-pull* ]] || exit 91
        if [[ "${TEST_ROLLBACK_UPDATE:-restore}" == restore ]]; then
            printf '%s\n' "${TEST_ROLLBACK_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
            printf '%s\n' "${TEST_ROLLBACK_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
        fi
        ;;
    install)
        [[ "$*" == *--no-pull* ]] || exit 92
        printf '%s\n' "${TEST_ROLLBACK_COMMIT}" >"${TEST_INSTALLED_COMMIT_FILE}"
        printf '%s\n' "${TEST_ROLLBACK_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
        ;;
    *)
        echo "unexpected flatpak command: $*" >&2
        exit 2
        ;;
esac
EOF

cat >"${TEMP_ROOT}/bin/ostree" <<'EOF'
#!/bin/bash
set -euo pipefail
echo "ostree $*" >>"${TEST_COMMAND_LOG}"
case "$*" in
    *" pull-local "*) echo "Importing OSTree objects..." ;;
    *" refs --create="*) ;;
    *" refs") echo app/org.mixxx.Mixxx/x86_64/master ;;
    *" rev-parse "*) echo "${TEST_IMPORTED_COMMIT:-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa}" ;;
    *" show "*)
        printf 'commit aaaa\nDate: now\n    Built from %s and %s\n' \
            "$(<"${TEST_INSTALLED_SOURCE_FILE}")" "${TEST_ROLLBACK_SOURCE}"
        ;;
    *" fsck") echo "Validating OSTree repository..." ;;
    init*) ;;
    *) echo "unexpected ostree command: $*" >&2; exit 2 ;;
esac
EOF
chmod 0755 "${TEMP_ROOT}/bin/flatpak" "${TEMP_ROOT}/bin/ostree"

export PATH="${TEMP_ROOT}/bin:/usr/bin:/bin"
export HOME="${TEMP_ROOT}"
export XDG_STATE_HOME="${TEMP_ROOT}/state"
export XDG_CACHE_HOME="${TEMP_ROOT}/cache"
export XDG_DATA_HOME="${TEMP_ROOT}/data"
export XDG_CONFIG_HOME="${TEMP_ROOT}/config"
export MIXXX_DECK_MAX_ARTIFACT_BYTES=1048576
export MIXXX_DECK_MIN_FREE_RESERVE_BYTES=1048576
export MIXXX_DECK_MAX_METADATA_BYTES=65536
export TEST_COMMAND_LOG="${TEMP_ROOT}/commands.log"
export TEST_INSTALLED_COMMIT_FILE="${TEMP_ROOT}/installed-commit"
export TEST_INSTALLED_SOURCE_FILE="${TEMP_ROOT}/installed-source"
export TEST_PROCESS_STATE=idle
export TEST_INSTALLED_SOURCE=1111111111111111111111111111111111111111
export TEST_ROLLBACK_SOURCE=2222222222222222222222222222222222222222
export TEST_ROLLBACK_COMMIT=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    >"${TEST_INSTALLED_COMMIT_FILE}"
printf '%s\n' "${TEST_INSTALLED_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"

rollback_dir="${XDG_CACHE_HOME}/mixxx-deck/repo-rollback/${TEST_ROLLBACK_SOURCE}"
mkdir -p "${rollback_dir}"
printf 'verified rollback bundle\n' >"${rollback_dir}/Mixxx.flatpak"
sha256sum "${rollback_dir}/Mixxx.flatpak" | awk '{print $1}' \
    >"${rollback_dir}/Mixxx.flatpak.sha256"
printf '%s\n' "${TEST_ROLLBACK_COMMIT}" >"${rollback_dir}/ostree-commit"

printf '%s\n' 3333333333333333333333333333333333333333 \
    >"${XDG_STATE_HOME}/mixxx-deck/current-source-sha"
printf '%s\n' 4444444444444444444444444444444444444444 \
    >"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha"

status_output="$("${SCRIPT_DIR}/deck_flatpak_deploy.sh" status)"
grep -Fq "Current build: installed:${TEST_INSTALLED_SOURCE}" <<<"${status_output}"
grep -Fq "Previous build: repo:${TEST_ROLLBACK_SOURCE}" <<<"${status_output}"
grep -Fq 'Rollback bundle: verified' <<<"${status_output}"

mv "${TEMP_ROOT}/bin/ostree" "${TEMP_ROOT}/bin/ostree.unavailable"
status_output="$("${SCRIPT_DIR}/deck_flatpak_deploy.sh" status)"
grep -Fq 'Rollback bundle: invalid' <<<"${status_output}"
mv "${TEMP_ROOT}/bin/ostree.unavailable" "${TEMP_ROOT}/bin/ostree"

: >"${TEST_COMMAND_LOG}"
"${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback
[[ "$(<"${TEST_INSTALLED_SOURCE_FILE}")" == "${TEST_ROLLBACK_SOURCE}" ]]
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/current-source-sha")" == "repo:${TEST_ROLLBACK_SOURCE}" ]]
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha")" == "local:${TEST_INSTALLED_SOURCE}" ]]
grep -q '^update .*--no-pull' "${TEST_COMMAND_LOG}"
! grep -q 'remote-add' "${TEST_COMMAND_LOG}"
grep -Fq "pull-local --depth=0 ${XDG_DATA_HOME}/flatpak/repo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
    "${TEST_COMMAND_LOG}"
grep -Fq 'refs --create=app/org.mixxx.Mixxx/x86_64/master aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' \
    "${TEST_COMMAND_LOG}"

printf '%s\n' "${TEST_INSTALLED_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    >"${TEST_INSTALLED_COMMIT_FILE}"
export TEST_ROLLBACK_UPDATE=noop
: >"${TEST_COMMAND_LOG}"
"${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback
grep -q '^install .*--no-pull' "${TEST_COMMAND_LOG}"

# An installed non-Deck Flatpak has no Built-from source SHA. It must still be
# snapshotted under its exact OSTree commit with explicit provenance.
: >"${TEST_INSTALLED_SOURCE_FILE}"
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    >"${TEST_INSTALLED_COMMIT_FILE}"
: >"${TEST_COMMAND_LOG}"
"${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback
snapshot_dir="${XDG_CACHE_HOME}/mixxx-deck/snapshots/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[[ "$(<"${XDG_STATE_HOME}/mixxx-deck/previous-source-sha")" == \
    snapshot:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ]]
jq -e '
    .schema_version == 1 and
    .kind == "installed-flatpak-snapshot" and
    .ostree_commit == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" and
    .source_sha == null
' "${snapshot_dir}/provenance.json" >/dev/null

# If the exported bundle resolves to a different commit than the captured
# installed base, fail before any install/update mutation.
: >"${TEST_INSTALLED_SOURCE_FILE}"
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    >"${TEST_INSTALLED_COMMIT_FILE}"
export TEST_IMPORTED_COMMIT=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
: >"${TEST_COMMAND_LOG}"
if "${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback 2>/dev/null; then
    echo "rollback accepted a changed snapshot base" >&2
    exit 1
fi
! grep -Eq '^(update|install) ' "${TEST_COMMAND_LOG}"
unset TEST_IMPORTED_COMMIT

printf '%s\n' "${TEST_INSTALLED_SOURCE}" >"${TEST_INSTALLED_SOURCE_FILE}"
printf '%s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    >"${TEST_INSTALLED_COMMIT_FILE}"
printf '%s\n' stale >"${rollback_dir}/Mixxx.flatpak.sha256"
: >"${TEST_COMMAND_LOG}"
if "${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback 2>/dev/null; then
    echo "rollback unexpectedly accepted a corrupt cached bundle" >&2
    exit 1
fi
! grep -Eq '^(update|install|build-bundle) ' "${TEST_COMMAND_LOG}"

export TEST_PROCESS_STATE=live
export TEST_LIVE_PID=$$
: >"${TEST_COMMAND_LOG}"
if "${SCRIPT_DIR}/deck_flatpak_deploy.sh" rollback 2>/dev/null; then
    echo "rollback unexpectedly ran while Mixxx was active" >&2
    exit 1
fi
! grep -Eq '^(update|install|build-bundle) ' "${TEST_COMMAND_LOG}"

MIXXX_DECK_CLIENT="${SCRIPT_DIR}/deck_flatpak_deploy.sh" \
    "${SCRIPT_DIR}/mixxx_break_glass.sh" --help >/dev/null

echo "Deck manual rollback tests passed."
