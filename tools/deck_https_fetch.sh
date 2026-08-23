#!/bin/bash
# Bounded HTTPS downloads whose initial and effective origins are approved.

deck_https_die() {
    echo "Error: $*" >&2
    return 1
}

deck_https_origin() {
    local url="$1"
    local remainder authority
    [[ "${url}" == https://* ]] || return 1
    remainder="${url#https://}"
    authority="${remainder%%/*}"
    [[ -n "${authority}" && "${authority}" != *'@'* &&
            "${authority}" != *'?'* && "${authority}" != *'#'* &&
            "${authority}" != *[[:space:]]* ]] || return 1
    printf 'https://%s\n' "${authority}"
}

deck_origin_is_approved() {
    local approved_csv="$1"
    local url="$2"
    local origin approved
    origin="$(deck_https_origin "${url}")" || return 1
    IFS=',' read -r -a approved_origins <<<"${approved_csv}"
    for approved in "${approved_origins[@]}"; do
        [[ -n "${approved}" ]] || continue
        if [[ "${approved}" == https://\*.* ]]; then
            local suffix authority
            suffix="${approved#https://\*}"
            authority="${origin#https://}"
            [[ "${authority}" == *"${suffix}" &&
                    "${authority}" != "${suffix#.}" ]] && return 0
        elif [[ "${origin}" == "${approved}" ]]; then
            return 0
        fi
    done
    return 1
}

deck_require_approved_https_url() {
    local approved_csv="$1"
    local url="$2"
    deck_origin_is_approved "${approved_csv}" "${url}" ||
        deck_https_die "URL is not on an approved HTTPS origin: ${url}"
}

deck_curl_download() {
    local approved_csv="$1"
    local max_bytes="$2"
    local output="$3"
    local url="$4"
    shift 4
    local effective_file effective_url
    if [[ ! "${max_bytes}" =~ ^[1-9][0-9]*$ ]]; then
        deck_https_die "Download limit must be a positive integer."
        return 1
    fi
    deck_require_approved_https_url "${approved_csv}" "${url}" || return 1
    effective_file="$(mktemp)"
    if ! curl "$@" \
            --fail --location --max-redirs 3 \
            --proto '=https' --proto-redir '=https' \
            --max-filesize "${max_bytes}" \
            --silent --show-error \
            --output "${output}" \
            --write-out '%{url_effective}\n' \
            "${url}" >"${effective_file}"; then
        rm -f -- "${output}" "${effective_file}"
        return 1
    fi
    effective_url="$(tail -n 1 "${effective_file}")"
    rm -f -- "${effective_file}"
    if ! deck_origin_is_approved "${approved_csv}" "${effective_url}"; then
        rm -f -- "${output}"
        deck_https_die "Download redirected outside the approved HTTPS origins: ${effective_url}"
    fi
}
