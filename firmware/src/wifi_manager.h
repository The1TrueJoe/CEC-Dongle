/*
 * WiFi Manager
 * Handles AP mode for provisioning and STA mode for normal operation
 */

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

enum class WiFiManagerState {
    Idle,
    AP,         // Access point mode (captive portal)
    Connecting, // Attempting STA connection
    Connected,  // STA connected
};

class WiFiManager {
public:
    void begin(const String &hostname, const String &ssid, const String &password) {
        hostname_ = hostname;
        WiFi.hostname(hostname);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);

        if (ssid.length() > 0) {
            connectSTA(ssid, password);
        } else {
            // config.json has no credentials — this happens whenever the
            // LittleFS partition gets rewritten (e.g. a UI/filesystem OTA
            // update), since that wipes the whole partition config.json
            // lives on. The SDK keeps its own last-used SSID/password in a
            // separate flash sector outside LittleFS entirely (written once
            // by connectSTA() below), so try that before falling back to the
            // setup AP — same 15s timeout path as a normal connect attempt,
            // via the existing state machine in loop().
            Serial.println("[WiFi] No saved credentials in config — trying SDK-persisted network...");
            WiFi.mode(WIFI_STA);
            WiFi.begin();
            state_ = WiFiManagerState::Connecting;
            connectStartMs_ = millis();
        }
    }

    void loop() {
        if (state_ == WiFiManagerState::AP) {
            dnsServer_.processNextRequest();
        }

        if (state_ == WiFiManagerState::Connecting) {
            if (WiFi.status() == WL_CONNECTED) {
                state_ = WiFiManagerState::Connected;
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - connectStartMs_ > connectTimeoutMs_) {
                Serial.println("[WiFi] Connection timeout, starting AP...");
                startAP();
            }
        }

        if (state_ == WiFiManagerState::Connected && WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost connection, reconnecting...");
            state_ = WiFiManagerState::Connecting;
            connectStartMs_ = millis();
            WiFi.reconnect();
        }

        // Async scan. A synchronous WiFi.scanNetworks() blocks for ~3-4s with
        // the softAP still up serving the very client requesting the scan —
        // on ESP8266 the AP and STA/scan duties share one radio, and that long
        // an uninterrupted block corrupts the SSID string pointers in the scan
        // result table (count and RSSI come back fine; SSID text comes back
        // empty). Async scanning steps through in small slices via loop(),
        // which is exactly what avoids that corruption window.
        if (wantScan_ && !scanInProgress_) {
            scanInProgress_ = true;
            WiFi.scanDelete();
            Serial.println("[WiFi] Starting WiFi scan...");
            WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
        }

        if (scanInProgress_) {
            int n = WiFi.scanComplete();
            if (n >= 0 || n == WIFI_SCAN_FAILED) {
                Serial.printf("[WiFi] Scan complete: %d networks found\n", n);
                scanCount_      = (n > 0) ? n : 0;
                scanDone_       = true;
                scanInProgress_ = false;
            }
            // n == WIFI_SCAN_RUNNING (-1): still in progress, check again next loop()
        }
    }

    void connectSTA(const String &ssid, const String &password) {
        Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid.c_str());

        // If we're in AP mode, stop it
        if (state_ == WiFiManagerState::AP) {
            dnsServer_.stop();
            WiFi.softAPdisconnect(true);
        }

        WiFi.mode(WIFI_STA);
        // Persist just this one write to the SDK's own flash sector — separate
        // from LittleFS, so it survives a future filesystem re-flash — then
        // immediately back to non-persistent so routine reconnects (loop()'s
        // WiFi.reconnect() on drop, or the no-arg WiFi.begin() fallback above)
        // don't re-write flash on every attempt.
        WiFi.persistent(true);
        WiFi.begin(ssid.c_str(), password.c_str());
        WiFi.persistent(false);
        state_ = WiFiManagerState::Connecting;
        connectStartMs_ = millis();
    }

    void startAP() {
        Serial.println("[WiFi] Starting AP mode...");
        WiFi.mode(WIFI_AP_STA);

        // Open network — this AP only exists transiently for initial setup
        // (it's replaced by the real WiFi connection once provisioned), so a
        // password just adds friction with nothing left to protect.
        String apName = "CEC-Dongle-" + String(ESP.getChipId(), HEX);
        WiFi.softAP(apName.c_str());
        delay(500);  // allow hardware to settle before scanning/DNS
        Serial.printf("[WiFi] AP SSID: %s (open)\n", apName.c_str());
        Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

        // Captive portal DNS
        dnsServer_.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer_.start(53, "*", WiFi.softAPIP());

        state_ = WiFiManagerState::AP;
    }

    WiFiManagerState state() const { return state_; }

    bool isConnected() const { return state_ == WiFiManagerState::Connected; }

    String localIP() const {
        if (state_ == WiFiManagerState::Connected) return WiFi.localIP().toString();
        return WiFi.softAPIP().toString();
    }

    String statusString() const {
        switch (state_) {
            case WiFiManagerState::AP:         return "AP Mode";
            case WiFiManagerState::Connecting: return "Connecting...";
            case WiFiManagerState::Connected:  return "Connected";
            default:                           return "Idle";
        }
    }

    // Returns {"scanning":true} while the async scan is running, or
    // {"scanning":false,"networks":[...]} once results are available.
    // The scan state machine runs in loop(); this method just sets a flag
    // to request a scan and returns the cached result when ready.
    String scanNetworks() {
        if (scanDone_) {
            String json = buildScanJson();
            scanDone_ = false;
            wantScan_ = false;
            return json;
        }
        // Start a new scan if not already requested/in progress
        if (!wantScan_) {
            wantScan_ = true;
        }
        return F("{\"scanning\":true}");
    }

private:
    String buildScanJson() {
        String json = "{\"scanning\":false,\"networks\":[";
        bool first = true;
        for (int i = 0; i < scanCount_; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // hidden network — nothing to select
            if (!first) json += ',';
            first = false;
            json += "{\"ssid\":\"" + escSsid(ssid) + "\","
                    "\"rssi\":" + String(WiFi.RSSI(i)) + ","
                    "\"encrypted\":" + String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false") + "}";
        }
        json += "]}";
        WiFi.scanDelete();
        return json;
    }

    // Minimal JSON string escaping for SSID values embedded in hand-built JSON.
    // An SSID can contain '"', '\', or control characters that would break the JSON.
    static String escSsid(const String &s) {
        String out;
        out.reserve(s.length() + 4);
        for (size_t i = 0; i < s.length(); i++) {
            const char c = s[i];
            if      (c == '"')  out += F("\\\"");
            else if (c == '\\') out += F("\\\\");
            else if (c == '\n') out += F("\\n");
            else if (c == '\r') out += F("\\r");
            else if ((uint8_t)c < 0x20) { char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (uint8_t)c); out += esc; }
            else                out += c;
        }
        return out;
    }

    WiFiManagerState state_ = WiFiManagerState::Idle;
    DNSServer dnsServer_;
    String hostname_;
    uint32_t connectStartMs_ = 0;
    uint32_t connectTimeoutMs_ = 15000;

    // WiFi scan state — driven entirely by loop(), read by scanNetworks()
    bool wantScan_       = false;
    bool scanInProgress_ = false;
    bool scanDone_       = false;
    int  scanCount_      = 0;
};
