#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "../overlay/src/helpers/esp32/V30EmergencyTransport.h"
namespace fixture {
enum class Route : uint8_t { Flood, Direct };
enum class Payload : uint8_t { RawCustom = 0x0f, NativeDm = 0x01, Group = 0x02 };
enum class Dispatch : uint8_t { Release, Consume, Deliver };
struct Packet { Route route; Payload payload; uint32_t logical_id; uint32_t attempt_id; };
Dispatch dispatch(const Packet& packet, std::vector<uint32_t>& delivered) {
  if (packet.payload == Payload::RawCustom) {
    if (packet.route != Route::Direct) return Dispatch::Consume;
    delivered.push_back(packet.logical_id); return Dispatch::Deliver;
  }
  if (packet.payload == Payload::NativeDm || packet.payload == Payload::Group) return Dispatch::Deliver;
  return Dispatch::Release;
}
void test_routing() {
  std::vector<uint32_t> delivered;
  assert(dispatch({Route::Flood, Payload::RawCustom, 7, 1}, delivered) == Dispatch::Consume);
  assert(delivered.empty());
  assert(dispatch({Route::Direct, Payload::RawCustom, 7, 1}, delivered) == Dispatch::Deliver);
  assert(delivered == std::vector<uint32_t>{7});
  assert(dispatch({Route::Direct, Payload::NativeDm, 0, 0}, delivered) == Dispatch::Deliver);
  assert(dispatch({Route::Direct, Payload::Group, 0, 0}, delivered) == Dispatch::Deliver);
}
void test_magic_and_retry_identity() {
  const uint8_t m30[] = {'M','3','0','E',1,7,0,0,0,1,0,0,0};
  assert(v30::v30ParseRaw(m30, sizeof(m30), true) == v30::RawDecision::Delivered);
  const uint8_t unknownRaw[] = {'X','X','X','X',1};
  assert(v30::v30ParseRaw(unknownRaw, sizeof(unknownRaw), true) == v30::RawDecision::Unrelated);
  const uint8_t badVersion[] = {'M','3','0','E',2,0,0,0,0,0,0,0,0};
  assert(v30::v30ParseRaw(badVersion, sizeof(badVersion), true) == v30::RawDecision::Consumed);
  const uint8_t legacy[] = {'M','2','9',0,0};
  assert(v30::v30ParseRaw(legacy, sizeof(legacy), true) == v30::RawDecision::Consumed);
}
static bool callbackCalled = false;
void callback(uint64_t, bool, uint32_t) { callbackCalled = true; }
void test_admission_and_completion_boundary() {
  const uint8_t payload[] = {1,2,3};
  assert(v30TryEnqueueEmergencyRaw(payload, sizeof(payload), 9, 2) == TxAdmission::Accepted);
  assert(v30TryEnqueueEmergencyRaw(payload, sizeof(payload), 10, 2) == TxAdmission::Busy);
  v30::V30RawFrame out{};
  assert(v30::v30TakeEmergencyRaw(out));
  assert(out.cookie == 9 && out.len == sizeof(payload) && out.priority == 2);
  callbackCalled = false;
  v30::v30SetTxResultCallback(callback);
  assert(!callbackCalled); // queue admission is not physical TX completion
  v30::onV30TxResult(9, true, 1234);
  assert(callbackCalled);
  v30::v30SetTxResultCallback(nullptr);
}
}  // namespace fixture
int main() {
  fixture::test_routing();
  fixture::test_magic_and_retry_identity();
  fixture::test_admission_and_completion_boundary();
  std::cout << "transport fixture OK\\n";
}
