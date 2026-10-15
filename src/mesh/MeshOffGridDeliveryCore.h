#pragma once

#include "MeshOffGridRouteBrain.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid
{

constexpr uint8_t kProtocolVersion = 14;
constexpr size_t kMessageIdSize = 16;

using MessageId = std::array<uint8_t, kMessageIdSize>;

enum class DeliveryState : uint8_t {
    Created = 0,
    Queued,
    Routing,
    Sending,
    TransportAck,
    RemoteAck,
    Delivered,
    RetryWait,
    Fallback,
    Expired,
    Failed,
};

enum class PayloadType : uint8_t {
    Text = 0,
    Control,
    Telemetry,
    Binary,
};

struct MessageEnvelope {
    uint8_t protocolVersion = kProtocolVersion;
    MessageId messageId{};
    uint32_t conversationId = 0;
    uint32_t senderId = 0;
    uint32_t destinationId = 0;
    uint32_t createdAt = 0;
    uint32_t expiresAt = 0;
    uint32_t sequence = 0;
    uint32_t payloadBytes = 0;
    DeliveryIntent intent = DeliveryIntent::Reliable;
    PayloadType payloadType = PayloadType::Text;
    uint8_t priority = 0;
    uint8_t allowedTransports = 0x7f;
};

class DeliveryStateMachine
{
  public:
    static bool canTransition(DeliveryState from, DeliveryState to);
    static bool isTerminal(DeliveryState state);
    static bool isExpired(const MessageEnvelope &message, uint32_t nowEpochSeconds);
};

template <size_t Capacity> class MessageDedupCache
{
  public:
    static_assert(Capacity > 0, "MessageDedupCache requires at least one slot");

    bool contains(const MessageId &id) const
    {
        for (size_t i = 0; i < count_; ++i) {
            if (ids_[i] == id)
                return true;
        }
        return false;
    }

    bool remember(const MessageId &id)
    {
        if (contains(id))
            return false;

        if (count_ < Capacity) {
            ids_[count_++] = id;
        } else {
            ids_[next_] = id;
            next_ = (next_ + 1) % Capacity;
        }
        return true;
    }

    size_t size() const { return count_; }
    constexpr size_t capacity() const { return Capacity; }

  private:
    std::array<MessageId, Capacity> ids_{};
    size_t count_ = 0;
    size_t next_ = 0;
};

} // namespace meshoffgrid
