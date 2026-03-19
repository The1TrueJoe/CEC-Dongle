/*
 * OTA Manager
 * Standard Arduino OTA support for ESP8266 network uploads.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>

class OtaManager {
public:
    void begin(const String &hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.setPort(8266);

        ArduinoOTA.onStart([]() {
            Serial.println("[OTA] Update start");
        });

        ArduinoOTA.onEnd([]() {
            Serial.println("\n[OTA] Update complete");
        });

        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            static uint8_t lastPercent = 255;
            const uint8_t percent = total ? (progress * 100U) / total : 0;
            if (percent != lastPercent) {
                lastPercent = percent;
                Serial.printf("[OTA] Progress: %u%%\r", percent);
            }
        });

        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("\n[OTA] Error[%u]: ", error);
            switch (error) {
                case OTA_AUTH_ERROR:    Serial.println("Auth Failed"); break;
                case OTA_BEGIN_ERROR:   Serial.println("Begin Failed"); break;
                case OTA_CONNECT_ERROR: Serial.println("Connect Failed"); break;
                case OTA_RECEIVE_ERROR: Serial.println("Receive Failed"); break;
                case OTA_END_ERROR:     Serial.println("End Failed"); break;
                default:                Serial.println("Unknown Error"); break;
            }
        });

        ArduinoOTA.begin();
        Serial.printf("[OTA] Ready: %s.local:%u\n", hostname.c_str(), ArduinoOTA.getPort());
    }

    void handle() {
        ArduinoOTA.handle();
    }
};
