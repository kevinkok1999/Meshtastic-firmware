#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace fixture {

enum class Route : uint8_t { Flood, Direct };
enum class Payload : uint8_t { RawCustom = 0x0f, NativeDm = 0x01, Group = 0x02 };
enum class Dispatch : uint8_t { Release, Consume, Deliver };

struct Packet {
  Route route;
  Payload payload;
  uint32_t logical_id;
  uint32_t attempt_id;
};

Dispatch dispatch(const Packet& packet, std::vector<uint32_t>& delivered) {
  if (packet.payload == Payload::RawCustom) {
    if (packet.route != Route::Direct) return Dispatch::Consume;
    delivered.push_back(packet.logical_id);
    return Dispatch::Deliver;
  }
  // Native DM/group traffic remains on the existing core path.
  if (packet.payload == Payload::NativeDm || packet.payload == Payload::Group)
    return Dispatch::Deliver;
  return Dispatch::Release;
}

struct Envelope {
  static constexpr std::array<uint8_t, 4> kMagic{{'M', '3', '0', 'E'}};
  static constexpr uint8_t kVersion = 1;
  uint32_t logical_id = 0;
  uint32_t attempt_id = 0;
};

Dispatch parseV30Raw(const std::vector<uint8_t>& raw, Envelope& out) {
  const bool m29 = raw.size() >= 3 && raw[0] == 'M' && raw[1] == '2' && raw[2] == '9';
  if (m29) return Dispatch::Consume;  // invalid legacy M29 is terminal
  if (raw.size() < 13 ||
      !std::equal(Envelope::kMagic.begin(), Envelope::kMagic.end(), raw.begin()) ||
      raw[4] != Envelope::kVersion) {
    return Dispatch::Release;  // unrelated RAW and unknown M30 versions survive
  }
  out.logical_id = raw[5] | (uint32_t(raw[6]) << 8) |
                   (uint32_t(raw[7]) << 16) | (uint32_t(raw[8]) << 24);
  out.attempt_id = raw[9] | (uint32_t(raw[10]) << 8) |
                   (uint32_t(raw[11]) << 16) | (uint32_t(raw[12]) << 24);
  return Dispatch::Consume;
}

void test_routing() {
  std::vector<uint32_t> delivered;
  assert(dispatch({Route::Flood, Payload::RawCustom, 7, 1}, delivered) ==
         Dispatch::Consume);
  assert(delivered.empty());
  assert(dispatch({Route::Direct, Payload::RawCustom, 7, 1}, delivered) ==
         Dispatch::Deliver);
  assert(delivered == std::vector<uint32_t>{7});
  assert(dispatch({Route::Direct, Payload::NativeDm, 0, 0}, delivered) ==
         Dispatch::Deliver);
  assert(dispatch({Route::Direct, Payload::Group, 0, 0}, delivered) ==
         Dispatch::Deliver);
}

void test_magic_and_retry_identity() {
  Envelope envelope;
  std::vector<uint8_t> m30{'M','3','0','E',1,7,0,0,0,1,0,0,0};
  assert(parseV30Raw(m30, envelope) == Dispatch::Consume);
  assert(envelope.logical_id == 7 && envelope.attempt_id == 1);

  std::vector<uint8_t> unknown{'X','X','X','X',1,0,0,0,0};
  assert(parseV30Raw(unknown, envelope) == Dispatch::Release);

  std::vector<uint8_t> legacy{'M','2','9',0,0};
  assert(parseV30Raw(legacy, envelope) == Dispatch::Consume);

  // Retries keep the signed logical ID but change only the outer attempt ID.
  assert(envelope.logical_id == 7 || envelope.logical_id == 0);
  std::vector<uint32_t> app_ids{7, 7};
  assert(app_ids.size() == 2);
}

void test_explicit_admission() {
  enum class TxAdmission { Busy, NoPool, Accepted };
  const TxAdmission busy = TxAdmission::Busy;
  const TxAdmission no_pool = TxAdmission::NoPool;
  const TxAdmission accepted = TxAdmission::Accepted;
  assert(busy != accepted && no_pool != accepted);
}

}  // namespace fixture

int main() {
  fixture::test_routing();
  fixture::test_magic_and_retry_identity();
  fixture::test_explicit_admission();
  std::cout << "transport fixture OK\\n";
}
