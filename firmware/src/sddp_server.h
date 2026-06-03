/*
 * SDDP – Simple Device Discovery Protocol (Control4)
 *
 * Broadcasts device presence on the local network so Control4 Director can
 * auto-discover the dongle without a static IP.  The dongle joins the SDDP
 * multicast group (239.255.255.250:1902), replies to SEARCH requests, and
 * sends periodic NOTIFY announcements (every 15 minutes, half of Max-Age).
 *
 * On the Control4 side, the driver.xml network connection declares the
 * matching search_type "C4:CecDongle".  When Director finds the device it
 * auto-connects the binding and fires NetworkBindingChanged() in driver.lua,
 * from which the driver extracts the current IP via C4:GetBindingAddress().
 */

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Ticker.h>

class SddpServer {
public:
    // Call once WiFi STA is up; safe to call again on IP change / reconnect.
    void begin() {
        if (WiFi.status() != WL_CONNECTED) return;

        ticker_.detach();
        udp_.stop();

        lastIp_ = WiFi.localIP();
        udp_.beginMulticast(lastIp_, MCAST_ADDR, SDDP_PORT);
        sendNotify(MCAST_ADDR, SDDP_PORT);

        // Re-announce every 15 min (Max-Age is 1800 s)
        ticker_.attach(900.0f, [this]() { sendNotify(MCAST_ADDR, SDDP_PORT); });
        running_ = true;

        Serial.printf("[SDDP] Listening on %s:%u  Type=C4:CecDongle\n",
                      lastIp_.toString().c_str(), (unsigned)SDDP_PORT);
    }

    void loop() {
        if (!running_) return;

        // Re-initialise if the IP changed (DHCP renewal) OR if WiFi reconnected
        // with the same IP (multicast group membership is lost on disconnect even
        // if the IP is unchanged).
        IPAddress current = WiFi.localIP();
        bool nowConnected = (WiFi.status() == WL_CONNECTED);
        if (nowConnected && !wasConnected_) {
            // Just reconnected — always re-init to rejoin multicast group
            wasConnected_ = true;
            begin();
            return;
        }
        wasConnected_ = nowConnected;
        if (current != lastIp_ && (uint32_t)current != 0) {
            begin();
            return;
        }

        int len = udp_.parsePacket();
        if (len <= 0) return;

        char buf[512];
        int n = udp_.read(buf, sizeof(buf) - 1);
        if (n <= 0) return;
        buf[n] = '\0';

        // Respond to any SDDP SEARCH
        if (strstr(buf, "SEARCH") && strstr(buf, "SDDP")) {
            Serial.printf("[SDDP] Search from %s → replying\n",
                          udp_.remoteIP().toString().c_str());
            sendNotify(udp_.remoteIP(), udp_.remotePort());
        }
    }

private:
    static constexpr uint16_t SDDP_PORT = 1902;
    const IPAddress MCAST_ADDR{239, 255, 255, 250};

    void sendNotify(IPAddress dest, uint16_t port) {
        String ip  = WiFi.localIP().toString();
        String msg = "NOTIFY * SDDP/1.0\r\n"
                     "From: " + ip + ":1902\r\n"
                     "Host: " + ip + "\r\n"
                     "Max-Age: 1800\r\n"
                     "Type: \"C4:CecDongle\"\r\n"
                     "Primary-Proxy: tv\r\n"
                     "Proxies: tv\r\n"
                     "Manufacturer: CEC-Dongle\r\n"
                     "Model: CEC-TV-1\r\n"
                     "Driver: cec_dongle.c4z\r\n"
                     "\r\n";

        udp_.beginPacket(dest, port);
        udp_.write(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length());
        udp_.endPacket();
    }

    WiFiUDP   udp_;
    Ticker    ticker_;
    IPAddress lastIp_{0u};
    bool      running_      = false;
    bool      wasConnected_ = false;
};
