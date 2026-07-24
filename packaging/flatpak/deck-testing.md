# Deck Laptop Flatpak Testing

The deck laptop is a resource-constrained DJ appliance. It downloads and runs
VPS-built Flatpaks; it does not compile Mixxx or run heavyweight tests.

For the complete architecture, manifest contract, runner isolation model,
command semantics, one-time VPS bootstrap, failure recovery, and security
notes, read [deck-vps-pipeline.md](deck-vps-pipeline.md).

## Source And Build

Forgejo is the source of truth:

```text
ssh://git@forge.polinaria.world:900/andrew/mixxx.git
```

Promote an exact development commit to the deck channel:

```bash
git push origin HEAD:refs/heads/deck/candidate
```

Forgejo Actions builds the `x86_64` Flatpak on the isolated VPS runner, validates
the OSTree bundle and Mixxx binary, then publishes immutable build files and
corresponding source at:

```text
https://polinaria.world/mixxx-deck/builds/<source-sha>/
```

`latest.json` is changed only after a successful build and validation.

The current VPS has only 2 GiB RAM. The workflow therefore requires at least
4 GiB host swap, 3 GiB currently free memory-plus-swap, and 20 GiB free runner
data disk, then limits Flatpak Builder to one job. Run builds off-hours and move
the runner to a larger host if it OOMs or harms production services; never fall
back to building on the deck.

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
