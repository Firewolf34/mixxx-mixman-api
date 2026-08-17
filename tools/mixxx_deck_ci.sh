#!/bin/bash
# Inspect and dispatch the authoritative Forgejo Actions deck workflow.
# This is the CI companion to mixxx-deck, not an issue-tracker client.

set -euo pipefail

FORGEJO_BASE_URL="${MIXXX_FORGEJO_BASE_URL:-https://forge.polinaria.world}"
FORGEJO_OWNER="${MIXXX_FORGEJO_OWNER:-total-infra}"
FORGEJO_REPO="${MIXXX_FORGEJO_REPO:-mixxx}"
TOKEN_FILE="${MIXXX_FORGEJO_TOKEN_FILE:-${HOME}/.config/mixxx-deck/forgejo-actions-token}"
WORKFLOW_FILE="${MIXXX_FORGEJO_WORKFLOW:-deck-flatpak.yml}"
EXPECTED_REF="refs/heads/deck/candidate"
AUTH_CONFIG=""

usage() {
    cat <<'EOF'
Usage:
  mixxx-deck-ci configure
  mixxx-deck-ci runs [candidate-sha]
  mixxx-deck-ci status <candidate-sha>
  mixxx-deck-ci tasks <candidate-sha>
  mixxx-deck-ci wait <candidate-sha> [timeout-seconds]
  mixxx-deck-ci dispatch
  mixxx-deck-ci publication <candidate-sha>

Authentication:
  Store a Forgejo token as one line in:
    ~/.config/mixxx-deck/forgejo-actions-token

  The file must not be group/world accessible. Use a token restricted to
  total-infra/mixxx. read:repository is enough for inspection; write:repository is
  required for dispatch.

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

cleanup() {
    if [[ -n "${AUTH_CONFIG}" ]]; then
        rm -f -- "${AUTH_CONFIG}"
    fi
}
trap cleanup EXIT

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
    api_request GET \
        "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/actions/tasks?limit=50" |
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
    api_request POST \
        "/repos/${FORGEJO_OWNER}/${FORGEJO_REPO}/actions/workflows/${WORKFLOW_FILE}/dispatches" \
        '{"ref":"deck/candidate","return_run_info":true}'
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
            [[ $# -eq 1 ]] || { usage; exit 2; }
            prepare_auth
            command_dispatch
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
