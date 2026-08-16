#!/usr/bin/env bash
# OTA update helper — pushes firmware + filesystem to an already-provisioned
# dongle over WiFi. No FLH button, no USB.
#
# Usage: ./scripts/ota.sh <device-ip-or-hostname>
#   e.g. ./scripts/ota.sh cec-dongle.local
#        ./scripts/ota.sh 192.168.1.42
#
# Filesystem updates rewrite the whole LittleFS partition, including saved
# WiFi credentials — the firmware also persists those to a separate flash
# sector so this doesn't normally force re-provisioning, but expect the
# device to be briefly unreachable while it reboots between steps.

set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" == "" ]; then
  echo "Usage: $0 <device-ip-or-hostname>" >&2
  exit 1
fi
TARGET="$1"

echo "== CEC-Dongle OTA update -> $TARGET =="
echo
echo "-- Updating filesystem (web UI + wizard) --"
pio run -e slwf08-ota -t buildfs -t uploadfs --upload-port "$TARGET"

echo
echo "-- Updating firmware --"
pio run -e slwf08-ota -t upload --upload-port "$TARGET"

echo
echo "Done."
