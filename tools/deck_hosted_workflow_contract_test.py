#!/usr/bin/env python3
"""Lightweight source contract for the hosted Mixxx deck workflow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".forgejo" / "workflows" / "deck-flatpak.yml"
GUARD = "/usr/local/bin/hosted-deploy-lease-guard"


def main() -> None:
    workflow = WORKFLOW.read_text(encoding="utf-8")

    assert workflow.count(GUARD) == 6
    for command in (
        "tools/deck_runner_cache_cleanup.sh --prepare",
        "tools/check_deck_flatpak_manifest.sh",
        "tools/deck_build_preflight.sh --phase=prepare",
        "tools/flatpak_buildenv.sh setup --user",
        "tools/deck_flatpak_publish.sh",
        "tools/deck_runner_cache_cleanup.sh --finalize",
    ):
        offset = workflow.index(command)
        assert GUARD in workflow[max(0, offset - 320) : offset], command

    assert "hosted-deploy test.attest" in workflow
    assert "needs: flatpak-x86_64" in workflow
    assert '--arg outcome "${{ needs.flatpak-x86_64.result }}"' in workflow
    assert "${{ job.status }}" not in workflow


if __name__ == "__main__":
    main()
