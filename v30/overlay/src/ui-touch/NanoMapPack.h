#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace meshtastic::nanomap {

enum class PackStatus : uint8_t {
  Ok = 0,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  MetadataOutOfBounds,
  BadChecksum,
  BadMetadata,
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

 private:
  PackStatus status_ = PackStatus::TooSmall;
  size_t featureCount_ = 0;
  std::vector<std::string> names_;
};

}  // namespace meshtastic::nanomap
