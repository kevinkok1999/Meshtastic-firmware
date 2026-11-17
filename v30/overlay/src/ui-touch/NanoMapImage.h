#pragma once
#include <cstddef>
#include <cstdint>

namespace meshtastic::nanomap {
constexpr size_t kRawTilePartitionBytes = 0x4c0000;
constexpr size_t kRawPackLimit = 0x380000;

enum class ImageStatus : uint8_t { Ok, WrongPartition, NotNanoMap, BadHeader, NoMemory,
                                  BadCompression, DecodedLimit, BadChecksum, BadPadding };
enum class TileStorage : uint8_t { Unavailable, NanoMap, LegacyCandidate };

// A full partition image is mmapped/immutable. This is a bounded, synchronous
// structural inspection for a background worker, NEVER the radio/UI loop.
// CRC and padding checks are not cryptographic authentication.
ImageStatus locateRawImage(const uint8_t* data, size_t size, size_t& packSize);
TileStorage classifyTileHeader(const uint8_t* prefix, size_t size);

// Mount an intact legacy filesystem without ever formatting or opening a raw
// NanoMap partition as LittleFS. Corrupt volumes fail closed.
template<class Filesystem>
bool mountLegacyTilesWithoutFormat(Filesystem& fs, TileStorage storage) {
  if (storage != TileStorage::LegacyCandidate) return false;
  if (!fs.begin(false, "/tiles_lfs", 10, "tiles")) return false;
  const size_t bytes = fs.totalBytes();
  if (!bytes || bytes > kRawTilePartitionBytes) { fs.end(); return false; }
  return true;
}
}
