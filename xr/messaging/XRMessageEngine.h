#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

enum class DeliveryState : uint8_t { QUEUED = 0, SENDING, WAITING_ACK, DELIVERED, DEFERRED, FAILED, CANCELLED, EXPIRED };
enum class FailureReason : uint8_t { NONE = 0, ROUTER_MAX_RETRANSMIT, DUTY_CYCLE, NO_CHANNEL, PEER_UNREACHABLE, INTERNAL_ERROR };

struct MessageId {
    uint32_t deviceBootId = 0;
    uint32_t sequence = 0;
    bool operator==(const MessageId &other) const { return deviceBootId == other.deviceBootId && sequence == other.sequence; }
};

struct MessageRecord {
    static constexpr size_t MAX_TEXT_BYTES = 180;
    MessageId id{};
    uint32_t peerNode = 0;
    uint32_t createdAt = 0;
    uint32_t expiresAt = 0;
    uint32_t lastAttemptAt = 0;
    uint32_t nextEligibleAttemptAt = 0;
    uint8_t deliveryCycles = 0;
    DeliveryState state = DeliveryState::QUEUED;
    FailureReason lastFailure = FailureReason::NONE;
    uint16_t textLength = 0;
    char text[MAX_TEXT_BYTES + 1]{};
};

struct DeliveryPolicy {
    uint8_t maxDeliveryCycles = 6;
    uint32_t firstDeferredDelayMs = 30000;
    uint32_t maxDeferredDelayMs = 15u * 60u * 1000u;
    uint32_t peerSeenFreshMs = 90u * 1000u;
    uint8_t maxQueuedMessages = 24;
    uint32_t messageTtlMs = 24u * 60u * 60u * 1000u;
};

struct LinkOpportunity {
    uint32_t peerSeenAgeMs = UINT32_MAX;
    bool recoveryWindowActive = false;
    bool regionAllowsTxNow = false;
    uint8_t linkScore = 0;
};

class XRMessageEngine {
  public:
    static constexpr size_t MAX_OUTBOX = 24;
    explicit XRMessageEngine(const DeliveryPolicy &policy = DeliveryPolicy{});

    void setBootIdentity(uint32_t bootId, uint32_t nextSequenceFloor = 1);
    uint32_t bootId() const { return bootId_; }
    uint32_t nextSequenceFloor() const { return nextSequence_; }

    bool enqueue(uint32_t peerNode, const char *text, uint16_t textLength, uint32_t nowMs, MessageId &outId);
    void onRouterSendStarted(const MessageId &id, uint32_t nowMs);
    void onRouterWaitingAck(const MessageId &id, uint32_t nowMs);
    void onRouterDelivered(const MessageId &id, uint32_t nowMs);
    void onRouterFinalFailure(const MessageId &id, FailureReason reason, uint32_t nowMs);

    bool cancel(const MessageId &id);
    void expire(uint32_t nowMs);
    MessageRecord *nextReady(uint32_t peerNode, uint32_t nowMs, const LinkOpportunity &opportunity);

    MessageRecord *find(const MessageId &id);
    const MessageRecord *find(const MessageId &id) const;
    size_t queuedCount() const;
    size_t deliveredCount() const;

    bool seenBefore(const MessageId &id) const;
    void rememberReceived(const MessageId &id);

  private:
    static constexpr size_t RX_DEDUP_CACHE = 32;
    DeliveryPolicy policy_;
    MessageRecord outbox_[MAX_OUTBOX]{};
    bool used_[MAX_OUTBOX]{};
    MessageId received_[RX_DEDUP_CACHE]{};
    bool receivedUsed_[RX_DEDUP_CACHE]{};
    size_t receivedCursor_ = 0;
    uint32_t bootId_ = 0;
    uint32_t nextSequence_ = 1;

    static uint32_t deferredDelay(const DeliveryPolicy &policy, uint8_t cycle);
    static bool timeReached(uint32_t nowMs, uint32_t targetMs);
    static bool isPendingState(DeliveryState state);
    size_t activeQueueCount() const;
    MessageRecord *allocateSlot();
};

} // namespace meshoffgrid::xr
