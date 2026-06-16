# Deck Laptop Flatpak Testing

This workflow is for testing CI-built Mixxx changes on a Debian laptop connected to DJ hardware. Keep the laptop focused on installing and running artifacts; avoid rebuilding Mixxx there unless you need to debug a laptop-only issue.

## Build Artifact

Push the test branch to the fork and wait for the branch build workflow to finish. Download the `x86_64` Flatpak artifact from the successful run:

```bash
Mixxx-<git-description>-x86_64.flatpak
```

The matching `Debug.flatpak` artifact is only needed when you need debug symbols.

With GitHub CLI, the download loop is:

```bash
gh run list --repo <fork-owner>/mixxx --branch codex/rest-library-phase1-2.5.6 --limit 5
gh run download <run-id> --repo <fork-owner>/mixxx -D ~/Downloads/mixxx-artifacts
find ~/Downloads/mixxx-artifacts -name '*x86_64.flatpak' ! -name '*.Debug.flatpak'
```

## Laptop Setup

The laptop needs Flatpak, Flathub, and Mixxx's USB udev rules. From a Mixxx source checkout:

```bash
tools/deck_flatpak_deploy.sh setup
```

Unplug and replug USB controllers after setup so the new udev rules apply.

## Install And Run

Install a downloaded CI artifact:

```bash
tools/deck_flatpak_deploy.sh install ~/Downloads/mixxx-artifacts/Mixxx-*-x86_64.flatpak
```

Launch Mixxx:

```bash
tools/deck_flatpak_deploy.sh run
```

For the common one-step deploy loop:

```bash
tools/deck_flatpak_deploy.sh install-run ~/Downloads/mixxx-artifacts/Mixxx-*-x86_64.flatpak
```

Check local setup state with:

```bash
tools/deck_flatpak_deploy.sh status
```

## Test Checklist

- Confirm Mixxx launches from the Flatpak.
- Confirm audio input and output devices appear.
- Confirm decks and controllers are detected after reconnecting them.
- Exercise the feature under test and note the artifact filename or GitHub Actions run ID.
- If a behavior differs from a source build, keep the artifact and run output for comparison.

## Source Build Fallback

Only build on the laptop when artifact testing is not enough. Use the normal Debian build environment and keep the build directory for incremental rebuilds:

```bash
tools/debian_buildenv.sh setup
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQT6=ON -DQML=ON -DBULK=ON -DFFMPEG=ON -DLOCALECOMPARE=ON -DMAD=ON -DMODPLUG=ON -DWAVPACK=ON -DINSTALL_USER_UDEV_RULES=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/mixxx
```
