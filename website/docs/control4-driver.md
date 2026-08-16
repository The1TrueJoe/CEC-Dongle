---
sidebar_position: 4
---

# Control4 Driver

The driver (`control4/driver.lua`) presents the dongle as a standard TV proxy — power, volume, mute, input switching, and transport/navigation commands all translate to CEC frames sent through the dongle's REST API.

## Installation

```bash
cd control4
zip -j CEC-Dongle.c4z driver.xml driver.lua
```

CI packages this automatically on every push and attaches it to GitHub releases — grab `control4-driver-*.c4z` from the [releases page](https://github.com/The1TrueJoe/CEC-Dongle/releases) instead of building it yourself, if you'd rather not check out the source.

1. Add the `.c4z` to your project in Composer Pro
2. Control4 Director auto-discovers the dongle via SDDP and binds to it — no manual IP entry required
3. Connect HDMI inputs in the Connections tab

If SDDP discovery is unavailable on your network, set the **Dongle IP Address** property manually in Composer Pro.

## Real-time state via TCP push, not polling

Control4 holds a genuine persistent TCP socket to the dongle, not a poll loop. The driver points Control4's native network-binding system at the dongle's push server:

```lua
C4:SetConnectionAddressPort(TCP_BINDING, g_dongleIP, TCP_PORT)  -- port 9000
```

Control4's framework handles the actual connect/reconnect; `NetworkConnectionChanged()` fires on state transitions, and `ReceivedFromNetwork()` streams newline-delimited JSON lines (buffered and split on `\n`) into `ApplyCecState()`, which fires the matching Control4 proxy events — `POWER_STATE_CHANGED`, `INPUT_CHANGED`, `VOLUME_CHANGED`, `MUTE_CHANGED` — the moment the dongle observes a change on the CEC bus. There's also a slower `/api/status` HTTP poll, but that's only for connection-health and config sync (see below), not for the state changes themselves.

## Config lives on the dongle, not in Composer Pro

The driver polls `/api/status` periodically and pulls in `tv_logical_address`, `audio_logical_address`, `volume_target`, `power_on_command`, and `power_off_command` — every CEC routing decision is made from the dongle's own saved config, not from Control4 driver properties. Tune all of this from the dongle's web UI; Control4 picks it up automatically on the next poll.

This matters most for `power_on_command`/`power_off_command` — see [Troubleshooting](/docs/troubleshooting#tv-doesnt-respond-to-powerontv_on-or-powerofftv_off) for why these exist and how to pick the right value for your TV.

## User Control Pressed needs a Released follow-up

Per the CEC spec, `User Control Pressed` (`0x44`) must be followed by `User Control Released` (`0x45`) — some TVs treat an unreleased key as held or ignore it outright. The driver's `SendCECKey()` helper sends both automatically for every remote-key-based command (`power_on_command`/`power_off_command` set to `user_control_power`, and all navigation/transport commands).
