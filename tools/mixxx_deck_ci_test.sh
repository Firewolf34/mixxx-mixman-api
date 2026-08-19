#!/bin/bash
# Focused local contract tests for the public mixxx-deck-ci command.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
client="${script_dir}/mixxx_deck_ci.sh"
test_root="$(mktemp -d)"
test_home="${test_root}/home"
fake_bin="${test_root}/bin"
token_file="${test_home}/.config/mixxx-deck/forgejo-actions-token"
candidate_sha="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

cleanup() {
    rm -rf -- "${test_root}"
}
trap cleanup EXIT

mkdir -p "${test_home}" "${fake_bin}"

help_output="$("${client}" --help)"
grep -Fq "mixxx-deck-ci install" <<<"${help_output}"
grep -Fq "mixxx-deck-ci status" <<<"${help_output}"
grep -Fq "Use mixxx-deck for signed artifact" <<<"${help_output}"
grep -Fq "Use forgejo-issues for Forgejo issue tickets" <<<"${help_output}"

HOME="${test_home}" "${client}" install >"${test_root}/install.stdout"
installed_client="${test_home}/.local/bin/mixxx-deck-ci"
[[ -x "${installed_client}" ]]
[[ "$(stat -c '%a' "${installed_client}")" == 755 ]]
"${installed_client}" --help >/dev/null
[[ ! -e "${token_file}" ]]
grep -Fq "No credential was created or changed" "${test_root}/install.stdout"

if HOME="${test_home}" "${client}" runs >"${test_root}/stdout" 2>"${test_root}/stderr"; then
    echo "Expected a missing-token failure." >&2
    exit 1
fi
grep -Fq "${token_file}" "${test_root}/stderr"

install -d -m 0700 "${token_file%/*}"
printf '%s\n' "test-token-value-1234567890" >"${token_file}"
chmod 0644 "${token_file}"
if HOME="${test_home}" "${client}" runs >"${test_root}/stdout" 2>"${test_root}/stderr"; then
    echo "Expected an unsafe-token-mode failure." >&2
    exit 1
fi
grep -Fq "must not be accessible to group or others" "${test_root}/stderr"
chmod 0600 "${token_file}"

cat >"${fake_bin}/curl" <<'EOF'
#!/bin/bash
set -euo pipefail

method="GET"
body=""
url=""
while (($# > 0)); do
    case "$1" in
        --request|--data-binary|--config|--connect-timeout|--header|--output)
            option="$1"
            value="$2"
            shift 2
            case "${option}" in
                --request) method="${value}" ;;
                --data-binary) body="${value}" ;;
            esac
            ;;
        --silent|--show-error|--fail|--head|--location)
            shift
            ;;
        http://*|https://*)
            url="$1"
            shift
            ;;
        *)
            echo "Unexpected fake curl argument: $1" >&2
            exit 64
            ;;
    esac
done

printf '%s\t%s\t%s\n' "${method}" "${url}" "${body}" >>"${MIXXX_TEST_REQUEST_LOG}"

candidate_sha="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
other_sha="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
old_run='{"id":100,"index_in_repo":40,"status":"failure","event":"push","prettyref":"deck/candidate","commit_sha":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","created":"2026-08-18T01:00:00Z","html_url":"https://forge.test/runs/100"}'

case "${url}" in
    */branches/deck%2Fcandidate)
        printf '{"name":"deck/candidate","commit":{"id":"%s"}}\n' "${candidate_sha}"
        ;;
    */actions/workflows/deck-flatpak.yml/dispatches)
        [[ "${method}" == POST ]]
        : >"${MIXXX_TEST_DISPATCH_STATE}"
        printf '{"accepted":true}\n'
        ;;
    */actions/tasks\?*)
        if [[ "${MIXXX_TEST_MODE}" == live-empty-event ]]; then
            printf '{"workflow_runs":[{"id":429,"run_number":46,"status":"waiting","event":"workflow_dispatch","head_branch":"deck/candidate","head_sha":"%s","workflow_id":"deck-flatpak.yml","url":"https://forge.test/runs/206"}]}\n' "${candidate_sha}"
        elif [[ "${MIXXX_TEST_MODE}" == mismatch-task ]]; then
            printf '{"workflow_runs":[{"id":430,"run_number":47,"status":"waiting","event":"push","head_branch":"dev","head_sha":"%s","workflow_id":"deck-flatpak.yml","url":"https://forge.test/runs/207"}]}\n' "${candidate_sha}"
        else
            printf '{"workflow_runs":[]}\n'
        fi
        ;;
    */actions/runs\?*)
        if [[ "${MIXXX_TEST_MODE}" == active ]]; then
            printf '{"workflow_runs":[{"id":200,"index_in_repo":41,"status":"running","event":"workflow_dispatch","prettyref":"deck/candidate","commit_sha":"%s","created":"2026-08-19T01:00:00Z","html_url":"https://forge.test/runs/200"}]}\n' "${candidate_sha}"
        elif [[ ! -e "${MIXXX_TEST_DISPATCH_STATE}" || "${MIXXX_TEST_MODE}" == timeout ]]; then
            printf '{"workflow_runs":[%s]}\n' "${old_run}"
        else
            case "${MIXXX_TEST_MODE}" in
                success)
                    new_run="{\"id\":201,\"index_in_repo\":42,\"status\":\"waiting\",\"event\":\"workflow_dispatch\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/201\"}"
                    ;;
                mismatch-sha)
                    new_run="{\"id\":202,\"index_in_repo\":43,\"status\":\"waiting\",\"event\":\"workflow_dispatch\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${other_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/202\"}"
                    ;;
                mismatch-ref)
                    new_run="{\"id\":203,\"index_in_repo\":44,\"status\":\"waiting\",\"event\":\"workflow_dispatch\",\"prettyref\":\"dev\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/203\"}"
                    ;;
                mismatch-event)
                    new_run="{\"id\":204,\"index_in_repo\":45,\"status\":\"waiting\",\"event\":\"push\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/204\"}"
                    ;;
                malformed)
                    new_run="{\"id\":205,\"status\":\"waiting\",\"event\":\"workflow_dispatch\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\"}"
                    ;;
                live-empty-event)
                    new_run="{\"id\":206,\"index_in_repo\":46,\"status\":\"waiting\",\"event\":\"\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/206\"}"
                    ;;
                mismatch-task)
                    new_run="{\"id\":207,\"index_in_repo\":47,\"status\":\"waiting\",\"event\":\"\",\"prettyref\":\"deck/candidate\",\"commit_sha\":\"${candidate_sha}\",\"workflow_id\":\"deck-flatpak.yml\",\"created\":\"2026-08-19T02:00:00Z\",\"html_url\":\"https://forge.test/runs/207\"}"
                    ;;
                *)
                    echo "Unknown fake mode ${MIXXX_TEST_MODE}" >&2
                    exit 65
                    ;;
            esac
            printf '{"workflow_runs":[%s,%s]}\n' "${old_run}" "${new_run}"
        fi
        ;;
    *)
        echo "Unexpected fake curl URL: ${url}" >&2
        exit 66
        ;;
esac
EOF
chmod 0755 "${fake_bin}/curl"

run_dispatch() {
    local mode="$1"
    local timeout="${2:-5}"
    local state_file="${test_root}/dispatch-${mode}.state"
    local request_log="${test_root}/dispatch-${mode}.requests"
    rm -f -- "${state_file}" "${request_log}"
    HOME="${test_home}" \
        PATH="${fake_bin}:${PATH}" \
        MIXXX_TEST_MODE="${mode}" \
        MIXXX_TEST_DISPATCH_STATE="${state_file}" \
        MIXXX_TEST_REQUEST_LOG="${request_log}" \
        MIXXX_FORGEJO_BASE_URL="https://forge.test" \
        MIXXX_FORGEJO_DISPATCH_POLL_INTERVAL_SECONDS=1 \
        "${client}" dispatch "${timeout}"
}

success_output="$(run_dispatch success)"
jq -e \
    --arg sha "${candidate_sha}" '
        .schema_version == 1 and
        .result == "dispatch-confirmed" and
        .workflow == "deck-flatpak.yml" and
        .event == "workflow_dispatch" and
        .ref == "refs/heads/deck/candidate" and
        .commit_sha == $sha and
        .run_id == 201 and
        .run_number == 42 and
        .status == "waiting" and
        .html_url == "https://forge.test/runs/201"
    ' <<<"${success_output}" >/dev/null
grep -Fq $'POST\thttps://forge.test/api/v1/repos/total-infra/mixxx/actions/workflows/deck-flatpak.yml/dispatches\t{"ref":"deck/candidate","return_run_info":true}' \
    "${test_root}/dispatch-success.requests"
if grep -Fq ".forgejo/workflows" "${test_root}/dispatch-success.requests"; then
    echo "Dispatch must use the bare workflow filename." >&2
    exit 1
fi

empty_event_output="$(run_dispatch live-empty-event)"
jq -e \
    --arg sha "${candidate_sha}" '
        .result == "dispatch-confirmed" and
        .workflow == "deck-flatpak.yml" and
        .event == "workflow_dispatch" and
        .ref == "refs/heads/deck/candidate" and
        .commit_sha == $sha and
        .run_id == 206 and
        .run_number == 46
    ' <<<"${empty_event_output}" >/dev/null
grep -Fq "/actions/tasks?limit=50" "${test_root}/dispatch-live-empty-event.requests"

if run_dispatch active >"${test_root}/active.stdout" 2>"${test_root}/active.stderr"; then
    echo "Expected an active-run refusal." >&2
    exit 1
fi
grep -Fq "refusing duplicate dispatch" "${test_root}/active.stderr"
if grep -Fq $'POST\t' "${test_root}/dispatch-active.requests"; then
    echo "Active-run refusal must happen before POST." >&2
    exit 1
fi

if run_dispatch timeout 1 >"${test_root}/timeout.stdout" 2>"${test_root}/timeout.stderr"; then
    echo "Expected dispatch confirmation timeout." >&2
    exit 1
fi
grep -Fq "timed out after 1 seconds" "${test_root}/timeout.stderr"

for mode in mismatch-sha mismatch-ref mismatch-event; do
    if run_dispatch "${mode}" >"${test_root}/${mode}.stdout" 2>"${test_root}/${mode}.stderr"; then
        echo "Expected ${mode} dispatch confirmation failure." >&2
        exit 1
    fi
    grep -Fq "did not match workflow_dispatch, deck/candidate" "${test_root}/${mode}.stderr"
done

if run_dispatch mismatch-task >"${test_root}/mismatch-task.stdout" 2>"${test_root}/mismatch-task.stderr"; then
    echo "Expected mismatched task confirmation failure." >&2
    exit 1
fi
grep -Fq "task metadata did not confirm workflow_dispatch" "${test_root}/mismatch-task.stderr"

if run_dispatch malformed >"${test_root}/malformed.stdout" 2>"${test_root}/malformed.stderr"; then
    echo "Expected malformed run metadata failure." >&2
    exit 1
fi
grep -Fq "malformed confirmed run metadata" "${test_root}/malformed.stderr"

echo "mixxx-deck-ci contract tests passed."
