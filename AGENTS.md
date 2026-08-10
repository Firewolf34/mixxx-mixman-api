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
- `.github/workflows/github-deck-candidate.yml` — GitHub fallback build
  trigger.
- `tools/deck_flatpak_publish.sh` — VPS build/validation/publication behavior.
- `tools/deck_flatpak_deploy.sh` — laptop staging/activation/rollback behavior.
- `tools/deck_forgejo_actions.sh` — authenticated Actions status, waiting,
  dispatch, and publication validation.

## Source And Deployment Authority

- Forgejo repository:
  `ssh://git@forge.polinaria.world:900/total-infra/mixxx.git`
- GitHub fallback-builder fork:
  `https://github.com/Firewolf34/mixxx-mixman-api.git`
- Official OSS upstream:
  `https://github.com/mixxxdj/mixxx.git`
- Forgejo is the source authority. GitHub is an optional faster builder; the
  Polinaria promoter may validate and import its `github/candidate` artifact
  into the GPG-signed public Flatpak repository without changing source
  authority.
- `deck/candidate` triggers the Forgejo build/publish path.
- `github/candidate` triggers the GitHub artifact-only build path.
- Port 900 is Forgejo Git SSH, not proof of an OS-level VPS shell.
- Promote an exact reviewed commit:

  ```bash
  git push origin <commit>:refs/heads/deck/candidate
  git push github <commit>:refs/heads/github/candidate
  ```

- Do not force-push shared development branches for deployment.
- The two promotions are independent and may point to different reviewed
  commits. Push only the provider-specific candidate ref whose build is wanted.
- A manual workflow dispatch must select `deck/candidate`.

## Branch Model

- Expected remotes are `origin` for `total-infra/mixxx` on Forgejo, `github`
  for `Firewolf34/mixxx-mixman-api`, and `upstream` for `mixxxdj/mixxx`.
- `main` is a clean mirror of official `upstream/main`. Never commit custom work
  to it, merge `dev` into it, or use it as a release branch. Update it only by
  fast-forwarding from freshly fetched `upstream/main`.
- `dev` is the long-lived custom development and integration branch. Start
  normal work from a clean, current `dev` checkout and push reviewed work there.
- Forgejo's repository default branch must be `dev`. Repair that setting before
  deleting superseded branch refs, because a bare clone otherwise starts on an
  obsolete development line.
- `deck/candidate` and `github/candidate` are release/build pointers, not
  development branches. Promote an exact reviewed commit reachable from `dev`.
  Forgejo builds and publishes `deck/candidate`; GitHub builds and retains a
  14-day Actions artifact for `github/candidate`, which the Polinaria promoter
  may import into the durable signed repository.
- Short-lived `feature/*` and `fix/*` branches are optional implementation
  aids. Merge them into `dev`, push `dev`, then delete them only after their
  commits are reachable from `dev`.
- For a possible OSS contribution, branch from clean `main`, cherry-pick only
  the selected fork commits, and open a PR against official upstream. This is
  separate from merging upstream updates into `dev`.
- Do not recreate historical branches such as `github-main-deck-workflow`; the
  provider-specific candidate branches replace that experiment.
- From any configured clone, promotion requires no checkout or merge:

  ```bash
  git fetch origin github upstream
  git push origin <reviewed-dev-sha>:refs/heads/deck/candidate
  git push github <reviewed-dev-sha>:refs/heads/github/candidate
  ```

- Synchronize the clean mirrors separately from custom development:

  ```bash
  git fetch upstream
  git branch --force main upstream/main
  git push origin upstream/main:refs/heads/main
  git push github upstream/main:refs/heads/main
  ```

  The pushes must normally be fast-forwards. Stop and investigate divergence;
  do not casually force-push either `main` or shared `dev`.
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
  session. The signed-repository updater may activate only after the shared
  launch lock and `flatpak ps` both prove Mixxx is idle.
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
- Forgejo publication source ref must be `refs/heads/deck/candidate`.
- GitHub artifact builds must come from `refs/heads/github/candidate` and use
  the same deck-specific manifest and bundle validation. Their retained
  artifact must contain `Mixxx.flatpak` plus schema-1 GitHub candidate metadata,
  and its OSTree subject must identify the event SHA.
- The event SHA must equal checked-out `HEAD`.
- Tracked working-tree changes are forbidden during publication.
- Validate the bundle with OSTree import/fsck and a headless Mixxx smoke test.
- Publish the Flatpak, schema-1 manifest, and corresponding source archive.
- SHA build directories are immutable.
- Update `latest.json` atomically only after all validation passes.
- Do not let a superseded job replace `latest.json`.
- The durable client update channel is the GPG-signed Flatpak repository under
  `/artifacts/flatpak/repo`; publish its signed summary only after bundle
  checksum, manifest, OSTree ref, and source-subject validation.
- Treat an existing immutable-build checksum mismatch as an incident.
- Treat a dependency source checksum mismatch as a supply-chain check, not a
  value to copy from an error message. Compare the received tree with the
  authoritative upstream tag or commit before changing a source pin. Prefer an
  exact Git `commit` plus its human-readable `tag` over forge-generated source
  archives whose bytes have proved unstable.
- The publisher may retry only recognized transient source-download failures:
  at most three pre-compilation attempts with bounded backoff and a private
  runner-state cache. Build with fresh downloads disabled after prefetch;
  never retry or weaken an integrity failure.
- Store any Forgejo API token outside the repository with mode 0600. Use a
  token restricted to `total-infra/mixxx`; never print, log, or commit it.
- Store the most recent failed custom workflow-step output only at private
  runner path `/data/logs/latest.log` (0700 directory, 0600 log, 2 MiB cap).
  It records caught `HUP`, `INT`, and `TERM` cancellations as well as ordinary
  failures; correlate it with the `mixxx-runner` system journal. It is
  diagnostic state, never a Git file or `/srv/artifacts`/Caddy content.

## Runner And Infrastructure Invariants

- Use a repository-scoped Forgejo runner, not a global runner.
- Host workflow steps run as the non-login `mixxx-runner` systemd service on
  the VPS. Its only configured writable host paths are the private `/data`
  runner-state bind and `/srv/artifacts` publication bind.
- No Docker socket, privileged mode, setuid Bubblewrap, added Linux
  capabilities, arbitrary host paths, sudo access, or login shell. Host
  Bubblewrap must use Ubuntu's enforced unprivileged-user-namespace profile.
- The native service deliberately does not use `ProtectKernelTunables=yes` or
  `ProtectKernelLogs=yes`: either locks part of `/proc` and breaks Bubblewrap's
  inner Flatpak sandbox. The non-login runner has an empty capability bounding
  set and no sudo, so it cannot alter host tunables or read kernel logs.
- The current VPS has only 2 GiB physical RAM and also hosts production.
- One concurrent job, one CPU, 768-MiB resident-memory, 768-MiB swap,
  1536-MiB combined RAM+swap, 512-PID, and eight-hour workflow limits.
- Flatpak Builder must run with one job.
- Deck candidates use the dedicated Release/no-debug manifest, GNU BFD
  low-memory flags, disabled LTO, and inherited low CPU/I/O priority.
- Keep `org.mixxx.Mixxx.deck.yaml` synchronized with the normal manifest.
  `tools/check_deck_flatpak_manifest.sh` must pass.
- Keep the synchronized Flatpak typeinfo workaround enabled. It preserves Qt
  C++ runtime registration while supplying a valid tooling-only marker for
  Qt 6.10's no-FUSE Flatpak Builder typeinfo-generation failure.
- Keep Qt QML cache generation disabled for the `Mixxx` and `Mixxx.Controls`
  modules in that environment. Their original embedded QML sources remain the
  runtime fallback; do not disable C++ type registration for `Mixxx`.
- Limit `Mixxx.Controls` registration/cache suppression to the synchronized
  Flatpak workaround. `NO_GENERATE_QMLTYPES` also removes Qt's definition of
  `qml_register_types_Mixxx_Controls()`, while its generated static plugin still
  references that symbol. Keep `src/qml/qmlcontrolsregistration.cpp` attached
  to the module whenever that option is active; normal builds must retain Qt's
  generated registration and cache loader.
- Keep the shared Flatpak ccache key normalization (`CCACHE_BASEDIR=/run/build`
  and `CCACHE_NOHASHDIR=true`) in both synchronized manifests. The publisher
  must reset and report per-attempt ccache statistics without increasing the
  512 MiB cache cap or retaining resumable build trees.
- Validate bundle provenance through the subject field in locale-stable normal
  `ostree show` output. A commit subject is not detached metadata, and
  `ostree show` has no `-s` subject option. Keep the shared helper and its
  regression test in the manifest preflight gate.
- GitHub's Flatpak action stamps `Built from GITHUB_SHA`. The Forgejo publisher
  must pass `MIXXX_FLATPAK_SOURCE_SHA` so `flatpak_build.sh` stamps the same
  subject through Flatpak Builder's supported `--subject` export option.
- Pin the GitHub Flatpak action's repository to `repo` and build directory to
  `build_flatpak`; bundle creation and the headless smoke test consume those
  exact directories.
- Require the hard-budget preflight: numeric cgroup v2 limits, no more than
  1536 MiB combined RAM+swap, at least 512 MiB host swap, 1536 MiB currently
  free memory-plus-swap, 15 GiB free runner data disk, 1 GiB free artifact
  disk, and startup PSI within the encoded thresholds.
- Run the publisher through `tools/deck_pressure_guard.sh`; severe PSI for one
  minute must terminate the build.
- Build off-hours. If the job OOMs, keep the hard ceiling and optimize the build
  rather than bypassing preflight, raising concurrency, or using the deck.
- Runner state and artifacts must resolve to separate filesystems and be the
  only writable binds in the systemd service; never use Docker runner volumes
  or expose another host path.
- The fixed 25 GiB attached storage is sufficient only with shallow checkout,
  runner-backed temporary and Flatpak Builder source state, a 512 MiB ccache,
  two retained builds, and transient-work cleanup. Preserve those limits.
- Caddy mounts artifacts read-only.
- Runner is configured to reach Forgejo through its public HTTPS route and
  receives no Docker network, socket, or application/database-network
  attachment. It retains outbound public-network access; workflow code is not
  confidential from its own runner state.
- Preserve all existing Docker volumes. Never use `docker compose down -v`.
- Server orchestration lives in `total-infra/total-infra`, not in this repository.

## Client Invariants

- Default manifest:
  `https://forge.polinaria.world/artifacts/latest.json`
- Accept only HTTPS `latest.json` publication roots.
- `auto`/`latest` selects the newest verified Forgejo or configured GitHub
  candidate completion, with Forgejo winning an exact tie. Explicit provider
  targets must never silently switch provider.
- Forgejo validation requires schema, channel, app, architecture, source ref,
  SHA formats, size, and exact immutable URLs. GitHub validation requires a
  successful `github/candidate` run, an unexpired exact artifact, its Actions
  API ZIP digest, schema-1 artifact metadata, and matching run/source SHA.
- Verify downloaded size and SHA-256.
- Import every staged bundle into a temporary local OSTree repository, fsck it,
  require the expected ref, and require its commit subject to identify its
  source SHA before installation.
- GitHub fallback tokens belong only in
  `~/.config/mixxx-deck/github-actions-token` mode 0600. Use a fine-grained
  token restricted to `Firewolf34/mixxx-mixman-api` with Actions:read only;
  never print, log, or commit it.
- Take an exclusive lock for activation.
- Refuse activation and rollback while Mixxx runs.
- Normal desktop launches must use `mixxx-deck run` and hold a shared lock for
  the Mixxx process lifetime. Automatic activation uses a nonblocking exclusive
  lock and rechecks `flatpak ps` after download.
- The user service starts at boot through systemd linger, waits for
  NetworkManager, checks every four hours, and downloads/deploys only on AC
  power. A battery or running-session deferral is a successful no-change check.
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
     tools/deck_ostree_validation.sh tools/deck_ostree_validation_test.sh \
     tools/deck_build_preflight.sh tools/deck_pressure_guard.sh \
     tools/deck_flatpak_publish.sh tools/deck_flatpak_deploy.sh \
     tools/deck_forgejo_actions.sh
   tools/check_deck_flatpak_manifest.sh
   forgejo-runner validate --workflow \
     --path .forgejo/workflows/deck-flatpak.yml
   git diff --check
   ```

6. Run actual compilation and bundle validation on a configured CI runner,
   never on the deck laptop.
7. Push the reviewed commit to `dev`, then promote that exact commit to
   `deck/candidate`, `github/candidate`, or both depending on which build is
   wanted.
8. Treat provider candidate branches as build authority. Forgejo publishes
   locally; the Polinaria promoter independently validates successful GitHub
   artifacts before importing either provider into the signed repository.
9. For Forgejo publication, confirm the checkout SHA, runner label, preflight,
   one-job build, absence of PSI/OOM termination, and final **Success** state.
10. Verify the public manifest and immutable files before staging a Forgejo
    build. Before staging a GitHub build, verify the successful exact-SHA run,
    artifact digest/metadata, bundle checksum, and OSTree source subject through
    `mixxx-deck`; never manually bypass the client checks.
11. Manual releases may still stage before ending the DJ session.
12. Automatic activation is allowed only through the idle/AC interlock; retain
    rollback and never work around a running-session deferral.

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
