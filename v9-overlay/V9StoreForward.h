#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "V9DeliveryCore.h"

namespace ops::v9 {

struct StoreItem {
    bool used = false;
    MessageId128 id{};
    uint8_t destination[4] = {};
    char text[160] = {};
    uint32_t createdMs = 0;
    uint32_t nextTryMs = 0;
    uint32_t expiryMs = 0;
    uint8_t attempts = 0;
};

class V9StoreForward {
public:
    static constexpr size_t CAPACITY = 8;
    static constexpr uint8_t MAX_ATTEMPTS = 6;

    bool enqueue(const MessageId128& id, const uint8_t destination[4], const char* text,
                 uint32_t nowMs, uint32_t ttlMs);
    int nextDue(uint32_t nowMs) const;
    StoreItem* item(size_t index);
    const StoreItem* item(size_t index) const;
    void markAccepted(size_t index);
    void markFailed(size_t index, uint32_t nowMs);
    void purgeExpired(uint32_t nowMs);
    size_t count() const;

private:
    std::array<StoreItem, CAPACITY> _items{};
    static uint32_t backoffMs(uint8_t attempts);
    static bool reached(uint32_t nowMs, uint32_t deadlineMs);
};

} // namespace ops::v9
