# Polinaria Mixxx Fork Agent Instructions

This fork is the authoritative source for a custom Mixxx deployment used on a
resource-constrained DJ laptop.

Before changing the deck pipeline, read:

- `packaging/flatpak/deck-vps-pipeline.md` — complete architecture and
  operations contract.
- `packaging/flatpak/deck-testing.md` — concise deck acceptance procedure.
- `.forgejo/workflows/deck-flatpak.yml` — Forgejo build trigger.
- `tools/deck_flatpak_publish.sh` — VPS build/validation/publication behavior.
- `tools/deck_flatpak_deploy.sh` — laptop staging/activation/rollback behavior.

## Source And Deployment Authority

- Forgejo repository:
  `ssh://git@forge.polinaria.world:900/andrew/mixxx.git`
- Forgejo is authoritative. GitHub is not part of the operational pipeline.
- `deck/candidate` is the only publication branch.
- Port 900 is Forgejo Git SSH, not proof of an OS-level VPS shell.
- Promote an exact reviewed commit:

  ```bash
  git push origin <commit>:refs/heads/deck/candidate
  ```

- Do not force-push shared development branches for deployment.
- A manual workflow dispatch must select `deck/candidate`.

## Deck Laptop Constraints

- The deck laptop is old and slow. Never compile Mixxx there.
- Never run Flatpak Builder, CMake/Ninja builds, heavy tests, CI runners, or
  sustained build workloads on a deck.
- Never automatically stop, restart, activate, or roll back Mixxx during a DJ
  session.
- `check` and `stage` may run while Mixxx is active; activation and rollback
  must refuse.
- Preserve the Mixxx database, profile, preferences, analysis data, crates,
  playlists, controller mappings, recordings, logs, and Flatpak data.
- Never use Flatpak uninstall/delete-data as an update procedure.
- Keep a verified previous bundle through acceptance.

## Build And Publication Invariants

- Runner label is `mixxx-flatpak-x86_64`.
- Build architecture is `x86_64`.
- Application ID is `org.mixxx.Mixxx`.
- Expected ref is `app/org.mixxx.Mixxx/x86_64/master`.
- Source ref must be `refs/heads/deck/candidate`.
- The event SHA must equal checked-out `HEAD`.
- Tracked working-tree changes are forbidden during publication.
- Validate the bundle with OSTree import/fsck and a headless Mixxx smoke test.
- Publish the Flatpak, schema-1 manifest, and corresponding source archive.
- SHA build directories are immutable.
- Update `latest.json` atomically only after all validation passes.
- Do not let a superseded job replace `latest.json`.
- Treat an existing immutable-build checksum mismatch as an incident.

## Runner And Infrastructure Invariants

- Use a repository-scoped Forgejo runner, not a global runner.
- Host workflow steps run inside a dedicated outer container.
- No Docker socket, privileged mode, or arbitrary volume mounts.
- The current VPS has only 2 GiB physical RAM and also hosts production.
- One concurrent job, one CPU, 1152-MiB resident-memory, 384-MiB swap,
  1536-MiB combined RAM+swap, 512-PID, and three-hour limits.
- Flatpak Builder must run with one job.
- Deck candidates use the dedicated Release/no-debug manifest, GNU BFD
  low-memory flags, disabled LTO, and inherited low CPU/I/O priority.
- Keep `org.mixxx.Mixxx.deck.yaml` synchronized with the normal manifest.
  `tools/check_deck_flatpak_manifest.sh` must pass.
- Require the hard-budget preflight: numeric cgroup v2 limits, no more than
  1536 MiB combined RAM+swap, at least 512 MiB host swap, 1536 MiB currently
  free memory-plus-swap, and 20 GiB free runner data disk.
- Build off-hours. If the job OOMs, keep the hard ceiling and optimize the build
  rather than bypassing preflight, raising concurrency, or using the deck.
- Persistent runner cache/data and artifact volumes are allowed.
- Caddy mounts artifacts read-only.
- Runner uses a dedicated network through Caddy and does not join the internal
  application/database network.
- Preserve Docker named volumes. Never use `docker compose down -v`.
- Server orchestration lives in `andrew/total-infra`, not in this repository.

## Client Invariants

- Default manifest:
  `https://polinaria.world/mixxx-deck/latest.json`
- Accept only HTTPS `latest.json` publication roots.
- Validate schema, channel, app, architecture, source ref, SHA formats, size,
  and exact immutable URLs.
- Verify downloaded size and SHA-256.
- Take an exclusive lock for activation.
- Refuse activation and rollback while Mixxx runs.
- Snapshot a different installed user Flatpak before replacing it.
- Verify cached checksums before installation.
- Verify installed source SHA after installation.
- Protect current, previous, and staged bundles from pruning.
- Do not modify the Mixxx profile.

## Change Procedure

1. Inspect applicable instructions and current branch state.
2. Preserve unrelated user changes.
3. Make the smallest coherent source/pipeline change.
4. Update:
   - `packaging/flatpak/deck-vps-pipeline.md`;
   - `packaging/flatpak/deck-testing.md`;
   - `andrew/total-infra/docs/OPERATIONS.md` for server changes;
   - deck-local docs and `AGENTS.md` for changed client/safety behavior.
5. Run lightweight validation:

   ```bash
   bash -n tools/check_deck_flatpak_manifest.sh \
     tools/deck_build_preflight.sh tools/deck_flatpak_publish.sh \
     tools/deck_flatpak_deploy.sh
   tools/check_deck_flatpak_manifest.sh
   forgejo-runner validate --workflow \
     --path .forgejo/workflows/deck-flatpak.yml
   git diff --check
   ```

6. Run actual compilation and bundle validation only on the VPS runner.
7. Promote an exact commit to `deck/candidate`.
8. Verify the public manifest before staging.
9. Stage before ending the DJ session.
10. Activate only with explicit operator approval and retain rollback.

## Incident Defaults

- If the public manifest is missing or invalid, keep the installed deck build.
- If a workflow fails, diagnose the VPS runner; never fall back to compiling on
  the deck.
- If a checksum fails, do not install the artifact.
- If Mixxx is running, do not work around the activation interlock.
- If candidate acceptance fails, close Mixxx, roll back, preserve logs, and fix
  forward with a new commit.
