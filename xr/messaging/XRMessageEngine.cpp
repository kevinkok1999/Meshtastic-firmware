#include "XRMessageEngine.h"

#include <algorithm>
#include <cstring>

namespace meshoffgrid::xr {

XRMessageEngine::XRMessageEngine(const DeliveryPolicy &policy) : policy_(policy) {}

void XRMessageEngine::setBootIdentity(uint32_t bootId, uint32_t nextSequenceFloor)
{
    bootId_ = bootId;
    nextSequence_ = std::max<uint32_t>(nextSequenceFloor, 1u);
}

bool XRMessageEngine::timeReached(uint32_t nowMs, uint32_t targetMs)
{
    return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

bool XRMessageEngine::isPendingState(DeliveryState state)
{
    return state == DeliveryState::QUEUED || state == DeliveryState::SENDING || state == DeliveryState::WAITING_ACK ||
           state == DeliveryState::DEFERRED;
}

size_t XRMessageEngine::activeQueueCount() const
{
    size_t count = 0;
    for (size_t i = 0; i < MAX_OUTBOX; ++i)
        if (used_[i] && isPendingState(outbox_[i].state))
            ++count;
    return count;
}

MessageRecord *XRMessageEngine::allocateSlot()
{
    for (size_t i = 0; i < MAX_OUTBOX; ++i) {
        if (!used_[i]) {
            used_[i] = true;
            outbox_[i] = MessageRecord{};
            return &outbox_[i];
        }
    }

    MessageRecord *candidate = nullptr;
    for (size_t i = 0; i < MAX_OUTBOX; ++i) {
        if (!used_[i])
            continue;
        const DeliveryState state = outbox_[i].state;
        const bool reclaimable = state == DeliveryState::DELIVERED || state == DeliveryState::FAILED ||
                                 state == DeliveryState::CANCELLED || state == DeliveryState::EXPIRED;
        if (!reclaimable)
            continue;
        if (!candidate || static_cast<int32_t>(outbox_[i].createdAt - candidate->createdAt) < 0)
            candidate = &outbox_[i];
    }

    if (candidate)
        *candidate = MessageRecord{};
    return candidate;
}

bool XRMessageEngine::enqueue(uint32_t peerNode, const char *text, uint16_t textLength, uint32_t nowMs, MessageId &outId)
{
    if (!text || textLength == 0 || textLength > MessageRecord::MAX_TEXT_BYTES || peerNode == 0 || bootId_ == 0)
        return false;

    const size_t configuredLimit = std::min<size_t>(policy_.maxQueuedMessages, MAX_OUTBOX);
    if (activeQueueCount() >= configuredLimit)
        return false;

    MessageRecord *record = allocateSlot();
    if (!record)
        return false;

    if (nextSequence_ == 0)
        nextSequence_ = 1;

    record->id = {bootId_, nextSequence_++};
    record->peerNode = peerNode;
    record->createdAt = nowMs;
    record->expiresAt = policy_.messageTtlMs ? nowMs + policy_.messageTtlMs : 0;
    record->nextEligibleAttemptAt = nowMs;
    record->state = DeliveryState::QUEUED;
    record->textLength = textLength;
    std::memcpy(record->text, text, textLength);
    record->text[textLength] = '\0';
    outId = record->id;
    return true;
}

void XRMessageEngine::onRouterSendStarted(const MessageId &id, uint32_t nowMs)
{
    if (auto *record = find(id)) {
        if (!isPendingState(record->state))
            return;
        record->state = DeliveryState::SENDING;
        record->lastAttemptAt = nowMs;
    }
}

void XRMessageEngine::onRouterWaitingAck(const MessageId &id, uint32_t nowMs)
{
    if (auto *record = find(id)) {
        if (record->state != DeliveryState::SENDING && record->state != DeliveryState::WAITING_ACK)
            return;
        record->state = DeliveryState::WAITING_ACK;
        record->lastAttemptAt = nowMs;
    }
}

void XRMessageEngine::onRouterDelivered(const MessageId &id, uint32_t nowMs)
{
    if (auto *record = find(id)) {
        if (!isPendingState(record->state))
            return;
        record->state = DeliveryState::DELIVERED;
        record->lastFailure = FailureReason::NONE;
        record->lastAttemptAt = nowMs;
        record->nextEligibleAttemptAt = 0;
    }
}

uint32_t XRMessageEngine::deferredDelay(const DeliveryPolicy &policy, uint8_t cycle)
{
    if (cycle <= 1)
        return policy.firstDeferredDelayMs;
    const uint8_t shift = std::min<uint8_t>(cycle - 1, 5);
    uint64_t delay = static_cast<uint64_t>(policy.firstDeferredDelayMs) << shift;
    return static_cast<uint32_t>(std::min<uint64_t>(delay, policy.maxDeferredDelayMs));
}

void XRMessageEngine::onRouterFinalFailure(const MessageId &id, FailureReason reason, uint32_t nowMs)
{
    if (auto *record = find(id)) {
        if (!isPendingState(record->state))
            return;

        if (record->expiresAt && timeReached(nowMs, record->expiresAt)) {
            record->state = DeliveryState::EXPIRED;
            return;
        }

        record->lastFailure = reason;
        if (reason != FailureReason::DUTY_CYCLE && record->deliveryCycles != UINT8_MAX)
            ++record->deliveryCycles;

        if (record->deliveryCycles >= policy_.maxDeliveryCycles && reason != FailureReason::DUTY_CYCLE) {
            record->state = DeliveryState::FAILED;
            return;
        }

        record->state = DeliveryState::DEFERRED;
        const uint8_t delayCycle = std::max<uint8_t>(record->deliveryCycles, 1);
        record->nextEligibleAttemptAt = nowMs + deferredDelay(policy_, delayCycle);
    }
}

bool XRMessageEngine::cancel(const MessageId &id)
{
    if (auto *record = find(id)) {
        if (!isPendingState(record->state))
            return false;
        record->state = DeliveryState::CANCELLED;
        record->nextEligibleAttemptAt = 0;
        return true;
    }
    return false;
}

void XRMessageEngine::expire(uint32_t nowMs)
{
    for (size_t i = 0; i < MAX_OUTBOX; ++i) {
        if (!used_[i] || !isPendingState(outbox_[i].state) || outbox_[i].expiresAt == 0)
            continue;
        if (timeReached(nowMs, outbox_[i].expiresAt)) {
            outbox_[i].state = DeliveryState::EXPIRED;
            outbox_[i].nextEligibleAttemptAt = 0;
        }
    }
}

MessageRecord *XRMessageEngine::nextReady(uint32_t peerNode, uint32_t nowMs, const LinkOpportunity &opportunity)
{
    expire(nowMs);
    if (!opportunity.regionAllowsTxNow)
        return nullptr;

    const bool peerFresh = opportunity.peerSeenAgeMs <= policy_.peerSeenFreshMs;
    if (!peerFresh && !opportunity.recoveryWindowActive)
        return nullptr;

    MessageRecord *best = nullptr;
    for (size_t i = 0; i < MAX_OUTBOX; ++i) {
        if (!used_[i])
            continue;
        MessageRecord &record = outbox_[i];
        if (record.peerNode != peerNode)
            continue;
        if (record.state != DeliveryState::QUEUED && record.state != DeliveryState::DEFERRED)
            continue;
        if (!timeReached(nowMs, record.nextEligibleAttemptAt))
            continue;
        if (!best || static_cast<int32_t>(record.createdAt - best->createdAt) < 0)
            best = &record;
    }
    return best;
}

MessageRecord *XRMessageEngine::find(const MessageId &id)
{
    for (size_t i = 0; i < MAX_OUTBOX; ++i)
        if (used_[i] && outbox_[i].id == id)
            return &outbox_[i];
    return nullptr;
}

const MessageRecord *XRMessageEngine::find(const MessageId &id) const
{
    for (size_t i = 0; i < MAX_OUTBOX; ++i)
        if (used_[i] && outbox_[i].id == id)
            return &outbox_[i];
    return nullptr;
}

size_t XRMessageEngine::queuedCount() const { return activeQueueCount(); }

size_t XRMessageEngine::deliveredCount() const
{
    size_t count = 0;
    for (size_t i = 0; i < MAX_OUTBOX; ++i)
        if (used_[i] && outbox_[i].state == DeliveryState::DELIVERED)
            ++count;
    return count;
}

bool XRMessageEngine::seenBefore(const MessageId &id) const
{
    for (size_t i = 0; i < RX_DEDUP_CACHE; ++i)
        if (receivedUsed_[i] && received_[i] == id)
            return true;
    return false;
}

void XRMessageEngine::rememberReceived(const MessageId &id)
{
    if (id.deviceBootId == 0 || id.sequence == 0 || seenBefore(id))
        return;
    received_[receivedCursor_] = id;
    receivedUsed_[receivedCursor_] = true;
    receivedCursor_ = (receivedCursor_ + 1) % RX_DEDUP_CACHE;
}

} // namespace meshoffgrid::xr
