#!/bin/bash
# Fail closed when the constrained VPS lacks enough swap/headroom for Mixxx.

set -euo pipefail

MIN_SWAP_KIB="${MIXXX_BUILD_MIN_SWAP_KIB:-4194304}"
MIN_HEADROOM_KIB="${MIXXX_BUILD_MIN_HEADROOM_KIB:-3145728}"
MIN_DATA_DISK_KIB="${MIXXX_BUILD_MIN_DATA_DISK_KIB:-20971520}"
MIN_CGROUP_MEMORY_BYTES="${MIXXX_BUILD_MIN_CGROUP_MEMORY_BYTES:-1073741824}"
MIN_CGROUP_SWAP_BYTES="${MIXXX_BUILD_MIN_CGROUP_SWAP_BYTES:-3221225472}"
DATA_PATH="${MIXXX_BUILD_DATA_PATH:-/data}"

die() {
    echo "Error: $*" >&2
    exit 1
}

for value_name in \
        MIN_SWAP_KIB \
        MIN_HEADROOM_KIB \
        MIN_DATA_DISK_KIB \
        MIN_CGROUP_MEMORY_BYTES \
        MIN_CGROUP_SWAP_BYTES; do
    value="${!value_name}"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] ||
        die "${value_name} must be a positive integer."
done
[[ "${DATA_PATH}" == /* && -d "${DATA_PATH}" ]] ||
    die "MIXXX_BUILD_DATA_PATH must name an existing absolute directory."

mem_total_kib="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)"
mem_available_kib="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
swap_total_kib="$(awk '/^SwapTotal:/ {print $2}' /proc/meminfo)"
swap_free_kib="$(awk '/^SwapFree:/ {print $2}' /proc/meminfo)"
available_headroom_kib=$((mem_available_kib + swap_free_kib))
data_disk_available_kib="$(df -Pk "${DATA_PATH}" | awk 'NR == 2 {print $4}')"

echo "Build host memory: total=${mem_total_kib} KiB available=${mem_available_kib} KiB"
echo "Build host swap: total=${swap_total_kib} KiB free=${swap_free_kib} KiB"
echo "Build host immediate memory+swap headroom: ${available_headroom_kib} KiB"
echo "Build data disk available at ${DATA_PATH}: ${data_disk_available_kib} KiB"

((swap_total_kib >= MIN_SWAP_KIB)) ||
    die "At least ${MIN_SWAP_KIB} KiB of host swap is required for this 2 GiB VPS."
((available_headroom_kib >= MIN_HEADROOM_KIB)) ||
    die "At least ${MIN_HEADROOM_KIB} KiB of free memory+swap is required before building."
((data_disk_available_kib >= MIN_DATA_DISK_KIB)) ||
    die "At least ${MIN_DATA_DISK_KIB} KiB free at ${DATA_PATH} is required before building."

if [[ -r /sys/fs/cgroup/memory.max ]]; then
    cgroup_memory_max="$(</sys/fs/cgroup/memory.max)"
    if [[ "${cgroup_memory_max}" != "max" ]]; then
        [[ "${cgroup_memory_max}" =~ ^[0-9]+$ ]] ||
            die "Unexpected cgroup memory.max value: ${cgroup_memory_max}"
        ((cgroup_memory_max >= MIN_CGROUP_MEMORY_BYTES)) ||
            die "Runner cgroup memory.max is below ${MIN_CGROUP_MEMORY_BYTES} bytes."
    fi
fi

if [[ -r /sys/fs/cgroup/memory.swap.max ]]; then
    cgroup_swap_max="$(</sys/fs/cgroup/memory.swap.max)"
    if [[ "${cgroup_swap_max}" != "max" ]]; then
        [[ "${cgroup_swap_max}" =~ ^[0-9]+$ ]] ||
            die "Unexpected cgroup memory.swap.max value: ${cgroup_swap_max}"
        ((cgroup_swap_max >= MIN_CGROUP_SWAP_BYTES)) ||
            die "Runner cgroup swap allowance is below ${MIN_CGROUP_SWAP_BYTES} bytes."
    fi
fi

if [[ -r /proc/pressure/memory ]]; then
    echo "Current memory pressure:"
    sed -n '1,2p' /proc/pressure/memory
fi

echo "Low-memory build preflight passed. Flatpak compilation must remain at one job."
