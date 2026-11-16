#include "V30FileJournalStorage.h"
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <climits>
namespace v30 {
V30FileJournalStorage::V30FileJournalStorage(const char* p,size_t n):path_(p?p:""),slots_(n),file_(nullptr){}
V30FileJournalStorage::~V30FileJournalStorage(){if(file_)std::fclose(file_);}
bool V30FileJournalStorage::ensureOpen() const {
  if(slots_==0 || slots_>kV30JournalMaxRecords || slots_>size_t(LONG_MAX)/sizeof(V30JournalRecord)) return false;
  if(file_) return std::fseek(file_,0,SEEK_END)==0 && std::ftell(file_)==long(slots_*sizeof(V30JournalRecord));
  auto* self=const_cast<V30FileJournalStorage*>(this);
  self->file_=std::fopen(path_.c_str(),"r+b");
  if(!self->file_) return false; // missing is not formatted implicitly
  if(std::fseek(self->file_,0,SEEK_END)!=0 || std::ftell(self->file_) != long(self->slots_*sizeof(V30JournalRecord))) return false;
  return std::fseek(self->file_,0,SEEK_SET)==0;
}
uint32_t V30FileJournalStorage::checksum(const V30JournalRecord&r){V30JournalRecord c=r;c.crc32=0;const auto*p=reinterpret_cast<const uint8_t*>(&c);uint32_t x=2166136261u;for(size_t i=0;i<sizeof(c);++i){x^=p[i];x*=16777619u;}return x;}
bool V30FileJournalStorage::valid(const V30JournalRecord&r){return r.magic==0x56334A52u && r.crc32==checksum(r);}
bool V30FileJournalStorage::read(size_t i,V30JournalRecord&r){if(i>=slots_||!ensureOpen())return false;if(std::fseek(file_,long(i*sizeof(r)),SEEK_SET)!=0||std::fread(&r,1,sizeof(r),file_)!=sizeof(r))return false;return valid(r);}
bool V30FileJournalStorage::isEmpty(size_t i)const{V30JournalRecord r{};if(i>=slots_||!ensureOpen())return false;if(std::fseek(file_,long(i*sizeof(r)),SEEK_SET)!=0||std::fread(&r,1,sizeof(r),file_)!=sizeof(r))return false;const auto*p=reinterpret_cast<const uint8_t*>(&r);for(size_t n=0;n<sizeof(r);++n)if(p[n]!=0)return false;return true;}
bool V30FileJournalStorage::write(size_t i,const V30JournalRecord&r){if(i>=slots_||!ensureOpen()||!valid(r)||!isEmpty(i))return false;if(std::fseek(file_,long(i*sizeof(r)),SEEK_SET)!=0||std::fwrite(&r,1,sizeof(r),file_)!=sizeof(r))return false;return flush();}
bool V30FileJournalStorage::clear(size_t i){if(i>=slots_||!ensureOpen())return false;V30JournalRecord z{};if(std::fseek(file_,long(i*sizeof(z)),SEEK_SET)!=0||std::fwrite(&z,1,sizeof(z),file_)!=sizeof(z))return false;return flush();}
bool V30FileJournalStorage::flush(){return file_&&std::fflush(file_)==0&&::fsync(fileno(file_))==0;}
}
