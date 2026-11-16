#include "V30EmergencyTransport.h"
#include <string.h>

namespace {
constexpr size_t kQueueCapacity = 8;
v30::V30RawFrame queue[kQueueCapacity];
size_t queueCount = 0;
v30::V30TxResultCallback resultCallback = nullptr;
}

namespace v30 {

TxAdmission v30TryEnqueueEmergencyRaw(const uint8_t* data, size_t len,
                                       uint64_t cookie, uint8_t priority) {
  if (!data || len == 0 || len > sizeof(V30RawFrame::data) || priority == 0)
    return TxAdmission::NoPool;
  if (queueCount != 0) return TxAdmission::Busy;
  if (queueCount >= kQueueCapacity) return TxAdmission::NoPool;
  memcpy(queue[queueCount].data, data, len);
  queue[queueCount].len = static_cast<uint16_t>(len);
  queue[queueCount].cookie = cookie;
  queue[queueCount].priority = priority;
  ++queueCount;
  return TxAdmission::Accepted;
}

bool v30TakeEmergencyRaw(V30RawFrame& out) {
  if (queueCount == 0) return false;
  out = queue[0];
  queue[0] = V30RawFrame{};
  queueCount = 0;
  return true;
}

void v30SetTxResultCallback(V30TxResultCallback callback) {
  resultCallback = callback;
}

void onV30TxResult(uint64_t cookie, bool success, uint32_t airtimeUs) {
  if (resultCallback) resultCallback(cookie, success, airtimeUs);
}

RawDecision v30ParseRaw(const uint8_t* data, size_t len, bool direct,
                        uint32_t* logicalId, uint32_t* attemptId) {
  if (!data || len < 3) return RawDecision::Unrelated;
  if (data[0] == 'M' && data[1] == '2' && data[2] == '9')
    return RawDecision::Consumed;
  if (len < 5 || data[0] != 'M' || data[1] != '3' || data[2] != '0' ||
      data[3] != 'E') return RawDecision::Unrelated;
  // Any frame bearing V30 magic is owned by this parser. Unknown versions and
  // malformed lengths fail closed rather than falling through to native RAW.
  if (data[4] != 1 || len < 13) return RawDecision::Consumed;
  if (!direct) return RawDecision::Consumed;
  if (logicalId) memcpy(logicalId, data + 5, sizeof(uint32_t));
  if (attemptId) memcpy(attemptId, data + 9, sizeof(uint32_t));
  return RawDecision::Delivered;
}

}  // namespace v30
