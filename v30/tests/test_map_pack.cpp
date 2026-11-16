#include "../overlay/src/ui-touch/NanoMapPack.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

static std::vector<uint8_t> readFile(const char *path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
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
  const size_t featureKey = metadata.find("\\\"features\\\":12");
  assert(featureKey != std::string::npos);
  corrupt[10 + featureKey + 11] = 'x';
  write32(corrupt, corrupt.size() - 4, crc32(corrupt.data(), corrupt.size() - 4));
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::BadMetadata);
  corrupt = bytes;
  corrupt[6] = 0xff; corrupt[7] = 0xff; corrupt[8] = 0xff; corrupt[9] = 0xff;
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::MetadataOutOfBounds);
  return 0;
}
