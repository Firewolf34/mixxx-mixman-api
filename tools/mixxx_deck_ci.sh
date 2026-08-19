#!/bin/bash
# Inspect and dispatch the authoritative Forgejo Actions deck workflow.
# This is the CI companion to mixxx-deck, not an issue-tracker client.

set -euo pipefail

FORGEJO_BASE_URL="${MIXXX_FORGEJO_BASE_URL:-https://forge.polinaria.world}"
FORGEJO_OWNER="${MIXXX_FORGEJO_OWNER:-total-infra}"
FORGEJO_REPO="${MIXXX_FORGEJO_REPO:-mixxx}"
TOKEN_FILE="${MIXXX_FORGEJO_TOKEN_FILE:-${HOME}/.config/mixxx-deck/forgejo-actions-token}"
WORKFLOW_FILE="${MIXXX_FORGEJO_WORKFLOW:-deck-flatpak.yml}"
INSTALL_PATH="${MIXXX_FORGEJO_CI_INSTALL_PATH:-${HOME}/.local/bin/mixxx-deck-ci}"
DISPATCH_CONFIRM_TIMEOUT_SECONDS="${MIXXX_FORGEJO_DISPATCH_CONFIRM_TIMEOUT_SECONDS:-90}"
DISPATCH_POLL_INTERVAL_SECONDS="${MIXXX_FORGEJO_DISPATCH_POLL_INTERVAL_SECONDS:-5}"
CANDIDATE_BRANCH="deck/candidate"
EXPECTED_REF="refs/heads/deck/candidate"
AUTH_CONFIG=""
INSTALL_TEMP=""

usage() {
    cat <<'EOF'
Usage:
  mixxx-deck-ci install
  mixxx-deck-ci configure
  mixxx-deck-ci runs [candidate-sha]
  mixxx-deck-ci status <candidate-sha>
  mixxx-deck-ci tasks <candidate-sha>
  mixxx-deck-ci wait <candidate-sha> [timeout-seconds]
  mixxx-deck-ci dispatch [confirmation-timeout-seconds]
  mixxx-deck-ci publication <candidate-sha>

Authentication:
  Store a Forgejo token as one line in:
    ~/.config/mixxx-deck/forgejo-actions-token

  The file must not be group/world accessible. Use a token restricted to
  total-infra/mixxx. read:repository is enough for inspection; write:repository is
  required for dispatch. Configure a separate token on each machine; never copy
  a token between workstations.

Install:
  install writes this command atomically to ~/.local/bin/mixxx-deck-ci. It does
  not install deck services, use root, or create/change a credential.

Dispatch:
  dispatch resolves the exact deck/candidate SHA, refuses a duplicate active
  run, requests deck-flatpak.yml by its bare workflow filename, and succeeds
  only after a new workflow_dispatch run for that exact ref and SHA is visible.

Tool boundaries:
  Use mixxx-deck for signed artifact staging, activation, rollback, updates,
  and launching Mixxx. Use forgejo-issues for Forgejo issue tickets. This
  command is only for the total-infra/mixxx Forgejo Actions workflow.
EOF
}

die() {
    echo "Error: $*" >&2
    exit 1
}

require_sha() {
    [[ "${1:-}" =~ ^[0-9a-f]{40}$ ]] ||
        die "candidate SHA must be exactly 40 lowercase hexadecimal characters"
}

require_positive_integer() {
    local value="$1"
    local description="$2"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] ||
        die "${description} must be a positive integer number of seconds"
}

cleanup() {
    if [[ -n "${AUTH_CONFIG}" ]]; then
        rm -f -- "${AUTH_CONFIG}"
    fi
    if [[ -n "${INSTALL_TEMP}" ]]; then
        rm -f -- "${INSTALL_TEMP}"
    fi
}
trap cleanup EXIT

command_install() {
    ((EUID != 0)) || die "install must be run as a non-root user"
    [[ "${INSTALL_PATH}" == /* ]] || die "install path must be absolute"

    local install_dir
    install_dir="${INSTALL_PATH%/*}"
    mkdir -p -- "${install_dir}"
    INSTALL_TEMP="$(mktemp "${install_dir}/.mixxx-deck-ci.XXXXXX")"
    install -m 0755 "${BASH_SOURCE[0]}" "${INSTALL_TEMP}"
    mv -f -- "${INSTALL_TEMP}" "${INSTALL_PATH}"
    INSTALL_TEMP=""
    echo "Installed mixxx-deck-ci at ${INSTALL_PATH}."
    echo "No credential was created or changed. Run 'mixxx-deck-ci configure' on this machine if needed."
}

prepare_auth() {
    [[ -f "${TOKEN_FILE}" ]] ||
        die "Forgejo token file is missing: ${TOKEN_FILE}"

    local mode token
    mode="$(stat -c '%a' "${TOKEN_FILE}")"
    (( (8#${mode} & 077) == 0 )) ||
        die "Forgejo token file must not be accessible to group or others"

    IFS= read -r token <"${TOKEN_FILE}"
    [[ "${token}" =~ ^[A-Za-z0-9._-]{20,}$ ]] ||
        die "Forgejo token file does not contain a plausible single-line token"

    AUTH_CONFIG="$(mktemp)"
    chmod 600 "${AUTH_CONFIG}"
    printf 'header = "Authorization: token %s"\n' "${token}" >"${AUTH_CONFIG}"
    unset token
}

command_configure() {
    local token token_dir
    token_dir="${TOKEN_FILE%/*}"
    mkdir -p -- "${token_dir}"
    chmod 700 "${token_dir}"

    echo "Paste the repository-restricted Forgejo token, then press Enter." >&2
    IFS= read -r -s token </dev/tty
    echo >&2
    [[ "${token}" =~ ^[A-Za-z0-9._-]{20,}$ ]] ||
        die "token does not have the expected format"

    (
        umask 077
        printf '%s\n' "${token}" >"${TOKEN_FILE}"
    )
    unset token
    chmod 600 "${TOKEN_FILE}"

    prepare_auth
    if ! api_request GET "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}" >/dev/null; then
        rm -f -- "${TOKEN_FILE}"
        die "Forgejo rejected the token; the local token file was removed"
    fi
    echo "Forgejo API authentication succeeded for ${FORGEJO_OWNER}/${FORGEJO_REPO}."
}

api_request() {
    local method="$1"
    local path="$2"
    local body="${3:-}"
    local -a args=(
        --config "${AUTH_CONFIG}"
        --silent
        --show-error
        --fail
        --connect-timeout 15
        --request "${method}"
        --header "Accept: application/json"
    )
    if [[ -n "${body}" ]]; then
        args+=(--header "Content-Type: application/json" --data-binary "${body}")
    fi
    curl "${args[@]}" "${FORGEJO_BASE_URL}/api/v1${path}"
}

runs_json() {
    local sha="${1:-}"
    local path="/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/actions/runs?limit=50&ref=refs%2Fheads%2Fdeck%2Fcandidate"
    if [[ -n "${sha}" ]]; then
        require_sha "${sha}"
        path+="&head_sha=${sha}"
    fi
    api_request GET "${path}"
}

tasks_json() {
    api_request GET \
        "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/actions/tasks?limit=50"
}

candidate_sha() {
    local response sha
    response="$(api_request GET \
        "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/branches/deck%2Fcandidate")"
    if ! sha="$(jq -er '.commit.id | select(type == "string")' <<<"${response}")"; then
        die "Forgejo returned malformed deck/candidate branch metadata"
    fi
    require_sha "${sha}"
    printf '%s\n' "${sha}"
}

run_for_sha() {
    local sha="$1"
    runs_json "${sha}" |
        jq --arg sha "${sha}" '
            [.workflow_runs[] | select(.commit_sha == $sha)]
            | if length == 0 then
                error("no Forgejo Actions run found for candidate " + $sha)
              else
                max_by(.created)
              end
        '
}

command_runs() {
    local sha="${1:-}"
    runs_json "${sha}" |
        jq --arg sha "${sha}" '
            .workflow_runs
            | map(select(($sha == "") or (.commit_sha == $sha)))
            | sort_by(.created)
            | reverse
            | map({
                id,
                run_number: .index_in_repo,
                status,
                event,
                ref: .prettyref,
                commit_sha,
                workflow_id,
                created,
                started,
                stopped,
                html_url
              })
        '
}

command_status() {
    local sha="$1"
    require_sha "${sha}"
    run_for_sha "${sha}" |
        jq '{
            id,
            run_number: .index_in_repo,
            status,
            event,
            ref: .prettyref,
            commit_sha,
            workflow_id,
            created,
            started,
            stopped,
            duration,
            html_url
        }'
}

command_tasks() {
    local sha="$1"
    require_sha "${sha}"
    tasks_json |
        jq --arg sha "${sha}" '
            [.workflow_runs[] | select(.head_sha == $sha)]
            | sort_by(.created_at)
            | reverse
            | map({
                id,
                run_number,
                status,
                event,
                head_branch,
                head_sha,
                workflow_id,
                name,
                run_started_at,
                updated_at,
                url
              })
        '
}

command_wait() {
    local sha="$1"
    local timeout_seconds="${2:-86400}"
    require_sha "${sha}"
    [[ "${timeout_seconds}" =~ ^[1-9][0-9]*$ ]] ||
        die "timeout must be a positive integer number of seconds"

    local deadline status run
    deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if run="$(run_for_sha "${sha}" 2>/dev/null)"; then
            status="$(jq -r '.status' <<<"${run}")"
            jq '{
                run_number: .index_in_repo,
                status,
                commit_sha,
                updated,
                html_url
            }' <<<"${run}"
            case "${status}" in
                success)
                    command_publication "${sha}"
                    return 0
                    ;;
                failure|cancelled|skipped|blocked)
                    return 1
                    ;;
            esac
        else
            echo "No run found yet for ${sha}; waiting..." >&2
        fi
        sleep 30
    done
    die "timed out waiting for Forgejo Actions run ${sha}"
}

command_dispatch() {
    local timeout_seconds="${1:-${DISPATCH_CONFIRM_TIMEOUT_SECONDS}}"
    require_positive_integer "${timeout_seconds}" "dispatch confirmation timeout"
    require_positive_integer "${DISPATCH_POLL_INTERVAL_SECONDS}" "dispatch poll interval"
    [[ "${WORKFLOW_FILE}" != */* ]] ||
        die "workflow must be a bare filename, not a repository path"
    [[ "${WORKFLOW_FILE}" =~ ^[A-Za-z0-9._-]+\.ya?ml$ ]] ||
        die "workflow filename is invalid"

    local sha before_runs before_ids active_runs response deadline current_runs new_runs
    local run summary_run tasks matching_tasks run_number
    sha="$(candidate_sha)"
    before_runs="$(runs_json "${sha}")"
    jq -e '.workflow_runs | type == "array"' <<<"${before_runs}" >/dev/null ||
        die "Forgejo returned malformed Actions run data before dispatch"
    before_ids="$(jq -c '[.workflow_runs[] | .id | select(type == "number")] | unique' \
        <<<"${before_runs}")"
    active_runs="$(jq -c --arg sha "${sha}" '
        def candidate_ref:
            .prettyref == "deck/candidate" or
            .head_branch == "deck/candidate" or
            .ref == "refs/heads/deck/candidate";
        [.workflow_runs[]
         | select(.commit_sha == $sha and candidate_ref)
         | select(.status != "success" and
                  .status != "failure" and
                  .status != "cancelled" and
                  .status != "skipped" and
                  .status != "blocked")
         | {id, status, event, ref: (.prettyref // .head_branch // .ref)}]
        ' <<<"${before_runs}")"
    if [[ "$(jq 'length' <<<"${active_runs}")" -ne 0 ]]; then
        jq '{error: "active-candidate-run", active_runs: .}' <<<"${active_runs}" >&2
        die "deck/candidate ${sha} already has a nonterminal Actions run; refusing duplicate dispatch"
    fi

    response="$(api_request POST \
        "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/actions/workflows/${WORKFLOW_FILE}/dispatches" \
        "{\"ref\":\"${CANDIDATE_BRANCH}\",\"return_run_info\":true}")"
    : "${response}"

    deadline=$((SECONDS + timeout_seconds))
    while :; do
        current_runs="$(runs_json "${sha}")"
        jq -e '.workflow_runs | type == "array"' <<<"${current_runs}" >/dev/null ||
            die "Forgejo returned malformed Actions run data after dispatch"
        new_runs="$(jq -c --argjson before_ids "${before_ids}" '
            [.workflow_runs[]
             | select(.id | type == "number")
             | select(.id as $id | ($before_ids | index($id) | not))]
            | sort_by(.created // "")
            | reverse
            ' <<<"${current_runs}")"
        if [[ "$(jq 'length' <<<"${new_runs}")" -ne 0 ]]; then
            run="$(jq -ce --arg sha "${sha}" --arg workflow "${WORKFLOW_FILE}" '
                def candidate_ref:
                    .prettyref == "deck/candidate" or
                    .head_branch == "deck/candidate" or
                    .ref == "refs/heads/deck/candidate";
                [.[]
                 | select(.event == "workflow_dispatch")
                 | select(.commit_sha == $sha)
                 | select(.workflow_id == $workflow)
                 | select(candidate_ref)]
                | first // empty
                ' <<<"${new_runs}" 2>/dev/null || true)"

            summary_run=""
            if [[ -z "${run}" ]]; then
                summary_run="$(jq -ce --arg sha "${sha}" --arg workflow "${WORKFLOW_FILE}" '
                    def candidate_ref:
                        .prettyref == "deck/candidate" or
                        .head_branch == "deck/candidate" or
                        .ref == "refs/heads/deck/candidate";
                    [.[]
                     | select(.event == "")
                     | select(.commit_sha == $sha)
                     | select(.workflow_id == $workflow)
                     | select(candidate_ref)]
                    | first // empty
                    ' <<<"${new_runs}" 2>/dev/null || true)"
                if [[ -n "${summary_run}" ]]; then
                    run_number="$(jq -er '.index_in_repo | select(type == "number" and . > 0)' \
                        <<<"${summary_run}")" ||
                        die "Forgejo returned malformed empty-event run metadata"
                    tasks="$(tasks_json)"
                    jq -e '.workflow_runs | type == "array"' <<<"${tasks}" >/dev/null ||
                        die "Forgejo returned malformed Actions task data after dispatch"
                    matching_tasks="$(jq -c \
                        --argjson run_number "${run_number}" \
                        '[.workflow_runs[] | select(.run_number == $run_number)]' \
                        <<<"${tasks}")"
                    if [[ "$(jq 'length' <<<"${matching_tasks}")" -ne 0 ]]; then
                        if jq -e \
                                --arg sha "${sha}" \
                                --arg branch "${CANDIDATE_BRANCH}" \
                                --arg workflow "${WORKFLOW_FILE}" '
                                    any(.[];
                                        .event == "workflow_dispatch" and
                                        .head_branch == $branch and
                                        .head_sha == $sha and
                                        .workflow_id == $workflow)
                                ' <<<"${matching_tasks}" >/dev/null; then
                            run="$(jq '.event = "workflow_dispatch"' <<<"${summary_run}")"
                        else
                            jq '{error: "dispatch-task-mismatch", observed_tasks: map({id, run_number, event, status, head_branch, head_sha, workflow_id, url})}' \
                                <<<"${matching_tasks}" >&2
                            die "Forgejo task metadata did not confirm workflow_dispatch, deck/candidate, ${WORKFLOW_FILE}, and ${sha}"
                        fi
                    fi
                fi
            fi

            if [[ -n "${run}" ]]; then
                jq -e '
                    (.id | type == "number" and . > 0) and
                    (.index_in_repo | type == "number" and . > 0) and
                    (.status | type == "string" and length > 0) and
                    (.html_url | type == "string" and length > 0)
                    ' <<<"${run}" >/dev/null || {
                    jq '{error: "malformed-confirmed-run", observed_run: {id, index_in_repo, status, event, prettyref, commit_sha, workflow_id, html_url}}' \
                        <<<"${run}" >&2
                    die "Forgejo returned malformed confirmed run metadata"
                }
                jq \
                    --arg workflow "${WORKFLOW_FILE}" \
                    --arg ref "${EXPECTED_REF}" \
                    '{
                        schema_version: 1,
                        result: "dispatch-confirmed",
                        workflow: $workflow,
                        event,
                        ref: $ref,
                        commit_sha,
                        run_id: .id,
                        run_number: .index_in_repo,
                        status,
                        html_url
                    }' <<<"${run}"
                return 0
            fi

            if [[ -z "${summary_run}" ]]; then
                jq '{error: "dispatch-run-mismatch", observed_new_runs: map({id, event, status, commit_sha, workflow_id, ref: (.prettyref // .head_branch // .ref)})}' \
                    <<<"${new_runs}" >&2
                die "Forgejo created a new run, but its run/task metadata did not match workflow_dispatch, deck/candidate, ${WORKFLOW_FILE}, and ${sha}"
            fi
        fi
        ((SECONDS < deadline)) || break
        sleep "${DISPATCH_POLL_INTERVAL_SECONDS}"
    done
    die "timed out after ${timeout_seconds} seconds waiting for a new exact-SHA Forgejo Actions run"
}

command_publication() {
    local sha="$1"
    require_sha "${sha}"

    local manifest
    manifest="$(mktemp)"
    curl --silent --show-error --fail --connect-timeout 15 \
        --output "${manifest}" \
        "${FORGEJO_BASE_URL}/artifacts/latest.json"

    jq -e \
        --arg sha "${sha}" \
        --arg ref "${EXPECTED_REF}" '
            .schema_version == 1 and
            .channel == "deck-candidate" and
            .app_id == "org.mixxx.Mixxx" and
            .arch == "x86_64" and
            .source_sha == $sha and
            .source_ref == $ref and
            (.sha256 | test("^[0-9a-f]{64}$")) and
            (.source_sha256 | test("^[0-9a-f]{64}$")) and
            (.size_bytes | type == "number" and . > 0)
        ' "${manifest}" >/dev/null ||
        die "published manifest does not match candidate contract"

    jq . "${manifest}"
    local name
    for name in Mixxx.flatpak manifest.json source.tar.zst; do
        curl --silent --show-error --fail --head --connect-timeout 15 \
            "${FORGEJO_BASE_URL}/artifacts/builds/${sha}/${name}" >/dev/null
    done
    rm -f -- "${manifest}"
    echo "Publication validation succeeded for ${sha}."
}

main() {
    local command="${1:-}"
    case "${command}" in
        install)
            [[ $# -eq 1 ]] || { usage; exit 2; }
            command_install
            ;;
        configure)
            [[ $# -eq 1 ]] || { usage; exit 2; }
            command_configure
            ;;
        runs)
            prepare_auth
            command_runs "${2:-}"
            ;;
        status)
            [[ $# -eq 2 ]] || { usage; exit 2; }
            prepare_auth
            command_status "$2"
            ;;
        tasks)
            [[ $# -eq 2 ]] || { usage; exit 2; }
            prepare_auth
            command_tasks "$2"
            ;;
        wait)
            [[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
            prepare_auth
            command_wait "$2" "${3:-86400}"
            ;;
        dispatch)
            [[ $# -le 2 ]] || { usage; exit 2; }
            prepare_auth
            command_dispatch "${2:-${DISPATCH_CONFIRM_TIMEOUT_SECONDS}}"
            ;;
        publication)
            [[ $# -eq 2 ]] || { usage; exit 2; }
            command_publication "$2"
            ;;
        help|-h|--help|"")
            usage
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
}

main "$@"
