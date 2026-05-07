# Releasing TulparLang

`build.yml` produces two flavours of GitHub Release:

1. **Rolling main-channel** — every push to `main` builds Linux/macOS/
   Windows binaries, mints a `v2.1.0.<run>` tag, and uploads the assets.
   `tulpar update` (without `--stable`) tracks this channel so users on
   the rolling channel always have the latest verified main-branch
   build.
2. **Tagged stable** — pushing a tag matching `v*` (e.g. `v1.0.0`,
   `v1.0.0-rc1`) builds the same artifacts but uses the tag name
   verbatim as the version. Used to mint named milestones that don't
   get overwritten by the next main commit.

## Cutting a stable release

```bash
# 1. Make sure main is in the shape you want to ship.
git checkout main
git pull --ff-only

# 2. Tag the commit you want to release. Tag names should be
#    semver-style: `v1.0.0`, `v1.0.0-rc.1`, etc.
git tag -a v1.0.0 -m "TulparLang v1.0.0"

# 3. Push the tag. The CI workflow picks up the `v*` tag pattern
#    and runs the same build matrix you'd see for a main push, but
#    publishes under the tag name instead of the rolling auto-version.
git push origin v1.0.0
```

Within ~10 minutes, `https://github.com/hamer1818/TulparLang/releases`
should show the new tag with all assets attached. `tulpar update` users
who explicitly request the stable channel will pick it up on their
next check.

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
once and reused across every job:

- **Tag push** (`refs/tags/v*`): the tag name verbatim — `v1.0.0`.
- **Branch push**: `v2.1.0.<github.run_number>` — rolling.

Both paths flow through the same `TULPAR_VERSION` env var. If you
change the formula, update it everywhere it appears in `build.yml`
*and* the `Compute release tag` step in `create-release` so the
embedded version matches the published tag.

## Pre-release / RC tags

`-rc.N`, `-alpha.N`, etc. are valid tag suffixes — they trigger the
same workflow. The release is created with `prerelease: false` by
default; if you want a tag marked as pre-release, edit
`.github/workflows/build.yml`'s `softprops/action-gh-release` step
under the create-release job and pass `prerelease: true`.

## Rolling back

Releases can be deleted in the GitHub UI. The tag itself stays unless
also deleted (`git push --delete origin v1.0.0`). `tulpar update`'s
SHA256SUMS verification means a partial / corrupt release won't be
silently consumed — but it will still try to fetch and fail loudly,
which is noisier than a clean rollback. Prefer publishing a fixed
follow-up release (`v1.0.1`) over deleting `v1.0.0`.
