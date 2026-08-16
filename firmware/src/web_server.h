/*
 * Web Server — REST API + Config Web UI
 * Uses ESPAsyncWebServer for non-blocking HTTP
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <Ticker.h>
#ifdef ESP8266
#include <Updater.h>
// ESP8266 Updater.h does not define UPDATE_SIZE_UNKNOWN; use available flash space
#ifndef UPDATE_SIZE_UNKNOWN
#define UPDATE_SIZE_UNKNOWN ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)
#endif
// ESP8266 UpdaterClass uses getErrorString(); alias so web_server.h stays portable
#define errorString getErrorString
#else
#include <Update.h>
#endif
#include <circular_queue/circular_queue.h>

#include "cec_driver.h"
#include "cec_commands.h"
#include "config_manager.h"
#include "wifi_manager.h"

// ── JSON string escape helper ────────────────────────────────────────────────
// Escapes characters that are illegal inside a JSON string literal.
// Used when building JSON via raw string concatenation (not via ArduinoJson).
static String jsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        const char c = s[i];
        if      (c == '"')  out += F("\\\"");
        else if (c == '\\') out += F("\\\\");
        else if (c == '\n') out += F("\\n");
        else if (c == '\r') out += F("\\r");
        else if (c == '\t') out += F("\\t");
        else if ((uint8_t)c < 0x20) {
            char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (uint8_t)c);
            out += esc;
        } else {
            out += c;
        }
    }
    return out;
}

// ── Simple ring buffer for CEC event log ────────────────────────────────────

struct CecLogEntry {
    uint32_t timestamp;
    uint32_t seqNum;
    String direction; // "rx" or "tx"
    String hex;
    String readable;
};

class CecEventLog {
public:
    void setMaxSize(uint16_t sz) { maxSize_ = sz; }

    void add(const String &direction, const CecFrame &frame) {
        CecLogEntry entry;
        entry.timestamp = millis();
        entry.seqNum    = nextSeq_++;
        entry.direction = direction;
        entry.hex       = frame.toHexString();
        entry.readable  = frame.toReadableString();
        entries_.push_back(entry);
        while (entries_.size() > maxSize_) entries_.erase(entries_.begin());
    }

    String toJson() const { return toJsonSince(0); }

    // Returns entries with timestamp strictly after sinceMs (0 = all)
    String toJsonSince(uint32_t sinceMs) const {
        String json = "[";
        bool first = true;
        for (const auto &e : entries_) {
            if (sinceMs == 0 || e.timestamp > sinceMs) {
                if (!first) json += ",";
                json += "{\"seq\":" + String(e.seqNum)
                      + ",\"t\":" + String(e.timestamp)
                      + ",\"dir\":\"" + e.direction + "\""
                      + ",\"hex\":\"" + jsonEscape(e.hex) + "\""
                      + ",\"msg\":\"" + jsonEscape(e.readable) + "\"}";
                first = false;
            }
        }
        json += "]";
        return json;
    }

    void clear() { entries_.clear(); }

private:
    std::vector<CecLogEntry> entries_;
    uint16_t maxSize_ = 50;
    uint32_t nextSeq_ = 0;
};

// ── CEC TV State Tracker ─────────────────────────────────────────────────────
//
// Passively observes all CEC bus traffic and maintains a decoded view of TV
// state (power, active input, volume, mute).  Updated by the CEC callback in
// main.cpp; read by the /api/cec/state endpoint.

class CecStateTracker {
public:
    String   tvPower      = "unknown"; // "on" | "standby" | "turning_on" | "unknown"
    uint16_t activeSource = 0;         // physical address of active source (0 = unknown)
    int      volume       = -1;        // -1 = unknown; 0-100
    bool     mute         = false;
    bool     muteKnown    = false;
    uint32_t lastUpdatedMs = 0;

    // Call for every CEC frame (src/dst are logical addresses, data begins at opcode).
    // Returns true when any tracked state changed (used to trigger a TCP push).
    bool update(uint8_t src, uint8_t dst, const std::vector<uint8_t> &data) {
        if (data.empty()) return false;
        bool changed = false;
        const uint8_t opcode = data[0];

        switch (opcode) {
            case 0x04: // Image View On  → TV waking up
            case 0x0D: // Text View On
                if (tvPower != "on") { tvPower = "on"; changed = true; }
                break;

            case 0x36: // Standby — only care when sent to TV (0) or broadcast
                if ((dst == 0 || dst == 0xF) && tvPower != "standby") {
                    tvPower = "standby"; changed = true;
                }
                break;

            case 0x90: // Report Power Status (TV's reply to 0x8F Give Power Status)
                if (data.size() >= 2) {
                    const uint8_t ps = data[1];
                    // 0x00=On  0x01=Standby  0x02=Standby->On  0x03=On->Standby
                    String next = (ps == 0x00) ? "on"
                                : (ps == 0x02) ? "turning_on"
                                :                "standby";
                    if (tvPower != next) { tvPower = next; changed = true; }
                }
                break;

            case 0x82: // Active Source (broadcast — device claims an input)
                if (data.size() >= 3) {
                    uint16_t pa = ((uint16_t)data[1] << 8) | data[2];
                    if (activeSource != pa) { activeSource = pa; changed = true; }
                    // Seeing active source implies the TV is on
                    if (tvPower == "standby" || tvPower == "unknown") {
                        tvPower = "on"; changed = true;
                    }
                }
                break;

            case 0x86: // Set Stream Path (TV broadcasts to request an input)
                if (data.size() >= 3) {
                    uint16_t pa = ((uint16_t)data[1] << 8) | data[2];
                    if (activeSource != pa) { activeSource = pa; changed = true; }
                }
                break;

            case 0x9D: // Inactive Source
                if (data.size() >= 3) {
                    uint16_t pa = ((uint16_t)data[1] << 8) | data[2];
                    if (activeSource == pa) { activeSource = 0; changed = true; }
                }
                break;

            case 0x7A: // Report Audio Status (from ARC device or TV)
                if (data.size() >= 2) {
                    bool newMute = (data[1] & 0x80) != 0;
                    int  newVol  = data[1] & 0x7F;
                    if (!muteKnown || mute != newMute || volume != newVol) {
                        mute = newMute; muteKnown = true; volume = newVol; changed = true;
                    }
                }
                break;
        }

        if (changed) lastUpdatedMs = millis();
        return changed;
    }

    String toJson() const {
        int inputNum = (activeSource >> 12) & 0xF;
        char srcBuf[5]; snprintf(srcBuf, sizeof(srcBuf), "%04X", (unsigned)activeSource);
        String j = "{";
        j += "\"tv_power\":\"" + tvPower + "\"";
        j += ",\"active_source\":\"0x" + String(srcBuf) + "\"";
        j += ",\"active_input\":" + String(inputNum);
        j += ",\"volume\":" + String(volume);
        j += ",\"mute\":"   + String(muteKnown && mute ? "true" : "false");
        j += ",\"last_updated_ms\":" + String(lastUpdatedMs);
        j += "}";
        return j;
    }
};

// ── TCP Push Server ─────────────────────────────────────────────────────────
// Broadcasts a newline-delimited JSON line to every connected TCP client the
// moment CEC state changes. Control4 holds a persistent connection here and
// reacts via ReceivedFromNetwork() — no polling needed.

class CecPushServer {
public:
    void begin(uint16_t port = 9000) {
        server_ = new AsyncServer(port);
        server_->onClient([this](void*, AsyncClient *client) {
            clients_.push_back(client);
            Serial.printf("[Push] Client connected: %s (%u total)\n",
                          client->remoteIP().toString().c_str(),
                          (unsigned)clients_.size());
            client->onDisconnect([this](void*, AsyncClient *c) { removeClient(c); }, nullptr);
            client->onError([this](void*, AsyncClient *c, int8_t) { removeClient(c); }, nullptr);
            client->onTimeout([this](void*, AsyncClient *c, uint32_t) {
                Serial.printf("[Push] Client %s timed out — disconnecting\n",
                              c->remoteIP().toString().c_str());
                c->close();
                removeClient(c);
            }, nullptr);
        }, nullptr);
        server_->begin();
        Serial.printf("[Push] TCP push server on port %u\n", (unsigned)port);
    }

    // Send a JSON state snapshot to all connected clients (newline-delimited).
    void push(const String &json) {
        if (clients_.empty()) return;
        String line = json + "\n";
        const char *data = line.c_str();
        size_t len = line.length();
        for (AsyncClient *c : clients_) {
            if (c->connected() && c->space() >= len) {
                c->write(data, len);
            } else if (c->connected()) {
                Serial.printf("[Push] Client %s buffer full — dropping state update\n",
                              c->remoteIP().toString().c_str());
            }
        }
    }

private:
    void removeClient(AsyncClient *target) {
        clients_.erase(std::remove(clients_.begin(), clients_.end(), target),
                       clients_.end());
    }

    AsyncServer              *server_ = nullptr;
    std::vector<AsyncClient*> clients_;
};

// ── Deferred CEC command queue ───────────────────────────────────────────────
//
// ESPAsyncTCP callbacks on ESP8266 run from the lwIP callback context, not a
// separate task — blocking there stalls the WiFi/TCP stack for every other
// client on the network, not just the one who asked. cec_.send() blocks for
// ~70ms per frame (worse on retries), so HTTP handlers never call it directly;
// they resolve the command (cheap) and enqueue it here. WebServer::loop() —
// called from the sketch's main loop(), where blocking briefly is normal and
// already how CEC negotiation/broadcast work at boot — drains one entry per
// call and answers the held request once the frame is actually on the bus.
//
// AsyncWebServerRequest stays valid until req->send() is called; the only
// timeout that could fire first is the client's own idle timeout for the
// response (seconds), well clear of the worst case here (~1.75s: repeat=5 x
// 5 retries x ~70ms).

struct PendingCecCmd {
    uint8_t source;
    uint8_t dest;
    std::vector<uint8_t> data;
    bool release;
    uint8_t repeat;
    std::function<void(bool ok)> onDone;
};

// ── Web Server class ────────────────────────────────────────────────────────

class WebServer {
public:
    using VoidCb = std::function<void()>;

    WebServer(CecDriver &cec, ConfigManager &cfg, WiFiManager &wifi,
              CecEventLog &log, CecStateTracker &state)
        : server_(80), cec_(cec), cfg_(cfg), wifi_(wifi), log_(log), state_(state) {}

    // Register a callback fired when an HTTP OTA upload starts.
    // main.cpp uses this to pause the CEC ISR before flash writes begin.
    void onOtaBegin(VoidCb cb) { otaBeginCb_ = cb; }
    void onOtaEnd(VoidCb cb)   { otaEndCb_   = cb; }

    // Broadcast a CEC state JSON snapshot to all connected TCP push clients.
    void push(const String &json) { pushServer_.push(json); }

    void begin() {
        // ── Web UI ──────────────────────────────────────────────────────
        // Serve wizard in AP mode, main UI when connected to WiFi
        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest *req) {
            if (wifi_.isConnected()) sendUi(req);
            else                    sendWizard(req);
        });
        server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *req) {
            if (wifi_.isConnected()) sendUi(req);
            else                    sendWizard(req);
        });
        server_.on("/wizard.html", HTTP_GET, [this](AsyncWebServerRequest *req) {
            sendWizard(req);
        });
        server_.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->send(204);
        });

        // ── Captive portal redirects (all unknown paths → /)
        server_.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/");
        });
        server_.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/");
        });
        server_.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/");
        });
        server_.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/");
        });

        // ── REST: Status ────────────────────────────────────────────────

        server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
            JsonDocument doc;
            doc["version"]              = VERSION;
            doc["uptime_ms"]            = millis();
            doc["hostname"]             = cfg_.config.hostname;
            doc["wifi_status"]          = wifi_.statusString();
            doc["wifi_ip"]              = wifi_.localIP();
            doc["heap_free"]            = ESP.getFreeHeap();
            doc["cec_address"]          = cec_.address();
            doc["cec_physical"]         = cec_.physicalAddress();
            doc["cec_osd_name"]         = cec_.osdName();
            doc["tv_logical_address"]   = cfg_.config.tvLogicalAddress;
            doc["audio_logical_address"]= cfg_.config.audioLogicalAddress;
            doc["volume_target"]        = cfg_.config.volumeTarget;
            doc["power_on_command"]     = cfg_.config.powerOnCommand;
            doc["power_off_command"]    = cfg_.config.powerOffCommand;
            doc["device_type"]          = cfg_.config.deviceType;
            doc["auto_negotiate"]       = cfg_.config.autoNegotiate;
            doc["ota_enabled"]          = true;
            doc["ui_storage"]           = "littlefs-gzip";

            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── REST: CEC Send ──────────────────────────────────────────────

        // The onRequest callback below always fires — even when onBody also
        // ran — right after the body finishes, in the same call stack. It
        // must only answer the genuinely-empty-body case; the CEC send is
        // queued and answered later (see PendingCecCmd), so if onRequest sent
        // anything unconditionally here it would win the race and finalize
        // the response before the real one was ready.
        server_.on("/api/cec/send", HTTP_POST, [](AsyncWebServerRequest *req) {
            if (req->contentLength() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing body\"}");
            }
        }, nullptr, [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                           size_t index, size_t total) {
            JsonDocument doc;
            auto err = deserializeJson(doc, data, len);
            if (err) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            uint8_t destination = doc["destination"] | 0xF;
            uint8_t source = doc["source"] | cec_.address();
            JsonArray arr = doc["data"].as<JsonArray>();
            if (arr.isNull() || arr.size() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing 'data' array\"}");
                return;
            }

            std::vector<uint8_t> cecData;
            for (auto v : arr) cecData.push_back(v.as<uint8_t>());

            String hex = CecFrame(source, destination, cecData).toHexString();
            AsyncWebServerRequestPtr heldReq = req->pause();
            bool queued = enqueueCec(source, destination, cecData, false, 1,
                [heldReq, hex](bool ok) {
                    auto r = heldReq.lock();
                    if (!r) return; // client disconnected before the frame went out
                    String out = "{\"success\":" + String(ok ? "true" : "false")
                               + ",\"frame\":\"" + hex + "\"}";
                    r->send(ok ? 200 : 500, "application/json", out);
                });
            if (!queued) {
                req->send(503, "application/json",
                          "{\"success\":false,\"error\":\"CEC busy, try again\"}");
            }
        });

        // ── REST: Named CEC commands ────────────────────────────────────
        //
        //   GET  /api/cec/commands            → every supported command name
        //   POST /api/cec/cmd?name=volume_up  → run one (GET also accepted, so
        //                                       a plain URL is enough to drive it)
        //   optional &repeat=N (1-5) for volume steps
        //
        // Destinations come from the device config, so clients never need to
        // know logical addresses or opcodes.

        server_.on("/api/cec/commands", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", cecCommandListJson());
        });

        server_.on("/api/cec/cmd", HTTP_GET | HTTP_POST, [this](AsyncWebServerRequest *req) {
            String name;
            if      (req->hasParam("name"))       name = req->getParam("name")->value();
            else if (req->hasParam("name", true)) name = req->getParam("name", true)->value();

            uint8_t repeat = 1;
            if (req->hasParam("repeat"))
                repeat = (uint8_t)constrain(req->getParam("repeat")->value().toInt(), 1, 5);

            CecResolved r = cecResolve(name, cfg_.config, cec_.physicalAddress());
            if (!r.ok) {
                req->send(404, "application/json",
                          "{\"success\":false,\"error\":\"Unknown command\"}");
                return;
            }

            String nameEsc = jsonEscape(name);
            AsyncWebServerRequestPtr heldReq = req->pause();
            bool queued = enqueueCec(cec_.address(), r.dest, r.data, r.release, repeat,
                [heldReq, nameEsc](bool ok) {
                    auto held = heldReq.lock();
                    if (!held) return;
                    held->send(ok ? 200 : 500, "application/json",
                              String("{\"success\":") + (ok ? "true" : "false")
                              + ",\"command\":\"" + nameEsc + "\"}");
                });
            if (!queued) {
                req->send(503, "application/json",
                          "{\"success\":false,\"error\":\"CEC busy, try again\"}");
            }
        });

        // ── REST: CEC Convenience endpoints (aliases for named commands) ─

        registerAlias("/api/cec/power/on",     "tv_on");
        registerAlias("/api/cec/power/off",    "tv_off");
        registerAlias("/api/cec/volume/up",    "volume_up");
        registerAlias("/api/cec/volume/down",  "volume_down");
        registerAlias("/api/cec/volume/mute",  "mute");
        registerAlias("/api/cec/source/active", "active");

        // Set Stream Path (change input)
        // See the comment on /api/cec/send above — onRequest must not answer
        // unconditionally here, the real response is deferred to the queue.
        server_.on("/api/cec/input", HTTP_POST, [](AsyncWebServerRequest *req) {
            if (req->contentLength() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing body\"}");
            }
        }, nullptr, [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                           size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            uint16_t pa = doc["physical_address"] | 0;
            if (!pa) {
                req->send(400, "application/json", "{\"error\":\"Missing 'physical_address'\"}");
                return;
            }
            std::vector<uint8_t> d = {0x86, (uint8_t)(pa >> 8), (uint8_t)(pa & 0xFF)};
            AsyncWebServerRequestPtr heldReq = req->pause();
            bool queued = enqueueCec(cec_.address(), 0xF, d, false, 1, [heldReq](bool ok) {
                auto r = heldReq.lock();
                if (!r) return;
                r->send(ok ? 200 : 500, "application/json", successJson(ok));
            });
            if (!queued) {
                req->send(503, "application/json",
                          "{\"success\":false,\"error\":\"CEC busy, try again\"}");
            }
        });

        // ── REST: CEC Event Log ─────────────────────────────────────────

        server_.on("/api/cec/log", HTTP_GET, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", log_.toJson());
        });

        server_.on("/api/cec/log", HTTP_DELETE, [this](AsyncWebServerRequest *req) {
            log_.clear();
            req->send(200, "application/json", "{\"success\":true}");
        });

        // Returns events with timestamp after ?since=<ms>; omit or 0 for all
        server_.on("/api/cec/events", HTTP_GET, [this](AsyncWebServerRequest *req) {
            uint32_t since = 0;
            if (req->hasParam("since")) {
                since = (uint32_t)req->getParam("since")->value().toInt();
            }
            req->send(200, "application/json", log_.toJsonSince(since));
        });

        // Decoded TV state derived from passive CEC bus monitoring
        server_.on("/api/cec/state", HTTP_GET, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", state_.toJson());
        });

        // ── REST: Config ────────────────────────────────────────────────

        server_.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", cfg_.toJson());
        });

        // onRequest fires unconditionally, right after onBody, in the same
        // call stack — req->send() from onBody only stores a response object,
        // it doesn't finalize one (that happens once, driven by the framework,
        // after both callbacks return). An unconditional send() here would
        // silently replace whatever onBody already set. Only answer the
        // genuinely-empty-body case.
        server_.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {
            if (req->contentLength() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing body\"}");
            }
        }, nullptr, [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                           size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            auto &c = cfg_.config;
            if (doc.containsKey("wifi_ssid"))           c.wifiSsid             = doc["wifi_ssid"].as<String>();
            if (doc.containsKey("wifi_password"))        c.wifiPassword          = doc["wifi_password"].as<String>();
            if (doc.containsKey("hostname")) {
                c.hostname = doc["hostname"].as<String>();
                // Re-point the mDNS responder now. The setup wizard tells the
                // user to visit <hostname>.local right after saving, so waiting
                // for a reboot would hand them a name that doesn't resolve.
                MDNS.setHostname(c.hostname);
            }
            if (doc.containsKey("cec_pin"))              c.cecPin                = doc["cec_pin"];
            if (doc.containsKey("cec_address"))          c.cecAddress            = doc["cec_address"];
            if (doc.containsKey("cec_physical"))         c.cecPhysical           = doc["cec_physical"];
            if (doc.containsKey("cec_osd_name"))         c.cecOsdName            = doc["cec_osd_name"].as<String>();
            if (doc.containsKey("cec_promiscuous"))      c.cecPromiscuous        = doc["cec_promiscuous"];
            if (doc.containsKey("cec_monitor_mode"))     c.cecMonitorMode        = doc["cec_monitor_mode"];
            if (doc.containsKey("log_buffer_size"))      c.logBufferSize         = doc["log_buffer_size"];
            if (doc.containsKey("tv_logical_address"))   c.tvLogicalAddress      = doc["tv_logical_address"];
            if (doc.containsKey("audio_logical_address"))c.audioLogicalAddress   = doc["audio_logical_address"];
            if (doc.containsKey("volume_target"))        c.volumeTarget          = doc["volume_target"].as<String>();
            if (doc.containsKey("power_on_command"))     c.powerOnCommand        = doc["power_on_command"].as<String>();
            if (doc.containsKey("power_off_command"))    c.powerOffCommand       = doc["power_off_command"].as<String>();
            if (doc.containsKey("device_type"))          c.deviceType            = doc["device_type"].as<String>();
            if (doc.containsKey("auto_negotiate"))       c.autoNegotiate         = doc["auto_negotiate"];

            cfg_.save();
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Saved. Restart to apply CEC changes.\"}");
        });

        // ── REST: WiFi ──────────────────────────────────────────────────

        server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", wifi_.scanNetworks());
        });

        // See the comment on /api/config above — onRequest must not answer
        // unconditionally, it would clobber whatever onBody already set.
        server_.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *req) {
            if (req->contentLength() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing body\"}");
            }
        }, nullptr, [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                           size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";
            if (ssid.length() == 0) {
                req->send(400, "application/json", "{\"error\":\"Missing 'ssid'\"}");
                return;
            }

            cfg_.config.wifiSsid = ssid;
            cfg_.config.wifiPassword = pass;
            cfg_.save();

            req->send(200, "application/json", "{\"success\":true,\"message\":\"Connecting...\"}");

            // Defer the STA connect so the response is sent before WiFi mode changes.
            connectTicker_.once_ms(600, [this, ssid, pass]() {
                wifi_.connectSTA(ssid, pass);
            });
        });

        // ── REST: System ────────────────────────────────────────────────

        server_.on("/api/system/restart", HTTP_POST, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
            // Defer restart so the TCP stack can actually send the response first.
            // delay() inside an async handler blocks the stack and drops the response.
            restartTicker_.once_ms(800, []() {
                Serial.println("[Web] Restarting (scheduled)");
                ESP.restart();
            });
        });

        server_.on("/api/system/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
            cfg_.reset();
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Config reset. Restarting...\"}");
            restartTicker_.once_ms(800, []() {
                Serial.println("[Web] Restarting after factory reset");
                ESP.restart();
            });
        });

        // ── REST: OTA firmware upload (HTTP, browser-based) ─────────────
        // More reliable than ArduinoOTA/UDP on congested networks.
        // Upload a .bin produced by PlatformIO via a standard multipart POST.

        server_.on("/api/ota/update", HTTP_POST,
            // (1) Response handler — called after the upload body is fully received
            [this](AsyncWebServerRequest *req) {
                bool ok = !otaHasError_;
                if (ok) {
                    const char *msg = otaIsCombined_
                        ? "Firmware + filesystem written. Restarting..."
                        : "Firmware written. Restarting...";
                    String resp = "{\"success\":true,\"message\":\"";
                    resp += msg;
                    resp += "\"}";
                    req->send(200, "application/json", resp);
                    restartTicker_.once_ms(1200, []() {
                        Serial.println("[OTA HTTP] Restarting after successful flash");
                        ESP.restart();
                    });
                } else {
                    String err = Update.errorString();
                    String resp = "{\"success\":false,\"error\":\"" + jsonEscape(err) + "\"}";
                    req->send(500, "application/json", resp);
                    if (otaEndCb_) otaEndCb_();
                    Serial.printf("[OTA HTTP] Failed: %s\n", err.c_str());
                }
            },
            // (2) Upload handler — streams the binary into flash.
            //
            // Supports two file formats:
            //   • Plain firmware.bin  — standard U_FLASH update (backward compat)
            //   • combined.bin        — "CECF" magic header followed by firmware
            //                          then LittleFS image; flashes both in sequence
            //
            // Combined binary layout (all sizes uint32 LE):
            //   [0]  4 bytes  magic "CECF"
            //   [4]  4 bytes  firmware size
            //   [8]  4 bytes  filesystem size
            //   [12] 4 bytes  reserved (zeros)
            //   [16] fw bytes firmware.bin content
            //   [16+fw] fs bytes  littlefs.bin content
            [this](AsyncWebServerRequest *req, const String &filename,
                   size_t index, uint8_t *data, size_t len, bool final) {

                // ── Initialise on first chunk ────────────────────────
                if (index == 0) {
                    otaIsCombined_ = false;
                    otaFwSize_     = 0;
                    otaFsSize_     = 0;
                    otaWritten_    = 0;
                    otaFwDone_     = false;
                    otaHasError_   = false;
                    Serial.printf("[OTA HTTP] Upload begin: %s\n", filename.c_str());
                    if (otaBeginCb_) otaBeginCb_();

                    if (len < 4) {
                        // Extremely unlikely with any HTTP client; guard anyway
                        Serial.println("[OTA HTTP] First chunk too small to detect format");
                        otaHasError_ = true;
                        return;
                    }

                    if (memcmp(data, "CECF", 4) == 0) {
                        // ── Combined binary ──────────────────────────
                        if (len < 16) {
                            Serial.println("[OTA HTTP] Combined header split across chunks — not supported");
                            otaHasError_ = true;
                            return;
                        }
                        memcpy(&otaFwSize_, data + 4,  4);
                        memcpy(&otaFsSize_, data + 8,  4);
                        otaIsCombined_ = true;
                        Serial.printf("[OTA HTTP] Combined binary — fw=%u  fs=%u\n",
                                      otaFwSize_, otaFsSize_);
                        if (!Update.begin(otaFwSize_, U_FLASH)) {
                            Serial.printf("[OTA HTTP] begin(fw) failed: %s\n",
                                          Update.errorString().c_str());
                            otaHasError_ = true;
                            return;
                        }
                        // Advance past the 16-byte header
                        data += 16;
                        len  -= 16;
                    } else {
                        // ── Plain firmware binary ────────────────────
                        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                            Serial.printf("[OTA HTTP] begin() failed: %s\n",
                                          Update.errorString().c_str());
                            otaHasError_ = true;
                            return;
                        }
                    }
                }

                if (otaHasError_) return;

                // ── Write data ───────────────────────────────────────
                if (otaIsCombined_) {
                    while (len > 0) {
                        if (!otaFwDone_) {
                            // Writing firmware portion
                            size_t remain  = otaFwSize_ - otaWritten_;
                            size_t toWrite = (len < remain) ? len : remain;
                            Update.write(data, toWrite);
                            otaWritten_ += toWrite;
                            data        += toWrite;
                            len         -= toWrite;

                            if (otaWritten_ >= otaFwSize_) {
                                // Firmware portion complete
                                if (!Update.end(true)) {
                                    Serial.printf("[OTA HTTP] fw end() failed: %s\n",
                                                  Update.errorString().c_str());
                                    otaHasError_ = true;
                                    return;
                                }
                                Serial.println("[OTA HTTP] Firmware portion flashed OK");
                                otaFwDone_  = true;
                                otaWritten_ = 0;
                                // Start the filesystem update (even if no bytes left in this chunk)
                                if (!Update.begin(otaFsSize_, U_FS)) {
                                    Serial.printf("[OTA HTTP] begin(fs) failed: %s\n",
                                                  Update.errorString().c_str());
                                    otaHasError_ = true;
                                    return;
                                }
                            }
                        } else {
                            // Writing filesystem portion
                            size_t written = Update.write(data, len);
                            otaWritten_ += written;
                            len          = 0;
                        }
                    }

                    if (final) {
                        if (!Update.end(true)) {
                            Serial.printf("[OTA HTTP] fs end() failed: %s\n",
                                          Update.errorString().c_str());
                            otaHasError_ = true;
                        } else {
                            Serial.printf("[OTA HTTP] Filesystem portion flashed OK (%u bytes)\n",
                                          otaWritten_);
                            if (otaEndCb_) otaEndCb_();
                        }
                    }
                } else {
                    // ── Plain firmware ───────────────────────────────
                    if (Update.isRunning()) {
                        size_t written = Update.write(data, len);
                        if (written != len) {
                            Serial.printf("[OTA HTTP] write() error: %s\n",
                                          Update.errorString().c_str());
                        }
                    }
                    if (final) {
                        if (Update.end(true)) {
                            Serial.printf("[OTA HTTP] Success — %u bytes written\n",
                                          static_cast<unsigned>(index + len));
                            if (otaEndCb_) otaEndCb_();
                        } else {
                            Serial.printf("[OTA HTTP] end() failed: %s\n",
                                          Update.errorString().c_str());
                            otaHasError_ = true;
                        }
                    }
                }
            }
        );

        // ── CORS preflight ──────────────────────────────────────────────
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

        server_.onNotFound([](AsyncWebServerRequest *req) {
            if (req->method() == HTTP_OPTIONS) {
                req->send(200);
            } else {
                // Captive portal: redirect everything unknown to /
                req->redirect("/");
            }
        });

        pushServer_.begin(9000);
        server_.begin();
        Serial.println("[Web] Server started on port 80");
    }

    // Drain one queued CEC command per call — see PendingCecCmd for why this
    // can't run inside the HTTP handler that requested it. Call from the
    // sketch's main loop().
    void loop() {
        if (cecQueue_.empty()) return;
        PendingCecCmd cmd = std::move(cecQueue_.front());
        cecQueue_.erase(cecQueue_.begin());

        bool ok = true;
        for (uint8_t i = 0; i < cmd.repeat; i++) {
            ok = cec_.send(cmd.source, cmd.dest, cmd.data) && ok;
            log_.add("tx", CecFrame(cmd.source, cmd.dest, cmd.data));
            if (cmd.release) {
                cec_.send(cmd.source, cmd.dest, {0x45});
                log_.add("tx", CecFrame(cmd.source, cmd.dest, {0x45}));
            }
        }
        if (cmd.onDone) cmd.onDone(ok);
    }

    // Legacy convenience path → named command, so config changes apply everywhere.
    void registerAlias(const char *path, const char *command) {
        String cmd(command);
        server_.on(path, HTTP_GET | HTTP_POST, [this, cmd](AsyncWebServerRequest *req) {
            CecResolved r = cecResolve(cmd, cfg_.config, cec_.physicalAddress());
            AsyncWebServerRequestPtr heldReq = req->pause();
            bool queued = enqueueCec(cec_.address(), r.dest, r.data, r.release, 1,
                [heldReq](bool ok) {
                    auto held = heldReq.lock();
                    if (held) held->send(ok ? 200 : 500, "application/json", successJson(ok));
                });
            if (!queued) {
                req->send(503, "application/json",
                          "{\"success\":false,\"error\":\"CEC busy, try again\"}");
            }
        });
    }

private:
    static const char *successJson(bool ok) {
        return ok ? "{\"success\":true}" : "{\"success\":false}";
    }

    // Queue a CEC frame for WebServer::loop() to send. Returns false (queue
    // full) without touching `onDone` — caller answers the request itself.
    static constexpr size_t MAX_QUEUED_CEC_CMDS = 4;
    bool enqueueCec(uint8_t source, uint8_t dest, const std::vector<uint8_t> &data,
                     bool release, uint8_t repeat, std::function<void(bool)> onDone) {
        if (cecQueue_.size() >= MAX_QUEUED_CEC_CMDS) return false;
        cecQueue_.push_back({source, dest, data, release, repeat, onDone});
        return true;
    }

    void sendFileFromLittleFS(AsyncWebServerRequest *req, const String &stem) {
        String gzPath = "/" + stem + ".html.gz";
        String htmlPath = "/" + stem + ".html";
        if (LittleFS.exists(gzPath)) {
            AsyncWebServerResponse *response = req->beginResponse(LittleFS, gzPath, "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "public, max-age=3600");
            req->send(response);
            return;
        }
        if (LittleFS.exists(htmlPath)) {
            AsyncWebServerResponse *response = req->beginResponse(LittleFS, htmlPath, "text/html");
            response->addHeader("Cache-Control", "no-cache");
            req->send(response);
            return;
        }
        req->send(
            503, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>CEC Dongle</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#111318;color:#e4e4e7;display:grid;place-items:center;min-height:100vh;margin:0;padding:24px}main{max-width:560px;background:#181b22;border:1px solid #272c38;border-radius:10px;padding:20px}code{background:#1e222b;padding:2px 6px;border-radius:4px}</style></head><body><main><h1>Filesystem image not found</h1><p>Run <code>pio run -t uploadfs</code> in the <code>firmware</code> folder.</p></main></body></html>"
        );
    }

    void sendUi(AsyncWebServerRequest *req) {
        sendFileFromLittleFS(req, "index");
    }

    void sendWizard(AsyncWebServerRequest *req) {
        sendFileFromLittleFS(req, "wizard");
    }

    AsyncWebServer server_;
    CecPushServer  pushServer_;
    CecDriver &cec_;
    ConfigManager &cfg_;
    WiFiManager &wifi_;
    CecEventLog &log_;
    CecStateTracker &state_;
    VoidCb  otaBeginCb_;
    VoidCb  otaEndCb_;
    Ticker  restartTicker_;
    Ticker  connectTicker_;
    std::vector<PendingCecCmd> cecQueue_;

    // OTA upload state — valid for the duration of a single /api/ota/update POST
    bool     otaIsCombined_ = false;
    uint32_t otaFwSize_     = 0;
    uint32_t otaFsSize_     = 0;
    uint32_t otaWritten_    = 0;  // bytes written to the current Update pass
    bool     otaFwDone_     = false;
    bool     otaHasError_   = false;
};
