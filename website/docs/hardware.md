---
sidebar_position: 2
---

# Hardware

| Component | Detail |
|-----------|--------|
| Board | SMLIGHT SLWF-08 |
| MCU | ESP8266 (ESP-12E) |
| CEC GPIO | GPIO14 (default, configurable) |
| HDMI Pin 13 | → GPIO14 (CEC data) |
| HDMI Pin 17 | → GND |
| HDMI Pin 18 | → 5V (optional, for bus power) |

## No EDID / DDC line

The SLWF-08 only wires the single CEC data pin (GPIO14) to the HDMI connector — there's no I²C/DDC connection, confirmed against both official ESPHome configs SMLIGHT ships for this board ([`smlight-tech/slwf-08`](https://github.com/smlight-tech/slwf-08)). That means:

- The dongle **cannot read EDID**, so it can't auto-detect its own physical address (HDMI port number) from the TV — `cec_physical` stays a manually-configured value (default `0x4000`, i.e. "port 4 on the root TV").
- This only affects `active`/`input` routing. Power, volume, and navigation commands don't depend on it.

## No auto-reset circuit

Unlike most ESP8266 dev boards, the SLWF-08 doesn't wire GPIO0/EN to the USB serial adapter's DTR/RTS lines, so `esptool` can't automatically drop it into bootloader mode. Every **first-time** USB flash needs the manual FLH-button dance — see [Building & Flashing](/docs/building-and-flashing#first-time-usb-flash). Once WiFi is provisioned, all further updates can go over the air and never need this again.

## CEC bit-bang timing

The driver is a direct C++ port of [Palakis/esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec) (CEC 1.3a), adapted to run standalone on plain Arduino instead of inside ESPHome. Bit timing is interrupt-driven off `CHANGE` on the CEC GPIO; sending detaches the interrupt for the duration of the frame (bit-banging and interrupt-driven receive can't run concurrently on one pin).
