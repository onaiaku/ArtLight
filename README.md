<div align="center">

<img src=".github/assets/artlight.png" alt="ArtLight" width="180"/>

# ArtLight

**One Windows installer for the whole streaming stack.**

Self-hosted game streaming: your PC plays, your screen anywhere.

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/onaiaku/ArtLight?include_prereleases)](https://github.com/onaiaku/ArtLight/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/onaiaku/ArtLight/artlight-server.yml?branch=main)](https://github.com/onaiaku/ArtLight/actions)
[![Signed with](https://img.shields.io/badge/Code_signing-SignPath_Foundation-4c1)](https://signpath.org)

</div>

---

## What is ArtLight?

**ArtLight** turns a Windows gaming PC into a streaming host and gives you full control over the stream — in one installer.

It's two tools that share one install and one brand:

| | What it does |
|---|---|
| 🎮 **ArtLight Server** | The streaming engine. Serves your desktop or a virtual display to any Moonlight-compatible client over your network — hardware-encoded, low latency, with a virtual display driver, HDR support, and a built-in web UI for configuration. |
| 📊 **ArtLight Control** | The host-side control app. A live telemetry dashboard for your streams — session history, per-stream stats, NVIDIA driver tuning, game library sync, audio profiles — all without touching config files. |

Both are designed to be set up once and forgotten: install, pair your client, play.

## Highlights

- **Virtual display driver** — stream to a headless monitor with proper resolution and refresh-rate control
- **HDR / TrueHDR support** — with the pinned TrueHDR runtime
- **Hardware encoding** — NVENC and friends, frame pacing tuned for real gameplay
- **Live telemetry** — Control's dashboard tracks every session as it happens, no more blank stats
- **Game library sync** — Playnite integration so your library is stream-ready
- **Web UI** — configure the server from a browser, phone included
- **Works with Moonlight clients** — including our own [ArtMoon](https://github.com/onaiaku/ArtMoon)

## Requirements

- Windows 10 / 11 (x64)
- NVIDIA, AMD, or Intel GPU with hardware encoding
- A Moonlight client on the device you stream to — we recommend our own [**ArtMoon**](https://github.com/onaiaku/ArtMoon), built to pair with ArtLight out of the box

## Quick start

1. Download the latest installer from the [**Releases**](https://github.com/onaiaku/ArtLight/releases) page.
2. Run it — it sets up Server and Control together, pre-wired.
3. Open the server's web UI, pair your client, start streaming.

## Download & code signing

Windows release binaries (installer, MSI, and bundled executables) are code-signed through the **[SignPath Foundation](https://signpath.org)** — build provenance is verified from this public GitHub repository before signing.

## Privacy

ArtLight does not collect, transmit, or store any user data. All components run locally on your own hardware and network. There is no telemetry sent to the maintainers — stream statistics stay on your machine.

## Credits & lineage

ArtLight Server is a fork of [Sunshine](https://github.com/LizardByte/Sunshine) (via the Vibepollo fork), and stands on the shoulders of the Moonlight ecosystem. All upstream licenses are preserved — see [LICENSE](LICENSE) and [`server/NOTICE`](server/NOTICE).

ArtLight Control is a fork of [StreamTweak](https://github.com/FoggyBytes/StreamTweak) by FoggyBytes.

By **onaiaku** & Rias 💙
