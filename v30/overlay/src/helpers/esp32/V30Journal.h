#pragma once
#include <stddef.h>
#include <stdint.h>
namespace v30 {
constexpr size_t kV30JournalOriginBytes=32,kV30JournalMessageIdBytes=16,kV30JournalPeerBytes=32,kV30JournalPayloadBytes=128,kV30JournalMaxRecords=256;
enum class JournalState:uint8_t{Rejected=0,Queued=1,Durable=2,TxAttempt=3,TxComplete=4,RecipientAck=5,Expired=6};
enum class JournalDirection:uint8_t{Inbound=0,Outbound=1};
enum class JournalAdmission:uint8_t{Rejected,Queued,Durable};
enum class JournalAckAction:uint8_t{NoAck,AckAfterDurable,DuplicateAck};
enum class JournalReceiptResult:uint8_t{Rejected,Applied,Duplicate};
struct V30JournalBundle{uint8_t critical;JournalDirection direction;uint8_t authenticated;uint8_t origin[kV30JournalOriginBytes];uint8_t messageId[kV30JournalMessageIdBytes];uint8_t peer[kV30JournalPeerBytes];uint8_t payload[kV30JournalPayloadBytes];uint16_t payloadLen;};
struct V30JournalReceipt{uint8_t authenticated;uint8_t sender[kV30JournalPeerBytes];uint8_t recipient[kV30JournalPeerBytes];uint8_t origin[kV30JournalOriginBytes];uint8_t messageId[kV30JournalMessageIdBytes];};
struct V30JournalRecord{uint32_t magic;uint32_t generation;JournalState state;uint8_t reserved[3];V30JournalBundle bundle;uint32_t crc32;};
class V30JournalStorage{public:virtual~V30JournalStorage()=default;virtual size_t capacity()const=0;virtual bool read(size_t,V30JournalRecord&)=0;virtual bool write(size_t,const V30JournalRecord&)=0;virtual bool clear(size_t)=0;virtual bool flush()=0;};
struct V30JournalRecovery{size_t validRecords;size_t discardedRecords;uint32_t nextGeneration;};
class V30Journal{public:explicit V30Journal(V30JournalStorage&);V30JournalRecovery recover();JournalAdmission admit(const V30JournalBundle&);bool commit(const V30JournalBundle&);JournalAckAction receive(const V30JournalBundle&);JournalReceiptResult applyReceipt(const V30JournalReceipt&);bool markTxAttempt(const V30JournalBundle&);bool markTxComplete(const V30JournalBundle&);size_t count()const;size_t capacity()const;private:V30JournalStorage&storage_;V30JournalRecord records_[kV30JournalMaxRecords];bool valid_[kV30JournalMaxRecords];size_t capacity_;uint32_t nextGeneration_;bool loaded_;bool loadRecord(size_t);bool persist(const V30JournalBundle&,JournalState);bool sameIdentity(const V30JournalBundle&,const V30JournalBundle&)const;bool sameBundle(const V30JournalBundle&,const V30JournalBundle&)const;bool sameReceipt(const V30JournalRecord&,const V30JournalReceipt&)const;bool peerMatches(const uint8_t*,const uint8_t*)const;int find(const V30JournalBundle&)const;int freeIndex()const;static uint32_t checksum(const V30JournalRecord&);};
}