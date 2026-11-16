#include "../overlay/src/ui-touch/NanoMapPack.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <zlib.h>

using meshtastic::nanomap::NanoMapPack;
using meshtastic::nanomap::PackStatus;

static uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-(crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

static void write32(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
}


static std::vector<uint8_t> replaceBody(const std::vector<uint8_t> &pack,
                                        const std::vector<uint8_t> &body) {
  const size_t metaSize = static_cast<size_t>(pack[6]) |
      (static_cast<size_t>(pack[7]) << 8) | (static_cast<size_t>(pack[8]) << 16) |
      (static_cast<size_t>(pack[9]) << 24);
  const size_t prefixSize = 10 + metaSize;
  uLong bound = compressBound(static_cast<uLong>(body.size()));
  std::vector<uint8_t> compressed(bound);
  uLong compressedSize = bound;
  assert(compress2(compressed.data(), &compressedSize, body.data(),
                   static_cast<uLong>(body.size()), Z_BEST_COMPRESSION) == Z_OK);
  compressed.resize(compressedSize);
  std::vector<uint8_t> result(pack.begin(), pack.begin() + prefixSize);
  result.insert(result.end(), compressed.begin(), compressed.end());
  result.resize(result.size() + 4);
  write32(result, result.size() - 4, crc32(result.data(), result.size() - 4));
  return result;
}

static std::vector<uint8_t> inflateBody(const std::vector<uint8_t> &pack) {
  const size_t metaSize = static_cast<size_t>(pack[6]) |
      (static_cast<size_t>(pack[7]) << 8) | (static_cast<size_t>(pack[8]) << 16) |
      (static_cast<size_t>(pack[9]) << 24);
  const size_t offset = 10 + metaSize;
  std::vector<uint8_t> body(1024 * 1024);
  uLongf size = static_cast<uLongf>(body.size());
  assert(uncompress(body.data(), &size, pack.data() + offset,
                    static_cast<uLong>(pack.size() - 4 - offset)) == Z_OK);
  body.resize(size);
  return body;
}

static std::vector<uint8_t> readFile(const char *path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}


static void varuint(std::vector<uint8_t> &out, uint32_t value) {
  while (value >= 128) { out.push_back(static_cast<uint8_t>(value | 128)); value >>= 7; }
  out.push_back(static_cast<uint8_t>(value));
}
static void delta(std::vector<uint8_t> &out, int32_t value) {
  const uint32_t zig = value >= 0 ? static_cast<uint32_t>(value) * 2 :
      static_cast<uint32_t>(-static_cast<int64_t>(value) * 2 - 1);
  varuint(out, zig);
}
static std::vector<uint8_t> crossingBody(const std::string &kind = "primary",
                                        uint32_t province = 0, uint32_t zoom = 8,
                                        int32_t latitude = 5100000) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < 12; ++i) {
    varuint(out, 1); varuint(out, province); varuint(out, zoom);
    std::vector<uint8_t> geometry;
    varuint(geometry, 2);
    delta(geometry, 300000); delta(geometry, latitude);
    delta(geometry, 300000); delta(geometry, 0);
    varuint(out, geometry.size());
    out.insert(out.end(), geometry.begin(), geometry.end());
    varuint(out, kind.size());
    out.insert(out.end(), kind.begin(), kind.end());
  }
  return out;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  std::vector<uint8_t> bytes = readFile(argv[1]);
  NanoMapPack pack;
  assert(pack.open(bytes.data(), bytes.size()) == PackStatus::Ok);
  assert(pack.featureCount() == 12);
  assert(pack.nameCount() == 2);
  std::vector<meshtastic::nanomap::PlaceResult> places;
  assert(pack.lookupPlace("Do", places));
  assert(places.size() == 1 && places[0].name == "Dorp");
  assert(pack.lookupPlace("N", places));
  assert(places.size() == 1 && places[0].name == "N225");
  size_t selected = 0;
  assert(pack.forEachFeature(3.99, 50.99, 4.03, 51.03, 19, [&](const auto &record) { (void)record; ++selected; return true; }));
  assert(selected >= 1);


  // Both source endpoints are outside; output must be clipped to viewport edges.
  auto crossing = replaceBody(bytes, crossingBody());
  assert(pack.open(crossing.data(), crossing.size()) == PackStatus::Ok);
  size_t crossingCount = 0;
  assert(pack.forEachFeature(4.0, 50.9, 5.0, 51.1, 19, [&](const auto &record) {
    assert(record.geometry.size() == 2);
    assert(record.geometry[0].first == 400000 && record.geometry[1].first == 500000);
    ++crossingCount;
    return true;
  }));
  assert(crossingCount == 12);
  for (const auto &bad : {crossingBody("invalid"), crossingBody("primary", 12),
                          crossingBody("primary", 0, 2),
                          crossingBody("primary", 0, 8, 9100000)}) {
    auto invalid = replaceBody(bytes, bad);
    assert(pack.open(invalid.data(), invalid.size()) == PackStatus::Ok);
    assert(!pack.forEachFeature(3, 50, 7, 54, 19, [](const auto &) { return true; }));
  }
  // Canonical varints and trailing streams must be rejected with recomputed container CRC.
  auto badVarint = crossingBody();
  badVarint.erase(badVarint.begin());
  badVarint.insert(badVarint.begin(), {0x81, 0x80, 0x80, 0x80, 0x10});
  auto hostile = replaceBody(bytes, badVarint);
  assert(pack.open(hostile.data(), hostile.size()) == PackStatus::Ok);
  assert(!pack.forEachFeature(3, 50, 7, 54, 19, [](const auto &) { return true; }));
  // Records spanning >32 KiB exercise wraparound in tinfl's fixed dictionary.
  auto largeBody = crossingBody();
  largeBody.insert(largeBody.end(), 40000, 0);
  hostile = replaceBody(bytes, largeBody);
  assert(pack.open(hostile.data(), hostile.size()) == PackStatus::Ok);
  assert(!pack.forEachFeature(3, 50, 7, 54, 19, [](const auto &) { return true; }));

  std::vector<uint8_t> body = inflateBody(bytes);
  assert(!body.empty());
  body[0] = 0x80;  // unterminated varint
  auto malformed = replaceBody(bytes, body);
  assert(pack.open(malformed.data(), malformed.size()) == PackStatus::Ok);
  assert(!pack.forEachFeature(3.0, 50.0, 6.0, 53.0, 19, [&](const auto &record) { (void)record; return true; }));
  std::vector<uint8_t> bombBody(4 * 1024 * 1024 + 1, 0);
  auto bomb = replaceBody(bytes, bombBody);
  assert(pack.open(bomb.data(), bomb.size()) == PackStatus::Ok);
  assert(!pack.forEachFeature(3.0, 50.0, 6.0, 53.0, 19, [&](const auto &) { return true; }));

  std::vector<uint8_t> corrupt = bytes;
  corrupt.back() ^= 0x01;
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::BadChecksum);
  assert(pack.open(bytes.data(), 13) == PackStatus::TooSmall);
  corrupt = bytes;
  corrupt[0] = 'X';
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::BadMagic);
  corrupt = bytes;
  corrupt.resize(3670017);
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::MetadataOutOfBounds);
  corrupt = bytes;
  std::string metadata(reinterpret_cast<const char *>(corrupt.data() + 10), static_cast<size_t>(corrupt[6]) | (static_cast<size_t>(corrupt[7]) << 8) | (static_cast<size_t>(corrupt[8]) << 16) | (static_cast<size_t>(corrupt[9]) << 24));
  const size_t featureKey = metadata.find("\"features\":12");
  assert(featureKey != std::string::npos);
  corrupt[10 + featureKey + 11] = 'x';
  write32(corrupt, corrupt.size() - 4, crc32(corrupt.data(), corrupt.size() - 4));
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::BadMetadata);
  corrupt = bytes;
  corrupt[6] = 0xff; corrupt[7] = 0xff; corrupt[8] = 0xff; corrupt[9] = 0xff;
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::MetadataOutOfBounds);
  return 0;
}
