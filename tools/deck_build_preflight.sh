#!/bin/bash
# Fail closed when the constrained VPS lacks enough swap/headroom for Mixxx.

set -euo pipefail

MIN_SWAP_KIB="${MIXXX_BUILD_MIN_SWAP_KIB:-524288}"
MIN_HEADROOM_KIB="${MIXXX_BUILD_MIN_HEADROOM_KIB:-1572864}"
MIN_DATA_DISK_KIB="${MIXXX_BUILD_MIN_DATA_DISK_KIB:-15728640}"
MIN_ARTIFACT_DISK_KIB="${MIXXX_BUILD_MIN_ARTIFACT_DISK_KIB:-1048576}"
MIN_CGROUP_MEMORY_BYTES="${MIXXX_BUILD_MIN_CGROUP_MEMORY_BYTES:-805306368}"
MIN_CGROUP_SWAP_BYTES="${MIXXX_BUILD_MIN_CGROUP_SWAP_BYTES:-805306368}"
MAX_CGROUP_TOTAL_BYTES="${MIXXX_BUILD_MAX_CGROUP_TOTAL_BYTES:-1610612736}"
MAX_PSI_SOME_AVG60="${MIXXX_BUILD_MAX_PSI_SOME_AVG60:-10.00}"
MAX_PSI_FULL_AVG60="${MIXXX_BUILD_MAX_PSI_FULL_AVG60:-2.50}"
DATA_PATH="${MIXXX_BUILD_DATA_PATH:-/data}"
ARTIFACT_PATH="${MIXXX_BUILD_ARTIFACT_PATH:-/srv/artifacts}"
CGROUP_ROOT="${MIXXX_BUILD_CGROUP_ROOT:-/sys/fs/cgroup}"
PRESSURE_FILE="${MIXXX_BUILD_PRESSURE_FILE:-/proc/pressure/memory}"

die() {
    echo "Error: $*" >&2
    exit 1
}

for value_name in \
        MIN_SWAP_KIB \
        MIN_HEADROOM_KIB \
        MIN_DATA_DISK_KIB \
        MIN_ARTIFACT_DISK_KIB \
        MIN_CGROUP_MEMORY_BYTES \
        MIN_CGROUP_SWAP_BYTES \
        MAX_CGROUP_TOTAL_BYTES; do
    value="${!value_name}"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] ||
        die "${value_name} must be a positive integer."
done
[[ "${DATA_PATH}" == /* && -d "${DATA_PATH}" ]] ||
    die "MIXXX_BUILD_DATA_PATH must name an existing absolute directory."
[[ "${ARTIFACT_PATH}" == /* && -d "${ARTIFACT_PATH}" ]] ||
    die "MIXXX_BUILD_ARTIFACT_PATH must name an existing absolute directory."
[[ "${CGROUP_ROOT}" == /* && -d "${CGROUP_ROOT}" ]] ||
    die "MIXXX_BUILD_CGROUP_ROOT must name an existing absolute directory."
[[ "${PRESSURE_FILE}" == /* && -r "${PRESSURE_FILE}" ]] ||
    die "MIXXX_BUILD_PRESSURE_FILE must name a readable absolute file."
for value_name in MAX_PSI_SOME_AVG60 MAX_PSI_FULL_AVG60; do
    value="${!value_name}"
    [[ "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
        die "${value_name} must be a non-negative number."
done

mem_total_kib="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)"
mem_available_kib="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
swap_total_kib="$(awk '/^SwapTotal:/ {print $2}' /proc/meminfo)"
swap_free_kib="$(awk '/^SwapFree:/ {print $2}' /proc/meminfo)"
available_headroom_kib=$((mem_available_kib + swap_free_kib))
data_disk_available_kib="$(df -Pk "${DATA_PATH}" | awk 'NR == 2 {print $4}')"
artifact_disk_available_kib="$(df -Pk "${ARTIFACT_PATH}" | awk 'NR == 2 {print $4}')"
data_filesystem="$(df -P "${DATA_PATH}" | awk 'NR == 2 {print $1}')"
artifact_filesystem="$(df -P "${ARTIFACT_PATH}" | awk 'NR == 2 {print $1}')"
psi_some_avg60="$(
    awk '$1 == "some" {
        for (i = 1; i <= NF; ++i) {
            if ($i ~ /^avg60=/) {
                sub(/^avg60=/, "", $i)
                print $i
                exit
            }
        }
    }' "${PRESSURE_FILE}"
)"
psi_full_avg60="$(
    awk '$1 == "full" {
        for (i = 1; i <= NF; ++i) {
            if ($i ~ /^avg60=/) {
                sub(/^avg60=/, "", $i)
                print $i
                exit
            }
        }
    }' "${PRESSURE_FILE}"
)"
[[ "${psi_some_avg60}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
    die "Could not read memory PSI some avg60."
[[ "${psi_full_avg60}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
    die "Could not read memory PSI full avg60."

echo "Build host memory: total=${mem_total_kib} KiB available=${mem_available_kib} KiB"
echo "Build host swap: total=${swap_total_kib} KiB free=${swap_free_kib} KiB"
echo "Build host immediate memory+swap headroom: ${available_headroom_kib} KiB"
echo "Build data disk available at ${DATA_PATH}: ${data_disk_available_kib} KiB"
echo "Artifact disk available at ${ARTIFACT_PATH}: ${artifact_disk_available_kib} KiB"
echo "Build filesystems: data=${data_filesystem} artifacts=${artifact_filesystem}"
echo "Memory PSI avg60: some=${psi_some_avg60}% full=${psi_full_avg60}%"

((swap_total_kib >= MIN_SWAP_KIB)) ||
    die "At least ${MIN_SWAP_KIB} KiB of host swap is required for this 2 GiB VPS."
((available_headroom_kib >= MIN_HEADROOM_KIB)) ||
    die "At least ${MIN_HEADROOM_KIB} KiB of free memory+swap is required before building."
((data_disk_available_kib >= MIN_DATA_DISK_KIB)) ||
    die "At least ${MIN_DATA_DISK_KIB} KiB free at ${DATA_PATH} is required before building."
((artifact_disk_available_kib >= MIN_ARTIFACT_DISK_KIB)) ||
    die "At least ${MIN_ARTIFACT_DISK_KIB} KiB free at ${ARTIFACT_PATH} is required before building."
[[ "${data_filesystem}" != "${artifact_filesystem}" ]] ||
    die "Build data and published artifacts must use separate filesystems."
awk -v actual="${psi_some_avg60}" -v maximum="${MAX_PSI_SOME_AVG60}" \
    'BEGIN { exit !(actual <= maximum) }' ||
    die "Memory PSI some avg60 ${psi_some_avg60}% exceeds ${MAX_PSI_SOME_AVG60}%."
awk -v actual="${psi_full_avg60}" -v maximum="${MAX_PSI_FULL_AVG60}" \
    'BEGIN { exit !(actual <= maximum) }' ||
    die "Memory PSI full avg60 ${psi_full_avg60}% exceeds ${MAX_PSI_FULL_AVG60}%."

[[ -r "${CGROUP_ROOT}/memory.max" && -r "${CGROUP_ROOT}/memory.swap.max" ]] ||
    die "A cgroup v2 memory and swap limit is required."
cgroup_memory_max="$(<"${CGROUP_ROOT}/memory.max")"
cgroup_swap_max="$(<"${CGROUP_ROOT}/memory.swap.max")"
[[ "${cgroup_memory_max}" =~ ^[0-9]+$ ]] ||
    die "Runner cgroup memory.max must be a numeric limit, not ${cgroup_memory_max}."
[[ "${cgroup_swap_max}" =~ ^[0-9]+$ ]] ||
    die "Runner cgroup memory.swap.max must be a numeric limit, not ${cgroup_swap_max}."
((cgroup_memory_max >= MIN_CGROUP_MEMORY_BYTES)) ||
    die "Runner cgroup memory.max is below ${MIN_CGROUP_MEMORY_BYTES} bytes."
((cgroup_swap_max >= MIN_CGROUP_SWAP_BYTES)) ||
    die "Runner cgroup swap allowance is below ${MIN_CGROUP_SWAP_BYTES} bytes."
cgroup_total_max=$((cgroup_memory_max + cgroup_swap_max))
((cgroup_total_max <= MAX_CGROUP_TOTAL_BYTES)) ||
    die "Runner cgroup RAM+swap budget ${cgroup_total_max} exceeds ${MAX_CGROUP_TOTAL_BYTES} bytes."
echo "Runner cgroup budget: RAM=${cgroup_memory_max} swap=${cgroup_swap_max} total=${cgroup_total_max} bytes"

echo "Hard-budget build preflight passed. Flatpak compilation must remain at one job."
