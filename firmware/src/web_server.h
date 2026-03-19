/*
 * Web Server — REST API + Config Web UI
 * Uses ESPAsyncWebServer for non-blocking HTTP
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <circular_queue/circular_queue.h>

#include "cec_driver.h"
#include "config_manager.h"
#include "wifi_manager.h"

// ── Simple ring buffer for CEC event log ────────────────────────────────────

struct CecLogEntry {
    uint32_t timestamp;
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
        entry.direction = direction;
        entry.hex = frame.toHexString();
        entry.readable = frame.toReadableString();
        entries_.push_back(entry);
        while (entries_.size() > maxSize_) entries_.erase(entries_.begin());
    }

    String toJson() const {
        String json = "[";
        for (size_t i = 0; i < entries_.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"t\":" + String(entries_[i].timestamp)
                  + ",\"dir\":\"" + entries_[i].direction + "\""
                  + ",\"hex\":\"" + entries_[i].hex + "\""
                  + ",\"msg\":\"" + entries_[i].readable + "\"}";
        }
        json += "]";
        return json;
    }

    void clear() { entries_.clear(); }

private:
    std::vector<CecLogEntry> entries_;
    uint16_t maxSize_ = 50;
};

// ── Web Server class ────────────────────────────────────────────────────────

class WebServer {
public:
    WebServer(CecDriver &cec, ConfigManager &cfg, WiFiManager &wifi, CecEventLog &log)
        : server_(80), cec_(cec), cfg_(cfg), wifi_(wifi), log_(log) {}

    void begin() {
        // ── Web UI ──────────────────────────────────────────────────────
        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest *req) {
            sendUi(req);
        });
        server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *req) {
            sendUi(req);
        });
        server_.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->send(204);
        });

        // ── Captive portal redirects ────────────────────────────────────
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
            doc["version"] = VERSION;
            doc["uptime_ms"] = millis();
            doc["hostname"] = cfg_.config.hostname;
            doc["wifi_status"] = wifi_.statusString();
            doc["wifi_ip"] = wifi_.localIP();
            doc["heap_free"] = ESP.getFreeHeap();
            doc["cec_address"] = cec_.address();
            doc["cec_physical"] = cec_.physicalAddress();
            doc["cec_osd_name"] = cec_.osdName();
            doc["ota_enabled"] = true;
            doc["ui_storage"] = "littlefs-gzip";

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
            if (doc.containsKey("wifi_ssid"))        c.wifiSsid       = doc["wifi_ssid"].as<String>();
            if (doc.containsKey("wifi_password"))     c.wifiPassword    = doc["wifi_password"].as<String>();
            if (doc.containsKey("hostname"))          c.hostname        = doc["hostname"].as<String>();
            if (doc.containsKey("cec_pin"))           c.cecPin          = doc["cec_pin"];
            if (doc.containsKey("cec_address"))       c.cecAddress      = doc["cec_address"];
            if (doc.containsKey("cec_physical"))      c.cecPhysical     = doc["cec_physical"];
            if (doc.containsKey("cec_osd_name"))      c.cecOsdName      = doc["cec_osd_name"].as<String>();
            if (doc.containsKey("cec_promiscuous"))   c.cecPromiscuous  = doc["cec_promiscuous"];
            if (doc.containsKey("cec_monitor_mode"))  c.cecMonitorMode  = doc["cec_monitor_mode"];
            if (doc.containsKey("log_buffer_size"))   c.logBufferSize   = doc["log_buffer_size"];

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

            // Deferred connect (after response is sent)
            delay(500);
            wifi_.connectSTA(ssid, pass);
        });

        // ── REST: System ────────────────────────────────────────────────

        server_.on("/api/system/restart", HTTP_POST, [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
            delay(500);
            ESP.restart();
        });

        server_.on("/api/system/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
            cfg_.reset();
            req->send(200, "application/json", "{\"success\":true,\"message\":\"Config reset. Restarting...\"}");
            delay(500);
            ESP.restart();
        });

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

        server_.begin();
        Serial.println("[Web] Server started on port 80");
    }

private:
    void sendUi(AsyncWebServerRequest *req) {
        if (LittleFS.exists("/index.html.gz")) {
            AsyncWebServerResponse *response = req->beginResponse(LittleFS, "/index.html.gz", "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "public, max-age=3600");
            req->send(response);
            return;
        }

        if (LittleFS.exists("/index.html")) {
            AsyncWebServerResponse *response = req->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Cache-Control", "no-cache");
            req->send(response);
            return;
        }

        req->send(
            503,
            "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>CEC Dongle</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#111318;color:#e4e4e7;display:grid;place-items:center;min-height:100vh;margin:0;padding:24px}main{max-width:560px;background:#181b22;border:1px solid #272c38;border-radius:10px;padding:20px}code{background:#1e222b;padding:2px 6px;border-radius:4px}</style></head><body><main><h1>UI filesystem image not found</h1><p>Upload the LittleFS image after flashing firmware.</p><p>Run <code>pio run -t uploadfs</code> in the <code>firmware</code> folder, or use the OTA environment and upload the filesystem image over the network.</p></main></body></html>"
        );
    }

    AsyncWebServer server_;
    CecDriver &cec_;
    ConfigManager &cfg_;
    WiFiManager &wifi_;
    CecEventLog &log_;
};
