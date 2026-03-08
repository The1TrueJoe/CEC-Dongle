/*
 * CEC Driver - Standalone HDMI-CEC bit-bang implementation
 * Ported from Palakis/esphome-native-hdmi-cec (CEC 1.3a)
 * Adapted for plain Arduino framework on ESP8266
 *
 * Original: https://github.com/Palakis/esphome-native-hdmi-cec
 * License: MIT
 */

#include "cec_driver.h"

// ── Singleton for ISR access ────────────────────────────────────────────────

CecDriver *CecDriver::instance_ = nullptr;

// ── Public API ──────────────────────────────────────────────────────────────

void CecDriver::begin(uint8_t pin, uint8_t logicalAddress, uint16_t physicalAddress,
                       const String &osdName, bool promiscuous, bool monitorMode) {
    pin_ = pin;
    address_ = logicalAddress;
    physicalAddress_ = physicalAddress;
    promiscuous_ = promiscuous;
    monitorMode_ = monitorMode;
    setOsdName(osdName);

    instance_ = this;

    pinMode(pin_, INPUT_PULLUP);
    framesQueue_.reset();
    attachInterrupt(digitalPinToInterrupt(pin_), CecDriver::gpioISR, CHANGE);

    Serial.printf("[CEC] Initialised on GPIO%d, addr=0x%X, phys=0x%04X, osd=\"%s\"\n",
                  pin_, address_, physicalAddress_, osdName.c_str());
}

void CecDriver::setOsdName(const String &name) {
    osdNameBytes_.clear();
    for (size_t i = 0; i < name.length() && i < 14; i++) {
        osdNameBytes_.push_back((uint8_t)name[i]);
    }
}

String CecDriver::osdName() const {
    String s;
    for (auto b : osdNameBytes_) s += (char)b;
    return s;
}

void CecDriver::loop() {
    while (CecFrame *frame = framesQueue_.front()) {
        uint8_t header = frame->front();
        uint8_t srcAddr = (header >> 4) & 0xf;
        uint8_t dstAddr = header & 0xf;

        if (!promiscuous_ && dstAddr != 0x0F && dstAddr != address_) {
            framesQueue_.popFront();
            continue;
        }

        if (frame->size() == 1) {
            // Ping — already handled by ack mechanism
            framesQueue_.popFront();
            continue;
        }

        Serial.printf("[CEC] Received: %s\n",
                      CecFrame(srcAddr, dstAddr,
                               std::vector<uint8_t>(frame->begin() + 1, frame->end()))
                          .toHexString().c_str());

        std::vector<uint8_t> data(frame->begin() + 1, frame->end());
        framesQueue_.popFront();

        // Fire user callback
        bool handled = false;
        if (callback_) {
            callback_(srcAddr, dstAddr, data);
            handled = true;
        }

        // Built-in handlers for directly addressed messages
        bool isDirectlyAddressed = (dstAddr != 0xF && dstAddr == address_);
        if (isDirectlyAddressed && !handled) {
            tryBuiltinHandler(srcAddr, dstAddr, data);
        }
    }
}

// ── Built-in CEC protocol handlers ─────────────────────────────────────────

static uint8_t logicalAddressToDeviceType(uint8_t addr) {
    switch (addr) {
        case 0x0: return 0x00; // TV
        case 0x5: return 0x05; // Audio System
        case 0x1: case 0x2: case 0x9: return 0x01; // Recording Device
        case 0x3: case 0x6: case 0x7: case 0xA: return 0x03; // Tuner
        default: return 0x04; // Playback Device
    }
}

void CecDriver::tryBuiltinHandler(uint8_t source, uint8_t destination,
                                    const std::vector<uint8_t> &data) {
    if (data.empty()) return;

    uint8_t opcode = data[0];
    switch (opcode) {
        case 0x9F: // Get CEC Version → reply CEC Version 1.3a
            send(address_, source, {0x9E, 0x04});
            break;

        case 0x8F: // Give Device Power Status → Report Power Status: On
            send(address_, source, {0x90, 0x00});
            break;

        case 0x46: { // Give OSD Name → Set OSD Name
            std::vector<uint8_t> reply = {0x47};
            reply.insert(reply.end(), osdNameBytes_.begin(), osdNameBytes_.end());
            send(address_, source, reply);
            break;
        }

        case 0x83: { // Give Physical Address → Report Physical Address
            std::vector<uint8_t> reply = {0x84};
            reply.push_back((physicalAddress_ >> 8) & 0xFF);
            reply.push_back(physicalAddress_ & 0xFF);
            reply.push_back(logicalAddressToDeviceType(address_));
            send(address_, 0xF, reply); // broadcast
            break;
        }

        case 0x00: // Feature Abort — ignore
            break;

        default: // Unsupported → Feature Abort
            send(address_, source, {0x00, opcode, 0x00});
            break;
    }
}

// ── Send ────────────────────────────────────────────────────────────────────

bool CecDriver::send(uint8_t source, uint8_t destination,
                      const std::vector<uint8_t> &dataBytes) {
    if (monitorMode_) return false;

    bool isBroadcast = (destination == 0xF);
    CecFrame frame(source, destination, dataBytes);

    Serial.printf("[CEC] Sending: %s\n", frame.toHexString().c_str());

    // Signal Free Time between transmissions per CEC spec
    uint8_t freeBitPeriods = (lastSentUs_ > lastFallingEdgeUs_) ? 7 : 5;

    for (size_t attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        int32_t delay_val = 0;
        while ((delay_val = freeBitPeriods * TOTAL_BIT_US +
                            std::max(lastSentUs_, (uint32_t)lastFallingEdgeUs_) - micros()) > 0) {
            delayMicroseconds(delay_val < 100 ? delay_val : 100);
            freeBitPeriods = 5;
        }

        auto result = sendFrame(frame, isBroadcast);
        if (result == SendResult::Success) {
            Serial.println("[CEC] Frame sent and acknowledged");
            return true;
        }

        Serial.printf("[CEC] Send attempt %d failed: %s\n", attempt + 1,
                      (result == SendResult::BusCollision) ? "Bus Collision" : "No Ack");
        freeBitPeriods = 3;
    }

    Serial.println("[CEC] Send failed after max attempts");
    return false;
}

SendResult IRAM_ATTR CecDriver::sendFrame(const CecFrame &frame, bool isBroadcast) {
    detachInterrupt(digitalPinToInterrupt(pin_));
    auto result = SendResult::Success;

    bool success = sendStartBit();

    for (auto it = frame.begin(); it != frame.end() && success; ++it) {
        uint8_t currentByte = *it;

        // Send 8 data bits
        for (int8_t i = 7; i >= 0 && success; i--) {
            bool bitVal = ((currentByte >> i) & 0b1);
            if (it == frame.begin() && i >= 4 && bitVal) {
                success = sendHighAndTest(); // arbitration
            } else {
                sendBit(bitVal);
            }
        }

        if (!success) {
            result = SendResult::BusCollision;
            break;
        }

        // EOM bit
        bool isEom = (it == (frame.end() - 1));
        sendBit(isEom);

        // ACK bit
        bool value = sendHighAndTest();
        success = (value == isBroadcast);
        if (!success) {
            result = SendResult::NoAck;
            break;
        }
    }

    lastSentUs_ = micros();
    attachInterrupt(digitalPinToInterrupt(pin_), CecDriver::gpioISR, CHANGE);
    return result;
}

bool IRAM_ATTR CecDriver::sendStartBit() {
    setPinOutputLow();
    delayMicroseconds(3700);

    setPinInputHigh();
    delayMicroseconds(400);
    bool value = digitalRead(pin_);

    delayMicroseconds(400);
    value &= digitalRead(pin_);

    return value; // true = no collision
}

void IRAM_ATTR CecDriver::sendBit(bool bitValue) {
    const uint32_t lowDuration = bitValue ? HIGH_BIT_US : LOW_BIT_US;
    const uint32_t highDuration = TOTAL_BIT_US - lowDuration;

    setPinOutputLow();
    delayMicroseconds(lowDuration);
    setPinInputHigh();
    delayMicroseconds(highDuration);
}

bool IRAM_ATTR CecDriver::sendHighAndTest() {
    uint32_t startUs = micros();

    setPinOutputLow();
    delayMicroseconds(HIGH_BIT_US);
    setPinInputHigh();

    static const uint32_t SAFE_SAMPLE_US = 1050;
    uint32_t elapsed = micros() - startUs;
    if (elapsed < SAFE_SAMPLE_US) {
        delayMicroseconds(SAFE_SAMPLE_US - elapsed);
    }
    bool value = digitalRead(pin_);

    elapsed = micros() - startUs;
    if (elapsed < TOTAL_BIT_US) {
        delayMicroseconds(TOTAL_BIT_US - elapsed);
    }

    return value;
}

// ── Pin helpers ─────────────────────────────────────────────────────────────

void IRAM_ATTR CecDriver::setPinInputHigh() {
    pinMode(pin_, INPUT_PULLUP);
}

void IRAM_ATTR CecDriver::setPinOutputLow() {
    pinMode(pin_, OUTPUT_OPEN_DRAIN);
    digitalWrite(pin_, LOW);
}

// ── ISR ─────────────────────────────────────────────────────────────────────

void IRAM_ATTR CecDriver::gpioISR() {
    CecDriver *self = instance_;
    if (!self) return;

    const uint32_t now = micros();
    const bool level = digitalRead(self->pin_);

    if (level == self->lastLevel_) return; // spurious
    self->lastLevel_ = level;

    // Falling edge
    if (!level) {
        self->lastFallingEdgeUs_ = now;

        if (self->recvAckQueued_ && !self->monitorMode_) {
            self->recvAckQueued_ = false;
            self->setPinOutputLow();
            delayMicroseconds(LOW_BIT_US);
            self->setPinInputHigh();
        }
        return;
    }

    // Rising edge — process pulse length
    uint32_t pulseDuration = now - self->lastFallingEdgeUs_;

    if (pulseDuration > START_BIT_MIN_US) {
        // Start bit detected
        self->recvState_ = RecvState::ReceivingByte;
        resetRecvState();
        self->recvAckQueued_ = false;
        self->frameReceive_ = self->framesQueue_.back();
        return;
    }

    if (pulseDuration < HIGH_BIT_MIN_US / 4) return; // glitch

    bool value = (pulseDuration >= HIGH_BIT_MIN_US && pulseDuration <= HIGH_BIT_MAX_US);

    switch (self->recvState_) {
        case RecvState::ReceivingByte: {
            self->recvByteBuffer_ = (self->recvByteBuffer_ << 1) | (value & 0b1);
            self->recvBitCounter_++;

            if (self->recvBitCounter_ >= 8) {
                if (self->frameReceive_) {
                    self->frameReceive_->push_back(self->recvByteBuffer_);
                }
                self->recvBitCounter_ = 0;
                self->recvByteBuffer_ = 0;
                self->recvState_ = RecvState::WaitingForEOM;
            }
            break;
        }

        case RecvState::WaitingForEOM: {
            uint8_t destAddr = self->frameReceive_ ?
                               (self->frameReceive_->front() & 0x0F) : 0xF;
            if (destAddr != 0xF && destAddr == self->address_) {
                self->recvAckQueued_ = true;
            }

            bool isEOM = (value == 1);
            if (isEOM) {
                if (self->frameReceive_ && self->frameReceive_->size() > 0) {
                    self->framesQueue_.pushBack();
                    self->frameReceive_ = nullptr;
                }
                resetRecvState();
            }

            self->recvState_ = isEOM ? RecvState::WaitingForEOMAck
                                     : RecvState::WaitingForAck;
            break;
        }

        case RecvState::WaitingForAck:
            self->recvState_ = RecvState::ReceivingByte;
            break;

        case RecvState::WaitingForEOMAck:
            self->recvState_ = RecvState::Idle;
            break;

        default:
            break;
    }
}

void IRAM_ATTR CecDriver::resetRecvState() {
    if (!instance_) return;
    instance_->recvBitCounter_ = 0;
    instance_->recvByteBuffer_ = 0;
}
