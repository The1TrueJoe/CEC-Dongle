/*
 * Configuration Manager
 * Persistent storage of CEC and WiFi settings using LittleFS
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define CONFIG_FILE "/config.json"

struct Config {
    // WiFi
    String wifiSsid;
    String wifiPassword;
    String hostname = "cec-dongle";

    // CEC
    uint8_t cecPin       = DEFAULT_CEC_PIN;
    uint8_t cecAddress   = DEFAULT_CEC_ADDRESS;
    uint16_t cecPhysical = DEFAULT_CEC_PHYSICAL;
    String cecOsdName    = "CEC-Dongle";
    bool cecPromiscuous  = false;
    bool cecMonitorMode  = false;

    // Log buffer size
    uint16_t logBufferSize = 50;
};

class ConfigManager {
public:
    Config config;

    bool begin() {
        if (!LittleFS.begin()) {
            Serial.println("[Config] LittleFS mount failed, formatting...");
            LittleFS.format();
            if (!LittleFS.begin()) {
                Serial.println("[Config] LittleFS mount failed after format");
                return false;
            }
        }
        return load();
    }

    bool load() {
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (!f) {
            Serial.println("[Config] No config file found, using defaults");
            return false;
        }

        JsonDocument doc;
        auto err = deserializeJson(doc, f);
        f.close();

        if (err) {
            Serial.printf("[Config] Parse error: %s\n", err.c_str());
            return false;
        }

        config.wifiSsid      = doc["wifi_ssid"] | "";
        config.wifiPassword   = doc["wifi_password"] | "";
        config.hostname       = doc["hostname"] | "cec-dongle";
        config.cecPin         = doc["cec_pin"] | DEFAULT_CEC_PIN;
        config.cecAddress     = doc["cec_address"] | DEFAULT_CEC_ADDRESS;
        config.cecPhysical    = doc["cec_physical"] | DEFAULT_CEC_PHYSICAL;
        config.cecOsdName     = doc["cec_osd_name"] | "CEC-Dongle";
        config.cecPromiscuous = doc["cec_promiscuous"] | false;
        config.cecMonitorMode = doc["cec_monitor_mode"] | false;
        config.logBufferSize  = doc["log_buffer_size"] | 50;

        Serial.println("[Config] Loaded successfully");
        return true;
    }

    bool save() {
        JsonDocument doc;

        doc["wifi_ssid"]        = config.wifiSsid;
        doc["wifi_password"]    = config.wifiPassword;
        doc["hostname"]         = config.hostname;
        doc["cec_pin"]          = config.cecPin;
        doc["cec_address"]      = config.cecAddress;
        doc["cec_physical"]     = config.cecPhysical;
        doc["cec_osd_name"]     = config.cecOsdName;
        doc["cec_promiscuous"]  = config.cecPromiscuous;
        doc["cec_monitor_mode"] = config.cecMonitorMode;
        doc["log_buffer_size"]  = config.logBufferSize;

        File f = LittleFS.open(CONFIG_FILE, "w");
        if (!f) {
            Serial.println("[Config] Failed to open config file for writing");
            return false;
        }

        serializeJson(doc, f);
        f.close();
        Serial.println("[Config] Saved successfully");
        return true;
    }

    bool hasWifiCredentials() const {
        return config.wifiSsid.length() > 0;
    }

    void reset() {
        LittleFS.remove(CONFIG_FILE);
        config = Config();
        Serial.println("[Config] Reset to defaults");
    }

    String toJson() const {
        JsonDocument doc;
        doc["wifi_ssid"]        = config.wifiSsid;
        doc["wifi_password"]    = "***";  // masked
        doc["hostname"]         = config.hostname;
        doc["cec_pin"]          = config.cecPin;
        doc["cec_address"]      = config.cecAddress;
        doc["cec_physical"]     = config.cecPhysical;
        doc["cec_osd_name"]     = config.cecOsdName;
        doc["cec_promiscuous"]  = config.cecPromiscuous;
        doc["cec_monitor_mode"] = config.cecMonitorMode;
        doc["log_buffer_size"]  = config.logBufferSize;
        String out;
        serializeJson(doc, out);
        return out;
    }
};
