<div align="center">

<img src=".github/assets/artlight.png" alt="ArtLight" width="180"/>

# ArtLight

**One installer for Windows. One command on Linux.**

Self-hosted game streaming: your PC plays, your screen anywhere.

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/onaiaku/ArtLight?include_prereleases)](https://github.com/onaiaku/ArtLight/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/onaiaku/ArtLight/artlight-server.yml?branch=main)](https://github.com/onaiaku/ArtLight/actions)

---

</div>

---

## What is ArtLight?

**ArtLight** turns a gaming PC into a streaming host and gives you full control over the stream. On Windows that is one installer; on Linux it is a package and a one-line script.

It's two tools that share one install and one brand:

| | What it does |
|---|---|
| 🎮 **ArtLight Server** | The streaming engine. Serves your desktop or a virtual display to any Moonlight-compatible client over your network — hardware-encoded, low latency, with a virtual display driver, HDR support, and a built-in web UI for configuration. |
| 📊 **ArtLight Control** | The host-side control app. A live telemetry dashboard for your streams — session history, per-stream stats, NVIDIA driver tuning, game library sync, audio profiles — all without touching config files. |

Both are designed to be set up once and forgotten: install, pair your client, play.

## Highlights

- **PyroWave codec** — a GPU wavelet codec with very low encode and decode latency. Frames are independent, so a dropped one doesn't force a new keyframe. SDR and HDR, 8- and 10-bit colour, and 4:2:0 or 4:4:4 where the GPU and capture path allow. It wants a fast wired LAN. H.264, HEVC and AV1 are all still there and unchanged.
- **Its own virtual gamepad driver** — Xbox Series, Xbox One, DualSense, DualShock 4 and Switch Pro emulation with no ViGEmBus and no licence or licence key. Rumble, impulse triggers, touchpad, motion sensors, battery reporting, lightbar and adaptive triggers where the profile, client and game support them. The Windows installer offers it as an option, on by default; pick `vhf` in the web UI for automatic controller selection.
- **Remote Monitor and Remote Input** — launch Remote Monitor from Moonlight on another device and it becomes an extra, independently streamed display beside your main session, up to four clients. Remote Input attaches a client for control with no video.
- **Virtual display driver** — stream to a headless monitor with proper resolution and refresh-rate control
- **HDR / TrueHDR support** — with the pinned TrueHDR runtime
- **Hardware encoding** — NVENC and friends, frame pacing tuned for real gameplay
- **Smoother VRR capture** — capture timing follows game presentation more closely, including uneven callbacks
- **Live telemetry** — Control's dashboard tracks every session as it happens, no more blank stats
- **Remote power** — sleep, hibernate or restart the host from the dashboard, without walking to it
- **Shared clipboard** — copy on one side, paste on the other while you stream, off until you switch it on
- **Game library sync** — Playnite integration so your library is stream-ready
- **Web UI** — configure the server from a browser, phone included. The v2 interface is now the default, with a changelog panel so you can see what changed without leaving the dashboard
- **Works with Moonlight clients** — including our own [ArtMoon](https://github.com/onaiaku/ArtMoon)
- **Runs on Linux too** — Arch Linux and CachyOS are supported, with the virtual display driver built as a kernel module for the kernel you are running

## Requirements

**Windows**

- Windows 10 / 11 (x64)
- NVIDIA, AMD, or Intel GPU with hardware encoding
- A Moonlight client on the device you stream to — we recommend our own [**ArtMoon**](https://github.com/onaiaku/ArtMoon), built to pair with ArtLight out of the box

**Linux (beta)**

- Arch Linux or CachyOS, x86_64
- KDE Plasma 6 on **Wayland**, started by SDDM or Plasma Login Manager
- Linux 6.16 or newer, plus the headers for the kernel you boot
- A GPU with a hardware H.264 encoder. Pre-login streaming is NVIDIA only
- Secure Boot is supported — no need to disable it

## Quick start

**Windows**

1. Download the latest installer from the [**Releases**](https://github.com/onaiaku/ArtLight/releases) page.
2. Run it — it sets up Server and Control together, pre-wired.
3. Open the server's web UI, pair your client, start streaming.

**Linux**

```bash
curl -fsSLO https://raw.githubusercontent.com/onaiaku/ArtLight/main/server/scripts/linux_install.sh
less linux_install.sh            # optional: read what it does
sudo bash linux_install.sh
```

It checks the requirements, installs the headers your running kernel needs, installs the package, opens the firewall if one is active, and tells you whether a reboot is needed. Re-running it is safe.

Manual installation, verification and troubleshooting are in the [**Linux install guide**](server/docs/linux/install.md).

## Download & code signing

Windows release binaries are **not yet code-signed**. An application to the SignPath Foundation was submitted and is awaiting broader project adoption before approval; signing will be added in a future release once approved. Until then, verify downloads by building from source or checking CI artifacts against this repository.

## Privacy

ArtLight does not collect, transmit, or store any user data. All components run locally on your own hardware and network. There is no telemetry sent to the maintainers — stream statistics stay on your machine.

## Credits & lineage

ArtLight Server is a fork of [Sunshine](https://github.com/LizardByte/Sunshine) (via the ArtLight fork), and stands on the shoulders of the Moonlight ecosystem. All upstream licenses are preserved — see [LICENSE](LICENSE) and [`server/NOTICE`](server/NOTICE).

ArtLight Control is a fork of [StreamTweak](https://github.com/FoggyBytes/StreamTweak) by FoggyBytes.

By **onaiaku** & Rias 💙
