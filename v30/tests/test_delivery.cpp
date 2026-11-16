#include "../overlay/src/helpers/esp32/V30Journal.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

using namespace v30;

struct MemoryStorage final : V30JournalStorage {
  V30JournalRecord records[kV30JournalMaxRecords]{};
  bool present[kV30JournalMaxRecords]{};
  size_t cap;
  bool failWrite = false;
  explicit MemoryStorage(size_t c = kV30JournalMaxRecords) : cap(c) {}
  size_t capacity() const override { return cap; }
  bool read(size_t i, V30JournalRecord& out) override {
    if (i >= cap || !present[i]) return false; out = records[i]; return true;
  }
  bool write(size_t i, const V30JournalRecord& in) override {
    if (failWrite || i >= cap) return false;
    records[i] = in; present[i] = true; return true;
  }
  bool clear(size_t i) override { if (i >= cap) return false; present[i] = false; return true; }
  bool flush() override { return !failWrite; }
  void corrupt(size_t i) { if (i < cap && present[i]) records[i].crc32 ^= 0x1u; }
};

static V30JournalBundle bundle(uint8_t n, uint8_t peer = 0xA0) {
  V30JournalBundle b{};
  b.critical = 1;
  memset(b.origin, n, sizeof(b.origin));
  memset(b.messageId, n + 1, sizeof(b.messageId));
  memset(b.peer, peer, sizeof(b.peer));
  b.payloadLen = 3;
  b.payload[0] = 'v'; b.payload[1] = '3'; b.payload[2] = n;
  return b;
}

static V30JournalReceipt receiptFor(const V30JournalBundle& b, uint8_t sender) {
  V30JournalReceipt r{};
  memset(r.sender, sender, sizeof(r.sender));
  memcpy(r.recipient, b.origin, sizeof(r.recipient));
  memcpy(r.origin, b.origin, sizeof(r.origin));
  memcpy(r.messageId, b.messageId, sizeof(r.messageId));
  return r;
}

int main() {
  MemoryStorage storage;
  V30Journal journal(storage);
  assert(journal.recover().validRecords == 0);

  auto first = bundle(1);
  assert(journal.admit(first) == JournalAdmission::Durable);
  assert(journal.receive(first) == JournalAckAction::DuplicateAck);
  assert(journal.count() == 1);

  assert(journal.markTxAttempt(first));
  assert(journal.markTxComplete(first)); // TX completion is not recipient delivery.
  auto wrong = receiptFor(first, 0xB0);
  assert(journal.applyReceipt(wrong) == JournalReceiptResult::Rejected);
  auto good = receiptFor(first, 0xA0);
  assert(journal.applyReceipt(good) == JournalReceiptResult::Applied);
  assert(journal.applyReceipt(good) == JournalReceiptResult::Duplicate);

  // Reboot recovery retains durable identity and does not duplicate UI admission.
  V30Journal rebooted(storage);
  auto rec = rebooted.recover();
  assert(rec.validRecords >= 1);
  assert(rebooted.receive(first) == JournalAckAction::DuplicateAck);

  // A torn/latest record is discarded while the earlier valid identity remains.
  storage.corrupt(storage.cap - 1);
  V30Journal recovered(storage);
  recovered.recover();
  assert(recovered.receive(first) == JournalAckAction::DuplicateAck);

  // Storage failure must never produce an ACK.
  MemoryStorage failing(4);
  failing.failWrite = true;
  V30Journal noAck(failing);
  auto incoming = bundle(9, 0xC0);
  assert(noAck.receive(incoming) == JournalAckAction::NoAck);
  assert(noAck.count() == 0);

  // Full bounded capacity rejects the 257th distinct message.
  MemoryStorage bounded(kV30JournalMaxRecords);
  V30Journal full(bounded);
  for (size_t i = 0; i < kV30JournalMaxRecords; ++i)
    assert(full.admit(bundle(static_cast<uint8_t>(i))) == JournalAdmission::Durable);
  assert(full.admit(bundle(0xFE)) == JournalAdmission::Durable); // duplicate, even when full
  V30JournalBundle extra = bundle(0xFF);
  memset(extra.origin, 0xEE, sizeof(extra.origin));
  memset(extra.messageId, 0xDD, sizeof(extra.messageId));
  assert(full.admit(extra) == JournalAdmission::Rejected);

  // Explicit reduced physical capacity is honoured.
  MemoryStorage small(2);
  V30Journal limited(small);
  assert(limited.capacity() == 2);
  assert(limited.admit(bundle(20)) == JournalAdmission::Durable);
  assert(limited.admit(bundle(21)) == JournalAdmission::Durable);
  assert(limited.receive(bundle(22)) == JournalAckAction::NoAck);

  puts("v30 journal tests passed");
  return 0;
}
