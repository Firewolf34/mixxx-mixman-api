# Polinaria Mixxx Fork Agent Instructions

This fork is the authoritative source for a custom Mixxx deployment used on a
resource-constrained DJ laptop.

## Local Private Planning

- Local-only planning may live under `private-docs/`, ignored through
  `.git/info/exclude`. If present, consult the relevant planning note before
  changing REST-backed library or recommendation work.
- Never place API secrets, endpoint names, hostnames, or other private service
  details in tracked files.

Before changing the deck pipeline, read:

- `packaging/flatpak/deck-vps-pipeline.md` — complete architecture and
  operations contract.
- `packaging/flatpak/deck-testing.md` — concise deck acceptance procedure.
- `.forgejo/workflows/deck-flatpak.yml` — Forgejo build trigger.
- `tools/deck_flatpak_publish.sh` — VPS build/validation/publication behavior.
- `tools/deck_flatpak_deploy.sh` — laptop staging/activation/rollback behavior.
- `tools/deck_forgejo_actions.sh` — authenticated Actions status, waiting,
  dispatch, and publication validation.

## Source And Deployment Authority

- Forgejo repository:
  `ssh://git@forge.polinaria.world:900/total-infra/mixxx.git`
- Forgejo is authoritative. GitHub is not part of the operational pipeline.
- `deck/candidate` is the only publication branch.
- Port 900 is Forgejo Git SSH, not proof of an OS-level VPS shell.
- Promote an exact reviewed commit:

  ```bash
  git push origin <commit>:refs/heads/deck/candidate
  ```

- Do not force-push shared development branches for deployment.
- A manual workflow dispatch must select `deck/candidate`.

## Branch Model

- `dev` is the only long-lived development and integration branch. Start normal
  work from a clean, current `dev` checkout and push reviewed work there.
- Forgejo's repository default branch must be `dev`. Repair that setting before
  deleting superseded branch refs, because a bare clone otherwise starts on an
  obsolete development line.
- `deck/candidate` is the only release/build pointer. Do not develop directly
  on it; promote an exact reviewed commit reachable from `dev` when a VPS build
  is desired.
- Short-lived `feature/*` and `fix/*` branches are optional implementation
  aids. Merge them into `dev`, push `dev`, then delete them only after their
  commits are reachable from `dev`.
- `origin/main`, `github/*`, and historical branch refs are upstream/reference
  inputs, not development or deployment targets. Never push custom work to
  them or select them for a deck workflow.
- Before editing, stop if the current worktree is dirty for an unrelated
  reason. The normal starting point is:

  ```bash
  git switch dev
  git pull --ff-only origin dev
  git status --short
  ```

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
- Treat a dependency source checksum mismatch as a supply-chain check, not a
  value to copy from an error message. Compare the received tree with the
  authoritative upstream tag or commit before changing a source pin. Prefer an
  exact Git `commit` plus its human-readable `tag` over forge-generated source
  archives whose bytes have proved unstable.
- Store any Forgejo API token outside the repository with mode 0600. Use a
  token restricted to `total-infra/mixxx`; never print, log, or commit it.

## Runner And Infrastructure Invariants

- Use a repository-scoped Forgejo runner, not a global runner.
- Host workflow steps run inside a dedicated outer container.
- No Docker socket, privileged mode, or arbitrary volume mounts. The runner has
  the one documented exception of `SYS_ADMIN` for Bubblewrap's nested Flatpak
  mount sandbox; do not add other capabilities or broaden this exception
  without a dedicated security review.
- The current VPS has only 2 GiB physical RAM and also hosts production.
- One concurrent job, one CPU, 768-MiB resident-memory, 768-MiB swap,
  1536-MiB combined RAM+swap, 512-PID, and three-hour limits.
- Flatpak Builder must run with one job.
- Deck candidates use the dedicated Release/no-debug manifest, GNU BFD
  low-memory flags, disabled LTO, and inherited low CPU/I/O priority.
- Keep `org.mixxx.Mixxx.deck.yaml` synchronized with the normal manifest.
  `tools/check_deck_flatpak_manifest.sh` must pass.
- Require the hard-budget preflight: numeric cgroup v2 limits, no more than
  1536 MiB combined RAM+swap, at least 512 MiB host swap, 1536 MiB currently
  free memory-plus-swap, 15 GiB free runner data disk, 1 GiB free artifact
  disk, and startup PSI within the encoded thresholds.
- Run the publisher through `tools/deck_pressure_guard.sh`; severe PSI for one
  minute must terminate the build.
- Build off-hours. If the job OOMs, keep the hard ceiling and optimize the build
  rather than bypassing preflight, raising concurrency, or using the deck.
- Runner data and artifacts must be required bind mounts backed by separate
  provider-mounted filesystems, never Docker named volumes stored on `/`.
- The fixed 25 GiB attached storage is sufficient only with shallow checkout,
  runner-backed temporary data, a 512 MiB ccache, two retained builds, and
  transient-work cleanup. Preserve those limits.
- Caddy mounts artifacts read-only.
- Runner uses a dedicated network through Caddy and does not join the internal
  application/database network.
- Preserve all existing Docker volumes. Never use `docker compose down -v`.
- Server orchestration lives in `total-infra/total-infra`, not in this repository.

## Client Invariants

- Default manifest:
  `https://forge.polinaria.world/artifacts/latest.json`
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

1. Inspect applicable instructions and current branch state; use a clean
   `dev` worktree for normal development.
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
     tools/deck_build_preflight.sh tools/deck_pressure_guard.sh \
     tools/deck_flatpak_publish.sh tools/deck_flatpak_deploy.sh \
     tools/deck_forgejo_actions.sh
   tools/check_deck_flatpak_manifest.sh
   forgejo-runner validate --workflow \
     --path .forgejo/workflows/deck-flatpak.yml
   git diff --check
   ```

6. Run actual compilation and bundle validation only on the VPS runner.
7. Push the reviewed commit to `dev`, then promote that exact commit to
   `deck/candidate`.
8. Treat the newest **Deck Flatpak Build** run in Forgejo Actions as the build
   authority. Confirm its checkout SHA, runner label, preflight, one-job build,
   absence of PSI/OOM termination, and final **Success** state.
9. Verify the public manifest and immutable files before staging.
10. Stage before ending the DJ session.
11. Activate only with explicit operator approval and retain rollback.

## Incident Defaults

- If the public manifest is missing or invalid, keep the installed deck build.
- If a workflow fails, diagnose the VPS runner; never fall back to compiling on
  the deck.
- If a dependency checksum fails, do not bypass it or blindly adopt the
  received checksum. Verify the source tree against upstream and fix forward
  with an immutable pin.
- If a published artifact checksum fails, do not install the artifact.
- If Mixxx is running, do not work around the activation interlock.
- If candidate acceptance fails, close Mixxx, roll back, preserve logs, and fix
  forward with a new commit.
