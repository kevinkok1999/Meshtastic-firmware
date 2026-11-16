#include "V30AirtimeGovernor.h"

#include <cassert>
#include <cstdint>

using v30::V30AirtimeGovernor;

static void testColdStartAndExactBoundary() {
  V30AirtimeGovernor g;
  assert(!g.mayTransmit(0, 1000, 1));
  assert(!g.mayTransmit(V30AirtimeGovernor::kWindowUs - 1, 1000, 1));
  assert(g.mayTransmit(V30AirtimeGovernor::kWindowUs, 1000, 1));
  g.recordAttempt(V30AirtimeGovernor::kWindowUs, 1000, 1);
  assert(g.usedUs(V30AirtimeGovernor::kWindowUs) == 1000);
}

static void testEightPercentSharedAcrossBands() {
  V30AirtimeGovernor::Snapshot s{};
  s.clockInitialized = true;
  V30AirtimeGovernor g;
  assert(g.restoreLedger(s, true));
  assert(g.mayTransmit(0, V30AirtimeGovernor::kBudgetUs, 1));
  g.recordAttempt(0, V30AirtimeGovernor::kBudgetUs, 1);
  assert(!g.mayTransmit(1, 1, 2));
  assert(g.mayTransmit(V30AirtimeGovernor::kWindowUs + 1, 1, 2));
}

static void testMeasuredReconciliationAndDuplicateCompletion() {
  V30AirtimeGovernor::Snapshot s{};
  s.clockInitialized = true;
  V30AirtimeGovernor g;
  assert(g.restoreLedger(s, true));
  assert(g.mayTransmit(0, 100, 7));
  g.recordAttempt(10, 500, 7);
  assert(g.usedUs(10) == 500);
  g.recordAttempt(10, 999999, 7);
  assert(g.usedUs(10) == 500);
}

static void testConservativeFailureAndNoPriorityBypass() {
  V30AirtimeGovernor::Snapshot s{};
  s.clockInitialized = true;
  V30AirtimeGovernor g;
  assert(g.restoreLedger(s, true));
  assert(g.mayTransmit(0, V30AirtimeGovernor::kBudgetUs - 10, 1));
  g.recordAttempt(0, V30AirtimeGovernor::kBudgetUs - 10, 1);
  assert(!g.mayTransmit(1, 1000000, 255));
}

static void testRollbackAndRestoreValidation() {
  V30AirtimeGovernor::Snapshot s{};
  s.clockInitialized = true;
  s.lastNowUs = 1000;
  V30AirtimeGovernor g;
  assert(g.restoreLedger(s, true));
  assert(!g.mayTransmit(999, 1, 1));
  V30AirtimeGovernor::Snapshot bad{};
  bad.count = V30AirtimeGovernor::kLedgerCapacity + 1;
  assert(!g.restoreLedger(bad, true));
}

int main() {
  testColdStartAndExactBoundary();
  testEightPercentSharedAcrossBands();
  testMeasuredReconciliationAndDuplicateCompletion();
  testConservativeFailureAndNoPriorityBypass();
  testRollbackAndRestoreValidation();
  return 0;
}
