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
Docker volume: mixxx-deck-artifacts
        |
        | read-only Caddy mount
        v
https://polinaria.world/mixxx-deck/
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
| Latest candidate | `https://polinaria.world/mixxx-deck/latest.json` |
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
- is limited to four CPUs and 8 GiB RAM;
- handles one job at a time;
- has no Docker socket;
- is not privileged;
- has no arbitrary container volume allowlist;
- writes only its persistent data/cache volume and the artifact volume;
- uses a dedicated bridge shared with Caddy;
- does not join the internal application/database network;
- restarts with `unless-stopped`.

Persistent runner data includes Flatpak SDK/dependency state, ccache, action
cache, and job work directories. A cold first build may be much slower than
later builds.

The artifact volume is writable by the runner and read-only in Caddy.

## Build And Publication Algorithm

`tools/deck_flatpak_publish.sh` performs these checks and actions:

1. Validate absolute publication root, positive retention, `x86_64`, and exact
   candidate ref.
2. Require build, Flatpak, Git, OSTree, checksum, compression, timeout, and
   locking tools.
3. Resolve checked-out `HEAD` and compare it with the Forgejo event SHA.
4. Reject tracked checkout modifications.
5. Acquire a publication lock so concurrent jobs cannot race publication.
6. Build `Mixxx.flatpak` with:

   ```bash
   packaging/flatpak/flatpak_build.sh bundle
   ```

7. Import the bundle into a temporary OSTree repository.
8. Run `ostree fsck`.
9. Require `app/org.mixxx.Mixxx/x86_64/master`.
10. Require the bundle commit subject to identify the Git source SHA.
11. Run a 30-second headless `/app/bin/mixxx --version` smoke test.
12. Create a `git archive` source tarball compressed with Zstandard.
13. Calculate bundle/source SHA-256 values and bundle byte length.
14. Write manifest schema version 1.
15. Atomically move files from a staging directory into the immutable build
    directory.
16. If the immutable directory already exists, require the bundle checksum to
    match.
17. Query the remote candidate ref. If a newer candidate exists, retain this
    immutable build but do not promote it.
18. Atomically replace `latest.json`.
19. Retain the ten most recent server build directories by default.

A failure before step 18 leaves the previous `latest.json` unchanged.

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
https://polinaria.world/mixxx-deck/latest.json
https://polinaria.world/mixxx-deck/builds/<sha>/manifest.json
https://polinaria.world/mixxx-deck/builds/<sha>/Mixxx.flatpak
https://polinaria.world/mixxx-deck/builds/<sha>/source.tar.zst
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
  "source_sha": "1afacfdfcb663139dc342da51153e7169f49d75a",
  "source_ref": "refs/heads/deck/candidate",
  "built_at": "2026-07-24T00:00:00Z",
  "bundle_url": "https://polinaria.world/mixxx-deck/builds/1afacfdfcb663139dc342da51153e7169f49d75a/Mixxx.flatpak",
  "source_url": "https://polinaria.world/mixxx-deck/builds/1afacfdfcb663139dc342da51153e7169f49d75a/source.tar.zst",
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
curl --fail https://polinaria.world/mixxx-deck/latest.json | jq .
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

1. deploy the `total-infra` implementation that enables Actions and the Caddy
   artifact route;
2. validate `docker compose config` with and without the `mixxx-build` profile;
3. recreate Forgejo and Caddy;
4. enable the Actions unit for `andrew/mixxx`;
5. create a repository-scoped runner;
6. store runner UUID/token in the ignored server `.env`;
7. build and start `mixxx-runner`;
8. verify label, isolation, volumes, networks, resource limits, and logs;
9. manually dispatch the first build on `deck/candidate` if its push predates
   Actions enablement;
10. verify `latest.json` and all immutable files return HTTP 200.

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
  https://polinaria.world/mixxx-deck/latest.json
curl --fail --head \
  https://polinaria.world/mixxx-deck/builds/<sha>/Mixxx.flatpak
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

Do not destroy or recreate named volumes as a troubleshooting shortcut.

## Security Notes

- Write access to `deck/candidate` is deployment authority.
- Workflow code runs as the runner user inside the dedicated container and can
  write the artifact volume.
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
- validate the Forgejo workflow schema;
- run `git diff --check`;
- perform the actual build only on the VPS runner;
- test staging before activation;
- keep a previous verified bundle through acceptance.
