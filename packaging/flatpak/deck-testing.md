# Deck Laptop Flatpak Testing

The deck laptop is a resource-constrained DJ appliance. It downloads and runs
VPS-built Flatpaks; it does not compile Mixxx or run heavyweight tests.

For the complete architecture, manifest contract, runner isolation model,
command semantics, one-time VPS bootstrap, failure recovery, and security
notes, read [deck-vps-pipeline.md](deck-vps-pipeline.md).

## Source And Build

Forgejo is the source of truth, GitHub is an optional faster builder, and the
official upstream remote remains the clean source for future synchronization:

```text
ssh://git@forge.polinaria.world:900/total-infra/mixxx.git
```

Work from `dev`; keep `main` identical to official `upstream/main` and never put
custom fork commits on it. Promote an exact reviewed commit already reachable
from `dev` to the provider-specific release branch:

```bash
git push origin HEAD:refs/heads/deck/candidate
git push github HEAD:refs/heads/github/candidate
```

The first command triggers the resource-constrained Forgejo runner and its
authoritative publication pipeline. The second triggers a GitHub-hosted build
using the same deck-specific manifest and validation, then retains
`Mixxx-flatpak-x86_64` as a three-day Actions artifact. It does not update
Forgejo's `latest.json`; it contains the bundle and a workflow-produced
metadata contract for the deck client. The release refs are pointers only and
must never receive direct development commits.

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
The C++-backed `Mixxx` module keeps automatic type registration, but also uses
its embedded original QML sources instead of cache generation after Qt 6.10
fails while building its cache loader. The Flatpak manifests enable a no-FUSE
workaround that keeps the generated C++ registration source and provides a
valid empty `.qmltypes` tooling marker. This limits QML editor metadata and
bytecode caching for the Flatpak build only; it does not change deck runtime
behavior or grant the runner access to `/dev/fuse`.

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

Forgejo is always available through its public manifest. To let the laptop use
the temporary GitHub fallback too, create a fine-grained GitHub token restricted
to `Firewolf34/mixxx-mixman-api` with **Actions: read** only, then run:

```bash
mixxx-deck github-configure
```

The client stores the token only at
`~/.config/mixxx-deck/github-actions-token` mode 0600. It never writes the
token to the checkout, an artifact, or a command line. Without this setup, the
client safely uses Forgejo alone.

## Stage, Activate, And Roll Back

Checking and staging are safe while Mixxx is running:

```bash
mixxx-deck check
mixxx-deck stage
```

`stage` defaults to `auto`: it chooses the newest verified completion between
Forgejo and GitHub, with Forgejo winning an exact timestamp tie. Use an explicit
provider when testing a particular build:

```bash
mixxx-deck stage forgejo
mixxx-deck stage github
mixxx-deck stage forgejo:<source-sha>
mixxx-deck stage github:<source-sha>
```

The client validates the Forgejo manifest/immutable bundle or, for GitHub, the
successful candidate run, unexpired artifact, Actions API archive digest,
artifact metadata, bundle checksum and size. It then imports the bundle into a
temporary local OSTree repository, runs `fsck`, and requires the expected
Flatpak ref and source SHA in its commit subject before recording it as staged.

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

Use `mixxx-deck status` to show installed, staged, and rollback
provider-qualified builds. `mixxx-deck check` shows the current available
provider and source revision.

## Acceptance Checklist

- Confirm the authoritative Forgejo Actions run finished **Success** for the
  exact candidate SHA.
- Confirm the selected provider and bundle identify the requested source commit.
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
