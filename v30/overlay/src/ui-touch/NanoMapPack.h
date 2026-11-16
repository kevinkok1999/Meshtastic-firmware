#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace meshtastic::nanomap {

enum class PackStatus : uint8_t {
  Ok = 0,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  MetadataOutOfBounds,
  BadChecksum,
  BadMetadata,
  GeometryCorrupt,
  GeometryTooLarge,
};

struct FeatureRecord {
  uint16_t nameIndex;
  uint8_t provinceIndex;
  uint8_t zoomMin;
  std::string kind;
  std::vector<std::pair<int32_t, int32_t>> geometry;
};

struct PlaceResult {
  std::string name;
  uint16_t dictionaryIndex;
};

class NanoMapPack {
 public:
  PackStatus open(const uint8_t *data, size_t size);
  PackStatus status() const { return status_; }
  size_t featureCount() const { return featureCount_; }
  size_t nameCount() const { return names_.size(); }
  bool lookupPlace(const std::string &prefix, std::vector<PlaceResult> &out,
                   size_t limit = 16) const;
  const std::vector<std::string> &names() const { return names_; }
  bool forEachFeature(double minLon, double minLat, double maxLon, double maxLat,
                      uint8_t maxZoom, const std::function<bool(const FeatureRecord &)> &callback) const;

 private:
  PackStatus status_ = PackStatus::TooSmall;
  size_t featureCount_ = 0;
  std::vector<std::string> names_;
};

}  // namespace meshtastic::nanomap
