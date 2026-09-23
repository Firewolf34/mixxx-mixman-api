#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEMPORARY="$(mktemp -d)"
trap 'rm -rf -- "${TEMPORARY}"' EXIT
mkdir -p "${TEMPORARY}/bin"

cat >"${TEMPORARY}/bin/hosted-deploy" <<'EOF'
#!/bin/bash
set -euo pipefail
[[ "$1" == "lease.status" ]]
jq -e '
    .lease == "fixture-lease" and
    .run_id == "42" and
    .sha == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
' >/dev/null
[[ "${FIXTURE_GATEWAY_OUTCOME:-success}" == success ]]
EOF
chmod 0755 "${TEMPORARY}/bin/hosted-deploy"
printf '%s\n' fixture-lease >"${TEMPORARY}/lease"

env \
    PATH="${TEMPORARY}/bin:${PATH}" \
    HOSTED_DEPLOY_LEASE_FILE="${TEMPORARY}/lease" \
    HOSTED_DEPLOY_RUN_ID=42 \
    HOSTED_DEPLOY_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    "${SCRIPT_DIR}/check_hosted_capacity_lease.sh"

if env \
        PATH="${TEMPORARY}/bin:${PATH}" \
        HOSTED_DEPLOY_LEASE_FILE="${TEMPORARY}/missing" \
        HOSTED_DEPLOY_RUN_ID=42 \
        HOSTED_DEPLOY_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
        "${SCRIPT_DIR}/check_hosted_capacity_lease.sh" 2>/dev/null; then
    echo "missing lease was accepted" >&2
    exit 1
fi

if env \
        PATH="${TEMPORARY}/bin:${PATH}" \
        FIXTURE_GATEWAY_OUTCOME=failure \
        HOSTED_DEPLOY_LEASE_FILE="${TEMPORARY}/lease" \
        HOSTED_DEPLOY_RUN_ID=42 \
        HOSTED_DEPLOY_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
        "${SCRIPT_DIR}/check_hosted_capacity_lease.sh" 2>/dev/null; then
    echo "invalid gateway lease was accepted" >&2
    exit 1
fi

WORKFLOW="${SCRIPT_DIR}/../.forgejo/workflows/deck-flatpak.yml"
RELEASE_JOB="$(sed -n '/^  release-capacity:/,$p' "${WORKFLOW}")"

grep -Fq 'for attempt in 1 2 3; do' <<<"${RELEASE_JOB}"
grep -Fq 'if [ "$attempt" -lt 3 ]; then' <<<"${RELEASE_JOB}"
grep -Fq 'timeout 30s hosted-deploy lease.release >/dev/null' <<<"${RELEASE_JOB}"
if grep -Fq 'hosted-deploy lease.release >/dev/null || true' <<<"${RELEASE_JOB}"; then
    echo "capacity release failures are suppressed" >&2
    exit 1
fi
if grep -Fq '/usr/local/bin/hosted-deploy-lease-guard' <<<"${RELEASE_JOB}"; then
    echo "release cleanup is incorrectly wrapped by the lease guard" >&2
    exit 1
fi
failure_line="$(grep -nF 'if [ "$release_confirmed" != true ]; then' <<<"${RELEASE_JOB}" | cut -d: -f1)"
removal_line="$(grep -nF 'rm -f -- "$HOSTED_DEPLOY_LEASE_FILE"' <<<"${RELEASE_JOB}" | cut -d: -f1)"
[[ -n "${failure_line}" && -n "${removal_line}" && "${failure_line}" -lt "${removal_line}" ]]
grep -Fq 'HOSTED_DEPLOY_LEASE_FILE: /data/locks/hosted-deploy-${{ forgejo.run_id }}.lease' "${WORKFLOW}"
if grep -Fq 'HOSTED_DEPLOY_LEASE_FILE: /data/tmp/' "${WORKFLOW}"; then
    echo "capacity lease is stored in transient cleanup space" >&2
    exit 1
fi
checkout_line="$(grep -nF 'uses: https://data.forgejo.org/actions/checkout@' <<<"${RELEASE_JOB}" | tail -1 | cut -d: -f1)"
cleanup_line="$(grep -nF 'tools/deck_runner_cache_cleanup.sh --finalize' <<<"${RELEASE_JOB}" | cut -d: -f1)"
[[ -n "${checkout_line}" && -n "${cleanup_line}" && "${removal_line}" -lt "${checkout_line}" && "${checkout_line}" -lt "${cleanup_line}" ]]
[[ "$(grep -Fc 'if: always()' <<<"${RELEASE_JOB}")" -eq 1 ]]
