#include "NanoMapImage.h"
#include "miniz/miniz_tinfl.h"
#include <cstring>
#include <memory>
#include <new>

namespace meshtastic::nanomap {
namespace {
constexpr size_t kHeaderSize = 10;
constexpr size_t kDecodedLimit = 4 * 1024 * 1024;
constexpr uint8_t kMagic[4] = {'M','M','N','1'};
uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc=0xffffffffu;
  while(n--) {
    crc ^= *p++;
    for(unsigned i=0;i<8;++i) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
  }
  return ~crc;
}
struct ProbeWorkspace { tinfl_decompressor state; uint8_t dictionary[TINFL_LZ_DICT_SIZE]; };
}
TileStorage classifyTileHeader(const uint8_t* prefix, size_t size) {
  if (!prefix || size < sizeof(kMagic)) return TileStorage::Unavailable;
  return std::memcmp(prefix,kMagic,sizeof(kMagic))==0 ? TileStorage::NanoMap : TileStorage::LegacyCandidate;
}
ImageStatus locateRawImage(const uint8_t* data, size_t size, size_t& packSize) {
  packSize=0;
  if(!data || size!=kRawTilePartitionBytes) return ImageStatus::WrongPartition;
  if(classifyTileHeader(data,size)!=TileStorage::NanoMap) return ImageStatus::NotNanoMap;
  if(data[4]!=1 || data[5]!=0) return ImageStatus::BadHeader;
  const size_t metaSize=le32(data+6);
  if(metaSize<2 || metaSize>65536 || metaSize>kRawPackLimit-kHeaderSize-4) return ImageStatus::BadHeader;
  const size_t start=kHeaderSize+metaSize;
  std::unique_ptr<ProbeWorkspace> workspace(new(std::nothrow) ProbeWorkspace{});
  if(!workspace) return ImageStatus::NoMemory;
  tinfl_init(&workspace->state);
  size_t inputOffset=start, outputOffset=0, total=0;
  // Zlib's end marker gives the exact payload length even if the final CRC
  // bytes equal the erased-flash pad value 0xff. Never guess by trimming pad.
  while(true) {
    size_t consumed=kRawPackLimit-4-inputOffset;
    size_t produced=TINFL_LZ_DICT_SIZE-outputOffset;
    auto& d=workspace->dictionary;
    const tinfl_status state=tinfl_decompress(&workspace->state,data+inputOffset,&consumed,
        d,d+outputOffset,&produced,TINFL_FLAG_PARSE_ZLIB_HEADER);
    inputOffset+=consumed;
    if(produced>kDecodedLimit-total) return ImageStatus::DecodedLimit;
    total+=produced;outputOffset=(outputOffset+produced)%TINFL_LZ_DICT_SIZE;
    if(state==TINFL_STATUS_DONE) break;
    if(state!=TINFL_STATUS_HAS_MORE_OUTPUT || (!consumed&&!produced)) return ImageStatus::BadCompression;
  }
  if(le32(data+inputOffset)!=crc32(data,inputOffset)) return ImageStatus::BadChecksum;
  const size_t exact=inputOffset+4;
  for(size_t i=exact;i<size;++i) if(data[i]!=0xff) return ImageStatus::BadPadding;
  packSize=exact;
  return ImageStatus::Ok;
}
}
