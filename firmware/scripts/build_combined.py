"""
build_combined.py — PlatformIO post-build extra script.

After EITHER the firmware binary OR the filesystem image is built, check
whether BOTH now exist.  When they do, stitch them together into a single
combined.bin using the "CECF" container format:

  Offset  Size  Field
  ------  ----  -----
  0       4     Magic: ASCII "CECF"
  4       4     Firmware size (uint32 LE)
  8       4     Filesystem size (uint32 LE)
  12      4     Reserved (zeros)
  16      fw    firmware.bin content
  16+fw   fs    littlefs.bin content

The web-UI OTA endpoint (/api/ota/update) recognises the CECF magic and
flashes firmware first, then the filesystem, in a single browser upload.
"""

Import("env")
import os
import struct


MAGIC = b"CECF"
HEADER_SIZE = 16


def _make_combined(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    fw_path   = os.path.join(build_dir, "firmware.bin")
    fs_path   = os.path.join(build_dir, "littlefs.bin")
    out_path  = os.path.join(build_dir, "combined.bin")

    if not os.path.exists(fw_path) or not os.path.exists(fs_path):
        return  # Wait until both halves are present

    with open(fw_path, "rb") as f:
        fw_data = f.read()
    with open(fs_path, "rb") as f:
        fs_data = f.read()

    header = MAGIC + struct.pack("<III", len(fw_data), len(fs_data), 0)
    assert len(header) == HEADER_SIZE

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(fw_data)
        f.write(fs_data)

    total = HEADER_SIZE + len(fw_data) + len(fs_data)
    print(
        f"\n[CEC-Dongle] Combined binary: {out_path}\n"
        f"             Firmware: {len(fw_data):,} bytes | "
        f"Filesystem: {len(fs_data):,} bytes | "
        f"Total: {total:,} bytes\n"
        f"  → Upload combined.bin via the web UI to update everything at once.\n"
    )


# Hook fires after whichever of the two binaries is just built.
# The function silently skips when the other half isn't present yet.
env.AddPostAction("$BUILD_DIR/firmware.bin",  _make_combined)
env.AddPostAction("$BUILD_DIR/littlefs.bin",  _make_combined)
