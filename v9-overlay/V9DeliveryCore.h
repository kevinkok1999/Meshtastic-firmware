#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ops::v9 {

struct MessageId128 {
    uint64_t hi = 0;
    uint64_t lo = 0;

    bool operator==(const MessageId128& other) const {
        return hi == other.hi && lo == other.lo;
    }
    bool operator!=(const MessageId128& other) const {
        return !(*this == other);
    }
};

class MessageIdGenerator {
public:
    void seed(uint64_t nodePart, uint64_t sessionNonce);
    MessageId128 next();

private:
    uint64_t _nodePart = 0;
    uint64_t _sessionNonce = 0;
    uint64_t _counter = 0;
};

class SelectiveAck32 {
public:
    void reset();
    bool markReceived(uint8_t fragmentIndex);
    bool received(uint8_t fragmentIndex) const;
    bool complete(uint8_t fragmentCount) const;
    uint32_t bitmap() const { return _bitmap; }
    uint32_t missingMask(uint8_t fragmentCount) const;

private:
    uint32_t _bitmap = 0;
};

class XorErasureFec {
public:
    static constexpr size_t MAX_FRAGMENT_BYTES = 192;
    static constexpr size_t MAX_DATA_FRAGMENTS = 4;

    static bool makeParity(
        const uint8_t* const fragments[],
        const size_t lengths[],
        size_t fragmentCount,
        uint8_t* parityOut,
        size_t parityCapacity,
        size_t& parityLength
    );

    static bool recoverOne(
        uint8_t* fragments[],
        size_t lengths[],
        bool present[],
        size_t fragmentCount,
        const uint8_t* parity,
        size_t parityLength,
        size_t missingIndex,
        size_t expectedMissingLength
    );
};

} // namespace ops::v9
