#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xbee {

constexpr uint8_t API_START_DELIMITER = 0x7E;
constexpr uint8_t FRAME_AT_COMMAND = 0x08;
constexpr uint8_t FRAME_AT_RESPONSE = 0x88;
constexpr uint8_t FRAME_TX_REQUEST = 0x10;
constexpr uint8_t FRAME_MODEM_STATUS = 0x8A;
constexpr uint8_t FRAME_TX_STATUS = 0x8B;
constexpr uint8_t FRAME_RX_PACKET = 0x90;

constexpr uint16_t RESERVED_NETWORK_ADDRESS = 0xFFFE;
constexpr uint64_t BROADCAST_64 = 0x000000000000FFFFULL;

class XBeeApiCodec
{
  public:
    static size_t buildTransmitRequest(uint8_t *out, size_t outCapacity, uint8_t frameId, uint64_t destination64,
                                       const uint8_t *payload, size_t payloadLength, uint8_t broadcastRadius = 0,
                                       uint8_t transmitOptions = 0);

    static size_t buildAtCommand(uint8_t *out, size_t outCapacity, uint8_t frameId, char command0, char command1,
                                 const uint8_t *value = nullptr, size_t valueLength = 0);

    static bool checksumValid(const uint8_t *frameData, size_t frameDataLength, uint8_t checksum);
    static uint8_t checksum(const uint8_t *frameData, size_t frameDataLength);

  private:
    static void writeU64BigEndian(uint8_t *out, uint64_t value);
};

} // namespace meshoffgrid::xbee

#endif
