#include "XBeeApiCodec.h"

namespace ops::v8 {

void XBeeApiCodec::writeU64BigEndian(uint8_t* out, uint64_t value) {
    for (int i = 7; i >= 0; --i) out[7 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFU);
}

uint8_t XBeeApiCodec::checksum(const uint8_t* frameData, size_t frameDataLength) {
    uint8_t sum = 0;
    for (size_t i = 0; i < frameDataLength; ++i) sum = static_cast<uint8_t>(sum + frameData[i]);
    return static_cast<uint8_t>(0xFFU - sum);
}

bool XBeeApiCodec::checksumValid(const uint8_t* frameData, size_t frameDataLength, uint8_t checksumByte) {
    uint8_t sum = checksumByte;
    for (size_t i = 0; i < frameDataLength; ++i) sum = static_cast<uint8_t>(sum + frameData[i]);
    return sum == 0xFFU;
}

size_t XBeeApiCodec::buildTransmitRequest(uint8_t* out, size_t outCapacity, uint8_t frameId,
                                          uint64_t destination64, const uint8_t* payload,
                                          size_t payloadLength, uint8_t broadcastRadius,
                                          uint8_t transmitOptions) {
    if (!out || (!payload && payloadLength != 0)) return 0;
    constexpr size_t fixedFrameDataLength = 14;
    const size_t frameDataLength = fixedFrameDataLength + payloadLength;
    const size_t totalLength = 1 + 2 + frameDataLength + 1;
    if (frameDataLength > 0xFFFFU || outCapacity < totalLength) return 0;

    size_t cursor = 0;
    out[cursor++] = XBEE_API_START;
    out[cursor++] = static_cast<uint8_t>((frameDataLength >> 8) & 0xFFU);
    out[cursor++] = static_cast<uint8_t>(frameDataLength & 0xFFU);
    const size_t frameDataStart = cursor;

    out[cursor++] = XBEE_FRAME_TX_REQUEST;
    out[cursor++] = frameId;
    writeU64BigEndian(&out[cursor], destination64);
    cursor += 8;
    out[cursor++] = static_cast<uint8_t>((XBEE_RESERVED_NETWORK_ADDRESS >> 8) & 0xFFU);
    out[cursor++] = static_cast<uint8_t>(XBEE_RESERVED_NETWORK_ADDRESS & 0xFFU);
    out[cursor++] = broadcastRadius;
    out[cursor++] = transmitOptions;
    for (size_t i = 0; i < payloadLength; ++i) out[cursor++] = payload[i];
    out[cursor++] = checksum(&out[frameDataStart], frameDataLength);
    return cursor;
}

} // namespace ops::v8
