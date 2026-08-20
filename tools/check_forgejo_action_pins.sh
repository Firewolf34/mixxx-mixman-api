#!/bin/bash
# Reject mutable third-party action references in Forgejo workflows.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
WORKFLOW_DIR="${1:-${REPO_ROOT}/.forgejo/workflows}"

if [[ $# -gt 1 ]]; then
    echo "Usage: ${0##*/} [workflow-directory]" >&2
    exit 2
fi

if [[ ! -d "${WORKFLOW_DIR}" ]]; then
    echo "Error: Forgejo workflow directory does not exist: ${WORKFLOW_DIR}" >&2
    exit 1
fi

shopt -s globstar nullglob
workflow_files=("${WORKFLOW_DIR}"/**/*.yml "${WORKFLOW_DIR}"/**/*.yaml)
if [[ ${#workflow_files[@]} -eq 0 ]]; then
    echo "Error: no Forgejo workflow files found in ${WORKFLOW_DIR}" >&2
    exit 1
fi

failed=0
references=0
for workflow_file in "${workflow_files[@]}"; do
    [[ -f "${workflow_file}" ]] || continue
    line_number=0
    while IFS= read -r line || [[ -n "${line}" ]]; do
        line_number=$((line_number + 1))
        if [[ ! "${line}" =~ ^[[:space:]]*(-[[:space:]]*)?uses[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            continue
        fi

        value="${BASH_REMATCH[2]}"
        value="${value%%[[:space:]]#*}"
        value="${value#"${value%%[![:space:]]*}"}"
        value="${value%"${value##*[![:space:]]}"}"
        if [[ "${value}" == \"*\" && "${value}" == *\" ]] ||
                [[ "${value}" == \'*\' && "${value}" == *\' ]]; then
            value="${value:1:${#value}-2}"
        fi

        references=$((references + 1))
        if [[ "${value}" == ./* ]]; then
            continue
        fi
        if [[ "${value}" =~ ^docker://[^[:space:]@]+@sha256:[0-9a-f]{64}$ ]]; then
            continue
        fi
        if [[ "${value}" =~ ^[^[:space:]@]+@[0-9a-f]{40}$ ]]; then
            continue
        fi

        echo "Error: ${workflow_file}:${line_number}: mutable or non-reviewable" \
            "action reference: ${value}" >&2
        failed=1
    done <"${workflow_file}"
done

if ((failed)); then
    exit 1
fi

echo "Forgejo action references are immutable (${references} checked)."
