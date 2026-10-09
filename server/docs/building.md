# Building ArtLight Server

ArtLight Server builds with CMake and Ninja. The CMake root is **`server/`** — every command
below runs from the repository root unless the step says otherwise.

If you only want to *run* ArtLight, install it instead. See the
[Linux install guide](linux/install.md) for Arch Linux and CachyOS, or the
[releases page](https://github.com/onaiaku/ArtLight/releases) for the Windows installer.
This page is for building it yourself.

The CI workflow is the authoritative recipe: [`.github/workflows/ci-archlinux.yml`](../../.github/workflows/ci-archlinux.yml)
for Linux, [`.github/workflows/artlight-server-windows.yml`](../../.github/workflows/artlight-server-windows.yml)
for Windows. If this page and the workflow ever disagree, the workflow is right.

## Toolchain

| Compiler | Version |
| :--- | :--- |
| GCC | 14+ |
| Clang | 17+ |
| Apple Clang | 15+ |

The browser interface is built with Node.js and npm. CMake's `web_ui` target installs the
locked dependencies with lifecycle scripts disabled, generates the design tokens, type-checks
the Vue source, and writes the production bundle to `<build-dir>/assets/web`. The server target
and installer packaging depend on it, and packaging fails rather than shipping an incomplete
configuration interface.

For frontend-only work, run `npm ci --ignore-scripts` then `npm run dev` from
`src_assets/common/assets/web`. `npm run build` writes a production bundle.

## Clone

```bash
git clone --recurse-submodules https://github.com/onaiaku/ArtLight.git
cd ArtLight
```

Submodules are not optional. The WebRTC wrapper, the PyroWave tree, and several packaging and
UI dependencies come in as submodules or downloaded release artifacts. A clone without
`--recurse-submodules` will fail at configure time rather than at build time.

## Linux

### Dependencies

Arch and CachyOS, matching what CI installs:

```bash
sudo pacman -S --needed base-devel cmake ninja git nodejs npm python openssl
sudo pacman -S --needed "$(pacman -Qqo /usr/lib/modules/$(uname -r)/vmlinuz)-headers"
```

The headers must match the kernel you are running, not the newest one available. The release
build also needs `dkms` for the virtual-display module and GCC 14, which the packages carry
because rolling Arch repositories no longer provide it.

`scripts/linux_build.sh` collects the dependency lists for Debian-based and Fedora-based
distributions as well; that script is a convenience, not a supported path.

### Configure, build, test

```bash
cmake -S server -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -j2
```

The test suite builds by default. Turn it off with `-DBUILD_TESTS=OFF` for a faster
edit-compile loop; a packaging build should always run it.

### Install

```bash
sudo cmake --install build
```

The package recipe overrides the install locations (`-DCMAKE_INSTALL_PREFIX=/usr`,
`-DSUNSHINE_EXECUTABLE_PATH=/usr/bin/artlight`, `-DSUNSHINE_ASSETS_DIR=share/artlight`,
publisher metadata). A plain local install uses the defaults and is not the same layout the
package produces.

### Building the Arch package

The packaged build is what Arch and CachyOS users receive, and it does more than a plain
build: it generates the PKGBUILD from the template, validates the AppStream metainfo and the
desktop files, and runs the test suite before packaging. Follow the recipe in the
[Linux install guide](linux/install.md#build-from-source).

### KMS capture and file capabilities

Do **not** add file capabilities to the `artlight` binary. On Linux the public `/usr/bin/artlight`
must stay unprivileged; KMS capture runs inside the private `/usr/libexec/vibeshine/artlight-host`,
which the package installs with exactly `cap_sys_admin,cap_sys_nice=p`, and the package hook
verifies that the public binary has no capabilities. Install through the Arch package rather
than running a capability-patched build directly.

The rest of `/usr/libexec/vibeshine/` is the privileged side: the session broker, the session
controller, the machine host, the display-power and limiter helpers, and the `vibeshine-*`
virtual-display helpers that come with the driver.

### CUDA

CUDA-enabled builds need CUDA Toolkit **12.0 or newer**. The native
[local build/deploy script](linux/local-development.md#build-settings) uses an installed
toolkit and compiler; it does not pin CUDA 13 or GCC 15. Choose a host compiler your toolkit
supports, and a C++ compiler and standard library that meet the project's C++23 requirements.

Release packages deliberately pin **CUDA 12.9.1 with GCC 14** so Maxwell, Pascal (including
GTX 10-series) and Volta targets stay in. CUDA 13 cannot compile those targets, so upgrading
the toolkit can *reduce* GPU compatibility. Release packaging also enables
`SUNSHINE_REQUIRE_CUDA_PASCAL=ON`, which requires CUDA 12.9 and the `sm_61` target. Developer
builds can leave that option off.

The Arch package uses a private build-time toolkit and does not require users to install the
CUDA toolkit to run ArtLight Server.

When you change toolchain versions, run `python3 tests/unit/test_cuda_release_policy.py` and
build the affected packages locally before publishing.

## Windows

> [!WARNING]
> Cross-compilation is not supported. Build on the target architecture.

Install [MSYS2](https://www.msys2.org), start **MSYS2 UCRT64**, and:

```bash
pacman -Syu
export TOOLCHAIN="ucrt-x86_64"

dependencies=(
  "git"
  "mingw-w64-${TOOLCHAIN}-boost"           # optional
  "mingw-w64-${TOOLCHAIN}-cmake"
  "mingw-w64-${TOOLCHAIN}-cppwinrt"
  "mingw-w64-${TOOLCHAIN}-curl-winssl"
  "mingw-w64-${TOOLCHAIN}-libjpeg-turbo"
  "mingw-w64-${TOOLCHAIN}-libpng"
  "mingw-w64-${TOOLCHAIN}-libwebp"
  "mingw-w64-${TOOLCHAIN}-miniupnpc"
  "mingw-w64-${TOOLCHAIN}-ninja"
  "mingw-w64-${TOOLCHAIN}-onevpl"
  "mingw-w64-${TOOLCHAIN}-openssl"
  "mingw-w64-${TOOLCHAIN}-opus"
  "mingw-w64-${TOOLCHAIN}-toolchain"
  "mingw-w64-${TOOLCHAIN}-MinHook"
  "mingw-w64-${TOOLCHAIN}-nsis"
)
pacman -S "${dependencies[@]}"
```

You also need [Node.js](https://nodejs.org) for the web interface, the
[.NET SDK](https://dotnet.microsoft.com/download) for ArtLight Control, and
[WiX Toolset](https://github.com/wixtoolset/wix) for the MSI.

### Build

```bash
cmake -S server -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target all
```

### Package the MSI

```bash
cmake --build build --target package_msi
```

This produces `ArtLight Server.msi`. It is an intermediate: the single installer consumes it, so it is
not published on its own. The download is `ArtLight Setup.exe`.

**The virtual gamepad driver is bundled by default on x64, and configure fails rather than quietly
building an installer without it.** `package_msi` needs the pinned libvirtualgamepad producer package.
Either supply it the way release CI does:

```bash
cmake -S server -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSUNSHINE_LIBVIRTUALGAMEPAD_PREBUILT_DIR=<unpacked libvirtualgamepad release> \
  -DSUNSHINE_VHF_GAMEPAD_RELEASE_TAG=v0.1.0-beta.6 \
  -DSUNSHINE_VHF_GAMEPAD_RELEASE_ASSET_SHA256=a45a8ae27d2764ad26a4b89d43d1e2dc43510d84bddd5899aac7c80499754782 \
  -DSUNSHINE_VHF_GAMEPAD_SOURCE_REVISION=4b56fb9da177f320fb2d7ddb1b6262e5d55d2750 \
  -DSUNSHINE_VHF_GAMEPAD_DRIVER_VER=09/22/2026,0.1.0.39 \
  -DSUNSHINE_VHF_GAMEPAD_PROTOCOL_VERSION=2
```

or pass `-DSUNSHINE_BUNDLE_VHF_GAMEPAD_DRIVER=OFF` to build one that deliberately ships without it.
Do not ship that one: it installs no virtual gamepad, and because the bootstrapper reads the bundle
state back out of the MSI, it also collapses the installer's *"Install ArtLight Server virtual gamepad
driver"* tick — leaving the user with no way to add the driver afterwards.

### Build the single installer

The `ArtLight Setup.exe` that users download is a bootstrapper containing the server MSI and,
optionally, the ArtLight Control installer. Three pieces:

1. **ArtLight Control** — publish it, then compile its installer:

   ```powershell
   dotnet publish control/ArtLightControlService/ArtLightControlService.csproj -c Release -r win-x64 --self-contained false
   dotnet publish control/ArtLightControl/ArtLightControl.csproj -c Release -r win-x64 --self-contained false
   & "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" control\Installer.iss
   ```

   Inno Setup 6 or 7 both work; the build looks for either.

2. **The bootstrapper** — embeds the Control installer and builds the Setup executable:

   ```powershell
   & server/packaging/windows/bootstrapper/build_bootstrapper.ps1
   ```

   Control is only offered in the wizard when a payload is actually embedded, so a build
   without it collapses that section instead of showing a broken option.

3. **Pinned artifacts** — the virtual display driver, the virtual gamepad driver and the
   TrueHDR runtime are downloaded at build time by the `scripts/download_*_release.ps1`
   helpers, and the WebRTC runtime is pinned by `scripts/pin_webrtc_release.ps1`.

### WebRTC (optional, Windows only)

ArtLight Server can link against the libwebrtc C++ wrapper when `SUNSHINE_ENABLE_WEBRTC=ON`.
The wrapper source is vendored as the `third-party/libwebrtc` submodule, but WebRTC itself must
be built separately and staged in a directory containing `include/` and `lib/`.

A helper script drives the whole depot_tools / gclient / gn / ninja flow:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_mingw_webrtc.ps1
```

By default it builds under `%LOCALAPPDATA%\ArtLight Server\deps\libwebrtc\src`, stages the
reusable SDK under `%LOCALAPPDATA%\ArtLight Server\deps\libwebrtc\out`, and removes the large
source workspace only after verifying the staged headers, DLL, import library and completed-build
DLL identity. The retained `out` directory is shared across every ArtLight Server build
directory, worktree and checkout on the machine, so wiping `build/` does not destroy it. A
failed build keeps its sources for diagnosis; pass `-RetainBuildSources` while iterating on
WebRTC itself.

`cmake/dependencies/webrtc.cmake` looks in that same default location, so no `-DWEBRTC_ROOT=...`
is needed after the first build. Set `ARTLIGHT_DEPS_DIR=<path>` to relocate the cache.

## Options that matter

| Option | Default | What it does |
| :--- | :--- | :--- |
| `SUNSHINE_ENABLE_CUDA` | `OFF` | Build the CUDA paths (NVENC and the CUDA-backed filters). |
| `SUNSHINE_REQUIRE_CUDA_PASCAL` | `OFF` | Require CUDA 12.9 and the `sm_61` target for a Pascal-compatible release build. |
| `SUNSHINE_ENABLE_PYROWAVE` | `ON` on Linux, `OFF` on Windows | Build the PyroWave codec. On Windows this compiles a stub. |
| `BUILD_TESTS` | `ON` | Build and register the test suite. |
| `BUILD_WERROR` | `OFF` | Treat warnings as errors. Packaging turns it on. |
| `SUNSHINE_ENABLE_WEBRTC` | `OFF` | Link the libwebrtc wrapper (Windows). |
| `SUNSHINE_CONFIGURE_ONLY` | `OFF` | Generate the packaged configuration files and exit, without building. |
| `SUNSHINE_CONFIGURE_PKGBUILD` | `OFF` | Generate the Arch `PKGBUILD` from its template. Pair with `SUNSHINE_CONFIGURE_ONLY`. |

The full list lives in [`server/cmake/prep/options.cmake`](../cmake/prep/options.cmake).

## Remote build

If you would rather not build locally:

1. Fork the repository.
2. Enable Actions on your fork.
3. Run the **ArtLight Server** workflow manually, or push a version tag to trigger a release build.
4. Download the artifacts from the run summary.

A manual dispatch with no inputs builds both platforms. On a push, the release gate decides
whether to build: it only treats the push as a release when the commit carries a version tag
that matches `project(ArtLightServer VERSION ...)` in `server/CMakeLists.txt` and no release
with that tag exists yet.

## macOS and FreeBSD

Build support for macOS and FreeBSD is inherited from upstream and is **not built or tested by
ArtLight**. There is no macOS or FreeBSD installer, and neither platform is part of a release.
`scripts/macos_build.sh` is a starting point, not a supported path.

## Troubleshooting

| Symptom | What to do |
| :--- | :--- |
| Configure fails immediately, complaints about missing sources | The clone is missing submodules. Run `git submodule update --init --recursive`. |
| `web_ui` target fails | Node.js or npm is missing, or the npm install failed. Run `npm ci --ignore-scripts` inside `src_assets/common/assets/web` and read the first error, not the last. |
| CUDA configure fails | The host compiler is not one your toolkit accepts. Set `CMAKE_CUDA_HOST_COMPILER`, or configure with `-DSUNSHINE_ENABLE_CUDA=OFF`. |
| Warnings are suddenly fatal | `BUILD_WERROR=ON` is set, usually because you configured with the packaging options. Configure without it for development. |
| Build succeeds but the app has no display driver | The driver is a kernel module, not part of the build. Install it with `sudo artlight driver install` after installing the package. |
| `check()` fails inside a `makepkg` run | The package test phase needs the running kernel's headers and, with CUDA, the pinned toolkit. Pass `--nocheck` only to unblock a local experiment. |
