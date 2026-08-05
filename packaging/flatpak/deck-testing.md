# Deck Laptop Flatpak Testing

The deck laptop is a resource-constrained DJ appliance. It downloads and runs
VPS-built Flatpaks; it does not compile Mixxx or run heavyweight tests.

For the complete architecture, manifest contract, runner isolation model,
command semantics, one-time VPS bootstrap, failure recovery, and security
notes, read [deck-vps-pipeline.md](deck-vps-pipeline.md).

## Source And Build

Forgejo is the source of truth:

```text
ssh://git@forge.polinaria.world:900/total-infra/mixxx.git
```

Work from `dev`; it is the only long-lived development branch. `deck/candidate`
is the build/release pointer, so promote an exact reviewed commit already
reachable from `dev` only when you want a VPS build:

```bash
git push origin HEAD:refs/heads/deck/candidate
```

Forgejo Actions builds the `x86_64` Flatpak on the isolated VPS runner, validates
the OSTree bundle and Mixxx binary, then publishes immutable build files and
corresponding source at:

```text
https://forge.polinaria.world/artifacts/builds/<source-sha>/
```

`latest.json` is changed only after a successful build and validation.

For every candidate push, sign in to
`https://forge.polinaria.world/total-infra/mixxx/actions`, open the newest
**Deck Flatpak Build** run, and treat its final state as authoritative. A push
should create the run automatically within roughly a minute. If it does not,
dispatch the workflow once on `deck/candidate`; never select another branch.
Confirm runner `mixxx-flatpak-x86_64`, the exact checkout SHA, successful
hard-budget preflight, one Flatpak Builder job, no PSI exit 75 or cgroup OOM,
and final **Success** before checking publication.

The unprivileged native runner intentionally has no `/dev/fuse`; the service
sets `MIXXX_FLATPAK_DISABLE_ROFILES_FUSE=1`, so the build wrapper uses Flatpak
Builder's `--disable-rofiles-fuse` mode rather than adding a
device mount or privileged access.

For unattended status and publication verification, configure the repository
client once and then query or wait on the exact candidate:

```bash
tools/deck_forgejo_actions.sh configure
tools/deck_forgejo_actions.sh status <candidate-sha>
tools/deck_forgejo_actions.sh tasks <candidate-sha>
tools/deck_forgejo_actions.sh wait <candidate-sha>
```

Create the token in Forgejo user settings with access restricted specifically
to `total-infra/mixxx`. `read:repository` supports inspection; use
`write:repository` only if this client must also perform the manual
`dispatch` fallback. The token is stored outside Git in
`~/.config/mixxx-deck/forgejo-api-token` with mode 0600.

Dependency sources are integrity-pinned. Before compilation, the VPS retries a
transient source-download failure up to three times using its bounded private
source cache; the actual build then disables fresh downloads. A source checksum
failure must still stop before compilation and be verified against the
authoritative upstream tag; do not copy the received checksum into a manifest.
Forge-generated archives whose container bytes have proved unstable should be
replaced with exact Git commit-plus-tag pins. This keeps the source immutable
without compiling or investigating on the deck laptop.

The pure-QML `Mixxx.Controls` module intentionally does not generate C++ type
metadata or a QML cache loader. This avoids Qt 6.10 registrar/cache-generator
failures while retaining embedded QML resources, generated `qmldir`, and the
static plugin; it does not change deck runtime behavior or acceptance steps.
The C++-backed `Mixxx` module keeps automatic type registration and QML caching;
the Flatpak manifests enable a Qt 6.10 no-FUSE workaround that writes full
typeinfo to a private temporary file before directly copying it into the normal
generated location. Its C++ registration source is still generated and checked;
this avoids only the failing atomic typeinfo-file commit and does not grant the
runner access to `/dev/fuse`.

For a failed custom runner step, the VPS operator can inspect the runner-only
`/data/logs/latest.log` record. It retains the last 2 MiB of the most recent
failure with mode 0600 and is intentionally outside the published artifact
tree. Do not copy this diagnostic log to the deck, Git, or a public web route.

The current VPS has only 2 GiB RAM. The workflow requires at least 512 MiB host
swap, 1536 MiB currently free memory-plus-swap, 12 GiB free for a cold SDK
setup or 6.5 GiB for a warm build, and 1 GiB free on a separate artifact
filesystem. Retained SDK, Flatpak Builder source state, and ccache are capped
at 5 GiB. The runner has a hard 768 MiB RAM plus 768 MiB swap budget. Startup
PSI must remain below the encoded thresholds, and severe PSI for one minute
aborts a running build. Flatpak Builder uses one job and a Release/no-debug,
low-memory-linker manifest. If it OOMs, keep the ceiling and investigate; never
fall back to the deck.

## Laptop Setup

From a current Mixxx checkout, install the lightweight client and USB rules:

```bash
tools/deck_flatpak_deploy.sh setup
```

This installs `~/.local/bin/mixxx-deck`. Reconnect controllers after the first
udev setup.

## Stage, Activate, And Roll Back

Checking and staging are safe while Mixxx is running:

```bash
mixxx-deck check
mixxx-deck stage
```

Activation is deliberately blocked while Mixxx runs. Stop Mixxx, then:

```bash
mixxx-deck activate
mixxx-deck run
```

Before replacing a different installed build, activation exports it from the
local Flatpak repository into the rollback cache. This does not compile Mixxx.

The combined command still refuses to interrupt a running session:

```bash
mixxx-deck deploy
```

Return to the previously cached build with:

```bash
mixxx-deck rollback
```

Use `mixxx-deck status` to show installed, staged, previous, and available
source revisions.

## Acceptance Checklist

- Confirm the authoritative Forgejo Actions run finished **Success** for the
  exact candidate SHA.
- Confirm the manifest and bundle identify the requested source commit.
- Confirm Mixxx launches from the user Flatpak.
- Confirm audio input and output devices appear.
- Confirm decks and controllers are detected after reconnecting them.
- Exercise the REST recommendation library, MixMan session steering, and
  request diagnostics.
- Verify rollback launches with the same library database and controller
  configuration.

If a laptop-specific issue cannot be reproduced from the artifact, collect the
logs and debug it on the VPS or another development machine. Do not build Mixxx
on the deck laptop.

## Documentation Maintenance

When the workflow, publisher, manifest, client, runner, Caddy route, retention,
or safety behavior changes, update both this quick guide and
`deck-vps-pipeline.md`. Server orchestration changes must also update
`andrew/total-infra/docs/OPERATIONS.md`.
