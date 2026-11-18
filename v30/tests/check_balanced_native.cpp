#include "NanoMapPack.h"
#include "NanoMapImage.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>

int main(int argc, char **argv) {
  using namespace meshtastic::nanomap;
  if (argc != 2) return 1;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 2;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
  size_t packSize = 0;
  if (locateRawImage(data.data(), data.size(), packSize) != ImageStatus::Ok) return 3;
  NanoMapPack pack;
  if (pack.open(data.data(),packSize) != PackStatus::Ok) return 4;
  std::array<std::map<std::string,size_t>,12> seen;
  std::array<size_t,12> namedPlaces{};
  if (!pack.forEachFeature(-180,-90,180,90,19,[&](const FeatureRecord &r) {
    if (r.provinceIndex >= 12 || r.geometry.empty()) return false;
    ++seen[r.provinceIndex][r.kind];
    if (r.kind == "place" && r.nameIndex) ++namedPlaces[r.provinceIndex];
    return true;
  })) return 5;
  for (size_t i=0;i<12;++i) {
    for (const auto *k:{"boundary","motorway","trunk","primary","secondary","tertiary","local","water","place"}) {
      if (!seen[i][k]) return 6;
      std::cout << i << ' ' << k << " native_segments=" << seen[i][k] << '\n';
    }
    if (!namedPlaces[i]) return 7;
    std::cout << i << " named_places=" << namedPlaces[i] << '\n';
  }
  std::cout << "Native image and stream accepted: " << pack.featureCount()
            << " packed features, " << pack.nameCount() << " names in " << packSize << " bytes\n";
  return 0;
}
