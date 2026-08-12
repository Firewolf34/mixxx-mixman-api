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
