#pragma once
#include <stddef.h>
#include <stdint.h>

namespace v30 {

class V30AirtimeGovernor {
public:
  static constexpr uint64_t kWindowUs = 3600ULL * 1000000ULL;
  static constexpr uint64_t kBudgetUs = 288ULL * 1000000ULL;
  static constexpr size_t kLedgerCapacity = 64;

  struct Entry {
    uint64_t startUs;
    uint64_t endUs;
    uint64_t durationUs;
    uint8_t band;
    bool reserved;
  };

  struct Snapshot {
    uint64_t lastNowUs;
    size_t count;
    bool clockInitialized;
    bool faulted;
    Entry entries[kLedgerCapacity];
  };

  V30AirtimeGovernor();
  bool mayTransmit(uint64_t nowUs, uint64_t estimatedUs, uint8_t band);
  void recordAttempt(uint64_t nowUs, uint64_t actualOrConservativeUs,
                     uint8_t band);
  bool restoreLedger(const Snapshot& snapshot, bool trustedClock);
  Snapshot snapshot() const;
  uint64_t usedUs(uint64_t nowUs) const;
  bool clockTrusted() const;
  bool faulted() const;

private:
  Entry entries_[kLedgerCapacity];
  size_t count_;
  uint64_t lastNowUs_;
  bool clockInitialized_;
  bool clockTrusted_;
  bool faulted_;
  uint64_t trustSinceUs_;

  bool acceptClock(uint64_t nowUs);
  void prune(uint64_t nowUs);
  bool append(uint64_t nowUs, uint64_t durationUs, uint8_t band,
              bool reserved);
  static uint64_t saturatingAdd(uint64_t a, uint64_t b);
  static bool liveAt(const Entry& entry, uint64_t nowUs);
};

}  // namespace v30
