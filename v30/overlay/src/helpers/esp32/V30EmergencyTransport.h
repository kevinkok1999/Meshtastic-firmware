#pragma once
#include <stddef.h>
#include <stdint.h>
namespace v30 {
enum class TxAdmission : uint8_t { Busy, NoPool, Accepted };
struct V30RawFrame { uint8_t data[184]; uint16_t len; uint64_t cookie; uint8_t priority; };
using V30TxResultCallback = void (*)(uint64_t, bool, uint32_t);
enum class RawDecision : uint8_t { Unrelated, Consumed, Delivered };
TxAdmission v30TryEnqueueEmergencyRaw(const uint8_t*, size_t, uint64_t, uint8_t);
bool v30TakeEmergencyRaw(V30RawFrame&);
void v30SetTxResultCallback(V30TxResultCallback);
void onV30TxResult(uint64_t, bool, uint32_t);
RawDecision v30ParseRaw(const uint8_t*, size_t, bool, uint32_t*, uint32_t*);
}
using v30::TxAdmission;
using v30::v30TryEnqueueEmergencyRaw;
using v30::onV30TxResult;
