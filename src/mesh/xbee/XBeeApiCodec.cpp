#include "XBeeApiCodec.h"

#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

namespace meshoffgrid::xbee {

void XBeeApiCodec::writeU64BigEndian(uint8_t *out, uint64_t value)
{
    for (int i = 7; i >= 0; --i)
        out[7 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

uint8_t XBeeApiCodec::checksum(const uint8_t *frameData, size_t frameDataLength)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < frameDataLength; ++i)
        sum = static_cast<uint8_t>(sum + frameData[i]);
    return static_cast<uint8_t>(0xFF - sum);
}

bool XBeeApiCodec::checksumValid(const uint8_t *frameData, size_t frameDataLength, uint8_t frameChecksum)
{
    uint8_t sum = frameChecksum;
    for (size_t i = 0; i < frameDataLength; ++i)
        sum = static_cast<uint8_t>(sum + frameData[i]);
    return sum == 0xFF;
}

size_t XBeeApiCodec::buildAtCommand(uint8_t *out, size_t outCapacity, uint8_t frameId, char command0, char command1,
                                    const uint8_t *value, size_t valueLength)
{
    if (!out || (valueLength && !value))
        return 0;

    const size_t frameDataLength = 4 + valueLength;
    const size_t totalLength = 1 + 2 + frameDataLength + 1;
    if (outCapacity < totalLength || frameDataLength > 0xFFFF)
        return 0;

    size_t cursor = 0;
    out[cursor++] = API_START_DELIMITER;
    out[cursor++] = static_cast<uint8_t>((frameDataLength >> 8) & 0xFF);
    out[cursor++] = static_cast<uint8_t>(frameDataLength & 0xFF);
    const size_t frameStart = cursor;

    out[cursor++] = FRAME_AT_COMMAND;
    out[cursor++] = frameId;
    out[cursor++] = static_cast<uint8_t>(command0);
    out[cursor++] = static_cast<uint8_t>(command1);
    for (size_t i = 0; i < valueLength; ++i)
        out[cursor++] = value[i];

    out[cursor++] = checksum(&out[frameStart], frameDataLength);
    return cursor;
}

size_t XBeeApiCodec::buildTransmitRequest(uint8_t *out, size_t outCapacity, uint8_t frameId, uint64_t destination64,
                                          const uint8_t *payload, size_t payloadLength, uint8_t broadcastRadius,
                                          uint8_t transmitOptions)
{
    if (!out || (payloadLength && !payload))
        return 0;

    constexpr size_t fixedFrameDataLength = 14;
    const size_t frameDataLength = fixedFrameDataLength + payloadLength;
    const size_t totalLength = 1 + 2 + frameDataLength + 1;
    if (frameDataLength > 0xFFFF || outCapacity < totalLength)
        return 0;

    size_t cursor = 0;
    out[cursor++] = API_START_DELIMITER;
    out[cursor++] = static_cast<uint8_t>((frameDataLength >> 8) & 0xFF);
    out[cursor++] = static_cast<uint8_t>(frameDataLength & 0xFF);
    const size_t frameDataStart = cursor;

    out[cursor++] = FRAME_TX_REQUEST;
    out[cursor++] = frameId;
    writeU64BigEndian(&out[cursor], destination64);
    cursor += 8;
    out[cursor++] = static_cast<uint8_t>((RESERVED_NETWORK_ADDRESS >> 8) & 0xFF);
    out[cursor++] = static_cast<uint8_t>(RESERVED_NETWORK_ADDRESS & 0xFF);
    out[cursor++] = broadcastRadius;
    out[cursor++] = transmitOptions;

    for (size_t i = 0; i < payloadLength; ++i)
        out[cursor++] = payload[i];

    out[cursor++] = checksum(&out[frameDataStart], frameDataLength);
    return cursor;
}

} // namespace meshoffgrid::xbee

#endif
