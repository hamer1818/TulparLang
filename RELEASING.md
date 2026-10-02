# Releasing TulparLang

**Since 2026-09-21 releases are automatic.** Every merge to `main`
mints a tag and publishes a GitHub Release —
`.github/workflows/otomatik-surum.yml` computes the next version and
pushes the tag; `build.yml` then builds, runs the tests and creates
the Release exactly as it always did for a hand-pushed tag. Nothing
is published without a green build: the tag run is a full build from
source (tag pushes never reuse PR artifacts).

The numbers stay meaningful — this is **not** the rolling
`v2.1.0.<run>` scheme the project removed in 3.13.0. Each merge takes
one SemVer step, PATCH by default:

- **PATCH** (default) — anything that isn't marked otherwise.
- **MINOR** / **MAJOR** — put a line `Surum: minor` or `Surum: major`
  (`Release: ...` also works) in the PR description / commit body.
  Squash-merge carries it into the commit message, which is what the
  workflow reads.

Skipped automatically (both noted in the job summary, never silent):
a docs-only merge (`README.md`, `benchmarks/RESULTS.{md,json}` —
the binary is unchanged) and a commit that already carries a `v*` tag
(so re-running the job cannot double-release).

You can still cut a release by hand — push a `v*` tag as below — and
the auto-release job will simply see the tag and stand down.

There is **no version number to bump by hand** (since 2026-10-02).
`CMakeLists.txt` used to carry `project(TulparLang VERSION 3.13.1)` and it
fed the `<version>-dev` label of every non-release build; the automation
never bumped it, so branch and local builds said `3.13.1-dev` while
`v3.37.x` was out. The string is now derived from the git tag on every
build (`cmake/TulparVersion.cmake`), see *`TULPAR_VERSION` resolution*
below.

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
| `libtulpar_runtime-<platform>.a`       | Per-platform runtime archive (linked into AOT-compiled user binaries). |
| `TameEngine-<platform>.tar.gz`         | The 3D scene editor as a standalone bundle — binary + texture/sound/model palettes + sample scenes. Does **not** require the compiler to run. |
| `SHA256SUMS.txt`                       | `sha256sum -b` manifest. `tulpar update` verifies every download against this. |
| `SHA256SUMS.txt.asc`                   | Detached GPG signature over the manifest. Present only when the signing secret is configured (absent on forks). |

> **No Windows assets.** Native Windows support was dropped in 3.13.0 —
> `tulpar-windows-x64.exe`, the Inno Setup installer and the bundled MinGW
> DLLs are gone, along with the `build-windows` job and its `objdump -p`
> DLL-bundling guard. Windows users run the Linux build inside WSL.

### Why TameEngine ships as a bundle, not a bare binary

The editor's texture / sound / model browsers glob their paths **relative to
the working directory** (`examples/assets/dokular/*.png`,
`examples/assets/sesler/*.wav`, `varliklar/*.glb`), and the scene templates
build the same paths. A lone binary starts fine but shows empty palettes and
lays out untextured templates, so the archive preserves that layout and the
README tells the user to run it from inside the folder.

`tools/package_tameengine.sh` builds and **audits its own output** — palettes
non-empty, no build residue (`.o`/`.ll`) leaked, and on Linux the binary is
actually launched headless to prove it links and starts. A failing check
refuses to produce the archive rather than shipping a broken one. The
packaging step runs on **every** build, not only on tags: a release path that
is exercised once, at the worst possible moment, is how this repo has been
bitten before.

## `TULPAR_VERSION` resolution

At build time, the version embedded in the binary (returned by
`tulpar --version`, compared by `tulpar update --check`) is computed
as follows:

- **Tag push** (`refs/tags/v*`): the tag name verbatim — `v3.37.16`
  (`-DTULPAR_VERSION=<tag>`).
- **Everything else** (branch push, PR, local build): `git describe --tags
  --match 'v[0-9]*' --dirty` — `v3.37.16` on the clean tagged commit,
  `v3.37.16-4-gabc1234` four commits later, `-dirty` with uncommitted
  changes. No number is ever written by hand, so it cannot drift.
- **No git / no reachable tag** (source tarball, shallow clone):
  `0.0.0-dev` (`0.0.0-dev+g<sha>` when git works but no tag is reachable) —
  it says "unknown" instead of guessing. CI therefore checks out with full
  history (`fetch-depth: 0`, blobless).

It is regenerated on **every** `cmake --build` (the `tulpar_surum` target),
not cached at configure time — the old cached value survived a version bump
in the same build directory (measured 2026-09-02). An unchanged string
leaves the generated header untouched, so nothing recompiles.
`tools/surum_denetle.sh` runs right after the build in all three CI jobs and
fails when `tulpar version` differs from the tag / `git describe`; it also
self-checks the script's override, no-git and repo paths.

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
