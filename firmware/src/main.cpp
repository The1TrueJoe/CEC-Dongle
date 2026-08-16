/*
 * CEC-Dongle Firmware
 * Main entry point — ties together CEC driver, WiFi, config, and web server
 *
 * Hardware: SMLIGHT SLWF-08 (ESP8266 / ESP-12E)
 * CEC Pin: GPIO14 (default)
 */

#include <Arduino.h>
#include <ESP8266mDNS.h>

#include "config_manager.h"
#include "wifi_manager.h"
#include "cec_driver.h"
#include "ota_manager.h"
#include "web_server.h"
#include "sddp_server.h"

// ── Global instances ────────────────────────────────────────────────────────

ConfigManager configMgr;
WiFiManager   wifiMgr;
CecDriver     cecDriver;
CecEventLog     cecLog;
CecStateTracker cecState;
OtaManager      otaMgr;
SddpServer      sddpServer;
WebServer      *webServer = nullptr;

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

    // 2c. Advertise over mDNS/Bonjour so controllers auto-discover the dongle.
    // ArduinoOTA.begin() already started the responder on <hostname>.local and
    // calls MDNS.update() from handle(); the responder re-announces itself on
    // every interface change (AP -> STA, DHCP renewal), so this runs once here.
    // Discovery: browse _cec._tcp for this device specifically, or _http._tcp
    // for anything with a web UI (Home Assistant, `dns-sd -B _http._tcp`).
    MDNS.addService("cec", "tcp", 80);
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "model",   "CEC-Dongle");
    MDNS.addServiceTxt("http", "tcp", "version", VERSION);
    MDNS.addServiceTxt("http", "tcp", "api",     "/api/status");
    MDNS.addServiceTxt("http", "tcp", "push",    "9000");
    Serial.printf("[mDNS] Advertising %s.local  _cec._tcp / _http._tcp :80\n",
                  cfg.hostname.c_str());

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
        if (cecState.update(src, dst, data) && webServer) {
            webServer->push(cecState.toJson());
        }
        Serial.printf("[CEC RX] %s  =>  %s\n",
                      f.toHexString().c_str(),
                      f.toReadableString().c_str());
    });

    // 4. Start web server (REST API + UI)
    webServer = new WebServer(cecDriver, configMgr, wifiMgr, cecLog, cecState);
    // Wire HTTP OTA callbacks: pause CEC ISR during flash write, resume after
    webServer->onOtaBegin([&]() { cecDriver.pause(); });
    webServer->onOtaEnd([&]()   { cecDriver.resume(); });
    webServer->begin();

    Serial.println("[Main] Setup complete ✓");
}

// ── Loop ────────────────────────────────────────────────────────────────────

// Start SDDP once STA WiFi connects; re-announced automatically on IP change.
static bool sddpStarted = false;

// How often to poll the TV for power status (ms). Each poll blocks loop() for
// ~70 ms, or ~350 ms if the TV doesn't ACK, so don't push this much lower.
static constexpr uint32_t POWER_POLL_MS = 15000;

void loop() {
    wifiMgr.loop();
    if (!sddpStarted && wifiMgr.isConnected()) {
        sddpServer.begin();
        sddpStarted = true;
    }
    sddpServer.loop();
    otaMgr.handle();
    cecDriver.loop();
    if (webServer) webServer->loop(); // drains HTTP-requested CEC sends — see PendingCecCmd

    // Ask the TV for its power status periodically. Without this the state
    // tracker only learns anything when some other device happens to talk, so
    // a freshly booted dongle would report "unknown" indefinitely. The reply
    // (0x90 Report Power Status) is decoded by the normal RX path.
    static uint32_t lastPollMs = 0;
    if (wifiMgr.isConnected() && !otaMgr.isActive() &&
        millis() - lastPollMs > POWER_POLL_MS) {
        lastPollMs = millis();
        cecDriver.send(configMgr.config.tvLogicalAddress, {0x8F});
    }
}
