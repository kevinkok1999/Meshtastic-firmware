#pragma once
#include <stddef.h>
#include <stdint.h>

namespace v30 {

constexpr size_t kV30JournalOriginBytes = 32;
constexpr size_t kV30JournalMessageIdBytes = 16;
constexpr size_t kV30JournalPeerBytes = 32;
constexpr size_t kV30JournalPayloadBytes = 128;
constexpr size_t kV30JournalMaxRecords = 256;

enum class JournalState : uint8_t {
  Rejected = 0, Queued = 1, Durable = 2, TxAttempt = 3,
  TxComplete = 4, RecipientAck = 5, Expired = 6
};
enum class JournalAdmission : uint8_t { Rejected, Queued, Durable };
enum class JournalAckAction : uint8_t { NoAck, AckAfterDurable, DuplicateAck };
enum class JournalReceiptResult : uint8_t { Rejected, Applied, Duplicate };

struct V30JournalBundle {
  uint8_t critical;
  uint8_t origin[kV30JournalOriginBytes];
  uint8_t messageId[kV30JournalMessageIdBytes];
  uint8_t peer[kV30JournalPeerBytes];
  uint8_t payload[kV30JournalPayloadBytes];
  uint16_t payloadLen;
};

struct V30JournalReceipt {
  uint8_t sender[kV30JournalPeerBytes];
  uint8_t recipient[kV30JournalPeerBytes];
  uint8_t origin[kV30JournalOriginBytes];
  uint8_t messageId[kV30JournalMessageIdBytes];
};

struct V30JournalRecord {
  uint32_t magic;
  uint32_t generation;
  JournalState state;
  uint8_t reserved[3];
  V30JournalBundle bundle;
  uint32_t crc32;
};

class V30JournalStorage {
 public:
  virtual ~V30JournalStorage() = default;
  virtual size_t capacity() const = 0;
  virtual bool read(size_t index, V30JournalRecord& out) = 0;
  virtual bool write(size_t index, const V30JournalRecord& record) = 0;
  virtual bool clear(size_t index) = 0;
  virtual bool flush() = 0;
};

struct V30JournalRecovery {
  size_t validRecords;
  size_t discardedRecords;
  uint32_t nextGeneration;
};

class V30Journal {
 public:
  explicit V30Journal(V30JournalStorage& storage);
  V30JournalRecovery recover();
  JournalAdmission admit(const V30JournalBundle& bundle);
  bool commit(const V30JournalBundle& bundle);
  JournalAckAction receive(const V30JournalBundle& bundle);
  JournalReceiptResult applyReceipt(const V30JournalReceipt& receipt);
  bool markTxAttempt(const V30JournalBundle& bundle);
  bool markTxComplete(const V30JournalBundle& bundle);
  size_t count() const;
  size_t capacity() const;

 private:
  V30JournalStorage& storage_;
  V30JournalRecord records_[kV30JournalMaxRecords];
  bool valid_[kV30JournalMaxRecords];
  size_t capacity_;
  uint32_t nextGeneration_;
  bool loaded_;

  bool loadRecord(size_t index);
  bool persist(const V30JournalBundle& bundle, JournalState state);
  bool sameIdentity(const V30JournalBundle& a, const V30JournalBundle& b) const;
  bool sameReceipt(const V30JournalRecord& r, const V30JournalReceipt& receipt) const;
  bool peerMatches(const uint8_t* expected, const uint8_t* actual) const;
  int find(const V30JournalBundle& bundle) const;
  int freeIndex() const;
  static uint32_t checksum(const V30JournalRecord& record);
};

} // namespace v30
