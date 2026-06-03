/*
 * OTA Manager
 * Reliable OTA support for ESP8266:
 *   - ArduinoOTA (UDP-based, for PlatformIO/IDE uploads)
 *   - HTTP OTA is handled directly in web_server.h for browser-based uploads
 *
 * Key reliability improvements over the basic wrapper:
 *   - CEC ISR is paused before flash write begins (prevents bus collisions
 *     corrupting timing-sensitive bit-bang during write)
 *   - Deferred restart via Ticker (never calls ESP.restart() from inside
 *     an async callback, which can drop the response before sending)
 *   - Both onEnd and onError fire the resume callback so the ISR is
 *     always re-attached after OTA regardless of outcome
 */

#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <functional>

class OtaManager {
public:
    using VoidCb = std::function<void()>;

    // Register callbacks wired up by main.cpp
    void onBegin(VoidCb cb) { onBeginCb_ = cb; }
    void onEnd(VoidCb cb)   { onEndCb_   = cb; }

    void begin(const String &hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.setPort(8266);

        ArduinoOTA.onStart([this]() {
            otaActive_ = true;
            lastPct_   = 255;
            String type = (ArduinoOTA.getCommand() == U_FS) ? "filesystem" : "firmware";
            Serial.printf("\n[OTA] Starting %s update\n", type.c_str());
            if (onBeginCb_) onBeginCb_();
        });

        ArduinoOTA.onEnd([this]() {
            otaActive_ = false;
            Serial.println("\n[OTA] Complete — restarting");
            if (onEndCb_) onEndCb_();
            // ArduinoOTA library calls ESP.restart() itself after onEnd
        });

        ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
            uint8_t pct = total ? (uint8_t)((progress * 100UL) / total) : 0;
            if (pct != lastPct_) {
                lastPct_ = pct;
                Serial.printf("[OTA] %3u%%\r", pct);
            }
        });

        ArduinoOTA.onError([this](ota_error_t error) {
            otaActive_ = false;
            const char *msg = "Unknown";
            switch (error) {
                case OTA_AUTH_ERROR:    msg = "Auth Failed";                       break;
                case OTA_BEGIN_ERROR:   msg = "Begin Failed — check flash layout"; break;
                case OTA_CONNECT_ERROR: msg = "Connect Failed";                    break;
                case OTA_RECEIVE_ERROR: msg = "Receive Failed — upload interrupted"; break;
                case OTA_END_ERROR:     msg = "End Failed — flash write error";    break;
                default: break;
            }
            Serial.printf("\n[OTA] Error[%u]: %s\n", (unsigned)error, msg);
            if (onEndCb_) onEndCb_(); // always resume CEC even on error
        });

        ArduinoOTA.begin();
        Serial.printf("[OTA] ArduinoOTA ready  hostname=%s  port=8266\n", hostname.c_str());
    }

    void handle() { ArduinoOTA.handle(); }

    bool isActive() const { return otaActive_; }

    // Schedule a deferred restart — safe to call from ESPAsyncWebServer callbacks
    // because Ticker fires from loop(), after the TCP stack has sent the response.
    void scheduleRestart(uint32_t delayMs = 1200) {
        restartTicker_.once_ms(delayMs, []() {
            Serial.println("[OTA] Restarting now");
            ESP.restart();
        });
    }

private:
    VoidCb  onBeginCb_;
    VoidCb  onEndCb_;
    Ticker  restartTicker_;
    bool    otaActive_ = false;
    uint8_t lastPct_   = 255;
};
