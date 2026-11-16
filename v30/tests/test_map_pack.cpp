#include "../overlay/src/ui-touch/NanoMapPack.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using meshtastic::nanomap::NanoMapPack;
using meshtastic::nanomap::PackStatus;

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
  corrupt[6] = 0xff; corrupt[7] = 0xff; corrupt[8] = 0xff; corrupt[9] = 0xff;
  assert(pack.open(corrupt.data(), corrupt.size()) == PackStatus::MetadataOutOfBounds);
  return 0;
}
