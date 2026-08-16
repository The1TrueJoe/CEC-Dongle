"""
SDDP OTA Discovery — PlatformIO extra script
=============================================
Automatically discovers the CEC-Dongle's current IP address via SDDP
(the same multicast protocol Control4 uses for auto-discovery) before
running an OTA upload or filesystem upload.

How it works
------------
1. Sends an SDDP SEARCH to the multicast group 239.255.255.250:1902
2. Waits up to TIMEOUT_S seconds for a NOTIFY reply with
   Type: "C4:CecDongle"
3. Parses the dongle IP from the "From: ip:port" header
4. Injects it as UPLOAD_PORT so espota.py targets the right device

If discovery times out (device offline, or same-subnet multicast blocked)
the configured upload_port is used as-is, so the build still works when you
pass --upload-port explicitly.

Usage in platformio.ini
-----------------------
[env:slwf08-ota]
extra_scripts =
    pre:scripts/build_web_ui.py
    scripts/sddp_ota.py
"""

Import("env")   # noqa: F821  (SCons import)

import socket
import time

MCAST_GRP  = "239.255.255.250"
SDDP_PORT  = 1902
TIMEOUT_S  = 5
# SDDP SEARCH message — asks any CEC-Dongle on the subnet to identify itself
SEARCH_MSG = (
    "SEARCH * SDDP/1.0\r\n"
    "ST: \"C4:CecDongle\"\r\n"
    "\r\n"
).encode()


def _discover_ip():
    """Send an SDDP SEARCH and return the first responding dongle's IP, or None."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 4)
        sock.settimeout(TIMEOUT_S)
        sock.sendto(SEARCH_MSG, (MCAST_GRP, SDDP_PORT))

        deadline = time.time() + TIMEOUT_S
        while time.time() < deadline:
            try:
                data, _addr = sock.recvfrom(1024)
                text = data.decode("utf-8", errors="ignore")
                # Only accept replies from a CEC-Dongle
                if "C4:CecDongle" not in text:
                    continue
                # Parse "From: <ip>:<port>" or "From: <ip>"
                for line in text.splitlines():
                    if line.lower().startswith("from:"):
                        ip_port = line.split(":", 1)[1].strip()
                        # ip_port is "192.168.1.x:1902" — extract just the IP
                        ip = ip_port.split(":")[0].strip()
                        if ip and ip != "0.0.0.0":
                            return ip
            except socket.timeout:
                break
    except Exception as exc:
        print("[SDDP] Discovery error: {}".format(exc))
    finally:
        try:
            sock.close()
        except Exception:
            pass
    return None


def _before_ota(source, target, env):  # noqa: ARG001
    """SCons pre-action: discover the device IP before espota runs."""
    print("[SDDP] Searching for CEC-Dongle on local network…")
    ip = _discover_ip()
    if ip:
        print("[SDDP] Discovered CEC-Dongle at {}".format(ip))
        env.Replace(UPLOAD_PORT=ip)
    else:
        configured = env.get("UPLOAD_PORT", "(not set)")
        print(
            "[SDDP] No device found within {}s — using configured upload_port: {}".format(
                TIMEOUT_S, configured
            )
        )
        print("[SDDP] Tip: pass --upload-port <ip> on the command line to override.")


# Register as a pre-action for both firmware and filesystem uploads.
# The hook fires only when these specific SCons targets are being run,
# not on every plain build.
env.AddPreAction("upload",   _before_ota)
env.AddPreAction("uploadfs", _before_ota)
