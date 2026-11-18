// Actual national pack smoke, not a hardware/display or completeness test.
#include "NanoMapPack.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <vector>

int main(int argc, char **argv) {
  using namespace meshtastic::nanomap;
  if (argc != 2) return 1;
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) return 2;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  NanoMapPack pack;
  if (pack.open(data.data(),data.size()) != PackStatus::Ok) return 3;
  std::array<size_t,12> seen{}, places{}, roads{};
  std::array<std::set<std::string>,12> layers;
  const std::set<std::string> expected{"boundary","place","water","motorway","trunk","primary","secondary","tertiary","local"};
  if (!pack.forEachFeature(3.2,50.7,7.3,53.7,19,[&](const FeatureRecord &r){
    if (r.provinceIndex >= 12 || r.geometry.empty()) return false;
    ++seen[r.provinceIndex];
    layers[r.provinceIndex].insert(r.kind);
    if (r.kind == "place") ++places[r.provinceIndex];
    if (r.kind == "motorway" || r.kind == "trunk" || r.kind == "primary" ||
        r.kind == "secondary" || r.kind == "tertiary" || r.kind == "local") ++roads[r.provinceIndex];
    return true;
  })) return 4;
  for (size_t i=0;i<12;++i) {
    if (!seen[i] || !places[i] || !roads[i] || layers[i] != expected) return 5;
    std::cout << i << ": geometry=" << seen[i] << " places=" << places[i] << " roads=" << roads[i] << '\n';
  }
  std::cout << "Native stream accepted " << pack.featureCount() << " packed features\n";
  return 0;
}
