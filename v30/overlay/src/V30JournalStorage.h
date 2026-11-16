#pragma once
// V30 F2 storage adapter. Caller injects an already-mounted SPIFFS/LittleFS FS.
// This header is intentionally not wired into firmware until integration gates pass.
#include <Arduino.h>
#include <FS.h>
namespace meshtastic::v30 {
enum class JournalStatus : uint8_t { Ok, Missing, Corrupt, Partial, Full, IoError };
class V30JournalStorage {
 public:
  V30JournalStorage(fs::FS& fs, const char* path, size_t capacity) : fs_(fs), path_(path), capacity_(capacity) {}
  JournalStatus append(uint64_t sequence, const uint8_t* payload, size_t length);
  JournalStatus read(bool (*visitor)(uint64_t, const uint8_t*, size_t, void*), void* context) const;
  JournalStatus flush();
 private:
  fs::FS& fs_; const char* path_; size_t capacity_;
  static constexpr uint32_t MAGIC = 0x31304A56; // "VJ01" little-endian
};
} // namespace meshtastic::v30
