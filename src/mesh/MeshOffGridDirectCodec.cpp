#include "MeshOffGridDirectCodec.h"

#include <algorithm>
#include <cstring>

namespace meshoffgrid
{

size_t directFragmentCount(size_t messageLength)
{
    if (messageLength == 0 || messageLength > kDirectMaxMessageBytes)
        return 0;
    return (messageLength + kDirectPayloadBytes - 1) / kDirectPayloadBytes;
}

bool buildDirectHello(uint32_t senderNode, DirectFrame &frame, size_t &frameLength)
{
    frame = {};
    frame.header.magic = kDirectFrameMagic;
    frame.header.version = kDirectFrameVersion;
    frame.header.type = static_cast<uint8_t>(DirectFrameType::Hello);
    frame.header.senderNode = senderNode;
    frameLength = sizeof(DirectFrameHeader);
    return senderNode != 0;
}

bool buildDirectDataFrame(uint32_t senderNode, uint32_t packetId, const uint8_t *message, size_t messageLength,
                          uint8_t fragmentIndex, DirectFrame &frame, size_t &frameLength)
{
    const size_t fragments = directFragmentCount(messageLength);
    if (!senderNode || !packetId || !message || fragments == 0 || fragmentIndex >= fragments)
        return false;

    const size_t offset = static_cast<size_t>(fragmentIndex) * kDirectPayloadBytes;
    const size_t payloadLength = std::min(kDirectPayloadBytes, messageLength - offset);

    frame = {};
    frame.header.magic = kDirectFrameMagic;
    frame.header.version = kDirectFrameVersion;
    frame.header.type = static_cast<uint8_t>(DirectFrameType::Data);
    frame.header.senderNode = senderNode;
    frame.header.packetId = packetId;
    frame.header.totalLength = static_cast<uint16_t>(messageLength);
    frame.header.fragmentOffset = static_cast<uint16_t>(offset);
    frame.header.fragmentIndex = fragmentIndex;
    frame.header.fragmentCount = static_cast<uint8_t>(fragments);
    frame.header.payloadLength = static_cast<uint16_t>(payloadLength);
    memcpy(frame.payload, message + offset, payloadLength);
    frameLength = sizeof(DirectFrameHeader) + payloadLength;
    return true;
}

bool parseDirectFrame(const uint8_t *bytes, size_t length, DirectFrame &frame)
{
    if (!bytes || length < sizeof(DirectFrameHeader) || length > sizeof(DirectFrame))
        return false;

    frame = {};
    memcpy(&frame, bytes, length);

    if (frame.header.magic != kDirectFrameMagic || frame.header.version != kDirectFrameVersion)
        return false;

    const auto type = static_cast<DirectFrameType>(frame.header.type);
    if (type == DirectFrameType::Hello) {
        return length == sizeof(DirectFrameHeader) && frame.header.senderNode != 0 && frame.header.packetId == 0 &&
               frame.header.totalLength == 0 && frame.header.fragmentOffset == 0 && frame.header.fragmentIndex == 0 &&
               frame.header.fragmentCount == 0 && frame.header.payloadLength == 0;
    }

    if (type != DirectFrameType::Data || frame.header.senderNode == 0 || frame.header.packetId == 0)
        return false;

    const size_t expectedFragments = directFragmentCount(frame.header.totalLength);
    if (expectedFragments == 0 || frame.header.fragmentCount != expectedFragments ||
        frame.header.fragmentIndex >= frame.header.fragmentCount)
        return false;

    const size_t expectedOffset = static_cast<size_t>(frame.header.fragmentIndex) * kDirectPayloadBytes;
    if (frame.header.fragmentOffset != expectedOffset)
        return false;

    const size_t expectedPayload =
        std::min(kDirectPayloadBytes, static_cast<size_t>(frame.header.totalLength) - expectedOffset);
    if (frame.header.payloadLength != expectedPayload || length != sizeof(DirectFrameHeader) + expectedPayload)
        return false;

    return true;
}

bool DirectReassemblyBuffer::reset(uint32_t senderNode, uint32_t packetId, uint16_t totalLength, uint8_t fragmentCount)
{
    const size_t expectedFragments = directFragmentCount(totalLength);
    if (!senderNode || !packetId || expectedFragments == 0 || fragmentCount != expectedFragments)
        return false;

    clear();
    active_ = true;
    senderNode_ = senderNode;
    packetId_ = packetId;
    totalLength_ = totalLength;
    fragmentCount_ = fragmentCount;
    return true;
}

bool DirectReassemblyBuffer::accept(const DirectFrame &frame)
{
    if (!active_ || frame.header.type != static_cast<uint8_t>(DirectFrameType::Data) ||
        frame.header.senderNode != senderNode_ || frame.header.packetId != packetId_ ||
        frame.header.totalLength != totalLength_ || frame.header.fragmentCount != fragmentCount_)
        return false;

    const uint8_t bit = static_cast<uint8_t>(1u << frame.header.fragmentIndex);
    if ((receivedMask_ & bit) != 0)
        return true;

    const size_t offset = frame.header.fragmentOffset;
    const size_t length = frame.header.payloadLength;
    if (offset + length > totalLength_ || offset + length > sizeof(data_))
        return false;

    memcpy(data_ + offset, frame.payload, length);
    receivedMask_ |= bit;
    return true;
}

bool DirectReassemblyBuffer::complete() const
{
    if (!active_ || fragmentCount_ == 0)
        return false;
    const uint8_t expectedMask = static_cast<uint8_t>((1u << fragmentCount_) - 1u);
    return receivedMask_ == expectedMask;
}

void DirectReassemblyBuffer::clear()
{
    active_ = false;
    senderNode_ = 0;
    packetId_ = 0;
    totalLength_ = 0;
    fragmentCount_ = 0;
    receivedMask_ = 0;
    memset(data_, 0, sizeof(data_));
}

} // namespace meshoffgrid
