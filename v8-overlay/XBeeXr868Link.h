#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

#include "XBeeApiCodec.h"

namespace ops::v8 {

class XBeeXr868Link {
public:
    static constexpr size_t MAX_TX_PAYLOAD = 73;
    static constexpr size_t MAX_FRAME_DATA = 512;

    using ReceiveCallback = void (*)(uint64_t source64, const uint8_t* payload, size_t payloadLength, uint8_t options);
    using TxStatusCallback = void (*)(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);

    bool begin(HardwareSerial& serial, uint32_t baud, int8_t rxPin, int8_t txPin);
    void end();
    void poll();
    uint8_t send(uint64_t destination64, const uint8_t* payload, size_t payloadLength,
                 uint8_t transmitOptions = 0, uint8_t broadcastRadius = 0);

    void onReceive(ReceiveCallback cb) { _receive = cb; }
    void onTxStatus(TxStatusCallback cb) { _txStatus = cb; }
    bool ready() const { return _serial != nullptr; }

private:
    enum class ParseState : uint8_t { WaitStart, LengthMsb, LengthLsb, FrameData, Checksum };

    HardwareSerial* _serial = nullptr;
    ParseState _state = ParseState::WaitStart;
    uint16_t _expected = 0;
    uint16_t _received = 0;
    uint8_t _data[MAX_FRAME_DATA] = {};
    uint8_t _nextFrameId = 1;
    ReceiveCallback _receive = nullptr;
    TxStatusCallback _txStatus = nullptr;

    void resetParser();
    void consume(uint8_t byte);
    void dispatch();
    uint8_t allocateFrameId();
    static uint64_t readU64(const uint8_t* p);
};

} // namespace ops::v8
