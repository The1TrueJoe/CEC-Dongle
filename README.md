# CEC-Dongle

**REST-controlled HDMI-CEC bridge firmware + Control4 TV driver**

Custom firmware for the [SMLIGHT SLWF-08](https://github.com/smlight-tech/slwf-08) ESP8266 HDMI-CEC dongle. Replaces the ESPHome-based firmware with a standalone Arduino build that exposes full CEC bus control via REST API, a built-in web UI, and WiFi captive-portal provisioning. Includes a matching Control4 driver that presents as a TV proxy.

📖 **[Full documentation & interactive API reference](https://the1truejoe.github.io/CEC-Dongle/)**

## Features

### Firmware (`firmware/`)
- **CEC 1.3a bit-bang driver** — ported from [Palakis/esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec), interrupt-driven, no polling
- **REST API** — full CEC send/receive, convenience endpoints for power, volume, input, plus raw CEC
- **Web UI** — maintainable multi-file source in `firmware/ui/`, compiled into a single minified page, stored in LittleFS and served pre-gzipped
- **WiFi provisioning** — boots into AP mode with captive portal when no WiFi is configured; scan and connect via the web UI
- **Persistent config** — all settings stored in LittleFS (GPIO pin, CEC address, physical address, OSD name, promiscuous/monitor modes)
- **CEC event log** — ring buffer of recent CEC messages (configurable size), accessible via REST
- **Standard ESP OTA** — supports Arduino OTA updates over the network after the first wired flash

### Control4 Driver (`control4/`)
- **TV proxy** — appears as a standard TV in Control4, with 4 HDMI inputs
- **CEC command masking** — all Control4 TV commands (power, volume, mute, input, navigation, transport) are translated to CEC and sent via the dongle REST API
- **Configurable** — CEC logical addresses, volume target, power-on command style, poll interval
- **Status polling** — periodic health check of the dongle with online/offline state tracking
- **Raw CEC** — send arbitrary CEC commands from Composer Pro programming

## Hardware

| Component | Detail |
|-----------|--------|
| Board | SMLIGHT SLWF-08 |
| MCU | ESP8266 (ESP-12E) |
| CEC GPIO | GPIO14 (default, configurable) |
| HDMI Pin 13 | → GPIO14 (CEC data) |
| HDMI Pin 17 | → GND |
| HDMI Pin 18 | → 5V (optional, for bus power) |

## REST API Reference

All endpoints return JSON. CORS headers are enabled for browser access.

### Status
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | Device status (uptime, WiFi, heap, CEC config) |

### Discovery

The dongle advertises itself over mDNS/Bonjour as soon as it joins WiFi — no IP
configuration anywhere:

| Record | Value |
|--------|-------|
| Hostname | `<hostname>.local` (default `cec-dongle.local`) |
| Service | `_cec._tcp` on port 80 — browse this to find dongles specifically |
| Service | `_http._tcp` on port 80, TXT `model` / `version` / `api` / `push` |
| Also | SDDP multicast for Control4, ArduinoOTA on `_arduino._tcp:8266` |

```bash
dns-sd -B _cec._tcp
```

### CEC Control — named commands

Clients use command *names*, never opcodes. Destination addresses come from the
device config, so the same call works whether audio goes to a soundbar or the TV.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/cec/commands` | List every supported command name |
| GET/POST | `/api/cec/cmd?name=<cmd>` | Run one. Optional `&repeat=N` (1–5) |

```bash
curl -X POST "http://cec-dongle.local/api/cec/cmd?name=tv_on"
curl -X POST "http://cec-dongle.local/api/cec/cmd?name=volume_up&repeat=5"
curl -X POST "http://cec-dongle.local/api/cec/cmd?name=input2"
```

CEC sends are queued (max 4 in flight) and processed one at a time between
polling the bus — the request just waits for its turn, which normally takes
under 100ms. `503` means the queue is full (rapid-fire commands with a
non-responsive bus); back off and retry.

Commands: `tv_on` `tv_off` `standby_all` `active` `inactive` `power_status`
`audio_status` `request_active_source` `input1`–`input8`, plus every CEC 1.4b
remote key — `select` `up` `down` `left` `right` `root_menu` `setup_menu`
`contents_menu` `exit` `num_0`–`num_9` `channel_up` `channel_down`
`previous_channel` `play` `stop` `pause` `record` `rewind` `fast_forward`
`eject` `forward` `backward` `power_toggle` `power_on_key` `power_off_key`
`volume_up` `volume_down` `mute` `mute_on` `mute_off`.

Remote keys are sent as User Control Pressed (`0x44`) followed by the matching
Released (`0x45`), which is what most TVs require before acting on a keypress.

### CEC Control — raw and legacy
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/cec/send` | Send raw CEC frame. Body: `{"destination": 0, "data": [0x04]}` |
| GET/POST | `/api/cec/power/on` | Alias for `tv_on` |
| GET/POST | `/api/cec/power/off` | Alias for `tv_off` |
| GET/POST | `/api/cec/volume/up` | Alias for `volume_up` |
| GET/POST | `/api/cec/volume/down` | Alias for `volume_down` |
| GET/POST | `/api/cec/volume/mute` | Alias for `mute` |
| GET/POST | `/api/cec/source/active` | Alias for `active` |
| POST | `/api/cec/input` | Switch input by raw PA. Body: `{"physical_address": 0x2000}` |

### CEC Log
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/cec/log` | Get recent CEC messages |
| DELETE | `/api/cec/log` | Clear the log |

### Configuration
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/config` | Current configuration |
| POST | `/api/config` | Update configuration (partial updates OK) |

### WiFi
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/wifi/scan` | Scan for available networks |
| POST | `/api/wifi/connect` | Connect to network. Body: `{"ssid": "...", "password": "..."}` |

### System
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/system/restart` | Restart the device |
| POST | `/api/system/reset` | Factory reset (clears config) |
| POST | `/api/ota/update` | Flash firmware binary (multipart upload from browser) |

## Building & Flashing

Requires [PlatformIO](https://platformio.org/). All commands run from the `firmware/` directory.

### One-time setup

```bash
cd firmware
python3 -m pip install -r requirements-ui.txt
```

### First-time USB flash

The SLWF-08 has no auto-reset circuit, so you enter bootloader mode manually once before running both flash commands.

`./scripts/flash.sh` walks through this interactively (prompts for the FLH-button sequence, then runs both flash commands back to back). By hand:

1. Hold the **FLH** button on the dongle
2. Plug in the USB cable while still holding FLH
3. Release FLH — chip is now in bootloader mode
4. Run both commands immediately (the `--after no_reset` flag keeps the chip in bootloader mode between them):

```bash
cd firmware
pio run -e slwf08 -t buildfs -t uploadfs   # Flash filesystem (web UI + wizard)
pio run -e slwf08 -t upload                # Flash firmware — no replug needed
```

> **macOS**: port is usually `/dev/cu.usbserial-210` (auto-detected). Override with `--upload-port /dev/cu.usbserial-XXXX`.  
> **Linux**: port is usually `/dev/ttyUSB0`.

### Web UI OTA (one file, updates everything)

Build the combined binary once — it contains both the firmware and the web UI:

```bash
cd firmware
pio run -e slwf08           # builds firmware
pio run -e slwf08 -t buildfs  # builds filesystem image → combined.bin is created
```

The build script stitches them into `.pio/build/slwf08/combined.bin`. Then:

1. Open the device web UI → **Firmware** tab
2. Choose `combined.bin` → click **Upload**

The device flashes firmware first, then the filesystem, and restarts — everything updated in one upload. You can also upload `firmware.bin` alone if you only need a firmware change.

### PlatformIO OTA (command line)

```bash
cd firmware
./scripts/ota.sh cec-dongle.local   # or an IP address
```

By hand:

```bash
cd firmware
pio run -e slwf08-ota -t buildfs -t uploadfs  # Update web UI (auto-discovers device via SDDP)
sleep 8                                        # Wait for device to reinitialise filesystem
pio run -e slwf08-ota -t upload               # Update firmware
```

Manual IP override when SDDP multicast is blocked:

```bash
pio run -e slwf08-ota -t upload --upload-port 192.168.1.x
```

### Serial monitor

```bash
pio device monitor
```

## Control4 Driver

1. Package the driver files:

   ```bash
   cd control4
   zip -j CEC-Dongle.c4z driver.xml driver.lua
   ```

2. Add to your project in Composer Pro
3. Control4 Director auto-discovers the dongle via SDDP and binds to it — no manual IP entry required
4. Connect HDMI inputs in the Connections tab

If SDDP auto-discovery is unavailable on your network, set the **Dongle IP Address** property manually in Composer Pro.

## First-Time Setup

1. Flash firmware and filesystem to the dongle (see above)
2. The dongle boots into **WiFi Setup** mode — look for `CEC-Dongle-XXXXXX` in your WiFi list (open network, no password)
3. Connect your phone or laptop to that network and open `http://192.168.4.1`
4. The setup wizard opens automatically. Select your home network, enter the password, and optionally set a hostname
5. Tap **Connect** — the dongle joins your WiFi. The wizard shows when to rejoin your home network
6. Reconnect to your home network and open the dongle's IP in a browser (check your router's DHCP table, or watch the serial monitor)
7. Configure CEC settings in the **CEC** tab (logical address, OSD name, TV/audio routing)
8. Use **Bus Monitor** to see live CEC traffic and verify the setup

## CEC Quick Reference

| Physical Address | Meaning |
|------------------|---------|
| `0x1000` | HDMI 1 on TV |
| `0x2000` | HDMI 2 on TV |
| `0x3000` | HDMI 3 on TV |
| `0x4000` | HDMI 4 on TV |
| `0x2100` | HDMI 1 on device connected to TV HDMI 2 |

| Logical Address | Device |
|-----------------|--------|
| `0` | TV |
| `1` | Recording Device 1 |
| `4` | Playback Device 1 |
| `5` | Audio System |
| `8` | Playback Device 2 |
| `14` (0xE) | Free Use |
| `15` (0xF) | Broadcast |

## Project Structure

```
├── firmware/
│   ├── platformio.ini          # PlatformIO build config (USB + OTA environments)
│   ├── requirements-ui.txt     # Python deps for the UI build pipeline
│   ├── data/                   # Generated LittleFS image (do not edit directly)
│   │   ├── index.html.gz       # Main UI bundle — minified + gzipped
│   │   └── wizard.html.gz      # WiFi setup wizard — minified + gzipped
│   ├── scripts/
│   │   ├── build_web_ui.py     # Inlines CSS/JS, minifies with minify-html, gzips
│   │   ├── build_combined.py   # Post-build: stitches firmware.bin + littlefs.bin → combined.bin
│   │   └── sddp_ota.py         # PlatformIO extra script: SDDP OTA auto-discovery
│   ├── ui/
│   │   ├── index.html          # Main web UI markup
│   │   ├── styles.css          # Main UI styles
│   │   ├── app.js              # Shared globals, navigation, status refresh
│   │   ├── app-bus.js          # Bus monitor, frame builder, log rendering
│   │   ├── app-config.js       # Config form, WiFi scan/connect, firmware upload
│   │   ├── wizard.html         # WiFi setup wizard (served in AP mode)
│   │   ├── wizard.css          # Wizard styles
│   │   └── wizard.js           # Wizard step logic
│   └── src/
│       ├── main.cpp            # Entry point — wires all modules together
│       ├── cec_driver.h/.cpp   # CEC 1.3a bit-bang driver (ISR, ring buffer)
│       ├── config_manager.h    # LittleFS persistent config (JSON)
│       ├── ota_manager.h       # ArduinoOTA (UDP) support
│       ├── sddp_server.h       # SDDP multicast announcements for auto-discovery
│       ├── wifi_manager.h      # AP provisioning + STA connection management
│       └── web_server.h        # REST API, TCP push server (port 9000), UI serving
├── control4/
│   ├── driver.xml              # C4 driver manifest (TV proxy, SDDP search_type)
│   └── driver.lua              # C4 driver logic (TCP push, CEC commands)
└── README.md
```

## TCP Push

The dongle maintains a persistent TCP server on **port 9000**. Whenever the CEC bus state changes (TV power, active input, volume, mute), it immediately pushes a newline-delimited JSON snapshot to all connected clients. The Control4 driver holds a persistent connection and reacts in `ReceivedFromNetwork()` — no polling required.

```json
{"tv_power":"on","active_source":"0x2000","active_input":2,"volume":42,"mute":false,"last_updated_ms":123456}
```

## Credits

- CEC bit-bang implementation ported from [Palakis/esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec) (MIT License)
- Hardware: [smlight-tech/slwf-08](https://github.com/smlight-tech/slwf-08)

## License

MIT
