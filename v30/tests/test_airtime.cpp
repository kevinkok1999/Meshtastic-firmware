#include "V30AirtimeGovernor.h"

#include <cassert>
using v30::V30AirtimeGovernor;

static V30AirtimeGovernor trusted() {
  V30AirtimeGovernor::Snapshot s{};
  s.clockInitialized = true;
  return [] (V30AirtimeGovernor::Snapshot x) {
    V30AirtimeGovernor g;
    assert(g.restoreLedger(x, true));
    return g;
  }(s);
}
static void coldStart() {
  V30AirtimeGovernor g;
  assert(!g.mayTransmit(0, 1, 1));
  assert(!g.mayTransmit(V30AirtimeGovernor::kWindowUs - 1, 1, 1));
  assert(g.mayTransmit(V30AirtimeGovernor::kWindowUs, 1, 1));
}
static void capAndBands() {
  auto g = trusted();
  assert(g.mayTransmit(0, V30AirtimeGovernor::kBudgetUs, 1));
  g.recordAttempt(1, V30AirtimeGovernor::kBudgetUs, 1);
  assert(!g.mayTransmit(2, 1, 2));
  assert(g.mayTransmit(V30AirtimeGovernor::kBudgetUs +
                       V30AirtimeGovernor::kWindowUs + 1, 1, 2));
}
static void inFlightAndPacketBoundary() {
  auto g = trusted();
  assert(g.mayTransmit(V30AirtimeGovernor::kWindowUs - 1, 10, 3));
  assert(!g.mayTransmit(V30AirtimeGovernor::kWindowUs, 1, 3));
  g.recordAttempt(V30AirtimeGovernor::kWindowUs, 10, 3);
  assert(g.usedUs(V30AirtimeGovernor::kWindowUs + 9) == 10);
  assert(g.usedUs(V30AirtimeGovernor::kWindowUs + 10 +
                  V30AirtimeGovernor::kWindowUs) == 0);
}
static void duplicateAndUnknownCompletionFailClosed() {
  auto g = trusted();
  assert(g.mayTransmit(0, 100, 4));
  g.recordAttempt(0, 500, 4);
  assert(g.faulted());
  assert(!g.mayTransmit(1, 1, 4));
  g.recordAttempt(1, 1, 4);
  assert(g.faulted());
}
static void restoreValidation() {
  V30AirtimeGovernor g;
  V30AirtimeGovernor::Snapshot bad{};
  bad.clockInitialized = true;
  bad.lastNowUs = 10;
  bad.count = 1;
  bad.entries[0] = {20, 20, 1, 1, false};
  assert(!g.restoreLedger(bad, true));
}
int main() {
  coldStart();
  capAndBands();
  inFlightAndPacketBoundary();
  duplicateAndUnknownCompletionFailClosed();
  restoreValidation();
}
