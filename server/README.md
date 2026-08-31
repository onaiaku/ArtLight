# ArtLight Server

**ArtLight Server** is the stream server of the [ArtLight](https://github.com/onaiaku/ArtLight) stack — it runs on your gaming PC and streams games to any Moonlight-compatible client.

Pairs with:
- **[ArtMoon](https://github.com/onaiaku/ArtMoon)** — our client, for a fully integrated experience
- **ArtLight Control** — the host-side dashboard shipped in the same installer

## What it does

- **Low-latency streaming** — self-hosted game stream host with support for AMD, Intel and NVIDIA GPUs, hardware encoding (NVENC, AMF, QSV) and AV1/HEVC
- **Display automation** — built-in virtual display driver, automatic display switching when a stream starts, and safeguards against getting "stuck" on a dummy display after crashes or reboots
- **Game library sync** — deep Playnite integration: recently played games appear in your client automatically, with artwork, launching and clean termination handled for you
- **Frame pacing** — RTSS and NVIDIA Control Panel integration to apply the right frame limit and disable V-Sync while streaming
- **Web UI** — a focused browser interface for setup, tuning, sessions and recovery

## Installation

Install via the unified **ArtLight installer**, which sets up both ArtLight Server and ArtLight Control together.

## Credits

Built on the work of the Sunshine, Apollo and Vibepollo projects.
