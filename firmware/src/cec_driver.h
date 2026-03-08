/*
 * CEC Driver - Standalone HDMI-CEC bit-bang implementation
 * Ported from Palakis/esphome-native-hdmi-cec (CEC 1.3a)
 * Adapted for plain Arduino framework on ESP8266
 *
 * Original: https://github.com/Palakis/esphome-native-hdmi-cec
 * License: MIT
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include <array>
#include <functional>
#include <cstring>

// ── CEC Opcode names (subset for human-readable decoding) ──────────────────

struct CecOpcodeName {
    uint8_t opcode;
    const char *name;
};

static const CecOpcodeName CEC_OPCODE_NAMES[] = {
    {0x00, "Feature Abort"},
    {0x04, "Image View On"},
    {0x0D, "Text View On"},
    {0x32, "Set Menu Language"},
    {0x36, "Standby"},
    {0x41, "Volume Up (User Control Pressed: Vol+)"},
    {0x42, "Volume Down (User Control Pressed: Vol-)"},
    {0x43, "Mute (User Control Pressed: Mute)"},
    {0x44, "User Control Pressed"},
    {0x45, "User Control Released"},
    {0x46, "Give OSD Name"},
    {0x47, "Set OSD Name"},
    {0x64, "Set OSD String"},
    {0x70, "System Audio Mode Request"},
    {0x72, "Set System Audio Mode"},
    {0x7A, "Report Audio Status"},
    {0x7D, "Give System Audio Mode Status"},
    {0x7E, "System Audio Mode Status"},
    {0x80, "Routing Change"},
    {0x81, "Routing Information"},
    {0x82, "Active Source"},
    {0x83, "Give Physical Address"},
    {0x84, "Report Physical Address"},
    {0x85, "Request Active Source"},
    {0x86, "Set Stream Path"},
    {0x87, "Device Vendor ID"},
    {0x89, "Vendor Command"},
    {0x8C, "Give Device Vendor ID"},
    {0x8D, "Menu Request"},
    {0x8E, "Menu Status"},
    {0x8F, "Give Device Power Status"},
    {0x90, "Report Power Status"},
    {0x91, "Get Menu Language"},
    {0x9D, "Inactive Source"},
    {0x9E, "CEC Version"},
    {0x9F, "Get CEC Version"},
    {0xA0, "Vendor Command With ID"},
    {0xFF, "Abort"},
};

static const char* CEC_LOGICAL_NAMES[] = {
    "TV",             // 0
    "Recording 1",    // 1
    "Recording 2",    // 2
    "Tuner 1",        // 3
    "Playback 1",     // 4
    "Audio System",   // 5
    "Tuner 2",        // 6
    "Tuner 3",        // 7
    "Playback 2",     // 8
    "Recording 3",    // 9
    "Tuner 4",        // 10 (0xA)
    "Reserved 11",    // 11 (0xB)
    "Reserved 12",    // 12 (0xC)
    "Reserved 13",    // 13 (0xD)
    "Free Use",       // 14 (0xE)
    "Broadcast",      // 15 (0xF)
};

// ── CEC Frame ───────────────────────────────────────────────────────────────

class CecFrame : public std::vector<uint8_t> {
public:
    CecFrame() = default;

    CecFrame(uint8_t initiator, uint8_t target, const std::vector<uint8_t> &payload)
        : std::vector<uint8_t>(1 + payload.size(), 0) {
        this->at(0) = ((initiator & 0xf) << 4) | (target & 0xf);
        std::memcpy(this->data() + 1, payload.data(), payload.size());
    }

    uint8_t initiator() const { return (this->at(0) >> 4) & 0xf; }
    uint8_t destination() const { return this->at(0) & 0xf; }
    uint8_t opcode() const { return (this->size() >= 2) ? this->at(1) : 0; }
    bool isBroadcast() const { return destination() == 0xf; }
    static constexpr int MAX_LENGTH = 16;

    // "40:36" hex format
    String toHexString() const {
        String result;
        for (size_t i = 0; i < size(); i++) {
            char buf[4];
            sprintf(buf, "%02X", (*this)[i]);
            result += buf;
            if (i < size() - 1) result += ":";
        }
        return result;
    }

    // "TV → Broadcast: Standby" human-readable
    String toReadableString() const {
        uint8_t src = initiator();
        uint8_t dst = destination();

        String result;
        result += CEC_LOGICAL_NAMES[src & 0xf];
        result += " -> ";
        result += CEC_LOGICAL_NAMES[dst & 0xf];
        result += ": ";

        if (size() < 2) {
            result += "Ping";
            return result;
        }

        uint8_t op = this->at(1);
        const char *name = nullptr;
        for (const auto &entry : CEC_OPCODE_NAMES) {
            if (entry.opcode == op) { name = entry.name; break; }
        }

        if (name) {
            result += name;
        } else {
            char buf[8];
            sprintf(buf, "0x%02X", op);
            result += buf;
        }

        // append extra data bytes
        if (size() > 2) {
            result += " [";
            for (size_t i = 2; i < size(); i++) {
                char buf[4];
                sprintf(buf, "%02X", (*this)[i]);
                result += buf;
                if (i < size() - 1) result += " ";
            }
            result += "]";
        }
        return result;
    }
};

// ── Ring buffer for ISR-safe frame queuing ──────────────────────────────────

template <unsigned int SIZE>
class FrameRingBuffer {
public:
    FrameRingBuffer() : front_idx_(0), back_idx_(0) {
        for (auto &f : store_) {
            f = new CecFrame;
            f->reserve(CecFrame::MAX_LENGTH);
        }
    }

    ~FrameRingBuffer() {
        for (auto &f : store_) delete f;
    }

    CecFrame* front() const { return isEmpty() ? nullptr : store_[front_idx_]; }
    void popFront() { cyclicIncr(front_idx_); }

    CecFrame* back() const {
        if (isFull()) return nullptr;
        store_[back_idx_]->clear();
        return store_[back_idx_];
    }
    void pushBack() { cyclicIncr(back_idx_); }

    bool isEmpty() const { return count() == 0; }
    bool isFull() const { return count() == SIZE; }
    void reset() { front_idx_ = 0; back_idx_ = 0; }

private:
    int count() const { int n = (int)(back_idx_ - front_idx_); if (n < 0) n += SIZE + 1; return n; }
    void cyclicIncr(volatile unsigned int &idx) { idx = (idx == SIZE) ? 0 : (idx + 1); }

    volatile unsigned int front_idx_;
    volatile unsigned int back_idx_;
    mutable std::array<CecFrame*, SIZE + 1> store_;
};

// ── Receiver state machine ──────────────────────────────────────────────────

enum class RecvState : uint8_t {
    Idle = 0,
    ReceivingByte = 2,
    WaitingForEOM = 3,
    WaitingForAck = 4,
    WaitingForEOMAck = 5,
};

enum class SendResult : uint8_t {
    Success = 0,
    BusCollision = 1,
    NoAck = 2,
};

// ── Message callback ────────────────────────────────────────────────────────

using CecMessageCallback = std::function<void(uint8_t source, uint8_t destination,
                                               const std::vector<uint8_t> &data)>;

// ── CEC Driver class ────────────────────────────────────────────────────────

class CecDriver {
public:
    CecDriver() = default;

    void begin(uint8_t pin, uint8_t logicalAddress, uint16_t physicalAddress,
               const String &osdName = "CEC-Dongle", bool promiscuous = false,
               bool monitorMode = false);
    void loop();

    bool send(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data);

    // Convenience: send from own address
    bool send(uint8_t destination, const std::vector<uint8_t> &data) {
        return send(address_, destination, data);
    }

    void setAddress(uint8_t addr) { address_ = addr; }
    uint8_t address() const { return address_; }

    void setPhysicalAddress(uint16_t pa) { physicalAddress_ = pa; }
    uint16_t physicalAddress() const { return physicalAddress_; }

    void setOsdName(const String &name);
    String osdName() const;

    void setPromiscuous(bool p) { promiscuous_ = p; }
    bool promiscuous() const { return promiscuous_; }

    void setMonitorMode(bool m) { monitorMode_ = m; }
    bool monitorMode() const { return monitorMode_; }

    void onMessage(CecMessageCallback cb) { callback_ = cb; }

    uint8_t pin() const { return pin_; }

private:
    // Pin helpers
    void IRAM_ATTR setPinInputHigh();
    void IRAM_ATTR setPinOutputLow();

    // ISR
    static void IRAM_ATTR gpioISR();
    static void IRAM_ATTR resetRecvState();

    // Built-in protocol handlers
    void tryBuiltinHandler(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data);

    // Send internals
    SendResult sendFrame(const CecFrame &frame, bool isBroadcast);
    bool sendStartBit();
    void sendBit(bool value);
    bool sendHighAndTest();

    // Config
    uint8_t pin_ = 14;
    uint8_t address_ = 0x05;
    uint16_t physicalAddress_ = 0x4000;
    std::vector<uint8_t> osdNameBytes_;
    bool promiscuous_ = false;
    bool monitorMode_ = false;
    CecMessageCallback callback_;

    // Receiver state (accessed from ISR)
    static CecDriver *instance_;
    volatile bool lastLevel_ = true;
    volatile uint32_t lastFallingEdgeUs_ = 0;
    volatile uint32_t lastSentUs_ = 0;
    volatile RecvState recvState_ = RecvState::Idle;
    volatile uint8_t recvBitCounter_ = 0;
    volatile uint8_t recvByteBuffer_ = 0;
    CecFrame *frameReceive_ = nullptr;
    volatile bool recvAckQueued_ = false;

    static constexpr int MAX_FRAMES_QUEUED = 4;
    FrameRingBuffer<MAX_FRAMES_QUEUED> framesQueue_;

    // CEC timing constants (microseconds)
    static constexpr uint32_t START_BIT_MIN_US = 3500;
    static constexpr uint32_t HIGH_BIT_MIN_US = 400;
    static constexpr uint32_t HIGH_BIT_MAX_US = 800;
    static constexpr uint32_t TOTAL_BIT_US = 2400;
    static constexpr uint32_t HIGH_BIT_US = 600;
    static constexpr uint32_t LOW_BIT_US = 1500;
    static constexpr size_t MAX_ATTEMPTS = 5;
};
