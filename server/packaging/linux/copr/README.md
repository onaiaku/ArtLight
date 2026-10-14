# COPR (Fedora) packaging

ArtLight's Fedora packages are built on COPR:
<https://copr.fedorainfracloud.org/coprs/onaiaku/artlight/>

COPR is **not** Fedora's official repository. It is a build service that compiles
the RPMs on Fedora's infrastructure and serves the result as our own dnf
repository. Users enable it with `sudo dnf copr enable onaiaku/artlight` and then
`sudo dnf install artlight`.

Fedora's official repositories are a separate track and are closed to ArtLight:
they require a formal package review and forbid both pre-built binaries and
network access during the build, and this package downloads and bundles the CUDA
toolkit. COPR is the home, not a stepping stone.

## Project configuration

Some of this lives only in COPR's settings, not in the repository. Keep this file
in sync with the live project.

| Setting | Value |
| --- | --- |
| Owner / project | `onaiaku` / `artlight` |
| Description | Self-hosted game stream host for Artmoon/Moonlight clients |
| Homepage | <https://github.com/onaiaku/ArtLight> |
| Internet access | enabled — the source script clones from GitHub and the build fetches CUDA |
| Build dependencies | `git` |
| Timeout | 18000 s |
| Source type | Custom (script) — `copr-build.sh` |

### Chroots

| Chroot | Enabled | Notes |
| --- | --- | --- |
| `fedora-43-x86_64` | yes | Previous release, EOL 9 December 2026. Ships `gcc14` natively. |
| `fedora-44-x86_64` | yes | Current stable. **Requires the Fedora 43 repo attachment below.** |
| `fedora-45-x86_64` | no | Not released yet. Enable after the final release; it should need nothing else. |

Keep only chroots that are expected to build. An enabled chroot that cannot build
marks the whole run `failed`, which hides a good chroot behind a bad one.

## The Fedora 44 `gcc14` attachment (critical)

Fedora 44 ships GCC 16 and has no `gcc14` package. `Sunshine.spec` requires
`gcc14` / `gcc14-c++`, and CUDA 12.9's host-compiler support stops at GCC 14 — so
those two packages are genuinely needed and cannot be swapped out. They exist in
Fedora 43's repository.

`fedora-44-x86_64` therefore carries two `additional_repos` pointing at Fedora 43:

- `https://dl.fedoraproject.org/pub/fedora/linux/updates/43/Everything/x86_64/`
- `https://dl.fedoraproject.org/pub/fedora/linux/releases/43/Everything/x86_64/os/`

Recreate it (note: `project-chroot` requires the `chrootname` argument, and
`project-chroot/edit` requires a JSON body):

```bash
python3 - <<'PY'
import base64, configparser, os, urllib.request
cp = configparser.ConfigParser(); cp.read(os.path.expanduser('~/.config/copr'))
c = cp['copr-cli']
auth = base64.b64encode(f"{c['login'].strip()}:{c['token'].strip()}".encode()).decode()
req = urllib.request.Request(
    "https://copr.fedorainfracloud.org/api_3/project-chroot/edit/onaiaku/artlight/fedora-44-x86_64",
    data=b'{"additional_repos":["https://dl.fedoraproject.org/pub/fedora/linux/updates/43/Everything/x86_64/",'
         b'"https://dl.fedoraproject.org/pub/fedora/linux/releases/43/Everything/x86_64/os/"]}',
    headers={'Authorization': f'Basic {auth}', 'Content-Type': 'application/json'}, method='PUT')
print(urllib.request.urlopen(req, timeout=60).status)
PY
```

Mixing Fedora 43 packages into a Fedora 44 build is safe here because only
packages *absent* from Fedora 44 can come from the extra repository; anything
present resolves to Fedora 44's newer version. This is the same workaround
`docs/linux/install.md` already documents for Fedora 44 users installing by hand.

## Version derivation

`copr-build.sh` clones the default branch and then derives the version:

- **Tagged commit** → the tag, minus any leading `v`.
- **Untagged tip** (main between releases) → `<newest release tag>-dev.<short sha>`,
  so packaging can still be proved between releases.

A bare `0.0.0` is deliberately refused by the build's own version guard, which
exists so a package can never sort below a real release and install over one. With
no tags at all the fallback stays `0.0.0`, so the guard still refuses — that
fail-safe is kept on purpose.

`Sunshine.spec` converts the `-` into RPM's `~` prerelease separator through
`%global rpm_version`, so `build_version 1.5.5-dev.593861f0` becomes
`Version: 1.5.5~dev.593861f0`.

## Submitting a build

`copr-cli` is not required — `~/.config/copr` plus the API is enough.

**COPR takes this script as pasted text; it does not read the repository copy.**
Editing `copr-build.sh` here has no effect on a build until the text is submitted.

```bash
python3 - <<'PY'
import base64, configparser, json, os, urllib.request
cp = configparser.ConfigParser(); cp.read(os.path.expanduser('~/.config/copr'))
c = cp['copr-cli']
auth = base64.b64encode(f"{c['login'].strip()}:{c['token'].strip()}".encode()).decode()
script = open('copr-build.sh').read()
body = {"ownername": "onaiaku", "projectname": "artlight", "script": script,
        "builddeps": "git", "enable_net": True, "timeout": 18000}
req = urllib.request.Request(
    "https://copr.fedorainfracloud.org/api_3/build/create/custom",
    data=json.dumps(body).encode(),
    headers={'Authorization': f'Basic {auth}', 'Content-Type': 'application/json'}, method='POST')
r = json.loads(urllib.request.urlopen(req, timeout=60).read())
print(r["id"], r["link"])
PY
```

Observed: submitting with `chroot_names` set to a single chroot still ran every
chroot the project had enabled. Chroots come from the project's enabled set.

## Verifying a build

Read the result **per chroot**, not from the headline state — one failing chroot
marks the whole run `failed` even when another succeeded.

```bash
# build result, per chroot
curl -s https://copr.fedorainfracloud.org/api_3/build/<build-id> | python3 -m json.tool

# published repo
curl -sL https://download.copr.fedorainfracloud.org/results/onaiaku/artlight/fedora-44-x86_64/repodata/repomd.xml
```

The RPMs land under `.../results/onaiaku/artlight/fedora-<NN>-x86_64/Packages/a/`.
A published package is the only proof the source script, the spec, `gcc14` and
CUDA 12.9-with-Pascal work together — an active build or a green source stage is
not.

## Attaching the RPMs to a GitHub release

The Fedora packages are built **once**, here on COPR. `fetch-copr-rpm.py` copies
the finished files onto the matching GitHub release, so a `dnf` user and someone
downloading by hand get the same bytes from the same compile. Nothing is rebuilt
for the release, and there is no second place to keep in step.

```bash
python3 server/packaging/linux/copr/fetch-copr-rpm.py --version 1.5.6 --output-dir incoming
```

The enabled chroot list comes from the COPR API, so a chroot added or removed in
COPR needs no change here. It fails unless *every* enabled chroot published that
exact version — a half-finished COPR run cannot half-populate a release.

`.github/workflows/attach-fedora-packages.yml` wraps it: resolve the release,
fetch, verify each package's own header with `rpm -qp`, attach, then read the
release back so the upload is proved rather than assumed. Dispatch it with the
v-less release tag. It needs no secrets — the COPR repository is public.

The version to ask for is whatever the spec made, not the tag: `1.5.6-rc1` is
packaged as `1.5.6~rc1` and `1.5.6-stable` as `1.5.6+stable`. `--print-version`
reports that transform without fetching anything.

Note the published filename keeps the Fedora release inside it
(`artlight-1.5.6-1.fc44.x86_64.rpm`), which still matches the installer's
`artlight-[0-9]*.x86_64.rpm` lookup. Each enabled chroot produces its own build,
so a Fedora 43 machine needs the `fc43` file — the release carries both.

## Constraints that must not be changed casually

- **The CUDA pin.** `Sunshine.spec` sets `cuda_version 12.9.1` and
  `-DSUNSHINE_REQUIRE_CUDA_PASCAL=ON`. CUDA dropped Maxwell, Pascal and Volta
  support after 12.9, and the GTX 1080 is Pascal. Bumping CUDA trades one group
  of users for another.
- **The `gcc14` ceiling.** CUDA 12.9 supports host compilers GCC 6.x–14.x.
  `--allow-unsupported-compiler` does not rescue GCC 15+ on `nvcc`.
- **The `0.0.0` fail-safe.** It exists to stop a dev package sorting over a real
  release.
