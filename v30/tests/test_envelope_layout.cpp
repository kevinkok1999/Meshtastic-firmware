#include "V30EnvelopeLayout.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
using namespace v30::envelope;
static std::array<uint8_t,184> frame(){
 std::array<uint8_t,184> b{};
 b[0]='M';b[1]='3';b[2]='0';b[3]='E';b[4]=1;
 b[5]=0x78;b[6]=0x56;b[7]=0x34;b[8]=0x12;b[9]=3;b[10]=0xCD;b[11]=0xAB;
 b[12]=2;b[13]=1;b[14]=4;b[15]=192;b[16]=19;
 b[17]=0x04;b[18]=0x03;b[19]=0x02;b[20]=0x01;
 b[21]=1;b[29]=0x08;b[30]=0x07;b[31]=0x06;b[32]=0x05;b[33]=0x04;b[34]=0x03;b[35]=0x02;b[36]=0x01;
 b[37]=1;b[69]=2;b[85]=0x42;b[104]=0x55;b[120]=0x66;
 return b;
}
int main(){
 View v{};
 auto b=frame();
 assert(parse(b.data(),184,v)==ParseResult::Ok);
 assert(v.attempt==0x12345678U && v.expiryMinutes==0x01020304U && v.reportedAgeMinutes==0xABCD);
 assert(v.sequence==UINT64_C(0x0102030405060708));
 assert(v.remaining==3 && v.kind==2 && v.priority==1 && v.maxCarry==4 && v.ttlHours==192 && v.bodySize==19);
 assert(v.logicalId==b.data()+21 && v.origin==b.data()+37 && v.recipientCommitment==b.data()+69);
 assert(v.ciphertext==b.data()+85 && v.tag==b.data()+104 && v.signature==b.data()+120);
 assert(parse(b.data(),183,v)==ParseResult::Invalid && v.wire==nullptr && v.ciphertext==nullptr);
 assert(parse(b.data(),185,v)==ParseResult::Invalid);
 auto wrong=b; wrong[0]='X'; assert(parse(wrong.data(),184,v)==ParseResult::Unrelated);
 assert(parse(b.data(),4,v)==ParseResult::Invalid); // our magic, truncated version
 for(auto i:{4,12,13,14,15,16}){
   auto bad=b; bad[i]=255;
   assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 }
 auto bad=b;bad[15]=0;assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad=b;bad[9]=5;assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad=b;for(size_t i=29;i<37;i++) bad[i]=0;assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad=b;for(size_t i=37;i<69;i++) bad[i]=0;assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad=b;for(size_t i=69;i<85;i++) bad[i]=0;assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad=b;bad[12]=6; assert(parse(bad.data(),184,v)==ParseResult::Invalid);
 bad[16]=18;assert(parse(bad.data(),183,v)==ParseResult::Ok && v.kind==6 && v.bodySize==18);
 // A mutable attempt/age is parsed but never interpreted as trusted freshness.
 bad=b;bad[5]=9;bad[10]=1;bad[11]=0;assert(parse(bad.data(),184,v)==ParseResult::Ok&&v.reportedAgeMinutes==1);
 assert(parse(nullptr,0,v)==ParseResult::Invalid);
 std::puts("V30 structural envelope tests passed; no cryptographic verification claimed");
}
