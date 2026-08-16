/*
 * Standard CEC command set (HDMI-CEC 1.4b)
 *
 * There is no Arduino "library of CEC commands" worth pulling in — libCEC is
 * Linux-only and huge. The standard *is* the opcode table plus the User Control
 * keycodes from CEC 1.4b Table 30, which is what this file encodes.
 *
 * Callers (Control4, Home Assistant, curl) use names — "tv_on", "volume_up",
 * "input2" — and never touch hex. Destinations come from the device config, so
 * a bus with a soundbar and a bus without one need no client-side changes.
 */

#pragma once

#include <Arduino.h>
#include <vector>

#include "config_manager.h"

// ── User Control keycodes (sent as 0x44 <key>, then 0x45 Release) ───────────
// The Release matters: many TVs treat a Pressed with no Release as a held key
// and either repeat it forever or ignore it entirely.

struct CecKey { const char *name; uint8_t key; };

static const CecKey CEC_KEYS[] = {
    // Navigation
    {"select",        0x00}, {"up",           0x01}, {"down",          0x02},
    {"left",          0x03}, {"right",        0x04},
    {"root_menu",     0x09}, {"setup_menu",   0x0A}, {"contents_menu", 0x0B},
    {"exit",          0x0D},
    // Numbers
    {"num_0", 0x20}, {"num_1", 0x21}, {"num_2", 0x22}, {"num_3", 0x23},
    {"num_4", 0x24}, {"num_5", 0x25}, {"num_6", 0x26}, {"num_7", 0x27},
    {"num_8", 0x28}, {"num_9", 0x29},
    // Channel
    {"channel_up",    0x30}, {"channel_down", 0x31}, {"previous_channel", 0x32},
    // Transport
    {"play",          0x44}, {"stop",         0x45}, {"pause",         0x46},
    {"record",        0x47}, {"rewind",       0x48}, {"fast_forward",  0x49},
    {"eject",         0x4A}, {"forward",      0x4B}, {"backward",      0x4C},
    // Power as a remote key — some TVs only honour these, others only the
    // Image View On / Standby opcodes below. Both are exposed; try each once.
    {"power_toggle",  0x40}, {"power_off_key", 0x6C}, {"power_on_key", 0x6D},
    // Volume — routed to the configured volume target, not the TV
    {"volume_up",     0x41}, {"volume_down",  0x42}, {"mute",          0x43},
    {"mute_on",       0x65}, {"mute_off",     0x66},
};

// Opcode-level commands (not remote keys). Kept as a list so /api/cec/commands
// can advertise the full vocabulary in one response.
static const char *CEC_SPECIALS[] = {
    "tv_on", "tv_off", "standby_all", "active", "inactive",
    "power_status", "audio_status", "request_active_source",
    "input1", "input2", "input3", "input4",
    "input5", "input6", "input7", "input8",
};

// ── Resolution ──────────────────────────────────────────────────────────────

struct CecResolved {
    uint8_t              dest    = 0;
    std::vector<uint8_t> data;
    bool                 release = false; // follow with 0x45 User Control Released
    bool                 ok      = false; // false = unknown command name
};

static inline bool cecKeyIsVolume(uint8_t k) {
    return (k >= 0x41 && k <= 0x43) || k == 0x65 || k == 0x66;
}

static inline uint8_t cecVolumeDest(const Config &c) {
    if (c.volumeTarget == "tv")        return c.tvLogicalAddress;
    if (c.volumeTarget == "broadcast") return 0xF;
    return c.audioLogicalAddress;
}

// Map a command name to a frame. ownPa is this device's physical address,
// needed only by the Active/Inactive Source announcements.
static CecResolved cecResolve(const String &name, const Config &c, uint16_t ownPa) {
    CecResolved r;
    const uint8_t paHi = (uint8_t)(ownPa >> 8), paLo = (uint8_t)(ownPa & 0xFF);

    // input1..input8 → Set Stream Path to HDMI port N (physical address N.0.0.0)
    if (name.length() == 6 && name.startsWith("input")) {
        uint8_t n = name[5] - '0';
        if (n >= 1 && n <= 8) {
            r.dest = 0xF;
            r.data = {0x86, (uint8_t)(n << 4), 0x00};
            r.ok   = true;
            return r;
        }
    }

    if (name == "tv_on") {
        r.dest = c.tvLogicalAddress;
        if      (c.powerOnCommand == "text_view_on")       r.data = {0x0D};
        else if (c.powerOnCommand == "user_control_power") { r.data = {0x44, 0x6D}; r.release = true; }
        else                                               r.data = {0x04}; // image_view_on
        r.ok = true;
        return r;
    }
    if (name == "tv_off") {
        r.dest = c.tvLogicalAddress;
        if (c.powerOffCommand == "user_control_power") { r.data = {0x44, 0x6C}; r.release = true; }
        else                                           r.data = {0x36}; // standby
        r.ok = true;
        return r;
    }
    if (name == "standby_all") { r.dest = 0xF;                   r.data = {0x36}; r.ok = true; return r; }
    if (name == "power_status"){ r.dest = c.tvLogicalAddress;    r.data = {0x8F}; r.ok = true; return r; }
    if (name == "audio_status"){ r.dest = c.audioLogicalAddress; r.data = {0x71}; r.ok = true; return r; }
    if (name == "request_active_source") { r.dest = 0xF;         r.data = {0x85}; r.ok = true; return r; }
    if (name == "active")      { r.dest = 0xF;                r.data = {0x82, paHi, paLo}; r.ok = true; return r; }
    if (name == "inactive")    { r.dest = c.tvLogicalAddress; r.data = {0x9D, paHi, paLo}; r.ok = true; return r; }

    for (const auto &k : CEC_KEYS) {
        if (name == k.name) {
            r.dest    = cecKeyIsVolume(k.key) ? cecVolumeDest(c) : c.tvLogicalAddress;
            r.data    = {0x44, k.key};
            r.release = true;
            r.ok      = true;
            return r;
        }
    }
    return r; // ok == false
}

// JSON array of every command name — served by GET /api/cec/commands so a
// client can enumerate the vocabulary instead of hardcoding it.
static String cecCommandListJson() {
    String j = "[";
    for (const auto &s : CEC_SPECIALS) { j += "\""; j += s;      j += "\","; }
    for (const auto &k : CEC_KEYS)     { j += "\""; j += k.name; j += "\","; }
    j.setCharAt(j.length() - 1, ']');
    return j;
}
