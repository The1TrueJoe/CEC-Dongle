/*
 * Web Server — REST API + Config Web UI
 * Uses ESPAsyncWebServer for non-blocking HTTP
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
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
            doc["device_type"]          = cfg_.config.deviceType;
            doc["auto_negotiate"]       = cfg_.config.autoNegotiate;
            doc["ota_enabled"]          = true;
            doc["ui_storage"]           = "littlefs-gzip";

            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── REST: CEC Send ──────────────────────────────────────────────

        server_.on("/api/cec/send", HTTP_POST, [](AsyncWebServerRequest *req) {
            req->send(400, "application/json", "{\"error\":\"Missing body\"}");
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

            bool ok = cec_.send(source, destination, cecData);

            // Log the sent frame
            CecFrame f(source, destination, cecData);
            log_.add("tx", f);

            JsonDocument resp;
            resp["success"] = ok;
            resp["frame"] = f.toHexString();
            String out;
            serializeJson(resp, out);
            req->send(ok ? 200 : 500, "application/json", out);
        });

        // ── REST: CEC Convenience endpoints ─────────────────────────────

        // Power on (Image View On → TV)
        server_.on("/api/cec/power/on", HTTP_POST, [this](AsyncWebServerRequest *req) {
            uint8_t dest = 0;
            if (req->hasParam("destination", true))
                dest = req->getParam("destination", true)->value().toInt();
            bool ok = cec_.send(dest, {0x04});
            log_.add("tx", CecFrame(cec_.address(), dest, {0x04}));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Standby
        server_.on("/api/cec/power/off", HTTP_POST, [this](AsyncWebServerRequest *req) {
            uint8_t dest = 0xF;
            if (req->hasParam("destination", true))
                dest = req->getParam("destination", true)->value().toInt();
            bool ok = cec_.send(dest, {0x36});
            log_.add("tx", CecFrame(cec_.address(), dest, {0x36}));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Volume Up
        server_.on("/api/cec/volume/up", HTTP_POST, [this](AsyncWebServerRequest *req) {
            bool ok = cec_.send(0x5, {0x44, 0x41});
            log_.add("tx", CecFrame(cec_.address(), 0x5, {0x44, 0x41}));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Volume Down
        server_.on("/api/cec/volume/down", HTTP_POST, [this](AsyncWebServerRequest *req) {
            bool ok = cec_.send(0x5, {0x44, 0x42});
            log_.add("tx", CecFrame(cec_.address(), 0x5, {0x44, 0x42}));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Mute
        server_.on("/api/cec/volume/mute", HTTP_POST, [this](AsyncWebServerRequest *req) {
            bool ok = cec_.send(0x5, {0x44, 0x43});
            log_.add("tx", CecFrame(cec_.address(), 0x5, {0x44, 0x43}));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Active Source
        server_.on("/api/cec/source/active", HTTP_POST, [this](AsyncWebServerRequest *req) {
            uint16_t pa = cec_.physicalAddress();
            std::vector<uint8_t> d = {0x82, (uint8_t)(pa >> 8), (uint8_t)(pa & 0xFF)};
            bool ok = cec_.send(0xF, d);
            log_.add("tx", CecFrame(cec_.address(), 0xF, d));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
        });

        // Set Stream Path (change input)
        server_.on("/api/cec/input", HTTP_POST, [](AsyncWebServerRequest *req) {
            req->send(400, "application/json", "{\"error\":\"Missing body\"}");
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
            bool ok = cec_.send(0xF, d);
            log_.add("tx", CecFrame(cec_.address(), 0xF, d));
            req->send(ok ? 200 : 500, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
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

        server_.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {
            req->send(400, "application/json", "{\"error\":\"Missing body\"}");
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
            if (doc.containsKey("hostname"))             c.hostname              = doc["hostname"].as<String>();
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
            if (doc.containsKey("device_type"))          c.deviceType            = doc["device_type"].as<String>();
            if (doc.containsKey("auto_negotiate"))       c.autoNegotiate         = doc["auto_negotiate"];

            cfg_.save();
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Saved. Restart to apply CEC changes.\"}");
        });

        // ── REST: WiFi ──────────────────────────────────────────────────

        server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *req) {
            req->send(200, "application/json", wifi_.scanNetworks());
        });

        server_.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *req) {
            req->send(400, "application/json", "{\"error\":\"Missing body\"}");
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
                bool ok = !Update.hasError();
                if (ok) {
                    req->send(200, "application/json",
                              "{\"success\":true,\"message\":\"Firmware written. Restarting...\"}");
                    restartTicker_.once_ms(1200, []() {
                        Serial.println("[OTA HTTP] Restarting after successful flash");
                        ESP.restart();
                    });
                } else {
                    String err = Update.errorString();
                    String resp = "{\"success\":false,\"error\":\"" + err + "\"}";
                    req->send(500, "application/json", resp);
                    // Re-attach CEC ISR since OTA failed (onOtaEnd not called by Update)
                    if (otaEndCb_) otaEndCb_();
                    Serial.printf("[OTA HTTP] Failed: %s\n", err.c_str());
                }
            },
            // (2) Upload handler — streams the binary into flash
            [this](AsyncWebServerRequest *req, const String &filename,
                   size_t index, uint8_t *data, size_t len, bool final) {
                if (index == 0) {
                    Serial.printf("[OTA HTTP] Upload begin: %s\n", filename.c_str());
                    // Pause CEC ISR before any flash write to avoid bus glitches
                    if (otaBeginCb_) otaBeginCb_();
                    // UPDATE_SIZE_UNKNOWN lets the Update lib use available flash space
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                        Serial.printf("[OTA HTTP] begin() failed: %s\n", Update.errorString());
                        return;
                    }
                }
                if (Update.isRunning()) {
                    size_t written = Update.write(data, len);
                    if (written != len) {
                        Serial.printf("[OTA HTTP] write() error: %s\n", Update.errorString());
                    }
                }
                if (final) {
                    if (Update.end(true)) {
                        Serial.printf("[OTA HTTP] Success — %u bytes written\n",
                                      static_cast<unsigned>(index + len));
                        if (otaEndCb_) otaEndCb_();
                    } else {
                        Serial.printf("[OTA HTTP] end() failed: %s\n", Update.errorString());
                        // otaEndCb_ called in response handler on error path above
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

private:
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
};
