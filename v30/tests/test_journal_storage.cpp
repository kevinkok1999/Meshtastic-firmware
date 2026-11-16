#include "v30/overlay/src/helpers/esp32/V30FileJournalStorage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){
 const char* path=argc>1?argv[1]:"journal.bin"; std::remove(path); std::FILE* init=std::fopen(path,"wb"); assert(init); std::fseek(init,long(4*sizeof(v30::V30JournalRecord))-1,SEEK_SET); std::fputc(0,init); std::fclose(init);
 v30::V30FileJournalStorage storage(path,4); v30::V30Journal journal(storage);
 auto rec=journal.recover(); assert(rec.validRecords==0&&!rec.degraded);
 v30::V30JournalBundle b{}; b.critical=1;b.direction=v30::JournalDirection::Outbound;b.authenticated=1;b.payloadLen=3;
 b.origin[0]=1;b.messageId[0]=2;b.peer[0]=3;std::memcpy(b.payload,"abc",3);
 assert(journal.admit(b)==v30::JournalAdmission::Durable); assert(journal.commit(b)); assert(storage.flush());
 v30::V30JournalRecord saved{}; assert(storage.read(0,saved)); assert(!storage.write(0,saved)); v30::V30JournalRecord zero{}; assert(!storage.write(1,zero)); assert(storage.clear(3)); assert(storage.isEmpty(3));
 { v30::V30FileJournalStorage reopened(path,4); v30::V30Journal j2(reopened); auto r=j2.recover(); assert(r.validRecords==1&&!r.degraded); assert(j2.count()==1); }
 std::FILE*f=std::fopen(path,"r+b"); assert(f); std::fputc(0xff,f); std::fclose(f);
 { v30::V30FileJournalStorage corrupt(path,4); v30::V30Journal j3(corrupt); auto r=j3.recover(); assert(r.degraded); }
 std::FILE*partial=std::fopen(path,"wb"); assert(partial); std::fseek(partial,long(4*sizeof(v30::V30JournalRecord))-2,SEEK_SET); std::fputc(0,partial); std::fclose(partial); assert(!v30::V30FileJournalStorage(path,4).isEmpty(0));
 std::FILE*payload=std::fopen(path,"r+b"); assert(payload); std::fseek(payload,long(4*sizeof(v30::V30JournalRecord))-1,SEEK_SET); std::fputc(0,payload); std::fseek(payload,long(sizeof(v30::V30JournalRecord))-1,SEEK_SET); std::fputc(1,payload); std::fclose(payload); { v30::V30FileJournalStorage nonzero(path,4); v30::V30Journal j4(nonzero); auto r=j4.recover(); assert(r.degraded); }
 { v30::V30FileJournalStorage missing("/no/such/v30-journal-file",4); v30::V30JournalRecord r{}; assert(!missing.read(0,r)); assert(!missing.isEmpty(0)); assert(!missing.flush()); }
 { v30::V30FileJournalStorage overflow(path,size_t(-1)); assert(!overflow.isEmpty(0)); assert(!overflow.clear(0)); }
 return 0;
}
