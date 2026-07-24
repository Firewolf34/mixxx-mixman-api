# Forgejo VPS To DJ Deck Flatpak Pipeline

## Scope

This document defines the custom build and deployment pipeline for the
Polinaria Mixxx fork.

It exists because the target DJ laptop is old, resource-constrained, and
performance-critical. The laptop must not compile Mixxx. Forgejo is the source
authority, a dedicated VPS runner does build work, Caddy distributes immutable
artifacts, and a small client stages/activates/rolls back user Flatpaks.

GitHub is not required by this pipeline. A GitHub fork may remain as a mirror,
but it is not a source, build, or deployment dependency.

## Goals

- Make hot-patching Mixxx repeatable without compiling on the deck laptop.
- Build the exact promoted Git commit on a reusable VPS runner.
- Fail before publication when architecture, source identity, bundle structure,
  or the headless binary smoke test is wrong.
- Publish an immutable, checksum-addressed Flatpak and corresponding source.
- Let the deck stage an update while Mixxx runs.
- Prevent activation or rollback while Mixxx runs.
- Preserve the Mixxx profile and Flatpak application data.
- Snapshot the installed app before first replacement so rollback works on the
  first candidate.
- Keep GitHub out of the operational loop.

## Non-Goals

- This is not an upstream Mixxx release process.
- This does not publish to Flathub.
- This does not support architectures other than `x86_64`.
- This does not automatically stop or restart Mixxx.
- This does not automatically activate new builds on any deck.
- This does not turn a deck laptop into a build or CI worker.
- This does not replace source review, hardware acceptance, or operator
  judgment.

## Topology

```text
capable development machine
        |
        | push exact Git commit
        v
Forgejo: andrew/mixxx
        |
        | refs/heads/deck/candidate
        v
.forgejo/workflows/deck-flatpak.yml
        |
        | runs-on: mixxx-flatpak-x86_64
        v
repository-scoped Forgejo Runner
host job inside dedicated VPS container
        |
        | Flatpak build + OSTree validation + smoke test
        v
provider-backed artifact bind mount
        |
        | read-only Caddy mount
        v
https://forge.polinaria.world/mixxx-deck/
        |
        | HTTPS manifest + immutable bundle
        v
tools/deck_flatpak_deploy.sh on the DJ laptop
```

## Authorities And Contracts

| Concern | Authority |
| --- | --- |
| Mixxx source | `ssh://git@forge.polinaria.world:900/andrew/mixxx.git` |
| Candidate selection | `refs/heads/deck/candidate` |
| Workflow | `.forgejo/workflows/deck-flatpak.yml` |
| Build/publish behavior | `tools/deck_flatpak_publish.sh` |
| Deck behavior | `tools/deck_flatpak_deploy.sh` |
| VPS orchestration | `andrew/total-infra`, branch `dev` |
| Latest candidate | `https://forge.polinaria.world/mixxx-deck/latest.json` |
| Immutable artifacts | `/mixxx-deck/builds/<source-sha>/` |
| Flatpak app/ref | `app/org.mixxx.Mixxx/x86_64/master` |

Port 900 is Forgejo Git SSH. Git access over that port does not imply an
operating-system VPS shell.

## Branch And Promotion Model

Feature development may occur on any intentional branch. A deck build is
authorized only by updating `deck/candidate`:

```bash
git fetch origin
git show --stat <candidate-sha>
git status --short
git push origin <candidate-sha>:refs/heads/deck/candidate
```

The workflow triggers automatically for pushes to that branch. It also supports
manual dispatch, but the dispatch must select `deck/candidate`.

The publisher rejects any source ref other than:

```text
refs/heads/deck/candidate
```

Do not force-push shared development branches to promote a build. Moving the
dedicated candidate ref is the promotion operation.

## Forgejo Workflow

The workflow selects the custom runner label:

```text
mixxx-flatpak-x86_64
```

It has a 180-minute job timeout, checks out the exact Forgejo event SHA with
history and submodules, installs the required Flatpak SDK for the runner user,
and calls the publisher.

The runner is repository-scoped so unrelated repositories cannot schedule
work. It accepts one job at a time.

## Runner Isolation And Persistence

The VPS implementation is versioned in `andrew/total-infra`.

The runner:

- uses Forgejo Runner `12.7.3`, downloaded with a pinned SHA-256;
- provides Node 24 for host JavaScript actions;
- runs workflow steps in host mode inside a dedicated outer container;
- runs on a VPS with only 2 GiB physical RAM alongside production services;
- is limited to one CPU, 1152 MiB resident memory, 384 MiB swap, a hard
  1536 MiB combined RAM+swap ceiling, and 512 processes;
- runs at CPU niceness 15 and best-effort I/O priority 7;
- handles one job at a time;
- has no Docker socket;
- is not privileged;
- has no arbitrary container volume allowlist;
- writes only its provider-backed data/cache and artifact bind mounts;
- uses a dedicated bridge shared with Caddy;
- does not join the internal application/database network;
- restarts with `unless-stopped`.

Persistent runner data includes Flatpak SDK/dependency state, ccache, action
cache, and job work directories. A cold first build may be much slower than
later builds. The workflow sets `FLATPAK_BUILDER_JOBS=1`, and
`packaging/flatpak/flatpak_build.sh` passes that as `--jobs=1`.

`tools/deck_build_preflight.sh` runs before SDK setup and again from the
publisher. It fails closed unless:

- host swap totals at least 512 MiB;
- current `MemAvailable + SwapFree` totals at least 1536 MiB;
- the runner `/data` filesystem has at least 20 GiB free;
- the separate `/srv/mixxx-deck` artifact filesystem has at least 4 GiB free;
- runner data and artifacts resolve to different filesystems;
- the runner cgroup allows at least 1 GiB resident memory;
- the runner cgroup allows at least 256 MiB swap;
- numeric cgroup v2 RAM plus swap limits total no more than 1536 MiB.

These gates make an attempt less dangerous; they do not guarantee that Mixxx
will link successfully within 1536 MiB. A failure should be contained inside
the runner cgroup rather than thrashing through gigabytes of host swap.

Deck publication uses `org.mixxx.Mixxx.deck.yaml`, synchronized with the normal
manifest except for these intentional low-memory changes:

- Release `-O2` build with no debug information;
- Flatpak debug extraction disabled and binaries stripped;
- interprocedural optimization/LTO explicitly disabled;
- GNU BFD forced for executable and shared-library links;
- `--no-keep-memory` makes BFD reread symbols instead of retaining them;
- `--reduce-memory-overheads` selects slower, smaller linker data structures.
- Source publication compression is restricted to one Zstandard worker.

The normal manifest retains its existing developer/debug behavior.
`tools/check_deck_flatpak_manifest.sh` normalizes the intentional deck-only
differences and compares the result with the normal manifest. Both the workflow
and publisher refuse to build if any other manifest content drifts.

The infrastructure uses two explicit, provider-backed bind mounts rather than
Docker named volumes under `/`. Runner data is writable only by the runner.
Artifacts are writable by the runner and mounted read-only in Caddy. Compose
refuses to create missing host paths; the VPS operator must provide both
dedicated mount paths in the ignored `.env`.

## Build And Publication Algorithm

`tools/deck_flatpak_publish.sh` performs these checks and actions:

1. Validate absolute publication root, positive retention, `x86_64`, and exact
   candidate ref.
2. Enforce the host/cgroup memory and swap preflight.
3. Require build, Flatpak, Git, OSTree, checksum, compression, timeout, and
   locking tools.
4. Resolve checked-out `HEAD` and compare it with the Forgejo event SHA.
5. Reject tracked checkout modifications.
6. Acquire a publication lock so concurrent jobs cannot race publication.
7. Build `Mixxx.flatpak` with one Flatpak Builder job:

   ```bash
   packaging/flatpak/flatpak_build.sh bundle
   ```

8. Import the bundle into a temporary OSTree repository.
9. Run `ostree fsck`.
10. Require `app/org.mixxx.Mixxx/x86_64/master`.
11. Require the bundle commit subject to identify the Git source SHA.
12. Run a 30-second headless `/app/bin/mixxx --version` smoke test.
13. Create a `git archive` source tarball compressed with Zstandard.
14. Calculate bundle/source SHA-256 values and bundle byte length.
15. Write manifest schema version 1.
16. Atomically move files from a staging directory into the immutable build
    directory.
17. If the immutable directory already exists, require the bundle checksum to
    match.
18. Query the remote candidate ref. If a newer candidate exists, retain this
    immutable build but do not promote it.
19. Atomically replace `latest.json`.
20. Retain the ten most recent server build directories by default.

A failure before step 19 leaves the previous `latest.json` unchanged.

## Artifact Layout

```text
/srv/mixxx-deck/
├── latest.json
└── builds/
    └── <source-sha>/
        ├── Mixxx.flatpak
        ├── manifest.json
        └── source.tar.zst
```

Public layout:

```text
https://forge.polinaria.world/mixxx-deck/latest.json
https://forge.polinaria.world/mixxx-deck/builds/<sha>/manifest.json
https://forge.polinaria.world/mixxx-deck/builds/<sha>/Mixxx.flatpak
https://forge.polinaria.world/mixxx-deck/builds/<sha>/source.tar.zst
```

Caddy serves `latest.json` with `Cache-Control: no-store`. Build paths receive
a long-lived immutable cache policy.

Do not edit files inside a published SHA directory. Fix forward with a new Git
commit and candidate SHA.

## Manifest Schema

Example:

```json
{
  "schema_version": 1,
  "channel": "deck-candidate",
  "app_id": "org.mixxx.Mixxx",
  "arch": "x86_64",
  "source_sha": "<40-character-source-sha>",
  "source_ref": "refs/heads/deck/candidate",
  "built_at": "2026-07-24T00:00:00Z",
  "bundle_url": "https://forge.polinaria.world/mixxx-deck/builds/<source-sha>/Mixxx.flatpak",
  "source_url": "https://forge.polinaria.world/mixxx-deck/builds/<source-sha>/source.tar.zst",
  "sha256": "<64 lowercase hex>",
  "source_sha256": "<64 lowercase hex>",
  "size_bytes": 123456789
}
```

Client validation requires:

- schema version `1`;
- channel `deck-candidate`;
- app ID `org.mixxx.Mixxx`;
- architecture `x86_64`;
- source ref `refs/heads/deck/candidate`;
- lowercase 40-hex source SHA;
- lowercase 64-hex bundle and source checksums;
- positive integer size;
- exact HTTPS build URL layout derived from the configured publication root.

After manifest validation, the client verifies actual download size and bundle
SHA-256.

## Deck Client State

Install:

```bash
tools/deck_flatpak_deploy.sh setup
```

Installed path:

```text
~/.local/bin/mixxx-deck
```

Cache:

```text
~/.cache/mixxx-deck/builds/<source-sha>/
```

State:

```text
~/.local/state/mixxx-deck/
```

Default client retention is three recent builds, with current, previous, and
staged builds protected from pruning even when those are distinct.

## Deck Command Semantics

```bash
mixxx-deck status
mixxx-deck check
mixxx-deck stage [latest|<source-sha>]
mixxx-deck activate [latest|<source-sha>]
mixxx-deck deploy [latest|<source-sha>]
mixxx-deck rollback
mixxx-deck run [mixxx-args...]
```

### Status

Local, read-only view of installed/staged/previous source SHAs, manifest URL,
running state, and Flatpak metadata.

### Check

Fetches and validates only the latest manifest, then compares installed and
available source SHAs.

### Stage

Fetches and verifies a manifest and bundle. Staging may run while Mixxx is
active because it does not alter the installed app.

### Activate

Takes a deployment lock and refuses while Mixxx is running. It verifies the
cached checksum, snapshots a different installed app into the rollback cache,
installs the user bundle, verifies its source SHA from Flatpak metadata, records
state, and prunes unneeded cache entries.

### Deploy

Stages then activates. The activation interlock still refuses a live
replacement. Prefer explicit staging followed by later activation.

### Rollback

Activates the cached previous bundle. Mixxx must be stopped.

### Run

Launches the user Flatpak. It does not select or install a candidate.

## Profile Preservation

Replacing the user Flatpak changes application code, not the Mixxx profile.

The target Flatpak has persistent `.mixxx` storage and explicit access to the
host Mixxx profile. The deployment client does not uninstall the application,
delete application data, or alter the profile.

Preserve:

- Mixxx database;
- preferences;
- analysis data;
- crates and playlists;
- controller mappings;
- recordings;
- logs;
- Flatpak application data.

Never use an uninstall/delete-data cycle as a deployment shortcut.

## Standard Release Procedure

### Developer

```bash
git fetch origin
git show --stat <candidate-sha>
git push origin <candidate-sha>:refs/heads/deck/candidate
```

Wait for **Deck Flatpak Build** to succeed.

### Public verification

```bash
curl --fail https://forge.polinaria.world/mixxx-deck/latest.json | jq .
```

Confirm the exact candidate SHA and contract fields.

### Deck pre-stage

```bash
mixxx-deck status
mixxx-deck check
mixxx-deck stage
mixxx-deck status
```

### End session and activate

Only after the operator confirms Mixxx can stop:

```bash
mixxx-deck activate
mixxx-deck status
mixxx-deck run
```

### Accept

Check launch, preserved library/preferences, audio, controllers, mappings,
REST recommendation behavior, MixMan steering, and diagnostics.

### Reject

Close Mixxx and run:

```bash
mixxx-deck rollback
mixxx-deck run
```

Record the candidate SHA and logs. Fix forward with a new commit.

## One-Time VPS Bootstrap

The infrastructure agent must:

1. deploy the latest `total-infra` implementation containing the 2 GiB safety
   corrections, Actions, and the Caddy artifact route;
2. inspect RAM, swap, disk, Docker usage, and memory pressure;
3. attach at least 25 GiB provider storage and configure separate runner-data
   and artifact filesystems, with at least 20 GiB and 4 GiB free respectively;
4. require at least 512 MiB host swap and 1536 MiB free memory-plus-swap;
5. validate `docker compose config` with and without the `mixxx-build` profile;
6. recreate Forgejo and Caddy;
7. enable the Actions unit for `andrew/mixxx`;
8. create a repository-scoped runner;
9. store runner UUID/token in the ignored server `.env`;
10. build and start `mixxx-runner`;
11. verify label, isolation, mounts, networks, resource limits, and logs;
12. manually dispatch the first build off-hours on `deck/candidate` if its push predates
   Actions enablement;
13. watch host pressure and verify `latest.json` plus all immutable files.

See `docs/OPERATIONS.md` in the `andrew/total-infra` repository for exact
server commands.

After bootstrap, routine builds require no server login.

## Troubleshooting

| Symptom | Safe interpretation and response |
| --- | --- |
| Public manifest is 404 | Caddy route is not deployed or no build succeeded; keep current deck build |
| Job is queued | repository runner is offline or label does not match |
| Publisher rejects ref | manual dispatch selected a branch other than `deck/candidate` |
| SDK/build dependency failure | diagnose VPS network/cache; never shift build to deck |
| Hard-budget preflight fails | correct cgroup/headroom/disk configuration; do not bypass |
| Build OOMs inside 1536 MiB | keep the ceiling and reduce build/link requirements further |
| Host thrashes/services degrade | stop the runner; verify the cgroup ceiling is actually active |
| OSTree check fails | bundle is not publishable |
| Smoke test fails | binary is not publishable |
| Existing SHA checksum differs | artifact integrity incident; do not overwrite |
| Newer candidate message | expected stale-build protection; latest remains newer |
| Manifest validation fails | do not download or install |
| Bundle size/checksum fails | discard partial file and retry; investigate repeated failures |
| Activation refuses | Mixxx is still running; end session normally |
| Installed SHA does not match | stop and restore cached previous build |
| Controller/audio regression | close candidate, roll back, preserve logs |
| Preferences appear absent | do not initialize/overwrite a new profile; verify permissions and roll back |

## Diagnostics

Public:

```bash
curl --fail --dump-header - \
  https://forge.polinaria.world/mixxx-deck/latest.json
curl --fail --head \
  https://forge.polinaria.world/mixxx-deck/builds/<sha>/Mixxx.flatpak
```

Deck:

```bash
mixxx-deck status
flatpak ps --columns=application
flatpak info --user org.mixxx.Mixxx
flatpak info --user --show-permissions org.mixxx.Mixxx
```

VPS:

```bash
docker compose --profile mixxx-build ps
docker compose --profile mixxx-build logs --tail=200 mixxx-runner
docker compose logs --tail=200 forgejo caddy
docker compose exec caddy caddy validate --config /etc/caddy/Caddyfile
```

Do not destroy existing volumes or replace the required provider-backed mounts
with root-backed Docker storage as a troubleshooting shortcut.

## Security Notes

- Write access to `deck/candidate` is deployment authority.
- Workflow code runs as the runner user inside the dedicated container and can
  write the artifact bind mount.
- The outer container is the host-job isolation boundary.
- The runner token is repository-scoped and must not be committed.
- Caddy gets only read access to artifacts.
- SHA-named directories are immutable.
- HTTPS plus strict manifest URL checks and SHA-256 validation protect
  transport integrity; an existing immutable checksum mismatch is an incident.
- Remove temporary Forgejo SSH keys when implementation access is no longer
  required.

## Pipeline Change Checklist

When changing workflow, publisher, client, manifest, or infrastructure:

- update this document;
- update `packaging/flatpak/deck-testing.md`;
- update `andrew/total-infra/docs/OPERATIONS.md` for server changes;
- update deck-local `AGENTS.md` and operator docs for changed safety behavior;
- run `bash -n` on shell scripts;
- run `tools/check_deck_flatpak_manifest.sh`;
- validate the Forgejo workflow schema;
- run `git diff --check`;
- perform the actual build only on the VPS runner;
- test staging before activation;
- keep a previous verified bundle through acceptance.
