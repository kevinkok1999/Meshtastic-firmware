#include "NanoMapPack.h"
#include <algorithm>
#include <cstring>

namespace meshtastic::nanomap {
namespace {
constexpr uint8_t kMagic[4] = {'M', 'M', 'N', '1'};
constexpr uint16_t kVersion = 1;

uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-(crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

bool quoted(const std::string &s, size_t &pos, std::string &value) {
  if (pos >= s.size() || s[pos] != '"') return false;
  ++pos;
  value.clear();
  while (pos < s.size()) {
    char c = s[pos++];
    if (c == '"') return true;
    if (c == '\\' && pos < s.size()) {
      char escaped = s[pos++];
      if (escaped != '"' && escaped != '\\' && escaped != '/') return false;
      value.push_back(escaped);
    } else {
      value.push_back(c);
    }
  }
  return false;
}

bool parseNames(const std::string &meta, std::vector<std::string> &names) {
  const std::string key = "\"names\":[";
  size_t pos = meta.find(key);
  if (pos == std::string::npos) return false;
  pos += key.size();
  if (pos >= meta.size()) return false;
  if (meta[pos] == ']') return true;
  while (true) {
    std::string value;
    if (!quoted(meta, pos, value)) return false;
    names.push_back(value);
    if (pos >= meta.size()) return false;
    if (meta[pos] == ']') return true;
    if (meta[pos++] != ',') return false;
  }
}
}  // namespace

PackStatus NanoMapPack::open(const uint8_t *data, size_t size) {
  names_.clear();
  featureCount_ = 0;
  status_ = PackStatus::TooSmall;
  if (data == nullptr || size < 14) return status_;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
    return status_ = PackStatus::BadMagic;
  const uint16_t version = static_cast<uint16_t>(data[4]) |
                           (static_cast<uint16_t>(data[5]) << 8);
  if (version != kVersion) return status_ = PackStatus::UnsupportedVersion;
  const uint32_t metadataSize = static_cast<uint32_t>(data[6]) |
      (static_cast<uint32_t>(data[7]) << 8) |
      (static_cast<uint32_t>(data[8]) << 16) |
      (static_cast<uint32_t>(data[9]) << 24);
  const size_t footer = size - 4;
  if (metadataSize > footer - 10) return status_ = PackStatus::MetadataOutOfBounds;
  const uint32_t expected = static_cast<uint32_t>(data[footer]) |
      (static_cast<uint32_t>(data[footer + 1]) << 8) |
      (static_cast<uint32_t>(data[footer + 2]) << 16) |
      (static_cast<uint32_t>(data[footer + 3]) << 24);
  if (crc32(data, footer) != expected) return status_ = PackStatus::BadChecksum;
  std::string meta(reinterpret_cast<const char *>(data + 10), metadataSize);
  if (meta.empty() || meta.front() != '{' || meta.back() != '}')
    return status_ = PackStatus::BadMetadata;
  const std::string featureKey = "\"features\":";
  const size_t featurePos = meta.find(featureKey);
  if (featurePos == std::string::npos) return status_ = PackStatus::BadMetadata;
  size_t cursor = featurePos + featureKey.size();
  while (cursor < meta.size() && meta[cursor] >= '0' && meta[cursor] <= '9') {
    featureCount_ = featureCount_ * 10 + static_cast<size_t>(meta[cursor++] - '0');
  }
  if (!parseNames(meta, names_)) return status_ = PackStatus::BadMetadata;
  status_ = PackStatus::Ok;
  return status_;
}

bool NanoMapPack::lookupPlace(const std::string &prefix,
                              std::vector<PlaceResult> &out,
                              size_t limit) const {
  out.clear();
  if (status_ != PackStatus::Ok || limit == 0) return false;
  for (size_t i = 0; i < names_.size() && out.size() < limit; ++i) {
    if (names_[i].compare(0, prefix.size(), prefix) == 0)
      out.push_back({names_[i], static_cast<uint16_t>(i)});
  }
  return true;
}

}  // namespace meshtastic::nanomap
