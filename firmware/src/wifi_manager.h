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
            startAP();
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
    }

    void connectSTA(const String &ssid, const String &password) {
        Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid.c_str());

        // If we're in AP mode, stop it
        if (state_ == WiFiManagerState::AP) {
            dnsServer_.stop();
            WiFi.softAPdisconnect(true);
        }

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        state_ = WiFiManagerState::Connecting;
        connectStartMs_ = millis();
    }

    void startAP() {
        Serial.println("[WiFi] Starting AP mode...");
        WiFi.mode(WIFI_AP_STA);

        String apName = "CEC-Dongle-" + String(ESP.getChipId(), HEX);
        WiFi.softAP(apName.c_str(), "cecdongle");
        Serial.printf("[WiFi] AP SSID: %s, Password: cecdongle\n", apName.c_str());
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

    String scanNetworks() {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\","
                    "\"rssi\":" + String(WiFi.RSSI(i)) + ","
                    "\"encrypted\":" + String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false") + "}";
        }
        json += "]";
        WiFi.scanDelete();
        return json;
    }

private:
    WiFiManagerState state_ = WiFiManagerState::Idle;
    DNSServer dnsServer_;
    String hostname_;
    uint32_t connectStartMs_ = 0;
    uint32_t connectTimeoutMs_ = 15000;
};
