# CEC-Dongle Juno driver

A `tv` driver for [CEC-Dongle](https://github.com/The1TrueJoe/CEC-Dongle) — REST-controlled
HDMI-CEC bridge firmware for the SMLIGHT SLWF-08 (ESP8266). Power, volume, mute, and input
switching over the dongle's own HTTP API, with real-time state pushed over a held-open TCP
connection rather than polled.

Lives in `juno/` of the main [CEC-Dongle](https://github.com/The1TrueJoe/CEC-Dongle) repo,
alongside the firmware it drives and the Control4 driver it mirrors — all three read the same
REST API. Full hardware docs, REST API reference, and firmware source:
**<https://the1truejoe.github.io/CEC-Dongle/>**

## Setup

The dongle advertises itself over mDNS (`_cec._tcp`) the moment it joins WiFi, so setup
normally just means picking it off a list. If nothing answers — a different VLAN, multicast
blocked — enter its address by hand; it's shown on the dongle's own setup wizard.

## Why this shape

- **Commands, not opcodes.** The dongle's REST API already resolves CEC logical addresses and
  destinations from its own saved config (`GET /api/cec/commands` lists everything it accepts).
  This driver only ever names a command — `tv_on`, `volume_up`, `input3` — the same way the
  project's own Control4 driver does. All CEC tuning happens once, on the dongle's web UI.
- **Push, not poll.** The dongle runs a small TCP server on port 9000 that streams one JSON
  state snapshot per line the instant something changes on the CEC bus — see
  `firmware/src/web_server.h`'s `CecPushServer`. This driver holds that connection open
  (`[[transport]] kind = "tcp", keepalive = true`) instead of asking the device anything.
- **No absolute volume, no held keys.** CEC itself has neither — only step up/down and a
  keypress-then-release. Declaring capabilities the hardware cannot back up would mean a control
  surface offering buttons that quietly do nothing; `set_volume` and `hold`/`release` are
  refused with a reason instead.
- **Four HDMI inputs is a guess.** The SLWF-08 has no EDID/DDC line, so — unlike a Roku or a
  Hisense set, which can report their real inputs — this driver has no way to ask the TV how
  many ports it actually has. See the project's own [hardware
  docs](https://the1truejoe.github.io/CEC-Dongle/docs/hardware#no-edid--ddc-line).

## Building

```bash
cd juno
cargo build --release
cargo test
```

Betas are built by [`junohouse/driver-ci`](https://github.com/junohouse/driver-ci) on every
push to `main` that touches `juno/` (see `.github/workflows/juno-driver.yml` at the repo root —
GitHub only reads workflows from the top-level `.github/`, not a subdirectory's), published to
this repo's own GitHub releases. To work on this against a local core checkout, uncomment the
`[patch]` block in `Cargo.toml`.
