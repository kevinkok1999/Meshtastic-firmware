#include "NanoMapImage.h"
#include "NanoMapPack.h"
#include <zlib.h>
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace meshtastic::nanomap;

static void le32(std::vector<uint8_t>& v, uint32_t n) {
  for (unsigned i=0;i<4;++i) v.push_back(static_cast<uint8_t>(n>>(8*i)));
}
static std::vector<uint8_t> image(size_t bodySize, size_t& length) {
  std::vector<uint8_t> body(bodySize, 0x21);
  uLongf compressedSize=compressBound(body.size());
  std::vector<uint8_t> compressed(compressedSize);
  assert(compress2(compressed.data(), &compressedSize, body.data(), body.size(), 9)==Z_OK);
  std::vector<uint8_t> v={'M','M','N','1',1,0};
  le32(v,2); v.push_back('{');v.push_back('}');
  v.insert(v.end(),compressed.begin(),compressed.begin()+compressedSize);
  le32(v,crc32(0,v.data(),v.size()));
  length=v.size();v.resize(kRawTilePartitionBytes,0xff);return v;
}
struct LegacyFs {
  bool open=true, formatted=false, ended=false, called=false;
  size_t bytes=0x4c0000;
  bool begin(bool formatOnFail, const char*, int, const char*) { called=true; formatted=formatOnFail; return open; }
  size_t totalBytes() const { return bytes; }
  void end() { ended=true; }
};
int main(int argc,char** argv){
  size_t len=0;auto v=image(100000,len); size_t out=0;
  assert(locateRawImage(v.data(),v.size(),out)==ImageStatus::Ok && out==len);
  v[len-1]^=1;assert(locateRawImage(v.data(),v.size(),out)==ImageStatus::BadChecksum && out==0);
  v[len-1]^=1;v[len+2]=0;assert(locateRawImage(v.data(),v.size(),out)==ImageStatus::BadPadding);
  v[len+2]=0xff;assert(locateRawImage(v.data(),v.size()-1,out)==ImageStatus::WrongPartition);
  auto bomb=image(4*1024*1024+1,len);assert(locateRawImage(bomb.data(),bomb.size(),out)==ImageStatus::DecodedLimit);
  auto bad=image(200,len);bad[6]=0xff;bad[7]=0xff;bad[8]=0xff;bad[9]=0xff;
  assert(locateRawImage(bad.data(),bad.size(),out)==ImageStatus::BadHeader);
  bad=image(200,len);bad[0]=0;assert(locateRawImage(bad.data(),bad.size(),out)==ImageStatus::NotNanoMap);
  LegacyFs good;assert(mountLegacyTilesWithoutFormat(good,TileStorage::LegacyCandidate));assert(good.called&&!good.formatted);
  LegacyFs raw;assert(!mountLegacyTilesWithoutFormat(raw,TileStorage::NanoMap));assert(!raw.called);
  LegacyFs missing;assert(!mountLegacyTilesWithoutFormat(missing,TileStorage::Unavailable));assert(!missing.called);
  LegacyFs corrupt;corrupt.bytes=0;assert(!mountLegacyTilesWithoutFormat(corrupt,TileStorage::LegacyCandidate));assert(corrupt.ended&&!corrupt.formatted);
  LegacyFs notMountable;notMountable.open=false;assert(!mountLegacyTilesWithoutFormat(notMountable,TileStorage::LegacyCandidate));assert(!notMountable.formatted);
  assert(classifyTileHeader(nullptr,0)==TileStorage::Unavailable);
  assert(classifyTileHeader(v.data(),4)==TileStorage::NanoMap);
  const uint8_t blank[]={0xff,0xff,0xff,0xff};assert(classifyTileHeader(blank,4)==TileStorage::LegacyCandidate);
  if(argc==2) {
    std::ifstream f(argv[1],std::ios::binary);assert(f.good());
    std::vector<uint8_t> real((std::istreambuf_iterator<char>(f)),{});
    const size_t realSize=real.size();assert(realSize>0&&realSize<kRawPackLimit);
    real.resize(kRawTilePartitionBytes,0xff);
    assert(locateRawImage(real.data(),real.size(),out)==ImageStatus::Ok&&out==realSize);
    NanoMapPack reader;assert(reader.open(real.data(),out)==PackStatus::Ok);
    assert(reader.featureCount()==12);
    size_t segments=0;
    assert(reader.forEachFeature(3,50,8,54,19,[&](const FeatureRecord&){++segments;return true;}));
    assert(segments>0);
  }
  std::puts("NanoMap raw image boundaries and no-format mount PASS");
}
