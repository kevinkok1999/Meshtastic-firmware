#include "../overlay/src/helpers/esp32/V30TxBinding.h"
#include <cassert>
#include <cstdint>

int main() {
  v30::V30TxBinding binding;
  v30::V30TxSettlement out{};
  int packetA = 0, packetB = 0;
  uint64_t cookie = 0;
  assert(!binding.bind(nullptr, 1));
  assert(!binding.bind(&packetA, 0));
  assert(binding.bind(&packetA, 42));
  assert(!binding.bind(&packetB, 43));
  assert(!binding.admit(&packetB, 100, true));
  assert(!binding.admit(&packetA, 100, false));
  assert(binding.admit(&packetA, 100, true));
  assert(!binding.drop(&packetA, cookie));
  assert(!binding.settle(&packetB, true, 30, out));
  assert(binding.settle(&packetA, true, 30, out));
  assert(out.cookie == 42 && out.success && out.conservativeMs == 100);
  assert(!binding.settle(&packetA, true, 30, out));

  assert(binding.bind(&packetA, 44));
  assert(binding.admit(&packetA, 100, true));
  assert(binding.settle(&packetA, false, 120, out));
  assert(out.cookie == 44 && !out.success && out.conservativeMs == 120);

  assert(binding.bind(&packetA, 45));
  assert(!binding.admit(&packetA, 0, true));
  assert(!binding.drop(&packetB, cookie));
  assert(binding.drop(&packetA, cookie) && cookie == 45);
  return 0;
}
