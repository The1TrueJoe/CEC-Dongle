# CEC-Dongle

**REST-controlled HDMI-CEC bridge firmware + Control4 TV driver**

Custom firmware for the [SMLIGHT SLWF-08](https://github.com/smlight-tech/slwf-08) ESP8266 HDMI-CEC dongle. Replaces the ESPHome-based firmware with a standalone Arduino build that exposes full CEC bus control via REST API, a built-in web UI, and WiFi captive-portal provisioning. Includes a matching Control4 driver that presents as a TV proxy.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  SMLIGHT SLWF-08 (ESP8266 / ESP-12E)                   │
│                                                         │
│  ┌──────────┐  ┌───────────┐  ┌──────────────────────┐ │
│  │ CEC      │  │ WiFi Mgr  │  │ Async Web Server     │ │
│  │ Driver   │  │ AP + STA  │  │ REST API + Web UI    │ │
│  │ GPIO14   │  │ Captive   │  │ Port 80              │ │
│  │ bit-bang │  │ Portal    │  │                      │ │
│  └────┬─────┘  └───────────┘  └──────────┬───────────┘ │
│       │                                   │             │
│       │ ISR-based CEC 1.3a               │ HTTP        │
│       ▼                                   ▼             │
│  ┌─────────┐                     ┌────────────────┐    │
│  │ HDMI    │                     │ Config Mgr     │    │
│  │ CEC Bus │                     │ LittleFS       │    │
│  └─────────┘                     └────────────────┘    │
└─────────────────────────────────────────────────────────┘
        ▲                                   ▲
        │ CEC wire                          │ HTTP/REST
        ▼                                   ▼
   ┌─────────┐                     ┌────────────────────┐
   │ TV /    │                     │ Control4 Driver    │
   │ AVR /   │                     │ TV Proxy           │
   │ Devices │                     │ CEC ↔ C4 commands  │
   └─────────┘                     └────────────────────┘
```

## Features

### Firmware (`firmware/`)
- **CEC 1.3a bit-bang driver** — ported from [Palakis/esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec), interrupt-driven, no polling
- **REST API** — full CEC send/receive, convenience endpoints for power, volume, input, plus raw CEC
- **Web UI** — single-page dark-themed dashboard with live CEC bus log, configuration, control buttons
- **WiFi provisioning** — boots into AP mode with captive portal when no WiFi is configured; scan and connect via the web UI
- **Persistent config** — all settings stored in LittleFS (GPIO pin, CEC address, physical address, OSD name, promiscuous/monitor modes)
- **CEC event log** — ring buffer of recent CEC messages (configurable size), accessible via REST

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

### CEC Control
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/cec/send` | Send raw CEC frame. Body: `{"destination": 0, "data": [0x04]}` |
| POST | `/api/cec/power/on` | Image View On → TV (addr 0) |
| POST | `/api/cec/power/off` | Standby → Broadcast |
| POST | `/api/cec/volume/up` | Volume Up → Audio System |
| POST | `/api/cec/volume/down` | Volume Down → Audio System |
| POST | `/api/cec/volume/mute` | Mute → Audio System |
| POST | `/api/cec/source/active` | Active Source broadcast |
| POST | `/api/cec/input` | Switch input. Body: `{"physical_address": 0x2000}` |

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

## Building

### Firmware

Requires [PlatformIO](https://platformio.org/).

```bash
cd firmware

# Build
pio run

# Upload via USB
pio run --target upload

# Monitor serial
pio device monitor
```

### Control4 Driver

1. Package `control4/driver.xml` and `control4/driver.lua` into a `.c4z` archive
2. Add to your Control4 project via Composer Pro
3. Set the **Dongle IP Address** property to the ESP's IP
4. Bind HDMI inputs as needed in your project

```bash
cd control4
zip -j CEC-Dongle.c4z driver.xml driver.lua
```

## First-Time Setup

1. Flash the firmware to the SLWF-08 dongle
2. The dongle boots into **AP mode** — look for `CEC-Dongle-XXXXX` WiFi network (password: `cecdongle`)
3. Connect and open `http://192.168.4.1` in a browser
4. Go to the **WiFi** tab, scan for your network, enter credentials, and click Connect
5. The dongle will connect to your WiFi and the web UI will be available at its new IP
6. Configure CEC settings in the **Config** tab (logical address, physical address, OSD name)
7. Use the **Control** tab to test CEC commands
8. Install the Control4 driver and point it at the dongle's IP

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
│   ├── platformio.ini          # PlatformIO build config
│   └── src/
│       ├── main.cpp            # Entry point
│       ├── cec_driver.h/.cpp   # CEC 1.3a bit-bang driver
│       ├── config_manager.h    # LittleFS persistent config
│       ├── wifi_manager.h      # AP + STA WiFi management
│       ├── web_server.h        # REST API + route handlers
│       └── web_ui.h            # Embedded HTML/CSS/JS web UI
├── control4/
│   ├── driver.xml              # C4 driver manifest (TV proxy)
│   └── driver.lua              # C4 driver logic
└── README.md
```

## Credits

- CEC bit-bang implementation ported from [Palakis/esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec) (MIT License)
- Hardware config from [smlight-tech/slwf-08](https://github.com/smlight-tech/slwf-08)

## License

MIT
