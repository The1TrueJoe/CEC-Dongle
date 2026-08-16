---
sidebar_position: 5
---

# Troubleshooting

## TV doesn't respond to `power/on`/`tv_on` or `power/off`/`tv_off`

CEC has two ways for a source device to ask a TV to power on:

- **`Image View On`** (`0x04`) — the spec-intended signal for "a source is about to send video," sent automatically by real playback devices right before they start streaming
- **`User Control Pressed`** with the dedicated Power key (`0x44 0x6D` for on, `0x44 0x6C` for off) — literally simulating a remote button press

Symmetrically for power-off: **`Standby`** (`0x36`) vs. the dedicated Power-Off remote key.

Plenty of TVs only implement one path. The dongle's default is the spec-preferred one (`Image View On` / `Standby`), but **some TVs ACK the frame at the protocol level and then silently ignore it** — the command looks successful (`{"success":true}`) but nothing happens.

**Fix:** switch `power_on_command` and/or `power_off_command` to `user_control_power` in the device's **CEC** config tab (or `POST /api/config`). This was confirmed against a real TV that ignored `Image View On`/`Standby` entirely but reacted to the dedicated remote keys within under a second.

To diagnose which case you're in, watch the bus log while sending the command:

```bash
curl -X POST "http://<device>/api/cec/cmd?name=tv_on"
curl "http://<device>/api/cec/log"
```

If you see your `tx` frame followed by a `Report Power Status` reply that still says standby a poll or two later, the TV ACKed but ignored it — switch to `user_control_power`.

## WiFi setup network (`CEC-Dongle-XXXXXX`) reappears after an update

A filesystem update (`uploadfs`) rewrites the entire LittleFS partition, which is where `config.json` — and the WiFi credentials in it — live. Current firmware also persists WiFi credentials to a **separate** flash sector via the ESP8266 SDK's own storage, specifically so this survives a filesystem update; the device tries that automatically on boot if `config.json` comes back empty. If it still falls back to its setup AP, just reconnect through the wizard once — see [Getting Started](/docs/getting-started#quick-start).

## WiFi network scan shows entries with no name, or the scan hangs

ESP8266 has a single 2.4GHz radio shared between the softAP (serving the setup wizard) and the scan itself — a long uninterrupted scan can corrupt the SSID string table (network count and signal strength come back fine, names come back blank) or stall the AP link briefly. The firmware scans asynchronously in small slices specifically to minimize this, and the wizard auto-retries a few times before giving up. If it's still unreliable, move closer to the dongle during setup (shorter scan = smaller corruption window) or retry manually.

## Command returns `503 CEC busy`

The dongle queues at most 4 CEC sends at a time and answers each once the frame is actually on the bus (normally under 100ms) — sending isn't instant because every frame requires proper CEC bus arbitration and free-time signaling. A `503` means the queue is full, which usually means the bus itself is unresponsive (nothing ACKing) and each send is exhausting its retry budget (~350ms worst case) rather than completing quickly. Back off and retry; check `/api/cec/log` for repeated "No Ack"/"Bus Collision" entries if it persists.

## Physical address / input switching doesn't work right

The SLWF-08 has no EDID/DDC connection ([hardware details](/docs/hardware#no-edid--ddc-line)), so `cec_physical` can't be auto-detected — it has to match the actual HDMI port the dongle is plugged into (`0x1000` for port 1, `0x2000` for port 2, etc.) and needs to be set manually in config if you move it to a different port.
