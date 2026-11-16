#pragma once
#include <stddef.h>
#include <stdint.h>
namespace v30 {
namespace envelope {
constexpr size_t kHeader=85, kTag=16, kSignature=64, kMaximum=184, kMaxBody=19;
enum class ParseResult : uint8_t { Ok, Unrelated, Invalid };
struct View {
    const uint8_t* wire=nullptr;
    size_t size=0;
    uint32_t attempt=0, expiryMinutes=0;
    uint16_t reportedAgeMinutes=0;
    uint8_t remaining=0, kind=0, priority=0, maxCarry=0, ttlHours=0, bodySize=0;
    uint64_t sequence=0;
    const uint8_t* logicalId=nullptr;
    const uint8_t* origin=nullptr;
    const uint8_t* recipientCommitment=nullptr;
    const uint8_t* ciphertext=nullptr;
    const uint8_t* tag=nullptr;
    const uint8_t* signature=nullptr;
};
// Structural validation only: Ok never means authenticated, fresh or delivered.
ParseResult parse(const uint8_t* data,size_t size,View& out);
}
}
