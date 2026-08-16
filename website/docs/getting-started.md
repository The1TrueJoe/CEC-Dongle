---
sidebar_position: 1
---

# Getting Started

CEC-Dongle is standalone Arduino firmware for the [SMLIGHT SLWF-08](https://smlight.tech/en-US/products/slwf-08) HDMI-CEC controller — a small ESP8266 dongle that plugs into a free HDMI port. It replaces the stock ESPHome firmware with a REST API, a built-in web UI, WiFi captive-portal provisioning, and a matching Control4 driver that presents the TV as a controllable proxy.

## What it does

- Bit-bangs the HDMI-CEC 1.3a protocol directly (no Home Assistant, no ESPHome) — interrupt-driven, no polling
- Exposes full CEC control over a [REST API](/api-reference) — send raw frames or use named commands (`tv_on`, `volume_up`, `input2`, …) that resolve destinations from your saved config
- Advertises itself over mDNS (`_cec._tcp`, `_http._tcp`) as soon as it joins WiFi — no static IP, no manual configuration on the client side
- Ships a [Control4 driver](/docs/control4-driver) that presents as a TV proxy with a persistent TCP push connection for real-time power/volume/input state

## Quick start

1. [Build and flash](/docs/building-and-flashing) the firmware and filesystem over USB (one-time — the board has no auto-reset circuit, so this needs the FLH button)
2. The dongle boots into an open WiFi network named `CEC-Dongle-XXXXXX` — connect to it and the setup wizard opens automatically at `http://192.168.4.1`
3. Pick your home network, optionally set a hostname, and hit Connect
4. Reconnect to your own network and open `http://<hostname>.local` (default `cec-dongle.local`)

From there, every future update can go over the air — see [Building & Flashing](/docs/building-and-flashing#ota-updates).

## Where to go next

- [REST API reference](/api-reference) — every endpoint, interactive
- [Control4 driver](/docs/control4-driver) — TV proxy setup and the TCP push mechanism
- [Troubleshooting](/docs/troubleshooting) — TV-specific CEC quirks and known gotchas
