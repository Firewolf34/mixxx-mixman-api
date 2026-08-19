# Forgejo VPS To DJ Deck Flatpak Pipeline

## Scope

This document defines the custom build and deployment pipeline for the
Polinaria Mixxx fork.

It exists because the target DJ laptop is old, resource-constrained, and
performance-critical. The laptop must not compile Mixxx. Forgejo is the source
authority, a dedicated VPS runner does build work, Caddy distributes immutable
artifacts, and a small client stages/activates/rolls back user Flatpaks.

GitHub is not required by the authoritative source pipeline. The GitHub fork
provides an optional faster build from `github/candidate`. A dedicated
Polinaria promoter validates that Actions artifact and may import it into the
same durable, GPG-signed Flatpak repository used by Coal. This does not replace
Forgejo as source authority.

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
- Keep the GitHub build optional and independently triggered.
- Promote either provider through one durable signed Flatpak repository without
  placing a GitHub credential on the deck.
- Check for updates within one minute of the lingering user manager starting at
  boot and every four hours after each completed check.
- Automatically activate only when Mixxx is stopped, the shared launch lock is
  free, and the laptop is on AC power.

## Non-Goals

- This is not an upstream Mixxx release process.
- This does not publish to Flathub.
- This does not support architectures other than `x86_64`.
- This does not automatically stop or restart Mixxx.
- This never automatically activates beneath a running Mixxx process.
- This does not turn a deck laptop into a build or CI worker.
- This does not replace source review, hardware acceptance, or operator
  judgment.

## Topology

```text
capable development machine
        |
        | push exact Git commit
        v
Forgejo: total-infra/mixxx
        |
        | refs/heads/deck/candidate
        v
.forgejo/workflows/deck-flatpak.yml
        |
        | runs-on: mixxx-flatpak-x86_64
        v
repository-scoped Forgejo Runner
native repository-scoped systemd runner on the VPS
        |
        | Flatpak build + OSTree validation + smoke test
        v
provider-backed artifact bind mount
        |
        | read-only Caddy mount
        v
https://forge.polinaria.world/artifacts/
        |
        | HTTPS manifest + immutable bundle
        v
tools/deck_flatpak_deploy.sh on the DJ laptop
```

## Authorities And Contracts

| Concern | Authority |
| --- | --- |
| Mixxx source | `ssh://git@forge.polinaria.world:900/total-infra/mixxx.git` |
| Candidate selection | `refs/heads/deck/candidate` |
| Workflow | `.forgejo/workflows/deck-flatpak.yml` |
| Build/publish behavior | `tools/deck_flatpak_publish.sh` |
| Shared host-capacity admission | `tools/check_hosted_capacity_lease.sh` and the root-owned TotalInfra deployment gateway |
| Deck behavior | `tools/deck_flatpak_deploy.sh` |
| VPS orchestration | `total-infra/total-infra`, branch `dev` |
| Latest candidate | `https://forge.polinaria.world/artifacts/latest.json` |
| Immutable artifacts | `/artifacts/builds/<source-sha>/` |
| Signed Flatpak descriptor | `/artifacts/flatpak/mixxx.flatpakrepo` |
| Signed Flatpak repository | `/artifacts/flatpak/repo` |
| Flatpak app/ref | `app/org.mixxx.Mixxx/x86_64/master` |

Port 900 is Forgejo Git SSH. Git access over that port does not imply an
operating-system VPS shell.

## Branch And Promotion Model

`main` is a clean mirror of official `upstream/main`. Never commit custom work
to it or merge `dev` into it. A future upstream contribution should use a
short-lived branch from clean `main` with only intentionally cherry-picked fork
commits.

`dev` is the long-lived custom development and integration branch. Begin normal
work from a clean `dev` checkout. Short-lived `feature/*` or `fix/*` branches
may support a focused change, but merge them into `dev` and delete them only
after their commits are reachable from `dev`.

Forgejo's repository default branch must also be `dev`, so a fresh clone starts
on the supported development line. Change the default before deleting a
superseded branch; Git cannot alter that Forgejo repository setting.

`deck/candidate` and `github/candidate` are not development branches. They are
provider-specific release/build pointers. A build is authorized only by
promoting an exact reviewed commit already reachable from `dev`:

```bash
git fetch origin
git switch dev
git pull --ff-only origin dev
git status --short
git show --stat <candidate-sha>
git push origin <candidate-sha>:refs/heads/deck/candidate
git push github <candidate-sha>:refs/heads/github/candidate
```

The Forgejo push runs `.forgejo/workflows/deck-flatpak.yml` on the VPS and may
publish `latest.json`. The GitHub push runs
`.github/workflows/github-deck-candidate.yml` on a GitHub-hosted runner and
uploads a 14-day `Mixxx-flatpak-x86_64` artifact containing `Mixxx.flatpak`
and its GitHub candidate manifest. The same commit may be promoted to either or
both refs. Historical refs such as `github-main-deck-workflow` must not be
recreated.

The workflow triggers automatically for pushes to that branch. It also supports
manual dispatch, but the dispatch must select `deck/candidate`.

Forgejo Actions is the source of truth for build success or failure. Within
roughly a minute of promotion, sign in at
`https://forge.polinaria.world/total-infra/mixxx/actions`, open the newest
**Deck Flatpak Build**, and verify:

- runner `mixxx-flatpak-x86_64`;
- checkout SHA exactly equals the promoted candidate;
- hard-budget preflight passed;
- Flatpak Builder used one job;
- no pressure-guard exit 75 or cgroup OOM;
- final job state is **Success**.

If no run appears, manually dispatch the workflow once with branch
`deck/candidate`. Do not dispatch another branch or repeatedly enqueue retries.

The publisher rejects any source ref other than:

```text
refs/heads/deck/candidate
```

Do not force-push shared development branches to promote a build. Moving the
dedicated candidate ref is the promotion operation.

### Diverged candidate-pointer recovery

A candidate ref may occasionally retain a patch-equivalent commit from an old
development history. A normal exact-SHA promotion then fails as a
non-fast-forward even though the release pointer has no unique source change.
Do not merge that obsolete release history into `dev`, delete the ref, or use
an unleased force. From a clean current `dev` checkout:

```bash
git fetch origin
git merge-base --is-ancestor <reviewed-dev-sha> origin/dev
git rev-parse origin/deck/candidate
git cherry <reviewed-dev-sha> origin/deck/candidate
git push --dry-run \
  --force-with-lease=refs/heads/deck/candidate:<observed-old-sha> \
  origin <reviewed-dev-sha>:refs/heads/deck/candidate
git push \
  --force-with-lease=refs/heads/deck/candidate:<observed-old-sha> \
  origin <reviewed-dev-sha>:refs/heads/deck/candidate
```

The ancestry check must succeed, and every line from `git cherry` must begin
with `-`, meaning the candidate-only patch is already represented in the
reviewed development history. Any `+` line, changed observed SHA, failed dry
run, or unexpected remote state is a stop condition. The final command updates
only the dedicated candidate pointer and triggers its normal build; it does not
authorize a non-fast-forward update of `dev` or `main`.

## GitHub Fallback Workflow

The GitHub fork keeps `main` synchronized exactly with official upstream and
keeps `github/candidate` as its only custom release pointer. A push to
`github/candidate` triggers a GitHub-hosted `ubuntu-24.04` build. The workflow
uses the deck-specific Release/no-debug manifest, verifies the event ref and
SHA, checks manifest synchronization, imports and fscks the OSTree bundle,
relies on the Flatpak action's `Built from GITHUB_SHA` export subject, reads
that locale-stable subject from normal `ostree show` output, and
requires it to identify `GITHUB_SHA`, runs the same
headless Mixxx version smoke test, and uploads `Mixxx.flatpak` beside a
schema-1 GitHub candidate manifest.

The action inputs pin the exported repository to `repo` and the initialized
build directory to `build_flatpak`. The later bundle and smoke-test commands
consume those exact paths; do not rely on the action's provider defaults.

GitHub artifacts do not produce the Forgejo source archive. The Polinaria
promoter lists only successful `github/candidate` workflow runs, requires an
unexpired exact artifact, verifies the API SHA-256 digest of its ZIP archive,
requires exactly the bundle and metadata files, verifies the metadata/run/SHA
contract and bundle checksum/size, then imports and fscks the bundle and checks
its OSTree commit subject. It finally signs the OSTree commit and repository
summary. These checks make GitHub a verified build provider, not source
authority.

The promoter retains systemd's `RestrictSUIDSGID=yes` hardening. Its atomic
staging directories request mode `0775`; the setgid artifact root supplies the
shared `mixxx-artifacts` group by inheritance. The publisher must not request
mode `2775` directly, because the service sandbox correctly rejects an
explicit setgid bit before publication.

## Forgejo Workflow

The workflow selects the custom runner label:

```text
mixxx-flatpak-x86_64
```

It has an eight-hour job timeout, checks out a shallow copy of the exact Forgejo
event SHA without unused submodules, installs the required Flatpak SDK for the
runner user, and calls the publisher through the PSI pressure guard.

The runner is repository-scoped so unrelated repositories cannot schedule
work. It accepts one job at a time.

### Authenticated Actions API client

Forgejo Actions runs are available from the repository API. The deck client
helper uses the live `/api/v1/repos/total-infra/mixxx/actions/runs` and
`/actions/tasks` endpoints to select the exact candidate SHA, and the workflow
dispatch endpoint for the explicit fallback. `wait` accepts success only for
that SHA and then verifies the public manifest plus all three immutable files.

```bash
mixxx-deck-ci configure
mixxx-deck-ci runs
mixxx-deck-ci status <candidate-sha>
mixxx-deck-ci tasks <candidate-sha>
mixxx-deck-ci wait <candidate-sha>
mixxx-deck-ci dispatch
mixxx-deck-ci publication <candidate-sha>
```

Forgejo is configured to reject anonymous API calls. Create a scoped user token
at **Settings → Applications** with repository access limited to
`total-infra/mixxx`. Use `read:repository` for monitoring. Grant
`write:repository` only when autonomous manual dispatch is required. The
interactive `configure` command stores the token at
`~/.config/mixxx-deck/forgejo-actions-token`, requires mode 0600, and validates it
without placing the value in Git, shell history, or curl's command-line
arguments.

The Forgejo 15 REST schema reports authoritative run and task state but does
not publish a raw job-log endpoint. A successful exact-SHA run plus complete
immutable publication is machine-verifiable. If a run fails, use its returned
`html_url` for the signed-in log view or obtain the isolated runner log from
the VPS operator.

## Runner Isolation And Persistence

The VPS implementation is versioned in `andrew/total-infra`.

The runner:

- uses Forgejo Runner `12.7.3`, downloaded with a pinned SHA-256;
- provides Node 24 for host JavaScript actions;
- runs as a repository-scoped native `mixxx-runner.service` under a dedicated
  non-login `mixxx-runner` UID/GID;
- runs on a VPS with only 2 GiB physical RAM alongside production services;
- is limited to one CPU, 768 MiB resident memory, 768 MiB swap, a hard
  1536 MiB combined RAM+swap ceiling, and 512 processes;
- runs at CPU niceness 15 and best-effort I/O priority 7;
- handles one job at a time;
- has no Docker socket;
- has no privilege escalation, Docker socket, Linux capabilities, sudo, login
  shell, arbitrary host mount, or internal application/database network access;
- has no `/dev/fuse`; the service sets
  `MIXXX_FLATPAK_DISABLE_ROFILES_FUSE=1` so the build wrapper passes
  `--disable-rofiles-fuse` to Flatpak Builder;
- uses host Bubblewrap with Ubuntu's enforced `bwrap` AppArmor profile to make
  unprivileged build namespaces; the unit deliberately permits namespace
  creation but retains its cgroup, filesystem, device, process, and
  network-family restrictions;
- omits systemd `ProtectKernelTunables=yes` and `ProtectKernelLogs=yes`,
  because either locks part of `/proc` and prevents Bubblewrap from mounting
  Flatpak Builder's required inner `/proc`; the non-login runner has an empty
  capability bounding set and cannot alter host tunables or read kernel logs;
- writes only its private `/data` runner-state bind and private `/srv/artifacts`
  publication bind;
- restarts on failure.

Persistent runner data retains Flatpak SDK/dependency state, a bounded ccache,
and Flatpak Builder's source/cache state. The workflow clears transient job
output, temporary files, and action cache before and after each job, but retains
the source state on the private `/data` filesystem. A cold first build may be
much slower than later builds. The workflow sets `FLATPAK_BUILDER_JOBS=1`, and
`packaging/flatpak/flatpak_build.sh` passes that as `--jobs=1`.

Both synchronized manifests set `CCACHE_BASEDIR=/run/build` and
`CCACHE_NOHASHDIR=true` only inside Flatpak build sandboxes. This removes the
per-Actions-job host checkout path from cache keys while retaining compiler,
flags, headers, and generated-source inputs in the key. The publisher resets
ccache counters at the start of each attempt and prints the attempt hit/miss
statistics on either success or failure. It does not retain resumable build
trees or raise the 512 MiB cache budget.

Each custom workflow step runs through `tools/deck_private_log.sh`. On an
ordinary nonzero exit, or a caught `HUP`, `INT`, or `TERM`, it atomically records
the step name, UTC timestamp, exit status, optional termination signal, and the
last 2 MiB of that step's streamed output at private runner path
`/data/logs/latest.log`. `SIGKILL` and an abrupt host failure cannot be caught.
The directory is mode 0700 and the log is mode 0600. It is deliberately outside
`/srv/artifacts`, is not mounted into Caddy, is not published, and is replaced
only by a newer failed wrapped step. The final cleanup step is intentionally not
wrapped so it cannot replace the causal failure record. The log is diagnostic
output, not an artifact; an administrator may read it through the runner's
private `/data` bind and correlate it with the `mixxx-runner` system journal.
Do not copy it to Git or the public artifact tree.

Before compilation, the publisher runs Flatpak Builder's `--download-only`
mode. A recognized transient network failure (connection/DNS or low-throughput
timeout, reset, unreachable network, HTTP 429, or HTTP 5xx) is retried at most
three times, with 20- and 40-second backoff. The successful source state is
then reused for the real build with `--disable-download`; this avoids an
unavailable mirror failing after compilation has started. A checksum or any
other unrecognized source failure is not retried and remains an integrity
incident.

The workflow's first executable step acquires the root-owned TotalInfra
host-capacity lease before checkout. Forgejo queues later runs of this workflow,
and the lease client waits for application test/build/deploy work already using
the host. The lease remains bound to the repository, commit, and workflow run
through publication and final cleanup. Both `tools/deck_pressure_guard.sh` and
`tools/deck_flatpak_publish.sh` validate it with the gateway before heavy work;
direct execution without the lease fails closed. The final workflow step
releases it, while the gateway's bounded expiry is the cancellation/crash
fallback. The Mixxx runner still has no application deployment operation,
Docker access, or shared runner registration.

`tools/deck_build_preflight.sh` distinguishes cold and warm runner state. It
requires 12 GiB free before a cold SDK setup and 6.5 GiB before a warm compile;
the publisher repeats the warm gate. Retained Flatpak SDK data, Flatpak Builder
source state, and ccache are capped at 5 GiB. It otherwise fails closed unless:

- host swap totals at least 512 MiB;
- current `MemAvailable + SwapFree` totals at least 1536 MiB;
- the separate `/srv/artifacts` artifact filesystem has at least 1 GiB free;
- runner data and artifacts resolve to different filesystems;
- the runner cgroup allows at least 768 MiB resident memory;
- the runner cgroup allows at least 768 MiB swap;
- numeric cgroup v2 RAM plus swap limits total no more than 1536 MiB.
- host PSI `some avg60` is no more than 10% and `full avg60` no more than 2.5%.

These gates make an attempt less dangerous; they do not guarantee that Mixxx
will link successfully within 1536 MiB. A failure should be contained inside
the runner cgroup rather than thrashing through gigabytes of host swap.
During the build, `tools/deck_pressure_guard.sh` samples host PSI every ten
seconds. Six consecutive samples above either 60% `some avg10` or 20%
`full avg10` terminate the build.

Deck publication uses `org.mixxx.Mixxx.deck.yaml`, synchronized with the normal
manifest except for these intentional low-memory changes:

- Release `-O2` build with no debug information;
- Flatpak debug extraction disabled and binaries stripped;
- interprocedural optimization/LTO explicitly disabled;
- GNU BFD forced for executable and shared-library links;
- `--no-keep-memory` makes BFD reread symbols instead of retaining them;
- `--reduce-memory-overheads` selects slower, smaller linker data structures.
- The full Flatpak test suite is disabled; publication instead requires OSTree
  validation and a headless Mixxx smoke test.
- Source publication compression uses one Zstandard worker at level 3.

The normal manifest retains its existing developer/debug behavior.
`tools/check_deck_flatpak_manifest.sh` normalizes the intentional deck-only
differences and compares the result with the normal manifest. Both the workflow
and publisher refuse to build if any other manifest content drifts.

`Mixxx.Controls` is a pure-QML module with no C++ types. Qt 6.10's
`qmltyperegistrar` rejects generation of its empty C++ type metadata and its
`qmlcachegen` fails while creating the cache loader in the constrained no-FUSE
Flatpak build, so that environment uses `NO_GENERATE_QMLTYPES` and
`NO_CACHEGEN`. Suppressing generated type registration also removes
`qml_register_types_Mixxx_Controls()`, although Qt's generated static plugin
still references it. The module therefore compiles a minimal equivalent
registration source that calls `qmlRegisterModule`; its original QML sources,
generated `qmldir`, and static plugin remain enabled. The two suppression flags
are Flatpak-workaround-only, so normal builds retain Qt's generated registration
and cache loader.

The C++-backed `Mixxx` module retains its normal C++ runtime registration, but
uses the original embedded QML sources instead of Qt cache generation. Qt 6.10
fails while generating both its `.qmltypes` tooling file and cache loader in
Flatpak Builder's required no-FUSE rofiles mode; the typeinfo failure persists
even when redirected to private `/tmp`. Both Flatpak manifests therefore enable
a narrowly scoped CMake workaround: it runs the real registrar without the
failing tooling-file option, verifies the generated C++ registration source,
and writes Qt's valid empty tooling marker at the expected `.qmltypes` path.
This limits editor/tool metadata and bytecode caching for that build environment
only; it does not disable runtime registration, alter embedded QML resources,
or weaken runner isolation.

The infrastructure uses two explicit, provider-backed host paths. Systemd
binds them privately into the runner as `/data` and `/srv/artifacts`. Runner
data is writable only by the runner; artifacts are writable by the runner and
mounted read-only in Caddy. The VPS operator must provide both dedicated mount
paths and retain the existing runner registration state.

The fixed storage budget is made workable by:

- checkout depth 1 with no unused submodules;
- `/data/tmp` for temporary validation and archives instead of Docker `/tmp`;
- a compressed 512 MiB ccache and retained Flatpak Builder source state,
  together capped at 5 GiB with the Flatpak SDK;
- disabled Forgejo action caching for this runner;
- two retained immutable server builds;
- explicit cleanup of job build trees and the temporary Flatpak repository
  after every job while retaining only the capped runner state.

## Build And Publication Algorithm

`tools/deck_flatpak_publish.sh` performs these checks and actions:

1. Validate the broker-issued host-capacity lease for this repository, commit,
   and workflow run.
2. Validate absolute publication root, positive retention, `x86_64`, and exact
   candidate ref.
3. Enforce the host/cgroup memory and swap preflight.
4. Require build, Flatpak, Git, OSTree, checksum, compression, timeout, and
   locking tools.
5. Resolve checked-out `HEAD` and compare it with the Forgejo event SHA.
6. Reject tracked checkout modifications.
7. Acquire a publication lock so concurrent jobs cannot race publication.
8. Download all integrity-pinned Flatpak sources into the persistent runner
   state. Retry only recognized transient network failures at most three times;
   checksum and other source failures stop immediately.
9. Build `Mixxx.flatpak` with one Flatpak Builder job and disable new source
   downloads so it uses the verified prefetch state. Pass the exact source SHA
   through `MIXXX_FLATPAK_SOURCE_SHA`; `flatpak_build.sh` exports the commit
   with `--subject=Built from <SHA>`:

   ```bash
   packaging/flatpak/flatpak_build.sh bundle
   ```

10. Import the bundle into a temporary OSTree repository.
11. Run `ostree fsck`.
12. Require `app/org.mixxx.Mixxx/x86_64/master`.
13. Read the subject field from locale-stable normal `ostree show` output and
    require it to identify the Git source SHA. The subject is part of the
    commit object, not detached metadata; `ostree show` has no `-s` option.
14. Run a 30-second headless `/app/bin/mixxx --version` smoke test.
15. Create a `git archive` source tarball compressed with Zstandard.
16. Calculate bundle/source SHA-256 values and bundle byte length.
17. Write manifest schema version 1.
18. Atomically move files from a staging directory into the immutable build
    directory.
19. If the immutable directory already exists, require the bundle checksum to
    match.
20. Query the remote candidate ref. If a newer candidate exists, retain this
    immutable build but do not promote it.
21. Atomically replace `latest.json`.
22. Retain the two most recent server build directories by default.

A failure before step 21 leaves the previous `latest.json` unchanged.

## Artifact Layout

```text
/srv/artifacts/
├── latest.json
├── flatpak/
│   ├── mixxx.flatpakrepo
│   └── repo/
└── builds/
    └── <source-sha>/
        ├── Mixxx.flatpak
        ├── manifest.json
        └── source.tar.zst
```

Public layout:

```text
https://forge.polinaria.world/artifacts/latest.json
https://forge.polinaria.world/artifacts/builds/<sha>/manifest.json
https://forge.polinaria.world/artifacts/builds/<sha>/Mixxx.flatpak
https://forge.polinaria.world/artifacts/builds/<sha>/source.tar.zst
```

Caddy serves `latest.json` with `Cache-Control: no-store`. Build paths receive
a long-lived immutable cache policy. The Flatpak descriptor and repository
summaries are never cached; repository objects and static deltas are immutable.
Static-delta generation is disabled by default on the 2 GiB VPS because its
temporary memory use can prevent the signed summary from completing. It may be
enabled explicitly only after a bounded resource test; Coal can update from the
normal immutable OSTree objects without deltas.

`/data/logs/latest.log` is not part of this layout and must never be added to
it or served by Caddy.

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
  "bundle_url": "https://forge.polinaria.world/artifacts/builds/<source-sha>/Mixxx.flatpak",
  "source_url": "https://forge.polinaria.world/artifacts/builds/<source-sha>/source.tar.zst",
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
~/.local/bin/deck_ostree_validation.sh
```

Cache:

```text
~/.cache/mixxx-deck/builds/<provider>/<source-sha>/
```

`provider` is `forgejo`, `github`, or `local` for an exported rollback
snapshot. A pre-provider cache at `builds/<source-sha>/` remains readable as
`legacy:<source-sha>` and is not destructively migrated.

State:

```text
~/.local/state/mixxx-deck/
```

Default client retention is three recent builds, with current, previous, and
staged builds protected from pruning even when those are distinct.

## Deck Command Semantics

```bash
mixxx-deck status
mixxx-deck github-configure
mixxx-deck check
mixxx-deck stage [auto|forgejo|github|forgejo:<source-sha>|github:<source-sha>]
mixxx-deck activate [auto|forgejo|github|forgejo:<source-sha>|github:<source-sha>]
mixxx-deck deploy [auto|forgejo|github|forgejo:<source-sha>|github:<source-sha>]
mixxx-deck rollback
mixxx-deck auto-update
mixxx-deck run [mixxx-args...]
```

`auto` is the default and `latest` remains an alias. It compares Forgejo's
manifest `built_at` with GitHub's successful workflow completion timestamp;
Forgejo wins an exact timestamp tie. If the selected provider fails final
download or provenance verification, automatic staging tries the other already
verified descriptor. Explicit provider targets never silently switch provider.

Configure GitHub only on decks that should use that fallback:

```bash
mixxx-deck github-configure
```

Use a fine-grained token limited to the fallback repository with **Actions:
read** only. The client writes it to
`~/.config/mixxx-deck/github-actions-token` mode 0600, validates it against the
repository API, and passes it to curl through a temporary mode-0600 config file
instead of the command line. Do not put the token in Git, shell history, or a
manifest.

### Status

Local, read-only view of installed/staged/previous provider-qualified builds,
Forgejo manifest URL, GitHub fallback configuration state, running state, and
Flatpak metadata.

### Check

Selects the newest verified available provider descriptor, then compares its
source SHA with the installed Flatpak. No bundle is downloaded.

### Stage

Fetches and verifies the selected provider's metadata and bundle. Forgejo uses
the immutable public manifest/bundle contract; GitHub uses the authenticated
Actions artifact contract. Both require local OSTree import/fsck and a source
SHA in the bundle subject. Staging may run while Mixxx is active because it does
not alter the installed app.

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

Launches the user Flatpak while holding a shared deployment lock for the
process lifetime. The local desktop entry routes normal graphical launches
through this command and preserves Flatpak file forwarding.

### Automatic update

The user systemd timer starts with the lingering user manager at boot, triggers
the service within one minute, and retriggers it four hours after each completed
check. The service waits up to three minutes for NetworkManager. It reads signed
repository metadata on any power source. If a new commit exists, it downloads
and deploys only on AC power.

Before downloading, it exits successfully when Mixxx is running. Process
detection requires both a matching `flatpak ps` record and a live wrapper PID
when the client can confirm it is in the host PID namespace. A matching record
whose host PID is hidden by an isolated maintenance namespace is treated as
active. This ignores confirmed-dead Flatpak instance records on the host while
treating namespace, command, or parse uncertainty as active and deferring
safely. After a download-only Flatpak pull it
attempts the nonblocking exclusive deployment lock and checks again. A launch
during download therefore leaves a verified pending update without changing
the installed deployment. Activation exports the current build for rollback,
deploys from the local Flatpak object cache, verifies the installed commit and
source SHA, and gives `mixxx --version` up to 90 seconds offscreen with an
isolated temporary home. A failure first requests the previous commit from the
local Flatpak repository, verifies the actual installed commit and source, then
falls back to the checksum-verified cached bundle if the commit request was a
no-op. An unverified rollback is recorded as `rollback-failed` and cannot be
overwritten by a later `up-to-date` check. It never stops or restarts Mixxx.

`loginctl enable-linger <deck-user>` is required once so the user timer and its
service run without a graphical login. Status is written atomically under
`~/.local/state/mixxx-deck/` and is also available in the user journal.

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
# Optional expedited artifact-only build:
git push github <candidate-sha>:refs/heads/github/candidate
```

If the normal candidate push is rejected as non-fast-forward, use the bounded
candidate-pointer recovery procedure above. Do not merge a release pointer
back into `dev` merely to make the push fast-forward.

Open the newest **Deck Flatpak Build** in Forgejo Actions and validate the
runner, exact SHA, preflight, one-job build, absence of PSI/OOM termination,
and final **Success** state. If the push did not create a run within roughly a
minute, dispatch it once on `deck/candidate`.

### Public verification

```bash
candidate_sha="<the successful deck/candidate SHA>"

curl --fail https://forge.polinaria.world/artifacts/latest.json | jq .
curl --fail --head https://forge.polinaria.world/artifacts/latest.json
curl --fail --head \
  "https://forge.polinaria.world/artifacts/builds/${candidate_sha}/Mixxx.flatpak"
curl --fail --head \
  "https://forge.polinaria.world/artifacts/builds/${candidate_sha}/manifest.json"
curl --fail --head \
  "https://forge.polinaria.world/artifacts/builds/${candidate_sha}/source.tar.zst"
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
Exercise the separate REST Library catalog with multiple metadata pages and
verify that browsing issues no audio requests. A deck or AutoDJ action must
download only the requested tracks, and a failed catalog refresh must retain
the prior complete in-memory snapshot. Explicit deck loads outrank ordered
AutoDJ batches and recommendation prefetch; replacing recommendations may
cancel only prefetch-only work. Verify that switching credentials on one
configured origin clears the prior catalog and pending requests, and that two
credential contexts with overlapping remote track IDs never share cached
audio. Credential rotation and logout must each select a distinct cache
identity without placing a bearer token in a filename, log, or persisted
metadata.
Confirm that direct policy and authoritative recommendation rows expose the
same hydrated core metadata as the catalog without per-candidate requests.
For a newly downloaded quiet-master fixture, manual playback and AutoDJ must
wait for the low-priority gain-only preparation pass and use its positive
ReplayGain on first audible play. A preparation failure must remain visible and
skip/refuse the affected track without changing gain during active playback.
MixMan acceptance uses session contract v3: verify server-issued instance
resume, generation-fenced playback publication, the limited Mixxx
recommendation projection, and both configured-bearer and explicitly
auth-disabled trusted-LAN operation. Authentication failures must remain
fail-closed and must not silently retry without credentials. Bearer credentials
must remain in the OS keychain, require HTTPS outside loopback, and stay on the
configured origin. Rapid deck changes must converge to the newest serialized
remote mutation without affecting local playback.

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
2. inspect RAM, swap, disk, existing runner state, and memory pressure;
3. use the attached provider storage with separate runner-data and artifact
   filesystems, with 12 GiB free for a cold SDK setup or 6.5 GiB for a warm
   build, plus 1 GiB free for artifacts;
4. require at least 512 MiB host swap and 1536 MiB free memory-plus-swap;
5. validate `docker compose config` and the native systemd unit;
6. recreate Forgejo and Caddy;
7. enable the Actions unit for `total-infra/mixxx`;
8. create a repository-scoped runner;
9. preserve the repository-scoped runner registration under
   `/var/lib/mixxx-runner-data` without printing or rotating it;
10. install and start `mixxx-runner.service` as UID/GID `10001`;
11. verify its label, Bubblewrap self-test, private binds, resource limits, and
    logs;
12. manually dispatch the first build off-hours on `deck/candidate` if its push predates
   Actions enablement;
13. watch host pressure and verify `latest.json` plus all immutable files.
14. provision the non-login `mixxx-promoter`, its repository-scoped GitHub
    Actions-read credential, and a dedicated Flatpak repository signing key;
15. verify the signed descriptor and repository with a clean Flatpak remote.

See `docs/OPERATIONS.md` in the `andrew/total-infra` repository for exact
server commands.

After bootstrap, routine builds require no server login.

## Troubleshooting

| Symptom | Safe interpretation and response |
| --- | --- |
| Public manifest is 404 | Caddy route is not deployed or no build succeeded; keep current deck build |
| Job is queued | repository runner is offline or label does not match |
| Publisher rejects ref | manual dispatch selected a branch other than `deck/candidate` |
| Transient dependency download fails | runner retries the download-only prefetch up to three times; if exhausted, retry only after the mirror/network recovers |
| SDK/build dependency failure | diagnose VPS network/cache; never shift build to deck |
| Dependency archive checksum fails | stop before compilation; compare its complete tree with the authoritative upstream tag/commit; never copy the received checksum blindly |
| Hard-budget preflight fails | correct cgroup/headroom/disk/PSI configuration; do not bypass |
| PSI guard exits 75 | host pressure remained severe for one minute; let production recover before retrying |
| Action stops without a compiler error | inspect the private `latest.log` signal record and `mixxx-runner` journal; `SIGKILL` or host loss leaves no signal record |
| Build OOMs inside 1536 MiB | keep the ceiling and reduce build/link requirements further |
| Host thrashes/services degrade | stop the runner; verify the cgroup ceiling is actually active |
| OSTree check fails | bundle is not publishable |
| Smoke test fails | binary is not publishable |
| Existing SHA checksum differs | artifact integrity incident; do not overwrite |
| Promoter staging `mkdir` returns `Operation not permitted` | keep `RestrictSUIDSGID=yes`; ensure the publisher requests mode `0775` and inherits the setgid artifact group |
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
  https://forge.polinaria.world/artifacts/latest.json
curl --fail --head \
  https://forge.polinaria.world/artifacts/builds/<sha>/Mixxx.flatpak
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
systemctl --no-pager --full status mixxx-runner.service
journalctl --no-pager -u mixxx-runner.service -n 200
docker compose logs --tail=200 forgejo caddy
docker compose exec caddy caddy validate --config /etc/caddy/Caddyfile
```

Do not destroy existing volumes or replace the required provider-backed mounts
with root-backed Docker storage as a troubleshooting shortcut.

### Dependency source integrity

Flatpak verifies the bytes of every archive before extracting it. A forge may
regenerate an automatic tag archive after a server or compression change even
when the tagged Git tree has not changed. That produces a different archive
SHA-256 and a safe build failure.

Do not treat the checksum printed by the failure as the replacement value.
Download from the authoritative upstream, resolve the advertised tag to its
commit, and compare all extracted tracked files, file modes, and symlinks
against that commit. If the trees differ, stop as a source-integrity incident.
If they are identical, prefer changing the Flatpak source to `type: git` with
both the exact commit and tag pinned. Flatpak Builder shallow-clones Git sources
by default, so this is also suitable for small dependencies on the constrained
runner.

SoundTouch 2.4.0 is pinned this way because Codeberg regenerated
`2.4.0.tar.gz`: the former archive SHA-256 was
`3dda3c9ab1e287f15028c010a66ab7145fa855dfa62763538f341e70b4d10abd`,
while the later archive was
`b54ca9724afcf0b8c5326a0afb1b1676f2b892ee01c57d5312232ce4bc27a077`.
Both extracted to the exact 124-file tree at official tag `2.4.0`, commit
`d994965fbbcf0f6ceeed0e72516968130c2912f0`. The pipeline pins that commit
instead of trusting either generated-archive byte representation.

## Security Notes

- Write access to `deck/candidate` is deployment authority.
- Workflow code runs as the dedicated non-login runner user and can write only
  the configured private artifact publication bind. It can read the runner
  state of that same OS user and has outbound public-network access; this is
  native host execution, not a Docker confidentiality boundary.
- The native systemd service and its resource limits are the outer host-job
  boundary; Bubblewrap is the inner unprivileged Flatpak build sandbox.
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
- run `tools/deck_flatpak_auto_update_test.sh`;
- validate the systemd user units with `systemd-analyze verify`;
- validate the Forgejo workflow schema;
- run `git diff --check`;
- perform the actual build only on the VPS runner;
- test staging before activation;
- keep a previous verified bundle through acceptance.
