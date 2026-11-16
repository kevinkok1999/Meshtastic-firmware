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
 { v30::V30FileJournalStorage reopened(path,4); v30::V30Journal j2(reopened); auto r=j2.recover(); assert(r.validRecords==1&&!r.degraded); assert(j2.count()==1); }
 std::FILE*f=std::fopen(path,"r+b"); assert(f); std::fputc(0xff,f); std::fclose(f);
 { v30::V30FileJournalStorage corrupt(path,4); v30::V30Journal j3(corrupt); auto r=j3.recover(); assert(r.degraded); }
 return 0;
}
