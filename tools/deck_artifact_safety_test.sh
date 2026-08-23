#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TEMPORARY="$(mktemp -d)"
trap 'rm -rf -- "${TEMPORARY}"' EXIT

# shellcheck source=tools/deck_storage_budget.sh
source "${SCRIPT_DIR}/deck_storage_budget.sh"
# shellcheck source=tools/deck_https_fetch.sh
source "${SCRIPT_DIR}/deck_https_fetch.sh"
# shellcheck source=tools/deck_tracked_source.sh
source "${SCRIPT_DIR}/deck_tracked_source.sh"
# shellcheck source=tools/deck_signed_candidate.sh
source "${SCRIPT_DIR}/deck_signed_candidate.sh"

deck_validate_storage_limits
mkdir -p "${TEMPORARY}/space"
deck_require_size_within_budget 1024 fixture
if deck_require_size_within_budget $((DECK_MAX_ARTIFACT_BYTES + 1)) fixture \
        2>/dev/null; then
    echo "oversized artifact was accepted" >&2
    exit 1
fi

mkdir -p "${TEMPORARY}/bin"
cat >"${TEMPORARY}/bin/df" <<'EOF'
#!/bin/bash
printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
printf 'fixture 9999999 0 %s 0%% /fixture\n' "${TEST_AVAILABLE_KIB}"
EOF
chmod 0755 "${TEMPORARY}/bin/df"
if PATH="${TEMPORARY}/bin:${PATH}" TEST_AVAILABLE_KIB=1 \
        deck_require_free_space "${TEMPORARY}/space" 1024 fixture 2>/dev/null; then
    echo "low-space operation was accepted" >&2
    exit 1
fi

repo="${TEMPORARY}/repo"
mkdir -p "${repo}"
git -C "${repo}" init -q
git -C "${repo}" config user.name fixture
git -C "${repo}" config user.email fixture@example.invalid
printf 'tracked\n' >"${repo}/tracked.txt"
printf 'ignored.txt\n' >"${repo}/.gitignore"
git -C "${repo}" add .gitignore tracked.txt
git -C "${repo}" commit -qm fixture
revision="$(git -C "${repo}" rev-parse HEAD)"
deck_require_pristine_build_checkout "${repo}"
deck_export_tracked_source "${repo}" "${revision}" "${TEMPORARY}/export"
[[ "$(<"${TEMPORARY}/export/tracked.txt")" == tracked ]]
[[ ! -e "${TEMPORARY}/export/.git" ]]

printf 'injected\n' >"${repo}/controller.js"
if deck_require_pristine_build_checkout "${repo}" 2>/dev/null; then
    echo "untracked build input was accepted" >&2
    exit 1
fi
rm -f -- "${repo}/controller.js"
printf 'ignored\n' >"${repo}/ignored.txt"
if deck_require_pristine_build_checkout "${repo}" 2>/dev/null; then
    echo "ignored build input was accepted" >&2
    exit 1
fi

cat >"${TEMPORARY}/bin/curl" <<'EOF'
#!/bin/bash
set -euo pipefail
output=""
write_out=""
url="${!#}"
while (($#)); do
    case "$1" in
        --output) output="$2"; shift 2 ;;
        --write-out) write_out="$2"; shift 2 ;;
        *) shift ;;
    esac
done
printf 'fixture\n' >"${output}"
printf '%s\n' "${TEST_EFFECTIVE_URL:-${url}}"
EOF
chmod 0755 "${TEMPORARY}/bin/curl"
PATH="${TEMPORARY}/bin:${PATH}" TEST_EFFECTIVE_URL=https://artifacts.example/build \
    deck_curl_download https://artifacts.example 1024 \
        "${TEMPORARY}/same-origin" https://artifacts.example/latest.json
[[ -s "${TEMPORARY}/same-origin" ]]
if PATH="${TEMPORARY}/bin:${PATH}" deck_curl_download \
        https://artifacts.example 1024 "${TEMPORARY}/initial-http" \
        http://artifacts.example/latest.json 2>/dev/null; then
    echo "initial HTTP URL was accepted" >&2
    exit 1
fi
if PATH="${TEMPORARY}/bin:${PATH}" deck_curl_download \
        https://artifacts.example 1024 "${TEMPORARY}/initial-cross-origin" \
        https://evil.example/latest.json 2>/dev/null; then
    echo "initial cross-origin URL was accepted" >&2
    exit 1
fi
if PATH="${TEMPORARY}/bin:${PATH}" \
        TEST_EFFECTIVE_URL=http://artifacts.example/build \
        deck_curl_download https://artifacts.example 1024 \
            "${TEMPORARY}/downgrade" https://artifacts.example/latest.json \
            2>/dev/null; then
    echo "HTTPS downgrade was accepted" >&2
    exit 1
fi
[[ ! -e "${TEMPORARY}/downgrade" ]]
if PATH="${TEMPORARY}/bin:${PATH}" \
        TEST_EFFECTIVE_URL=https://evil.example/build \
        deck_curl_download https://artifacts.example 1024 \
            "${TEMPORARY}/cross-origin" https://artifacts.example/latest.json \
            2>/dev/null; then
    echo "cross-origin redirect was accepted" >&2
    exit 1
fi
[[ ! -e "${TEMPORARY}/cross-origin" ]]

cat >"${TEMPORARY}/bin/ostree" <<'EOF'
#!/bin/bash
set -euo pipefail
case "$*" in
    *" remote refs --revision "*)
        printf 'polinaria-mixxx:mixxx/history/%s %s\n' \
            "${TEST_SOURCE_SHA}" "${TEST_SIGNED_COMMIT}"
        ;;
    *" pull "*) ;;
    *" rev-parse "*) printf '%s\n' "${TEST_SIGNED_COMMIT}" ;;
    *" show "*) [[ "${TEST_GPG_VALID:-yes}" == yes ]] ;;
    *) echo "unexpected ostree command: $*" >&2; exit 2 ;;
esac
EOF
chmod 0755 "${TEMPORARY}/bin/ostree"
mkdir -p "${TEMPORARY}/flatpak-repo"
export TEST_SOURCE_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
export TEST_SIGNED_COMMIT=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
PATH="${TEMPORARY}/bin:${PATH}" deck_require_signed_bundle_commit \
    "${TEMPORARY}/flatpak-repo" polinaria-mixxx \
    "${TEST_SOURCE_SHA}" "${TEST_SIGNED_COMMIT}"
if PATH="${TEMPORARY}/bin:${PATH}" deck_require_signed_bundle_commit \
        "${TEMPORARY}/flatpak-repo" polinaria-mixxx \
        "${TEST_SOURCE_SHA}" \
        cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc \
        2>/dev/null; then
    echo "bundle commit mismatch was accepted" >&2
    exit 1
fi
export TEST_GPG_VALID=no
if PATH="${TEMPORARY}/bin:${PATH}" deck_require_signed_bundle_commit \
        "${TEMPORARY}/flatpak-repo" polinaria-mixxx \
        "${TEST_SOURCE_SHA}" "${TEST_SIGNED_COMMIT}" 2>/dev/null; then
    echo "unsigned candidate commit was accepted" >&2
    exit 1
fi
unset TEST_GPG_VALID

echo "Deck artifact safety tests passed."
