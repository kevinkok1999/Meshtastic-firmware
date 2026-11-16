#include "../overlay/src/helpers/esp32/V30Journal.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
using namespace v30;
struct MemoryStorage final : V30JournalStorage {
  V30JournalRecord records[kV30JournalMaxRecords]{};
  bool present[kV30JournalMaxRecords]{};
  size_t cap; bool failWrite=false;
  explicit MemoryStorage(size_t c=kV30JournalMaxRecords):cap(c){}
  size_t capacity()const override{return cap;}
  bool read(size_t i,V30JournalRecord&out)override{
    if(i>=cap||!present[i]) return false;
    out=records[i]; return true;
  }
  bool write(size_t i,const V30JournalRecord&in)override{if(failWrite||i>=cap)return false;records[i]=in;present[i]=true;return true;}
  bool clear(size_t i)override{if(i>=cap)return false;present[i]=false;return true;}
  bool flush()override{return !failWrite;}
  void corrupt(size_t i){if(i<cap&&present[i])records[i].crc32^=1u;}
};
static V30JournalBundle bundle(uint8_t n,uint8_t peer=0xA0){V30JournalBundle b{};b.critical=1;memset(b.origin,n,32);memset(b.messageId,n+1,16);memset(b.peer,peer,32);b.payloadLen=3;b.payload[0]='v';b.payload[1]='3';b.payload[2]=n;return b;}
static V30JournalReceipt receiptFor(const V30JournalBundle&b,uint8_t sender){V30JournalReceipt r{};memset(r.sender,sender,32);memcpy(r.recipient,b.origin,32);memcpy(r.origin,b.origin,32);memcpy(r.messageId,b.messageId,16);return r;}
int main(){
  MemoryStorage storage; V30Journal journal(storage); assert(journal.recover().validRecords==0);
  auto first=bundle(1); assert(journal.admit(first)==JournalAdmission::Durable); assert(journal.receive(first)==JournalAckAction::DuplicateAck); assert(journal.count()==1);
  assert(journal.markTxAttempt(first)); assert(journal.markTxComplete(first));
  auto wrong=receiptFor(first,0xB0); assert(journal.applyReceipt(wrong)==JournalReceiptResult::Rejected);
  auto good=receiptFor(first,0xA0); assert(journal.applyReceipt(good)==JournalReceiptResult::Applied); assert(journal.applyReceipt(good)==JournalReceiptResult::Duplicate);
  V30Journal rebooted(storage); assert(rebooted.recover().validRecords>=1); assert(rebooted.receive(first)==JournalAckAction::DuplicateAck);
  storage.corrupt(3); V30Journal recovered(storage); recovered.recover(); assert(recovered.receive(first)==JournalAckAction::DuplicateAck);
  MemoryStorage failing(4); failing.failWrite=true; V30Journal noAck(failing); auto incoming=bundle(9,0xC0); assert(noAck.receive(incoming)==JournalAckAction::NoAck); assert(noAck.count()==0);
  MemoryStorage bounded(kV30JournalMaxRecords); V30Journal full(bounded);
  for(size_t i=0;i<kV30JournalMaxRecords;++i) assert(full.admit(bundle((uint8_t)i))==JournalAdmission::Durable);
  assert(full.admit(bundle(0xFE))==JournalAdmission::Durable);
  V30JournalBundle extra=bundle(0xFF);memset(extra.origin,0xEE,32);memset(extra.messageId,0xDD,16);assert(full.admit(extra)==JournalAdmission::Rejected);
  MemoryStorage small(2);V30Journal limited(small);assert(limited.capacity()==2);assert(limited.admit(bundle(20))==JournalAdmission::Durable);assert(limited.admit(bundle(21))==JournalAdmission::Durable);assert(limited.receive(bundle(22))==JournalAckAction::NoAck);
  puts("v30 journal tests passed"); return 0;
}