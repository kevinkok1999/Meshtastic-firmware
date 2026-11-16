#include "V30AirtimeGovernor.h"

#include <cstring>

namespace v30 {

V30AirtimeGovernor::V30AirtimeGovernor()
    : entries_{}, count_(0), lastNowUs_(0), clockInitialized_(false),
      clockTrusted_(false), faulted_(false), trustSinceUs_(0) {}

uint64_t V30AirtimeGovernor::saturatingAdd(uint64_t a, uint64_t b) {
  return (UINT64_MAX - a < b) ? UINT64_MAX : a + b;
}

bool V30AirtimeGovernor::liveAt(const Entry& entry, uint64_t nowUs) {
  if (entry.reserved)
    return true;  // unresolved physical TX must never age out of the ledger
  return nowUs < saturatingAdd(entry.endUs, kWindowUs);
}

bool V30AirtimeGovernor::acceptClock(uint64_t nowUs) {
  if (clockInitialized_ && nowUs < lastNowUs_) {
    clockTrusted_ = false;
    clockInitialized_ = false;
    trustSinceUs_ = 0;
    faulted_ = true;
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
    if (liveAt(entries_[i], nowUs))
      entries_[write++] = entries_[i];
  }
  for (size_t i = write; i < count_; ++i)
    entries_[i] = Entry{};
  count_ = write;
}

uint64_t V30AirtimeGovernor::usedUs(uint64_t nowUs) const {
  uint64_t total = 0;
  for (size_t i = 0; i < count_; ++i) {
    if (liveAt(entries_[i], nowUs))
      total = saturatingAdd(total, entries_[i].durationUs);
  }
  return total;
}

bool V30AirtimeGovernor::append(uint64_t nowUs, uint64_t durationUs,
                                uint8_t band, bool reserved) {
  if (count_ >= kLedgerCapacity || durationUs == 0 || durationUs > kBudgetUs)
    return false;
  entries_[count_++] =
      Entry{nowUs, saturatingAdd(nowUs, durationUs), durationUs, band, reserved};
  return true;
}

bool V30AirtimeGovernor::mayTransmit(uint64_t nowUs, uint64_t estimatedUs,
                                     uint8_t band) {
  if (estimatedUs == 0 || estimatedUs > kBudgetUs || faulted_ ||
      !acceptClock(nowUs))
    return false;
  prune(nowUs);
  for (size_t i = 0; i < count_; ++i)
    if (entries_[i].reserved)
      return false;
  const uint64_t total = usedUs(nowUs);
  if (total > kBudgetUs || estimatedUs > kBudgetUs - total)
    return false;
  return append(nowUs, estimatedUs, band, true);
}

void V30AirtimeGovernor::recordAttempt(uint64_t nowUs,
                                        uint64_t actualOrConservativeUs,
                                        uint8_t band) {
  if (!clockInitialized_ || nowUs < lastNowUs_) {
    faulted_ = true;
    return;
  }
  lastNowUs_ = nowUs;
  prune(nowUs);
  size_t pending = kLedgerCapacity;
  for (size_t i = 0; i < count_; ++i)
    if (entries_[i].reserved) {
      pending = i;
      break;
    }
  if (pending == kLedgerCapacity || entries_[pending].band != band) {
    faulted_ = true;
    return;
  }
  Entry& e = entries_[pending];
  const bool overshot = actualOrConservativeUs > e.durationUs;
  const uint64_t charge = overshot ? actualOrConservativeUs : e.durationUs;
  e.durationUs = charge > kBudgetUs ? kBudgetUs : charge;
  const uint64_t measuredEnd = saturatingAdd(e.startUs, e.durationUs);
  e.endUs = measuredEnd > nowUs ? measuredEnd : nowUs;
  e.reserved = false;
  if (overshot)
    faulted_ = true;
}

bool V30AirtimeGovernor::restoreLedger(const Snapshot& snapshot,
                                       bool trustedClock) {
  if (snapshot.count > kLedgerCapacity)
    return false;
  uint64_t total = 0;
  uint64_t previousStart = 0;
  size_t reservedCount = 0;
  for (size_t i = 0; i < snapshot.count; ++i) {
    const Entry& e = snapshot.entries[i];
    const uint64_t minimumEnd = saturatingAdd(e.startUs, e.durationUs);
    if (e.durationUs == 0 || e.durationUs > kBudgetUs ||
        e.endUs < minimumEnd ||
        (snapshot.clockInitialized && e.startUs > snapshot.lastNowUs) ||
        (i != 0 && e.startUs < previousStart))
      return false;
    total = saturatingAdd(total, e.durationUs);
    if (e.reserved && ++reservedCount > 1)
      return false;
    previousStart = e.startUs;
  }
  if (total > kBudgetUs)
    return false;
  std::memset(entries_, 0, sizeof(entries_));
  count_ = 0;
  clockInitialized_ = false;
  clockTrusted_ = false;
  faulted_ = false;
  trustSinceUs_ = 0;
  lastNowUs_ = 0;
  if (!trustedClock)
    return true;
  for (size_t i = 0; i < snapshot.count; ++i)
    entries_[count_++] = snapshot.entries[i];
  lastNowUs_ = snapshot.lastNowUs;
  clockInitialized_ = snapshot.clockInitialized;
  clockTrusted_ = snapshot.clockInitialized;
  faulted_ = snapshot.faulted;
  trustSinceUs_ = lastNowUs_;
  return true;
}

V30AirtimeGovernor::Snapshot V30AirtimeGovernor::snapshot() const {
  Snapshot out{};
  out.lastNowUs = lastNowUs_;
  out.count = count_;
  out.clockInitialized = clockInitialized_ && clockTrusted_;
  out.faulted = faulted_;
  for (size_t i = 0; i < count_; ++i)
    out.entries[i] = entries_[i];
  return out;
}

bool V30AirtimeGovernor::clockTrusted() const { return clockTrusted_; }
bool V30AirtimeGovernor::faulted() const { return faulted_; }

}  // namespace v30
