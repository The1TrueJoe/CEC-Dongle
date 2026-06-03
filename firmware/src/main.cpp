/*
 * CEC-Dongle Firmware
 * Main entry point — ties together CEC driver, WiFi, config, and web server
 *
 * Hardware: SMLIGHT SLWF-08 (ESP8266 / ESP-12E)
 * CEC Pin: GPIO14 (default)
 */

#include <Arduino.h>

#include "config_manager.h"
#include "wifi_manager.h"
#include "cec_driver.h"
#include "ota_manager.h"
#include "web_server.h"

// ── Global instances ────────────────────────────────────────────────────────

ConfigManager configMgr;
WiFiManager   wifiMgr;
CecDriver     cecDriver;
CecEventLog   cecLog;
OtaManager    otaMgr;
WebServer    *webServer = nullptr;

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("============================");
    Serial.printf("  CEC-Dongle v%s\n", VERSION);
    Serial.println("============================");

    // 1. Load config from flash
    configMgr.begin();
    auto &cfg = configMgr.config;

    // 2. Start WiFi (AP if no credentials, STA otherwise)
    wifiMgr.begin(cfg.hostname, cfg.wifiSsid, cfg.wifiPassword);

    // 2b. Enable standard ESP OTA (ArduinoOTA / UDP)
    // Pause CEC ISR before flash writes; resume (or restart) on completion/error.
    otaMgr.onBegin([&]() { cecDriver.pause(); });
    otaMgr.onEnd([&]()   { cecDriver.resume(); });
    otaMgr.begin(cfg.hostname);

    // 3. Initialise CEC driver
    cecLog.setMaxSize(cfg.logBufferSize);

    cecDriver.begin(
        cfg.cecPin,
        cfg.cecAddress,
        cfg.cecPhysical,
        cfg.cecOsdName,
        cfg.cecPromiscuous,
        cfg.cecMonitorMode
    );

    // Auto-negotiate logical address if enabled (waits for bus to settle first)
    delay(250);
    if (cfg.autoNegotiate) {
        cecDriver.negotiate(cfg.deviceType);
    }

    // Announce our physical address to all CEC devices on the bus
    delay(100);
    cecDriver.broadcastPhysicalAddress();

    // Register CEC message callback → log + serial
    cecDriver.onMessage([](uint8_t src, uint8_t dst, const std::vector<uint8_t> &data) {
        CecFrame f(src, dst, data);
        cecLog.add("rx", f);
        Serial.printf("[CEC RX] %s  =>  %s\n",
                      f.toHexString().c_str(),
                      f.toReadableString().c_str());
    });

    // 4. Start web server (REST API + UI)
    webServer = new WebServer(cecDriver, configMgr, wifiMgr, cecLog);
    // Wire HTTP OTA callbacks: pause CEC ISR during flash write, resume after
    webServer->onOtaBegin([&]() { cecDriver.pause(); });
    webServer->onOtaEnd([&]()   { cecDriver.resume(); });
    webServer->begin();

    Serial.println("[Main] Setup complete ✓");
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    wifiMgr.loop();
    otaMgr.handle();
    cecDriver.loop();
}
