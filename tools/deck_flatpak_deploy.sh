#!/bin/bash
# Helper for installing LAN-built Mixxx Flatpak bundles on a Debian deck-test laptop.

if [ -z "$BASH_VERSION" ]; then
    echo "Error: This script must be called as executable: ./deck_flatpak_deploy.sh ..." >&2
    exit 1
fi

set -euo pipefail

APP_ID="org.mixxx.Mixxx"
FLATHUB_REPO_URL="https://flathub.org/repo/flathub.flatpakrepo"
UDEV_RULE_SOURCE="res/linux/mixxx-usb-uaccess.rules"
UDEV_RULE_TARGET="/etc/udev/rules.d/69-mixxx-usb-uaccess.rules"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

usage() {
    cat <<EOF
Usage:
  tools/deck_flatpak_deploy.sh setup
  tools/deck_flatpak_deploy.sh install <Mixxx-*-x86_64.flatpak>
  tools/deck_flatpak_deploy.sh install-run <Mixxx-*-x86_64.flatpak> [mixxx-args...]
  tools/deck_flatpak_deploy.sh run [mixxx-args...]
  tools/deck_flatpak_deploy.sh status

Commands:
  setup       Install Flatpak if needed, add Flathub, and install Mixxx USB udev rules.
  install     Install or replace the user Flatpak from a LAN-built bundle.
  install-run Install a bundle, then launch Mixxx.
  run         Launch the installed Flatpak.
  status      Print the local Flatpak and udev setup status.
EOF
}

require_command() {
    local command_name="$1"
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Error: '${command_name}' is required but was not found on PATH." >&2
        exit 1
    fi
}

ensure_flatpak() {
    if command -v flatpak >/dev/null 2>&1; then
        return
    fi

    if command -v apt-get >/dev/null 2>&1; then
        echo "Installing Flatpak with apt..."
        sudo apt-get update
        sudo apt-get install -y flatpak
        return
    fi

    echo "Error: Flatpak is not installed. Install it with your distro package manager first." >&2
    exit 1
}

ensure_flathub() {
    ensure_flatpak
    flatpak remote-add --if-not-exists flathub "${FLATHUB_REPO_URL}"
}

install_udev_rules() {
    local source_path="${REPO_ROOT}/${UDEV_RULE_SOURCE}"
    if [ ! -f "${source_path}" ]; then
        echo "Error: udev rule source not found: ${source_path}" >&2
        exit 1
    fi

    sudo install -Dm644 "${source_path}" "${UDEV_RULE_TARGET}"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "Installed ${UDEV_RULE_TARGET}. Unplug and replug controllers before testing."
}

install_bundle() {
    local bundle_path="$1"

    if [ ! -f "${bundle_path}" ]; then
        echo "Error: Flatpak bundle not found: ${bundle_path}" >&2
        exit 1
    fi

    ensure_flathub
    flatpak install --user --reinstall -y "${bundle_path}"
}

run_mixxx() {
    require_command flatpak
    flatpak run "${APP_ID}" "$@"
}

print_status() {
    echo "Repo root: ${REPO_ROOT}"

    if command -v flatpak >/dev/null 2>&1; then
        echo "Flatpak: $(command -v flatpak)"
        if flatpak remotes | grep -q "^flathub"; then
            echo "Flathub remote: present"
        else
            echo "Flathub remote: missing"
        fi

        if flatpak info "${APP_ID}" >/dev/null 2>&1; then
            echo "Installed app:"
            flatpak info "${APP_ID}" | sed -n '1,8p'
        else
            echo "Installed app: ${APP_ID} is not installed"
        fi
    else
        echo "Flatpak: missing"
    fi

    if [ -f "${UDEV_RULE_TARGET}" ]; then
        echo "udev rules: ${UDEV_RULE_TARGET} is installed"
    else
        echo "udev rules: ${UDEV_RULE_TARGET} is missing"
    fi
}

if [ "$#" -lt 1 ]; then
    usage
    exit 2
fi

command="$1"
shift

case "${command}" in
    setup)
        if [ "$#" -ne 0 ]; then
            usage
            exit 2
        fi
        ensure_flathub
        install_udev_rules
        ;;
    install)
        if [ "$#" -ne 1 ]; then
            usage
            exit 2
        fi
        install_bundle "$1"
        ;;
    install-run)
        if [ "$#" -lt 1 ]; then
            usage
            exit 2
        fi
        bundle_path="$1"
        shift
        install_bundle "${bundle_path}"
        run_mixxx "$@"
        ;;
    run)
        run_mixxx "$@"
        ;;
    status)
        if [ "$#" -ne 0 ]; then
            usage
            exit 2
        fi
        print_status
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
