#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid
{

constexpr uint16_t kDirectFrameMagic = 0x4d47;
constexpr uint8_t kDirectFrameVersion = 1;
constexpr size_t kDirectMaxFrameBytes = 250;
constexpr size_t kDirectPayloadBytes = 220;
constexpr size_t kDirectMaxMessageBytes = 450;
constexpr size_t kDirectMaxFragments = (kDirectMaxMessageBytes + kDirectPayloadBytes - 1) / kDirectPayloadBytes;

enum class DirectFrameType : uint8_t {
    Hello = 1,
    Data = 2,
};

#pragma pack(push, 1)
struct DirectFrameHeader {
    uint16_t magic = kDirectFrameMagic;
    uint8_t version = kDirectFrameVersion;
    uint8_t type = 0;
    uint32_t senderNode = 0;
    uint32_t packetId = 0;
    uint16_t totalLength = 0;
    uint16_t fragmentOffset = 0;
    uint8_t fragmentIndex = 0;
    uint8_t fragmentCount = 0;
    uint16_t payloadLength = 0;
};

struct DirectFrame {
    DirectFrameHeader header{};
    uint8_t payload[kDirectPayloadBytes]{};
};
#pragma pack(pop)

static_assert(sizeof(DirectFrameHeader) == 20, "DirectLink header must stay wire-stable");
static_assert(sizeof(DirectFrame) <= kDirectMaxFrameBytes, "DirectLink frame must fit classic ESP-NOW payloads");

size_t directFragmentCount(size_t messageLength);
bool buildDirectHello(uint32_t senderNode, DirectFrame &frame, size_t &frameLength);
bool buildDirectDataFrame(uint32_t senderNode, uint32_t packetId, const uint8_t *message, size_t messageLength,
                          uint8_t fragmentIndex, DirectFrame &frame, size_t &frameLength);
bool parseDirectFrame(const uint8_t *bytes, size_t length, DirectFrame &frame);

class DirectReassemblyBuffer
{
  public:
    bool reset(uint32_t senderNode, uint32_t packetId, uint16_t totalLength, uint8_t fragmentCount);
    bool accept(const DirectFrame &frame);
    void clear();

    bool active() const { return active_; }
    bool complete() const;
    uint32_t senderNode() const { return senderNode_; }
    uint32_t packetId() const { return packetId_; }
    size_t length() const { return totalLength_; }
    const uint8_t *data() const { return data_; }

  private:
    bool active_ = false;
    uint32_t senderNode_ = 0;
    uint32_t packetId_ = 0;
    uint16_t totalLength_ = 0;
    uint8_t fragmentCount_ = 0;
    uint8_t receivedMask_ = 0;
    uint8_t data_[kDirectMaxMessageBytes]{};
};

} // namespace meshoffgrid
