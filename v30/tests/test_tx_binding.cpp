#include "V30RadioAdmissionBridge.h"
#include "V30TxBinding.h"
#include <cassert>
#include <type_traits>

using v30::V30PacketLease;
using v30::V30PacketLifecycle;
using v30::V30RadioAdmissionBridge;
using v30::V30QualifiedProfile;
using v30::V30AirtimeGovernor;

static_assert(!std::is_copy_constructible<V30PacketLifecycle>::value, "single pool owner");
static_assert(!std::is_copy_constructible<V30RadioAdmissionBridge>::value, "single airtime ledger");
static_assert(!std::is_copy_constructible<v30::V30TxBinding>::value, "single cookie owner");

static V30PacketLease dispatch(V30PacketLifecycle& life, int& storage) {
    auto lease = life.allocate(&storage);
    assert(lease.valid());
    assert(life.markQueued(lease));
    assert(life.markDispatching(lease));
    return lease;
}

static void lifetime() {
    V30PacketLifecycle life;
    int storage[17]{};
    assert(!life.allocate(nullptr).valid());
    auto first = life.allocate(&storage[0]);
    assert(first.valid());
    assert(!life.allocate(&storage[0]).valid());
    assert(!life.markDispatching(first)); // not yet queued
    assert(life.markQueued(first));
    assert(life.detach(first));
    assert(life.markQueued(first));
    assert(life.markDispatching(first));
    assert(life.release(first));
    auto reused = life.allocate(&storage[0]);
    assert(reused.valid() && reused != first);
    assert(!life.release(first)); // stale callback cannot release recycled slot
    assert(life.isAllocated(reused));
    assert(life.current(&storage[0]) == reused);
    for (size_t i=1; i<16; ++i) assert(life.allocate(&storage[i]).valid());
    assert(!life.allocate(&storage[16]).valid());
    assert(life.release(reused));
    assert(life.allocate(&storage[16]).valid());
}

static void account() {
    V30PacketLifecycle life;
    V30RadioAdmissionBridge bridge(life);
    int storage = 0;
    auto lease = dispatch(life, storage);
    auto prof = V30QualifiedProfile::Qualified8694;
    assert(!bridge.admit(lease, 200, 0, V30QualifiedProfile::Unqualified));
    const uint64_t start = V30AirtimeGovernor::kWindowUs;
    assert(!bridge.admit(lease, 200, start, prof));
    const uint64_t ready = start + V30AirtimeGovernor::kWindowUs;
    assert(!bridge.admit(lease, 200, ready - 1, prof));
    assert(bridge.admit(lease, 200, ready, prof));
    assert(!bridge.admit(lease, 1, ready, prof));
    // start failed / measured zero still consumes whole reservation in us.
    assert(bridge.complete(lease, 0, ready + 1000));
    assert(bridge.usedUs(ready + 1000) == 200000);
    assert(life.release(lease));
    auto second = dispatch(life, storage);
    assert(!bridge.admit(second, 287801, ready + 1001, prof)); // aggregate budget
    assert(bridge.admit(second, 1, ready + 1001, prof));
    assert(!bridge.complete(second, 2, ready + 3000)); // overrun must fault
    assert(bridge.faulted());
    assert(!bridge.admit(second, 1, ready + 3001, prof));
}

static void staleCompletion() {
    V30PacketLifecycle life;
    V30RadioAdmissionBridge bridge(life);
    int storage=0;
    auto old = dispatch(life, storage);
    auto prof = V30QualifiedProfile::Qualified8694;
    assert(!bridge.admit(old, 50, 0, prof));
    const auto ready = V30AirtimeGovernor::kWindowUs;
    assert(bridge.admit(old, 50, ready, prof));
    assert(life.release(old)); // illegal manager release while pending
    auto reused = dispatch(life, storage);
    assert(!bridge.complete(reused, 1, ready + 1000));
    assert(bridge.faulted());
    assert(!bridge.admit(reused, 1, ready + 1000, prof));
    assert(bridge.usedUs(ready + V30AirtimeGovernor::kWindowUs + 1) == 50000);
}

static void elapsedClockMismatch() {
    V30PacketLifecycle life;
    V30RadioAdmissionBridge bridge(life);
    int p=0;
    auto lease = dispatch(life, p);
    auto profile = V30QualifiedProfile::Qualified8694;
    assert(!bridge.admit(lease, 1, 0, profile));
    const uint64_t ready = V30AirtimeGovernor::kWindowUs;
    assert(bridge.admit(lease, 1, ready, profile));
    // Reported elapsed time is zero, but monotonic callback proves a longer
    // unresolved interval. Charge the larger interval and lock the policy.
    assert(!bridge.complete(lease, 0, ready+1001));
    assert(bridge.faulted());
    assert(bridge.usedUs(ready+1001) == 1001);
}

static void binding() {
    V30PacketLifecycle life;
    V30RadioAdmissionBridge bridge(life);
    v30::V30TxBinding binding;
    v30::V30TxSettlement out{};
    auto prof = V30QualifiedProfile::Qualified8694;
    int storage=0;
    auto old = life.allocate(&storage);
    assert(binding.bind(old, 1, life));
    assert(life.markQueued(old));
    assert(!binding.admit(old, bridge)); // queued is not dispatching
    assert(life.markDispatching(old));
    assert(!binding.admit(old, bridge)); // no real reservation, no admission
    assert(!bridge.admit(old, 100, 0, prof));
    const auto ready = V30AirtimeGovernor::kWindowUs;
    assert(bridge.admit(old, 100, ready, prof));
    assert(binding.admit(old, bridge));
    assert(life.release(old));
    auto reused = dispatch(life, storage);
    assert(reused != old);
    assert(!binding.settle(reused, true, 10, ready + 10000, bridge, out)); // same ptr, new gen
    assert(!binding.settle(old, true, 10, ready + 10000, bridge, out)); // old no longer live
    assert(bridge.faulted());
}

static void bindingCompletionAndDrop() {
    V30PacketLifecycle life;
    V30RadioAdmissionBridge bridge(life);
    v30::V30TxBinding binding;
    v30::V30TxSettlement out{};
    auto prof = V30QualifiedProfile::Qualified8694;
    int a = 0, b = 0;
    uint64_t cookie = 0;
    auto lease = life.allocate(&a), unrelated = life.allocate(&b);
    assert(!binding.bind(lease, 0, life));
    assert(binding.bind(lease, 42, life));
    assert(!binding.bind(unrelated, 100, life));
    assert(life.markQueued(lease));
    assert(life.markDispatching(lease));
    assert(!bridge.admit(lease, 100, 0, prof));
    const auto ready = V30AirtimeGovernor::kWindowUs;
    assert(bridge.admit(lease, 100, ready, prof));
    assert(!binding.admit(unrelated, bridge));
    assert(binding.admit(lease, bridge));
    assert(!binding.drop(lease, cookie));
    assert(!binding.settle(unrelated, true, 30, ready+30000, bridge, out));
    assert(binding.settle(lease, true, 30, ready+30000, bridge, out));
    assert(out.cookie == 42 && out.conservativeMs == 100 && out.success);
    assert(!binding.settle(lease, true, 30, ready+30000, bridge, out));
    assert(life.release(lease));

    auto next = life.allocate(&a);
    assert(binding.bind(next, 43, life));
    assert(life.markQueued(next) && life.markDispatching(next));
    assert(bridge.admit(next, 100, ready+30001, prof));
    assert(binding.admit(next, bridge));
    assert(binding.settle(next, false, 20, ready+50001, bridge, out));
    assert(out.cookie == 43 && out.conservativeMs == 100 && !out.success);
    assert(life.release(next));

    auto rejected = life.allocate(&a);
    assert(binding.bind(rejected, 44, life));
    assert(life.release(rejected)); // queue rejection has returned slot
    auto replacement = life.allocate(&a);
    assert(!binding.drop(replacement, cookie));
    assert(binding.drop(rejected, cookie) && cookie == 44);
    assert(binding.bind(replacement, 45, life));
}

int main() {
    lifetime();
    account();
    staleCompletion();
    elapsedClockMismatch();
    binding();
    bindingCompletionAndDrop();
}
