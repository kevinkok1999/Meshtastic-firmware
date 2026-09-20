#pragma once

#include <cstddef>
#include <cstdint>

namespace ops::v8 {

constexpr uint8_t XBEE_API_START = 0x7E;
constexpr uint8_t XBEE_FRAME_TX_REQUEST = 0x10;
constexpr uint8_t XBEE_FRAME_MODEM_STATUS = 0x8A;
constexpr uint8_t XBEE_FRAME_TX_STATUS = 0x8B;
constexpr uint8_t XBEE_FRAME_RX_PACKET = 0x90;
constexpr uint16_t XBEE_RESERVED_NETWORK_ADDRESS = 0xFFFE;

class XBeeApiCodec {
public:
    static size_t buildTransmitRequest(uint8_t* out, size_t outCapacity, uint8_t frameId,
                                       uint64_t destination64, const uint8_t* payload,
                                       size_t payloadLength, uint8_t broadcastRadius = 0,
                                       uint8_t transmitOptions = 0);
    static uint8_t checksum(const uint8_t* frameData, size_t frameDataLength);
    static bool checksumValid(const uint8_t* frameData, size_t frameDataLength, uint8_t checksumByte);
private:
    static void writeU64BigEndian(uint8_t* out, uint64_t value);
};

} // namespace ops::v8
