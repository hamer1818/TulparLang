# Releasing TulparLang

TulparLang uses **stable-only versioning** — releases are created
exclusively when a `v*` tag is pushed. Every push to `main` (and
every PR) builds and tests the code, but **no GitHub Release or tag
is minted automatically**. This keeps version numbers meaningful and
avoids the version-number inflation that comes with per-commit
releases.

## Versioning scheme

Versions follow [Semantic Versioning](https://semver.org/):

| Bump    | When                                              | Example          |
| ------- | ------------------------------------------------- | ---------------- |
| MAJOR   | Breaking language/stdlib/ABI changes              | `v3.0.0`         |
| MINOR   | New features, backwards-compatible                | `v2.2.0`         |
| PATCH   | Bug fixes, performance, docs                      | `v2.1.1`         |

Pre-release suffixes (`-rc.N`, `-alpha.N`, `-beta.N`) are valid and
trigger the same workflow. Edit the `prerelease:` flag in
`.github/workflows/build.yml` if you want them marked as pre-release
on GitHub.

## Cutting a release

```bash
# 1. Make sure main is in the shape you want to ship.
git checkout main
git pull --ff-only

# 2. Tag the commit. Use annotated tags for better `git describe` output.
git tag -a v2.2.0 -m "TulparLang v2.2.0"

# 3. Push the tag. CI picks up the `v*` pattern, builds all three
#    platforms, runs tests, and publishes a GitHub Release with the
#    tag name as the version.
git push origin v2.2.0
```

Within ~10 minutes, `https://github.com/hamer1818/TulparLang/releases`
should show the new tag with all assets attached. `tulpar update`
users will see the new version on their next check.

## What CI does on each event

| Event                   | Build + Test | Create Release |
| ----------------------- | :----------: | :------------: |
| PR to `main`            | ✅           | ❌             |
| Push to `main`          | ✅           | ❌             |
| Push tag `v*`           | ✅           | ✅             |

## What gets published

Every release ships:

| Asset                                  | What it is                                |
| -------------------------------------- | ----------------------------------------- |
| `tulpar-linux-x64`                     | Linux x86_64 driver binary.               |
| `tulpar-macos-universal`               | macOS Apple Silicon + Intel binary.       |
| `tulpar-windows-x64.exe`               | Windows portable driver.                  |
| `tulpar-setup-windows-x64.exe`         | Windows GUI installer (Inno Setup).       |
| `libtulpar_runtime-<platform>.a`       | Per-platform runtime archive (linked into AOT-compiled user binaries). |
| `libwinpthread-1.dll` etc.             | Windows runtime DLLs the GUI installer bundles. Standalone copies so the one-line `install.ps1` can fetch them too. |
| `SHA256SUMS.txt`                       | `sha256sum -b` manifest. `tulpar update` verifies every download against this. |

The DLL bundling guard in `build-windows` runs `objdump -p` on the
freshly-built `tulpar.exe` and fails the build if any imported DLL is
neither a stock-Windows system DLL nor one of the bundled set.
Catches the "binary picked up a new transitive dependency we forgot
to ship" failure mode at CI time.

## `TULPAR_VERSION` resolution

At build time, the version embedded in the binary (returned by
`tulpar --version`, compared by `tulpar update --check`) is computed
as follows:

- **Tag push** (`refs/tags/v*`): the tag name verbatim — `v2.2.0`.
- **Branch push / PR**: CMake's default `<project_version>-dev` (e.g.
  `2.1.0-dev`). No release is published for these builds.

The tag-push path flows through the `TULPAR_VERSION` env var. If you
change the formula, update it everywhere it appears in `build.yml`
*and* the `Compute release tag` step in `create-release` so the
embedded version matches the published tag.

## Rolling back

Releases can be deleted in the GitHub UI. The tag itself stays unless
also deleted (`git push --delete origin v2.2.0`). `tulpar update`'s
SHA256SUMS verification means a partial / corrupt release won't be
silently consumed — but it will still try to fetch and fail loudly,
which is noisier than a clean rollback. Prefer publishing a fixed
follow-up release (`v2.2.1`) over deleting `v2.2.0`.

## Cleaning up old rolling tags

The legacy CI model created `v2.1.0.<run_number>` tags on every push
to main. To clean those up:

```bash
# List all rolling tags
git tag -l 'v2.1.0.*'

# Delete them remotely (in batches)
git tag -l 'v2.1.0.*' | xargs -n 50 git push --delete origin

# Delete them locally
git tag -l 'v2.1.0.*' | xargs git tag -d
```

> **Note:** Deleting old tags also removes the corresponding GitHub
> Releases. Users on `tulpar update` will be unaffected — they'll
> simply see the latest *stable* release going forward.
