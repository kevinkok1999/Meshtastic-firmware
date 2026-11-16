#include "NanoMapPack.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace meshtastic::nanomap {
namespace {
constexpr uint8_t kMagic[4] = {'M', 'M', 'N', '1'};
constexpr uint16_t kVersion = 1;
constexpr size_t kMaxPackBytes = 3670016;
constexpr size_t kMaxMetadataBytes = 65536;
constexpr size_t kMaxNames = 65535;
constexpr size_t kMaxNameBytes = 256;
constexpr size_t kMaxFeatures = 1000000;

uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-(crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

bool validUtf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const uint8_t c = static_cast<uint8_t>(s[i++]);
    size_t continuation = 0;
    uint32_t codepoint = 0;
    if (c <= 0x7f) continue;
    if (c >= 0xc2 && c <= 0xdf) { continuation = 1; codepoint = c & 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { continuation = 2; codepoint = c & 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { continuation = 3; codepoint = c & 0x07; }
    else return false;
    for (size_t n = 0; n < continuation; ++n) {
      if (i >= s.size()) return false;
      const uint8_t next = static_cast<uint8_t>(s[i++]);
      if ((next & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation == 1 && codepoint < 0x80) ||
        (continuation == 2 && codepoint < 0x800) ||
        (continuation == 3 && codepoint < 0x10000) ||
        codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
  }
  return true;
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
    if (value.empty() || value.size() > kMaxNameBytes) return false;
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
  data_ = nullptr;
  size_ = 0;
  metadataSize_ = 0;
  status_ = PackStatus::TooSmall;
  if (data == nullptr || size < 14) return status_;
  if (size > kMaxPackBytes) return status_ = PackStatus::MetadataOutOfBounds;
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
  if (metadataSize > kMaxMetadataBytes || metadataSize > footer - 10) return status_ = PackStatus::MetadataOutOfBounds;
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
  const size_t digitsStart = cursor;
  while (cursor < meta.size() && meta[cursor] >= '0' && meta[cursor] <= '9') {
    if (featureCount_ > (kMaxFeatures / 10)) return status_ = PackStatus::BadMetadata;
    featureCount_ = featureCount_ * 10 + static_cast<size_t>(meta[cursor++] - '0');
    if (featureCount_ > kMaxFeatures) return status_ = PackStatus::BadMetadata;
  }
  if (cursor == digitsStart || (cursor >= meta.size() || (meta[cursor] != ',' && meta[cursor] != '}'))) return status_ = PackStatus::BadMetadata;
  if (!parseNames(meta, names_)) return status_ = PackStatus::BadMetadata;
  if (names_.size() > kMaxNames) return status_ = PackStatus::BadMetadata;
  for (const auto &name : names_) {
    if (name.empty() || name.size() > kMaxNameBytes || !validUtf8(name)) return status_ = PackStatus::BadMetadata;
    for (unsigned char ch : name) if (ch < 0x20) return status_ = PackStatus::BadMetadata;
  }
  data_ = data;
  size_ = size;
  metadataSize_ = metadataSize;
  status_ = PackStatus::Ok;
  return status_;
}


namespace {
constexpr size_t kMaxDecodedBody = 4 * 1024 * 1024;
constexpr size_t kMaxGeometryPoints = 4096;

bool readVaruint(const uint8_t *data, size_t size, size_t &pos, uint32_t &value) {
  value = 0;
  for (uint8_t shift = 0; shift < 35; shift += 7) {
    if (pos >= size) return false;
    const uint8_t byte = data[pos++];
    if (shift >= 32 && (byte & 0x7f) != 0) return false;
    value |= static_cast<uint32_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) return true;
  }
  return false;
}

int32_t unzigzag(uint32_t value) {
  return static_cast<int32_t>((value >> 1) ^ (~(value & 1) + 1));
}
}  // namespace

bool NanoMapPack::forEachFeature(
    double minLon, double minLat, double maxLon, double maxLat, uint8_t maxZoom,
    const std::function<bool(const FeatureRecord &)> &callback) const {
  if (status_ != PackStatus::Ok || !callback || minLon > maxLon || minLat > maxLat)
    return false;
  const size_t bodyOffset = 10 + metadataSize_;
  if (bodyOffset > size_ - 4) return false;
  const uint8_t *compressed = data_ + bodyOffset;
  const size_t compressedSize = size_ - 4 - bodyOffset;
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) return false;
  std::vector<uint8_t> decoded;
  decoded.reserve(std::min(kMaxDecodedBody, compressedSize * 4 + 1024));
  uint8_t output[1024];
  stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(compressed));
  stream.avail_in = static_cast<uInt>(std::min(compressedSize, static_cast<size_t>(std::numeric_limits<uInt>::max())));
  int result = Z_OK;
  while (result == Z_OK) {
    stream.next_out = output;
    stream.avail_out = sizeof(output);
    result = inflate(&stream, Z_NO_FLUSH);
    const size_t produced = sizeof(output) - stream.avail_out;
    if (decoded.size() + produced > kMaxDecodedBody) {
      inflateEnd(&stream);
      return false;
    }
    decoded.insert(decoded.end(), output, output + produced);
    if (stream.avail_in == 0 && result == Z_OK) {
      inflateEnd(&stream);
      return false;
    }
  }
  inflateEnd(&stream);
  if (result != Z_STREAM_END) return false;

  size_t pos = 0;
  for (size_t feature = 0; feature < featureCount_; ++feature) {
    uint32_t nameIndex = 0, province = 0, zoom = 0, geometryBytes = 0, pointCount = 0, kindBytes = 0;
    if (!readVaruint(decoded.data(), decoded.size(), pos, nameIndex) ||
        !readVaruint(decoded.data(), decoded.size(), pos, province) ||
        !readVaruint(decoded.data(), decoded.size(), pos, zoom) ||
        !readVaruint(decoded.data(), decoded.size(), pos, geometryBytes) ||
        nameIndex > kMaxNames || province >= 12 || zoom > 19 ||
        geometryBytes > decoded.size() - pos || geometryBytes == 0) return false;
    const size_t geometryEnd = pos + geometryBytes;
    if (!readVaruint(decoded.data(), geometryEnd, pos, pointCount) ||
        pointCount == 0 || pointCount > kMaxGeometryPoints) return false;
    FeatureRecord record;
    record.nameIndex = static_cast<uint16_t>(nameIndex);
    record.provinceIndex = static_cast<uint8_t>(province);
    record.zoomMin = static_cast<uint8_t>(zoom);
    int32_t previousLon = 0, previousLat = 0;
    bool intersects = false;
    for (uint32_t point = 0; point < pointCount; ++point) {
      uint32_t lonDelta = 0, latDelta = 0;
      if (!readVaruint(decoded.data(), geometryEnd, pos, lonDelta) ||
          !readVaruint(decoded.data(), geometryEnd, pos, latDelta)) return false;
      const int32_t lon = previousLon + unzigzag(lonDelta);
      const int32_t lat = previousLat + unzigzag(latDelta);
      previousLon = lon; previousLat = lat;
      record.geometry.push_back({lon, lat});
      const double dLon = static_cast<double>(lon) / 100000.0;
      const double dLat = static_cast<double>(lat) / 100000.0;
      intersects = intersects || (dLon >= minLon && dLon <= maxLon && dLat >= minLat && dLat <= maxLat);
    }
    if (pos != geometryEnd || !readVaruint(decoded.data(), decoded.size(), pos, kindBytes) ||
        kindBytes == 0 || kindBytes > 32 || kindBytes > decoded.size() - pos) return false;
    record.kind.assign(reinterpret_cast<const char *>(decoded.data() + pos), kindBytes);
    pos += kindBytes;
    if (record.nameIndex >= names_.size()) return false;
    if (record.zoomMin <= maxZoom && intersects && !callback(record)) return true;
  }
  return pos == decoded.size();
}

bool NanoMapPack::lookupPlace(const std::string &prefix,
                              std::vector<PlaceResult> &out,
                              size_t limit) const {
  out.clear();
  if (status_ != PackStatus::Ok || limit == 0 || limit > kMaxNames) return false;
  for (size_t i = 0; i < names_.size() && out.size() < limit; ++i) {
    if (names_[i].compare(0, prefix.size(), prefix) == 0)
      out.push_back({names_[i], static_cast<uint16_t>(i)});
  }
  return true;
}

}  // namespace meshtastic::nanomap
