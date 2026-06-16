# Deck Laptop Flatpak Testing

This workflow is for testing LAN-built or GitHub-built Mixxx changes on a Debian laptop connected to DJ hardware. Keep the laptop focused on installing and running artifacts; avoid rebuilding Mixxx there unless you need to debug a laptop-only issue.

## Build Artifact

Keep the LAN repo as the source of truth:

```bash
git push blue codex/rest-library-phase1-2.5.6
```

For a local Linux build machine:

```bash
git fetch blue
git switch codex/rest-library-phase1-2.5.6
git pull --ff-only blue codex/rest-library-phase1-2.5.6
tools/flatpak_buildenv.sh setup --system
packaging/flatpak/flatpak_build.sh bundle
```

For the GitHub build mirror, add the fork as a remote once:

```bash
git remote add github https://github.com/Firewolf34/mixxx-mixman-api.git
```

Then push the branch to GitHub and run the manual workflow:

```bash
git push github codex/rest-library-phase1-2.5.6
```

In GitHub, run **Actions > Deck Flatpak Build > Run workflow** for `codex/rest-library-phase1-2.5.6`, then download the `Mixxx-flatpak-x86_64` artifact.

Copy the resulting Flatpak artifact to the deck laptop:

```bash
Mixxx.flatpak
```

The matching `Debug.flatpak` artifact is only needed when you need debug symbols.

If the build machine publishes named artifacts, use the `x86_64` bundle and keep the filename or commit hash with the test notes:

```bash
Mixxx-<git-description>-x86_64.flatpak
```

## Laptop Setup

The laptop needs Flatpak, Flathub, and Mixxx's USB udev rules. From a Mixxx source checkout:

```bash
tools/deck_flatpak_deploy.sh setup
```

Unplug and replug USB controllers after setup so the new udev rules apply.

## Install And Run

Install a copied Flatpak artifact:

```bash
tools/deck_flatpak_deploy.sh install ~/Downloads/mixxx-artifacts/Mixxx.flatpak
```

Launch Mixxx:

```bash
tools/deck_flatpak_deploy.sh run
```

For the common one-step deploy loop:

```bash
tools/deck_flatpak_deploy.sh install-run ~/Downloads/mixxx-artifacts/Mixxx.flatpak
```

Check local setup state with:

```bash
tools/deck_flatpak_deploy.sh status
```

## Test Checklist

- Confirm Mixxx launches from the Flatpak.
- Confirm audio input and output devices appear.
- Confirm decks and controllers are detected after reconnecting them.
- Exercise the feature under test and note the artifact filename or source commit.
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
