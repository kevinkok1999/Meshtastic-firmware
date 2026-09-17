#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xbee {

constexpr uint8_t API_START_DELIMITER = 0x7E;
constexpr uint8_t FRAME_AT_COMMAND = 0x08;
constexpr uint8_t FRAME_TX_REQUEST = 0x10;
constexpr uint8_t FRAME_AT_RESPONSE = 0x88;
constexpr uint8_t FRAME_MODEM_STATUS = 0x8A;
constexpr uint8_t FRAME_TX_STATUS = 0x8B;
constexpr uint8_t FRAME_RX_PACKET = 0x90;

constexpr uint16_t RESERVED_NETWORK_ADDRESS = 0xFFFE;
constexpr uint64_t BROADCAST_64 = 0x000000000000FFFFULL;

class XBeeApiCodec
{
  public:
    /**
     * Build an API mode 1 Transmit Request (0x10).
     *
     * Output contains the complete serial frame:
     * 0x7E + length + frame data + checksum.
     *
     * Returns number of bytes written, or 0 when the output buffer is too small.
     */
    static size_t buildTransmitRequest(uint8_t *out, size_t outCapacity, uint8_t frameId, uint64_t destination64,
                                       const uint8_t *payload, size_t payloadLength, uint8_t broadcastRadius = 0,
                                       uint8_t transmitOptions = 0);

    /**
     * Build a local AT Command API frame (0x08).
     * parameterLength may be zero for read-only/query commands.
     */
    static size_t buildAtCommand(uint8_t *out, size_t outCapacity, uint8_t frameId, const char command[2],
                                 const uint8_t *parameter = nullptr, size_t parameterLength = 0);

    /**
     * Validate the checksum for frame-data bytes plus the checksum byte.
     * A valid API frame satisfies:
     *   sum(frameData) + checksum == 0xFF (least-significant byte).
     */
    static bool checksumValid(const uint8_t *frameData, size_t frameDataLength, uint8_t checksum);

    /**
     * Calculate the API checksum for frame-data bytes.
     */
    static uint8_t checksum(const uint8_t *frameData, size_t frameDataLength);

  private:
    static void writeU64BigEndian(uint8_t *out, uint64_t value);
};

} // namespace meshoffgrid::xbee
