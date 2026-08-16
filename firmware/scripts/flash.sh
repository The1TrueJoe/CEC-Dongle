#!/usr/bin/env bash
# Guided flash helper for the SLWF-08.
#
# The board has no auto-reset circuit, so the very first USB flash needs a
# manual FLH-button dance before esptool can talk to it (see
# https://the1truejoe.github.io/CEC-Dongle/docs/building-and-flashing).
# Everything after that first flash can go over the air — see `ota.sh`.
#
# Usage: ./scripts/flash.sh [upload-port]
#   upload-port is optional; PlatformIO auto-detects it if omitted.

set -euo pipefail
cd "$(dirname "$0")/.."

PORT_ARG=()
if [ "${1:-}" != "" ]; then
  PORT_ARG=(--upload-port "$1")
fi

echo "== CEC-Dongle USB flash =="
echo
echo "1. Hold the FLH button on the dongle"
echo "2. Plug in the USB cable while still holding FLH"
echo "3. Release FLH — the chip is now in bootloader mode"
echo
read -rp "Press Enter once the dongle is in bootloader mode... "

echo
echo "-- Flashing filesystem (web UI + wizard) --"
pio run -e slwf08 -t buildfs -t uploadfs "${PORT_ARG[@]}"

echo
echo "-- Flashing firmware (no replug needed) --"
pio run -e slwf08 -t upload "${PORT_ARG[@]}"

echo
echo "Done. The dongle should now boot into WiFi setup mode —"
echo "look for an open network named CEC-Dongle-XXXXXX."
