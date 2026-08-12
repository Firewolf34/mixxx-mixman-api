#!/bin/bash
# Require the root-owned TotalInfra broker's lease before heavy VPS work.

set -euo pipefail

LEASE_FILE="${HOSTED_DEPLOY_LEASE_FILE:-}"
RUN_ID="${HOSTED_DEPLOY_RUN_ID:-}"
SOURCE_SHA="${HOSTED_DEPLOY_SHA:-}"

die() {
    echo "Error: $*" >&2
    exit 1
}

[[ -n "${LEASE_FILE}" && "${LEASE_FILE}" == /* ]] ||
    die "HOSTED_DEPLOY_LEASE_FILE must name an absolute lease file."
[[ -r "${LEASE_FILE}" && -s "${LEASE_FILE}" ]] ||
    die "The shared hosted-deployment capacity lease is absent."
[[ "${RUN_ID}" =~ ^[1-9][0-9]{0,18}$ ]] ||
    die "HOSTED_DEPLOY_RUN_ID is invalid."
[[ "${SOURCE_SHA}" =~ ^[0-9a-f]{40}$ ]] ||
    die "HOSTED_DEPLOY_SHA is invalid."
command -v hosted-deploy >/dev/null 2>&1 ||
    die "The TotalInfra hosted-deploy client is unavailable."
command -v jq >/dev/null 2>&1 || die "jq is unavailable."

jq -n \
    --arg lease "$(<"${LEASE_FILE}")" \
    --arg run_id "${RUN_ID}" \
    --arg sha "${SOURCE_SHA}" \
    '{lease:$lease,run_id:$run_id,sha:$sha}' |
    hosted-deploy lease.status >/dev/null ||
    die "The shared hosted-deployment capacity lease is not valid for this run."
