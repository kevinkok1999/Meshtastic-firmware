#include "V30AirtimeGovernor.h"

#include <algorithm>
#include <cstring>

namespace v30 {

V30AirtimeGovernor::V30AirtimeGovernor()
    : entries_{}, count_(0), lastNowUs_(0), clockInitialized_(false),
      clockTrusted_(false), trustSinceUs_(0) {}

uint64_t V30AirtimeGovernor::saturatingAdd(uint64_t a, uint64_t b) {
  return (UINT64_MAX - a < b) ? UINT64_MAX : a + b;
}

bool V30AirtimeGovernor::acceptClock(uint64_t nowUs) {
  if (clockInitialized_ && nowUs < lastNowUs_) {
    // A monotonic rollback makes all timestamps uncertain. Fail closed until a
    // caller restores a trusted snapshot or a new monotonic hour elapses.
    clockTrusted_ = false;
    clockInitialized_ = false;
    trustSinceUs_ = 0;
    return false;
  }

  if (!clockInitialized_) {
    clockInitialized_ = true;
    lastNowUs_ = nowUs;
    if (!clockTrusted_) {
      trustSinceUs_ = nowUs;
      return false;
    }
    return true;
  }

  lastNowUs_ = nowUs;
  if (!clockTrusted_ && nowUs - trustSinceUs_ >= kWindowUs)
    clockTrusted_ = true;
  return clockTrusted_;
}

void V30AirtimeGovernor::prune(uint64_t nowUs) {
  size_t write = 0;
  for (size_t i = 0; i < count_; ++i) {
    const Entry& e = entries_[i];
    if (nowUs >= e.startUs && nowUs - e.startUs < kWindowUs)
      entries_[write++] = e;
  }
  for (size_t i = write; i < count_; ++i)
    entries_[i] = Entry{};
  count_ = write;
}

uint64_t V30AirtimeGovernor::usedUs(uint64_t nowUs) const {
  uint64_t total = 0;
  for (size_t i = 0; i < count_; ++i) {
    const Entry& e = entries_[i];
    if (nowUs >= e.startUs && nowUs - e.startUs < kWindowUs)
      total = saturatingAdd(total, e.durationUs);
  }
  return total;
}

bool V30AirtimeGovernor::append(uint64_t nowUs, uint64_t durationUs,
                                uint8_t band, bool reserved) {
  if (count_ >= kLedgerCapacity || durationUs == 0 || durationUs > kBudgetUs)
    return false;
  entries_[count_++] = Entry{nowUs, durationUs, band, reserved};
  return true;
}

bool V30AirtimeGovernor::mayTransmit(uint64_t nowUs, uint64_t estimatedUs,
                                     uint8_t band) {
  if (estimatedUs == 0 || estimatedUs > kBudgetUs || !acceptClock(nowUs))
    return false;
  prune(nowUs);
  const uint64_t total = usedUs(nowUs);
  if (total > kBudgetUs || estimatedUs > kBudgetUs - total)
    return false;
  return append(nowUs, estimatedUs, band, true);
}

void V30AirtimeGovernor::recordAttempt(uint64_t nowUs,
                                        uint64_t actualOrConservativeUs,
                                        uint8_t band) {
  if (!clockInitialized_ || nowUs < lastNowUs_)
    return;
  lastNowUs_ = nowUs;
  prune(nowUs);

  for (size_t i = 0; i < count_; ++i) {
    Entry& e = entries_[i];
    if (!e.reserved || e.band != band)
      continue;
    // The reservation is replaced, never added, preventing double accounting.
    const uint64_t charge =
        actualOrConservativeUs > e.durationUs ? actualOrConservativeUs
                                              : e.durationUs;
    e.durationUs = charge > kBudgetUs ? kBudgetUs : charge;
    e.reserved = false;
    return;
  }
  // A completion without a governor reservation is ambiguous. Ignoring it is
  // safer than silently creating unbounded or duplicate accounting.
}

bool V30AirtimeGovernor::restoreLedger(const Snapshot& snapshot,
                                       bool trustedClock) {
  if (snapshot.count > kLedgerCapacity)
    return false;
  uint64_t total = 0;
  for (size_t i = 0; i < snapshot.count; ++i) {
    const Entry& e = snapshot.entries[i];
    if (e.durationUs == 0 || e.durationUs > kBudgetUs)
      return false;
    total = saturatingAdd(total, e.durationUs);
  }
  if (total > kBudgetUs)
    return false;

  entries_ = {};
  count_ = 0;
  clockInitialized_ = false;
  clockTrusted_ = false;
  trustSinceUs_ = 0;
  lastNowUs_ = 0;

  if (!trustedClock) {
    // Do not reinterpret timestamps from an untrusted/rebooted monotonic
    // domain. The next observed clock begins a fresh conservative quarantine.
    return true;
  }

  for (size_t i = 0; i < snapshot.count; ++i)
    entries_[count_++] = snapshot.entries[i];
  lastNowUs_ = snapshot.lastNowUs;
  clockInitialized_ = snapshot.clockInitialized;
  clockTrusted_ = snapshot.clockInitialized;
  trustSinceUs_ = lastNowUs_;
  return true;
}

V30AirtimeGovernor::Snapshot V30AirtimeGovernor::snapshot() const {
  Snapshot out{};
  out.lastNowUs = lastNowUs_;
  out.count = count_;
  out.clockInitialized = clockInitialized_ && clockTrusted_;
  for (size_t i = 0; i < count_; ++i)
    out.entries[i] = entries_[i];
  return out;
}

bool V30AirtimeGovernor::clockTrusted() const { return clockTrusted_; }

}  // namespace v30
