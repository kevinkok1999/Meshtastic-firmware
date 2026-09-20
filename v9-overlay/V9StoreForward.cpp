#include "V9StoreForward.h"

#include <cstring>

namespace ops::v9 {

bool V9StoreForward::reached(uint32_t nowMs, uint32_t deadlineMs) {
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

uint32_t V9StoreForward::backoffMs(uint8_t attempts) {
    static constexpr uint32_t kBackoff[] = {2000, 5000, 15000, 30000, 60000, 120000};
    const size_t i = attempts < 6 ? attempts : 5;
    return kBackoff[i];
}

bool V9StoreForward::enqueue(const MessageId128& id, const uint8_t destination[4], const char* text,
                             uint32_t nowMs, uint32_t ttlMs) {
    if (!destination || !text || !text[0] || ttlMs == 0) return false;
    for (auto& slot : _items) {
        if (!slot.used) {
            slot = {};
            slot.used = true;
            slot.id = id;
            std::memcpy(slot.destination, destination, 4);
            std::strncpy(slot.text, text, sizeof(slot.text) - 1);
            slot.createdMs = nowMs;
            slot.nextTryMs = nowMs + 2000;
            slot.expiryMs = nowMs + ttlMs;
            return true;
        }
    }
    return false;
}

int V9StoreForward::nextDue(uint32_t nowMs) const {
    for (size_t i = 0; i < _items.size(); ++i) {
        const auto& item = _items[i];
        if (item.used && reached(nowMs, item.nextTryMs) && !reached(nowMs, item.expiryMs)) return static_cast<int>(i);
    }
    return -1;
}

StoreItem* V9StoreForward::item(size_t index) {
    return index < _items.size() && _items[index].used ? &_items[index] : nullptr;
}

const StoreItem* V9StoreForward::item(size_t index) const {
    return index < _items.size() && _items[index].used ? &_items[index] : nullptr;
}

void V9StoreForward::markAccepted(size_t index) {
    if (index < _items.size()) _items[index] = {};
}

void V9StoreForward::markFailed(size_t index, uint32_t nowMs) {
    if (index >= _items.size() || !_items[index].used) return;
    auto& item = _items[index];
    if (item.attempts < 255) ++item.attempts;
    if (item.attempts >= MAX_ATTEMPTS) {
        item = {};
        return;
    }
    item.nextTryMs = nowMs + backoffMs(item.attempts);
}

void V9StoreForward::purgeExpired(uint32_t nowMs) {
    for (auto& item : _items) {
        if (item.used && reached(nowMs, item.expiryMs)) item = {};
    }
}

size_t V9StoreForward::count() const {
    size_t n = 0;
    for (const auto& item : _items) if (item.used) ++n;
    return n;
}

} // namespace ops::v9
