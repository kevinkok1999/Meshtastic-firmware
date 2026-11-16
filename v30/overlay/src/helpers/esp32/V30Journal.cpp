#include "V30Journal.h"
#include <string.h>
namespace { constexpr uint32_t kMagic = 0x56334A52u; }
namespace v30 {
V30Journal::V30Journal(V30JournalStorage& storage)
    : storage_(storage), capacity_(storage.capacity() > kV30JournalMaxRecords ? kV30JournalMaxRecords : storage.capacity()), nextGeneration_(1), loaded_(false) { memset(records_,0,sizeof(records_)); memset(valid_,0,sizeof(valid_)); }
uint32_t V30Journal::checksum(const V30JournalRecord& record) {
  // Integrity/torn-write detection only; this is not malicious tamper protection.
  V30JournalRecord copy=record; copy.crc32=0; const uint8_t* p=reinterpret_cast<const uint8_t*>(&copy); uint32_t crc=2166136261u; for(size_t i=0;i<sizeof(copy);++i){crc^=p[i];crc*=16777619u;} return crc;
}
bool V30Journal::loadRecord(size_t i){if(i>=capacity_)return false; V30JournalRecord r{}; if(!storage_.read(i,r)||r.magic!=kMagic||r.crc32!=checksum(r)){valid_[i]=false;return false;} records_[i]=r;valid_[i]=true;return true;}
V30JournalRecovery V30Journal::recover(){V30JournalRecovery out{0,0,1};nextGeneration_=1;loaded_=true;for(size_t i=0;i<capacity_;++i){if(loadRecord(i)){++out.validRecords;if(records_[i].generation>=nextGeneration_)nextGeneration_=records_[i].generation+1;}else ++out.discardedRecords;}out.nextGeneration=nextGeneration_;return out;}
bool V30Journal::sameIdentity(const V30JournalBundle&a,const V30JournalBundle&b)const{return memcmp(a.origin,b.origin,kV30JournalOriginBytes)==0&&memcmp(a.messageId,b.messageId,kV30JournalMessageIdBytes)==0;}
int V30Journal::find(const V30JournalBundle& b)const{int f=-1;uint32_t newest=0;for(size_t i=0;i<capacity_;++i)if(valid_[i]&&sameIdentity(records_[i].bundle,b)&&(f<0||records_[i].generation>=newest)){f=(int)i;newest=records_[i].generation;}return f;}
int V30Journal::freeIndex()const{for(size_t i=0;i<capacity_;++i)if(!valid_[i])return(int)i;return-1;}
bool V30Journal::persist(const V30JournalBundle& b,JournalState state){
  const int existing=find(b); const int free=freeIndex(); const bool replace=existing>=0&&free<0; const int index=replace?existing:free; if(index<0)return false;
  V30JournalRecord r{};r.magic=kMagic;r.generation=nextGeneration_++;r.state=state;r.bundle=b;r.crc32=checksum(r);
  if(!storage_.write((size_t)index,r)||!storage_.flush())return false;
  V30JournalRecord rb{}; if(!storage_.read((size_t)index,rb)||rb.magic!=kMagic||rb.crc32!=checksum(rb)||rb.generation!=r.generation||rb.state!=state||memcmp(&rb.bundle,&b,sizeof(b))!=0){if(!replace){storage_.clear((size_t)index);storage_.flush();}return false;}
  records_[index]=rb;valid_[index]=true;return true;
}
JournalAdmission V30Journal::admit(const V30JournalBundle& b){if(!loaded_)recover();if(b.payloadLen>kV30JournalPayloadBytes)return JournalAdmission::Rejected;if(find(b)>=0)return JournalAdmission::Durable;if(freeIndex()<0)return JournalAdmission::Rejected;return persist(b,JournalState::Durable)?JournalAdmission::Durable:JournalAdmission::Rejected;}
bool V30Journal::commit(const V30JournalBundle& b){return admit(b)!=JournalAdmission::Rejected;}
JournalAckAction V30Journal::receive(const V30JournalBundle& b){if(find(b)>=0)return JournalAckAction::DuplicateAck;return admit(b)==JournalAdmission::Durable?JournalAckAction::AckAfterDurable:JournalAckAction::NoAck;}
bool V30Journal::peerMatches(const uint8_t*e,const uint8_t*a)const{return memcmp(e,a,kV30JournalPeerBytes)==0;}
bool V30Journal::sameReceipt(const V30JournalRecord&r,const V30JournalReceipt&x)const{return memcmp(r.bundle.origin,x.origin,kV30JournalOriginBytes)==0&&memcmp(r.bundle.messageId,x.messageId,kV30JournalMessageIdBytes)==0;}
JournalReceiptResult V30Journal::applyReceipt(const V30JournalReceipt& x){if(!loaded_)recover();for(size_t i=0;i<capacity_;++i)if(valid_[i]&&sameReceipt(records_[i],x)){if(!peerMatches(records_[i].bundle.peer,x.sender)||!peerMatches(records_[i].bundle.origin,x.recipient))return JournalReceiptResult::Rejected;if(records_[i].state==JournalState::RecipientAck)return JournalReceiptResult::Duplicate;V30JournalBundle b=records_[i].bundle;return persist(b,JournalState::RecipientAck)?JournalReceiptResult::Applied:JournalReceiptResult::Rejected;}return JournalReceiptResult::Rejected;}
bool V30Journal::markTxAttempt(const V30JournalBundle& b){int i=find(b);if(i<0||records_[i].state==JournalState::RecipientAck)return false;return persist(b,JournalState::TxAttempt);}
bool V30Journal::markTxComplete(const V30JournalBundle& b){int i=find(b);if(i<0||records_[i].state==JournalState::RecipientAck)return false;return persist(b,JournalState::TxComplete);}
size_t V30Journal::count()const{size_t n=0;for(size_t i=0;i<capacity_;++i)n+=valid_[i]?1:0;return n;}
size_t V30Journal::capacity()const{return capacity_;}
} // namespace v30
