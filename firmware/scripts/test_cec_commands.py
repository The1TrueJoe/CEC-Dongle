#!/usr/bin/env python3
"""Self-check for the CEC command table in src/cec_commands.h.

Catches the failure modes a hand-written table actually has: a duplicated name
silently shadowing another, a duplicated keycode, or a name advertised by
/api/cec/commands that cecResolve() has no branch for.

Run: python3 firmware/scripts/test_cec_commands.py
"""
import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "src" / "cec_commands.h"
text = SRC.read_text()


def block(start_marker):
    i = text.index(start_marker)
    return text[i:text.index("};", i)]


keys = re.findall(r'\{"([a-z0-9_]+)",\s*(0x[0-9A-Fa-f]{2})\}', block("CEC_KEYS[]"))
specials = re.findall(r'"([a-z0-9_]+)"', block("CEC_SPECIALS[]"))

assert len(keys) > 30, f"only parsed {len(keys)} keys — did the table format change?"
assert len(specials) > 10, f"only parsed {len(specials)} specials"

names = [n for n, _ in keys] + specials
dupes = {n for n in names if names.count(n) > 1}
assert not dupes, f"duplicate command names: {sorted(dupes)}"

codes = [c.lower() for _, c in keys]
dupe_codes = {c for c in codes if codes.count(c) > 1}
assert not dupe_codes, f"duplicate keycodes: {sorted(dupe_codes)}"

# Every advertised special must have a matching branch in cecResolve().
resolver = text[text.index("static CecResolved cecResolve"):]
for name in specials:
    if name.startswith("input"):
        continue  # handled by the numeric input1..8 branch
    assert f'name == "{name}"' in resolver, f"'{name}' advertised but never resolved"

# Volume keys must route to the volume target, not the TV.
for name, code in keys:
    if name in ("volume_up", "volume_down", "mute", "mute_on", "mute_off"):
        c = int(code, 16)
        assert (0x41 <= c <= 0x43) or c in (0x65, 0x66), \
            f"{name}={code} would not be routed to the volume target"

print(f"ok — {len(names)} commands, {len(keys)} keycodes, no duplicates")
sys.exit(0)
