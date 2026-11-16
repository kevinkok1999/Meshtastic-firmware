#include "V30Journal.h"
#include <string.h>

namespace {
constexpr uint32_t kMagic = 0x56334A52u; // V3JR
}

namespace v30 {

V30Journal::V30Journal(V30JournalStorage& storage)
    : storage_(storage), capacity_(storage.capacity() > kV30JournalMaxRecords
                                      ? kV30JournalMaxRecords
                                      : storage.capacity()),
      nextGeneration_(1), loaded_(false) {
  memset(records_, 0, sizeof(records_));
  memset(valid_, 0, sizeof(valid_));
}

uint32_t V30Journal::checksum(const V30JournalRecord& record) {
  // Integrity/torn-write detection only; this is not malicious tamper protection.
  V30JournalRecord copy = record;
  copy.crc32 = 0;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&copy);
  uint32_t crc = 2166136261u;
  for (size_t i = 0; i < sizeof(copy); ++i) {
    crc ^= bytes[i];
    crc *= 16777619u;
  }
  return crc;
}

bool V30Journal::loadRecord(size_t index) {
  if (index >= capacity_) return false;
  V30JournalRecord record{};
  if (!storage_.read(index, record) || record.magic != kMagic ||
      record.crc32 != checksum(record)) {
    valid_[index] = false;
    return false;
  }
  records_[index] = record;
  valid_[index] = true;
  return true;
}

V30JournalRecovery V30Journal::recover() {
  V30JournalRecovery result{0, 0, 1};
  nextGeneration_ = 1;
  loaded_ = true;
  for (size_t i = 0; i < capacity_; ++i) {
    if (loadRecord(i)) {
      ++result.validRecords;
      if (records_[i].generation >= nextGeneration_)
        nextGeneration_ = records_[i].generation + 1;
    } else {
      ++result.discardedRecords;
    }
  }
  result.nextGeneration = nextGeneration_;
  return result;
}

bool V30Journal::sameIdentity(const V30JournalBundle& a,
                              const V30JournalBundle& b) const {
  return memcmp(a.origin, b.origin, kV30JournalOriginBytes) == 0 &&
         memcmp(a.messageId, b.messageId, kV30JournalMessageIdBytes) == 0;
}

int V30Journal::find(const V30JournalBundle& bundle) const {
  int found = -1;
  uint32_t newest = 0;
  for (size_t i = 0; i < capacity_; ++i) {
    if (valid_[i] && sameIdentity(records_[i].bundle, bundle) &&
        (found < 0 || records_[i].generation >= newest)) {
      found = static_cast<int>(i);
      newest = records_[i].generation;
    }
  }
  return found;
}

int V30Journal::freeIndex() const {
  for (size_t i = 0; i < capacity_; ++i)
    if (!valid_[i]) return static_cast<int>(i);
  return -1;
}

bool V30Journal::persist(const V30JournalBundle& bundle, JournalState state) {
  const int index = freeIndex();
  if (index < 0) return false;
  V30JournalRecord record{};
  record.magic = kMagic;
  record.generation = nextGeneration_++;
  record.state = state;
  record.bundle = bundle;
  record.crc32 = checksum(record);

  // A record is admitted only after durable write, flush, and readback validation.
  if (!storage_.write(static_cast<size_t>(index), record) || !storage_.flush())
    return false;
  V30JournalRecord readback{};
  if (!storage_.read(static_cast<size_t>(index), readback) ||
      readback.magic != kMagic || readback.crc32 != checksum(readback) ||
      readback.generation != record.generation ||
      readback.state != state ||
      memcmp(&readback.bundle, &bundle, sizeof(bundle)) != 0) {
    storage_.clear(static_cast<size_t>(index));
    storage_.flush();
    return false;
  }
  records_[index] = readback;
  valid_[index] = true;
  return true;
}

JournalAdmission V30Journal::admit(const V30JournalBundle& bundle) {
  if (!loaded_) recover();
  if (bundle.payloadLen > kV30JournalPayloadBytes) return JournalAdmission::Rejected;
  const int existing = find(bundle);
  if (existing >= 0) return JournalAdmission::Durable;
  if (freeIndex() < 0) return JournalAdmission::Rejected;
  return persist(bundle, JournalState::Durable)
             ? JournalAdmission::Durable
             : JournalAdmission::Rejected;
}

bool V30Journal::commit(const V30JournalBundle& bundle) {
  return admit(bundle) != JournalAdmission::Rejected;
}

JournalAckAction V30Journal::receive(const V30JournalBundle& bundle) {
  const int existing = find(bundle);
  if (existing >= 0) return JournalAckAction::DuplicateAck;
  if (admit(bundle) == JournalAdmission::Durable)
    return JournalAckAction::AckAfterDurable;
  return JournalAckAction::NoAck;
}

bool V30Journal::peerMatches(const uint8_t* expected, const uint8_t* actual) const {
  return memcmp(expected, actual, kV30JournalPeerBytes) == 0;
}

bool V30Journal::sameReceipt(const V30JournalRecord& record,
                             const V30JournalReceipt& receipt) const {
  return memcmp(record.bundle.origin, receipt.origin, kV30JournalOriginBytes) == 0 &&
         memcmp(record.bundle.messageId, receipt.messageId,
                kV30JournalMessageIdBytes) == 0;
}

JournalReceiptResult V30Journal::applyReceipt(const V30JournalReceipt& receipt) {
  if (!loaded_) recover();
  for (size_t i = 0; i < capacity_; ++i) {
    if (!valid_[i] || !sameReceipt(records_[i], receipt)) continue;
    if (!peerMatches(records_[i].bundle.peer, receipt.sender) ||
        !peerMatches(records_[i].bundle.origin, receipt.recipient))
      return JournalReceiptResult::Rejected;
    if (records_[i].state == JournalState::RecipientAck)
      return JournalReceiptResult::Duplicate;
    V30JournalBundle bundle = records_[i].bundle;
    if (!persist(bundle, JournalState::RecipientAck))
      return JournalReceiptResult::Rejected;
    return JournalReceiptResult::Applied;
  }
  return JournalReceiptResult::Rejected;
}

bool V30Journal::markTxAttempt(const V30JournalBundle& bundle) {
  const int existing = find(bundle);
  if (existing < 0 || records_[existing].state == JournalState::RecipientAck)
    return false;
  return persist(bundle, JournalState::TxAttempt);
}

bool V30Journal::markTxComplete(const V30JournalBundle& bundle) {
  const int existing = find(bundle);
  if (existing < 0 || records_[existing].state == JournalState::RecipientAck)
    return false;
  return persist(bundle, JournalState::TxComplete);
}

size_t V30Journal::count() const {
  size_t result = 0;
  for (size_t i = 0; i < capacity_; ++i) result += valid_[i] ? 1 : 0;
  return result;
}

size_t V30Journal::capacity() const { return capacity_; }

} // namespace v30
