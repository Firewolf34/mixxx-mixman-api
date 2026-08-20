#!/bin/bash
# Short, memorable entry point for the deck's canonical offline rollback client.

set -euo pipefail

MIXXX_DECK="${MIXXX_DECK_CLIENT:-${HOME}/.local/bin/mixxx-deck}"

case "${1:-status}" in
    status)
        [[ $# -le 1 ]] || { echo "Usage: mixxx-break-glass [status|rollback]" >&2; exit 2; }
        exec "${MIXXX_DECK}" status
        ;;
    rollback)
        [[ $# -eq 1 ]] || { echo "Usage: mixxx-break-glass [status|rollback]" >&2; exit 2; }
        exec "${MIXXX_DECK}" rollback
        ;;
    -h|--help|help)
        echo "Usage: mixxx-break-glass [status|rollback]"
        echo "  status    Show the installed build and verified offline rollback target."
        echo "  rollback  Refuse if Mixxx is running, then restore that target without a network pull."
        ;;
    *)
        echo "Usage: mixxx-break-glass [status|rollback]" >&2
        exit 2
        ;;
esac
