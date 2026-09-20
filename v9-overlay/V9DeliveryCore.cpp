#include "V9DeliveryCore.h"

#include <algorithm>
#include <cstring>

namespace ops::v9 {

void MessageIdGenerator::seed(uint64_t nodePart, uint64_t sessionNonce) {
    _nodePart = nodePart;
    _sessionNonce = sessionNonce ? sessionNonce : 0x9E3779B97F4A7C15ULL;
    _counter = 0;
}

MessageId128 MessageIdGenerator::next() {
    ++_counter;
    if (_counter == 0) ++_counter;

    MessageId128 id;
    id.hi = _nodePart ^ (_sessionNonce + 0x9E3779B97F4A7C15ULL + (_counter << 6) + (_counter >> 2));
    id.lo = (_sessionNonce << 1) ^ (_counter * 0xD6E8FEB86659FD93ULL);
    return id;
}

void SelectiveAck32::reset() {
    _bitmap = 0;
}

bool SelectiveAck32::markReceived(uint8_t fragmentIndex) {
    if (fragmentIndex >= 32) return false;
    _bitmap |= (1UL << fragmentIndex);
    return true;
}

bool SelectiveAck32::received(uint8_t fragmentIndex) const {
    return fragmentIndex < 32 && (_bitmap & (1UL << fragmentIndex));
}

bool SelectiveAck32::complete(uint8_t fragmentCount) const {
    if (fragmentCount == 0 || fragmentCount > 32) return false;
    const uint32_t expected = fragmentCount == 32 ? 0xFFFFFFFFUL : ((1UL << fragmentCount) - 1UL);
    return (_bitmap & expected) == expected;
}

uint32_t SelectiveAck32::missingMask(uint8_t fragmentCount) const {
    if (fragmentCount == 0 || fragmentCount > 32) return 0;
    const uint32_t expected = fragmentCount == 32 ? 0xFFFFFFFFUL : ((1UL << fragmentCount) - 1UL);
    return expected & ~_bitmap;
}

bool XorErasureFec::makeParity(
    const uint8_t* const fragments[],
    const size_t lengths[],
    size_t fragmentCount,
    uint8_t* parityOut,
    size_t parityCapacity,
    size_t& parityLength
) {
    parityLength = 0;
    if (!fragments || !lengths || !parityOut || fragmentCount == 0 || fragmentCount > MAX_DATA_FRAGMENTS) return false;

    size_t maxLen = 0;
    for (size_t i = 0; i < fragmentCount; ++i) {
        if (!fragments[i] || lengths[i] > MAX_FRAGMENT_BYTES) return false;
        maxLen = std::max(maxLen, lengths[i]);
    }
    if (maxLen == 0 || maxLen > parityCapacity) return false;

    std::memset(parityOut, 0, maxLen);
    for (size_t i = 0; i < fragmentCount; ++i) {
        for (size_t j = 0; j < lengths[i]; ++j) parityOut[j] ^= fragments[i][j];
    }
    parityLength = maxLen;
    return true;
}

bool XorErasureFec::recoverOne(
    uint8_t* fragments[],
    size_t lengths[],
    bool present[],
    size_t fragmentCount,
    const uint8_t* parity,
    size_t parityLength,
    size_t missingIndex,
    size_t expectedMissingLength
) {
    if (!fragments || !lengths || !present || !parity ||
        fragmentCount == 0 || fragmentCount > MAX_DATA_FRAGMENTS ||
        missingIndex >= fragmentCount ||
        expectedMissingLength == 0 ||
        expectedMissingLength > MAX_FRAGMENT_BYTES ||
        parityLength < expectedMissingLength ||
        !fragments[missingIndex]) return false;

    size_t missingCount = 0;
    for (size_t i = 0; i < fragmentCount; ++i) if (!present[i]) ++missingCount;
    if (missingCount != 1 || present[missingIndex]) return false;

    std::memcpy(fragments[missingIndex], parity, expectedMissingLength);
    for (size_t i = 0; i < fragmentCount; ++i) {
        if (i == missingIndex) continue;
        if (!present[i] || !fragments[i] || lengths[i] > parityLength) return false;
        const size_t n = std::min(expectedMissingLength, lengths[i]);
        for (size_t j = 0; j < n; ++j) fragments[missingIndex][j] ^= fragments[i][j];
    }
    lengths[missingIndex] = expectedMissingLength;
    present[missingIndex] = true;
    return true;
}

} // namespace ops::v9
