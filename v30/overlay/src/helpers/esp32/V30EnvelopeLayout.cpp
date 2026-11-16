#include "V30EnvelopeLayout.h"
namespace v30 { namespace envelope {
namespace {
bool nonzero(const uint8_t* p,size_t n) {
    uint8_t any=0;
    for(size_t i=0;i<n;++i) any|=p[i];
    return any!=0;
}
uint32_t le32(const uint8_t* p) {
    uint32_t x=0;
    for(unsigned i=0;i<4;++i) x|=uint32_t(p[i])<<(8*i);
    return x;
}
uint64_t le64(const uint8_t* p) {
    uint64_t x=0;
    for(unsigned i=0;i<8;++i) x|=uint64_t(p[i])<<(8*i);
    return x;
}
}
static_assert(kHeader+kTag+kSignature+kMaxBody==kMaximum,"V30 RF payload limit");
ParseResult parse(const uint8_t* data,size_t size,View& out) {
    out=View{};
    if(!data || size>kMaximum) return ParseResult::Invalid;
    if(size<4 || data[0]!='M' || data[1]!='3' || data[2]!='0' || data[3]!='E')
        return ParseResult::Unrelated;
    if(size<kHeader+kTag+kSignature || data[4]!=1) return ParseResult::Invalid;
    const uint8_t kind=data[12], flags=data[13], maxCarry=data[14],
                  ttl=data[15], body=data[16];
    if(kind<1 || kind>6 || (flags&0xfcu)!=0 || maxCarry<1 || maxCarry>8 ||
       data[9]>maxCarry || ttl<1 || ttl>192 || body<1 || body>kMaxBody ||
       (kind==6 && body!=18) || size!=kHeader+size_t(body)+kTag+kSignature)
        return ParseResult::Invalid;
    const uint64_t sequence=le64(data+29);
    if(sequence==0 || !nonzero(data+37,32) || !nonzero(data+69,16))
        return ParseResult::Invalid;
    View v{};
    v.wire=data; v.size=size; v.attempt=le32(data+5);
    v.remaining=data[9];
    v.reportedAgeMinutes=uint16_t(data[10])|(uint16_t(data[11])<<8);
    v.kind=kind; v.priority=flags&3; v.maxCarry=maxCarry; v.ttlHours=ttl;
    v.bodySize=body; v.expiryMinutes=le32(data+17); v.sequence=sequence;
    v.logicalId=data+21; v.origin=data+37; v.recipientCommitment=data+69;
    v.ciphertext=data+kHeader; v.tag=v.ciphertext+body; v.signature=v.tag+kTag;
    out=v;
    return ParseResult::Ok;
}
}}
