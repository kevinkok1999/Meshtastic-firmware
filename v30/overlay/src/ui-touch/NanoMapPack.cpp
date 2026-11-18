#include "NanoMapPack.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include "miniz/miniz_tinfl.h"
#include <cmath>
#include <memory>
#include <new>

namespace meshtastic::nanomap {
namespace {
constexpr uint8_t kMagic[4] = {'M', 'M', 'N', '1'};
constexpr uint16_t kVersion = 1;
constexpr size_t kMaxPackBytes = 3670016;
constexpr size_t kMaxMetadataBytes = 65536;
constexpr size_t kMaxNames = 65535;
constexpr size_t kMaxNameBytes = 256;
constexpr size_t kMaxFeatures = 1000000;

bool crc32(const uint8_t *data, size_t size, MapProgress progress, uint32_t& result) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    if ((i & 0x3fffu)==0 && !progress.proceed()) return false;
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-(crc & 1u)));
  }
  result = crc ^ 0xffffffffu;return true;
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

PackStatus NanoMapPack::open(const uint8_t *data, size_t size, MapProgress progress) {
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
  uint32_t actual=0;
  if (!crc32(data,footer,progress,actual)) return status_=PackStatus::Cancelled;
  if (actual != expected) return status_ = PackStatus::BadChecksum;
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
  if (featureCount_ == 0) return status_ = PackStatus::BadMetadata;
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
// Bounded 32 KiB dictionary plus tinfl state. No full decoded body is retained.
class InflateCursor {
 public:
  InflateCursor(const uint8_t *data, size_t size) : input_(data), inputSize_(size) {
    tinfl_init(&state_);
  }
  bool byte(uint8_t &value) {
    if (read_ == available_ && !refill()) return false;
    if (total_ >= kMaxDecodedBody) return false;
    value = dictionary_[base_ + read_++];
    ++total_;
    return true;
  }
  size_t position() const { return total_; }
  bool finish() {
    uint8_t extra = 0;
    if (byte(extra)) return false;
    return !failed_ && done_ && inputOffset_ == inputSize_ && read_ == available_;
  }
 private:
  bool refill() {
    if (failed_ || done_) return false;
    size_t consumed = inputSize_ - inputOffset_;
    size_t produced = TINFL_LZ_DICT_SIZE - writeOffset_;
    base_ = writeOffset_;
    const tinfl_status status = tinfl_decompress(
        &state_, input_ + inputOffset_, &consumed, dictionary_,
        dictionary_ + writeOffset_, &produced, TINFL_FLAG_PARSE_ZLIB_HEADER);
    inputOffset_ += consumed;
    available_ = produced;
    read_ = 0;
    writeOffset_ = (writeOffset_ + produced) % TINFL_LZ_DICT_SIZE;
    done_ = status == TINFL_STATUS_DONE;
    failed_ = status < TINFL_STATUS_DONE ||
        (!done_ && status != TINFL_STATUS_HAS_MORE_OUTPUT) ||
        total_ + produced > kMaxDecodedBody;
    return !failed_ && produced != 0;
  }
  tinfl_decompressor state_{};
  uint8_t dictionary_[TINFL_LZ_DICT_SIZE]{};
  const uint8_t *input_;
  size_t inputSize_, inputOffset_ = 0, writeOffset_ = 0;
  size_t base_ = 0, read_ = 0, available_ = 0, total_ = 0;
  bool failed_ = false, done_ = false;
};

bool varuint(InflateCursor &cursor, uint32_t &value, size_t end = kMaxDecodedBody) {
  value = 0;
  for (unsigned i = 0; i < 5; ++i) {
    uint8_t byte = 0;
    if (cursor.position() >= end || !cursor.byte(byte)) return false;
    if (i == 4 && (byte & 0xf0)) return false;
    value |= static_cast<uint32_t>(byte & 0x7f) << (i * 7);
    if (!(byte & 0x80)) return i == 0 || (byte & 0x7f) != 0; // canonical encoding
  }
  return false;
}
int64_t unzigzag(uint32_t value) {
  return (value & 1) ? -static_cast<int64_t>(value >> 1) - 1 : static_cast<int64_t>(value >> 1);
}
bool allowedKind(const std::string &kind) {
  static const char *kinds[] = {"boundary", "motorway", "trunk", "primary", "secondary",
                                "tertiary", "local", "water", "place"};
  for (const auto *candidate : kinds) if (kind == candidate) return true;
  return false;
}
// Liang-Barsky clipping, including segments with both endpoints outside viewport.
bool clip(double &x0, double &y0, double &x1, double &y1,
          double xmin, double ymin, double xmax, double ymax) {
  const double dx = x1 - x0, dy = y1 - y0;
  const double p[] = {-dx, dx, -dy, dy};
  const double q[] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
  double lo = 0, hi = 1;
  for (unsigned i = 0; i < 4; ++i) {
    if (p[i] == 0) { if (q[i] < 0) return false; continue; }
    const double t = q[i] / p[i];
    if (p[i] < 0) lo = std::max(lo, t); else hi = std::min(hi, t);
    if (lo > hi) return false;
  }
  x1 = x0 + hi * dx; y1 = y0 + hi * dy;
  x0 += lo * dx; y0 += lo * dy;
  return true;
}
} // namespace

bool NanoMapPack::forEachFeature(
    double minLon, double minLat, double maxLon, double maxLat, uint8_t maxZoom,
    const std::function<bool(const FeatureRecord &)> &callback, MapProgress progress) const {
  if (status_ != PackStatus::Ok || !callback || maxZoom < 3 || maxZoom > 19 ||
      !std::isfinite(minLon) || !std::isfinite(minLat) ||
      !std::isfinite(maxLon) || !std::isfinite(maxLat) ||
      minLon > maxLon || minLat > maxLat || minLon < -180 || maxLon > 180 ||
      minLat < -90 || maxLat > 90) return false;
  const size_t bodyOffset = 10 + metadataSize_;
  if (bodyOffset >= size_ - 4) return false;
  std::unique_ptr<InflateCursor> cursor(new (std::nothrow) InflateCursor(
      data_ + bodyOffset, size_ - 4 - bodyOffset));
  if (!cursor) return false;
  bool deliver = true;
  for (size_t feature = 0; feature < featureCount_; ++feature) {
    if ((feature & 63u) == 0 && !progress.proceed()) return false;
    uint32_t name = 0, province = 0, zoom = 0, geometryBytes = 0, count = 0, kindSize = 0;
    if (!varuint(*cursor, name) || !varuint(*cursor, province) ||
        !varuint(*cursor, zoom) || !varuint(*cursor, geometryBytes) ||
        name > names_.size() || province >= 12 || zoom < 3 || zoom > 19 ||
        !geometryBytes || geometryBytes > 10 * kMaxGeometryPoints + 5 ||
        geometryBytes > kMaxDecodedBody - cursor->position()) return false;
    const size_t geometryEnd = cursor->position() + geometryBytes;
    if (!varuint(*cursor, count, geometryEnd) || !count || count > kMaxGeometryPoints) return false;
    FeatureRecord record;
    record.nameIndex = static_cast<uint16_t>(name); // zero = unnamed, otherwise one-based
    record.provinceIndex = static_cast<uint8_t>(province);
    record.zoomMin = static_cast<uint8_t>(zoom);
    record.geometry.reserve(count);
    int64_t lon = 0, lat = 0;
    for (uint32_t point = 0; point < count; ++point) {
      if (point && (point & 255u) == 0 && !progress.proceed()) return false;
      uint32_t dx = 0, dy = 0;
      if (!varuint(*cursor, dx, geometryEnd) || !varuint(*cursor, dy, geometryEnd)) return false;
      lon += unzigzag(dx); lat += unzigzag(dy);
      if (lon < -18000000 || lon > 18000000 || lat < -9000000 || lat > 9000000) return false;
      record.geometry.push_back({static_cast<int32_t>(lon), static_cast<int32_t>(lat)});
    }
    if (cursor->position() != geometryEnd || !varuint(*cursor, kindSize) ||
        !kindSize || kindSize > 9) return false;
    for (uint32_t i = 0; i < kindSize; ++i) {
      uint8_t ch = 0;
      if (!cursor->byte(ch)) return false;
      record.kind.push_back(static_cast<char>(ch));
    }
    if (!allowedKind(record.kind)) return false;
    if (!deliver || zoom > maxZoom) continue;
    FeatureRecord clipped;
    clipped.nameIndex = record.nameIndex;
    clipped.provinceIndex = record.provinceIndex;
    clipped.zoomMin = record.zoomMin;
    clipped.kind = record.kind;
    if (record.geometry.size() == 1) {
      const auto point = record.geometry[0];
      const double x = point.first / 100000.0, y = point.second / 100000.0;
      if (x >= minLon && x <= maxLon && y >= minLat && y <= maxLat) {
        clipped.geometry.push_back(point);
        if (!callback(clipped)) deliver = false;
      }
    } else {
      for (size_t i = 1; i < record.geometry.size() && deliver; ++i) {
        if ((i & 255u) == 0 && !progress.proceed()) return false;
        double x0 = record.geometry[i-1].first / 100000.0;
        double y0 = record.geometry[i-1].second / 100000.0;
        double x1 = record.geometry[i].first / 100000.0;
        double y1 = record.geometry[i].second / 100000.0;
        if (!clip(x0, y0, x1, y1, minLon, minLat, maxLon, maxLat)) continue;
        clipped.geometry = {{static_cast<int32_t>(std::llround(x0 * 100000)),
                             static_cast<int32_t>(std::llround(y0 * 100000))},
                            {static_cast<int32_t>(std::llround(x1 * 100000)),
                             static_cast<int32_t>(std::llround(y1 * 100000))}};
        if (!callback(clipped)) deliver = false;
      }
    }
  }
  return cursor->finish(); // validate checksum, full record count and no trailing body
}

bool NanoMapPack::lookupPlace(const std::string &prefix,
                              std::vector<PlaceResult> &out,
                              size_t limit) const {
  out.clear();
  if (status_ != PackStatus::Ok || limit == 0 || limit > kMaxNames) return false;
  for (size_t i = 0; i < names_.size() && out.size() < limit; ++i) {
    if (names_[i].compare(0, prefix.size(), prefix) == 0)
      out.push_back({names_[i], static_cast<uint16_t>(i + 1)});
  }
  return true;
}

}  // namespace meshtastic::nanomap

