---
sidebar_position: 3
---

# Building & Flashing

Requires [PlatformIO](https://platformio.org/) for building from source. If you just want to update an already-provisioned dongle, skip to [OTA updates](#ota-updates) — you don't need PlatformIO installed for that at all.

## One-time setup

```bash
cd firmware
python3 -m pip install -r requirements-ui.txt
```

## First-time USB flash

The SLWF-08 has no auto-reset circuit ([why](/docs/hardware#no-auto-reset-circuit)), so you enter bootloader mode manually once before the very first flash.

`firmware/scripts/flash.sh` walks through this interactively — prompts you through the FLH-button sequence, then runs both flash commands back to back:

```bash
cd firmware
./scripts/flash.sh                    # auto-detects the port
./scripts/flash.sh /dev/cu.usbserial-XXXX   # or specify it explicitly
```

Equivalent by hand, if you'd rather run the PlatformIO commands yourself:

1. Hold the **FLH** button on the dongle
2. Plug in the USB cable while still holding FLH
3. Release FLH — the chip is now in bootloader mode
4. Run both commands immediately (the firmware upload flags keep the chip in bootloader mode between them, so no replug is needed):

```bash
cd firmware
pio run -e slwf08 -t buildfs -t uploadfs   # flash filesystem (web UI + wizard)
pio run -e slwf08 -t upload                # flash firmware — no replug needed
```

:::tip macOS / Linux port
macOS is usually `/dev/cu.usbserial-XXXX` (auto-detected, override with `--upload-port`). Linux is usually `/dev/ttyUSB0`.
:::

Once this first flash is done and the [setup wizard](/docs/getting-started#quick-start) has joined it to your WiFi, every future update can go over the air.

## OTA updates

There are three ways to update a provisioned dongle — pick whichever fits.

### Web UI (no PlatformIO needed)

Grab `cec-dongle-combined.bin` from the [latest GitHub release](https://github.com/The1TrueJoe/CEC-Dongle/releases) — built automatically by CI, contains both firmware and filesystem in one file — then:

1. Open the device web UI → **Firmware** tab
2. Choose the `.bin` file → click **Upload**

The device flashes firmware first, then the filesystem, and restarts automatically. This is the only update path that needs no local tooling at all — useful for Control4 installers who don't have the firmware source checked out.

### PlatformIO OTA (command line)

```bash
cd firmware
./scripts/ota.sh cec-dongle.local     # or an IP address
```

Equivalent by hand:

```bash
cd firmware
pio run -e slwf08-ota -t buildfs -t uploadfs --upload-port <device-ip-or-hostname>
pio run -e slwf08-ota -t upload --upload-port <device-ip-or-hostname>
```

`slwf08-ota` auto-discovers the device via SDDP multicast if you omit `--upload-port`; pass it explicitly if discovery is unavailable on your network.

:::caution Filesystem updates wipe saved WiFi credentials from that partition
`uploadfs`/`buildfs+uploadfs` rewrites the *entire* LittleFS partition, including `config.json` — which is where WiFi credentials live. The firmware also persists WiFi credentials to a **separate** flash sector via the ESP8266 SDK's own storage (outside LittleFS), specifically so a filesystem update doesn't force you back through the setup wizard — the device falls back to that sector automatically on boot if `config.json` has no SSID saved. If you built firmware from *before* this was added, or if the SDK sector was itself never populated (very first boot, before any WiFi was ever configured), a filesystem-only update can still knock the device back into its setup AP — reconnect it through the wizard as usual if that happens.
:::

### Combined binary from source

```bash
cd firmware
pio run -e slwf08             # builds firmware
pio run -e slwf08 -t buildfs  # builds filesystem image → combined.bin is created
```

Produces `.pio/build/slwf08/combined.bin` — upload it via the web UI as above.

## How the combined binary works

`combined.bin` is firmware and filesystem image concatenated with a small header, assembled by `firmware/scripts/build_combined.py`:

| Offset | Size | Content |
|--------|------|---------|
| 0 | 4 bytes | Magic `"CECF"` |
| 4 | 4 bytes | Firmware size (uint32 LE) |
| 8 | 4 bytes | Filesystem size (uint32 LE) |
| 12 | 4 bytes | Reserved (zero) |
| 16 | firmware size | `firmware.bin` |
| 16 + firmware size | filesystem size | `littlefs.bin` |

`/api/ota/update` on the device detects the `CECF` header and flashes both halves in one pass; a plain `firmware.bin` (no header) is still accepted and flashes firmware only.

## Serial monitor

```bash
pio device monitor
```
