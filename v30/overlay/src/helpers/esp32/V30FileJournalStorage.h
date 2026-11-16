#pragma once
#include "V30Journal.h"
#include <cstdio>
#include <string>
namespace v30 {
// Fixed-size slot backend. It never creates, formats, or truncates a pre-existing file.
class V30FileJournalStorage final : public V30JournalStorage {
 public:
  V30FileJournalStorage(const char* path, size_t slots);
  ~V30FileJournalStorage() override;
  size_t capacity() const override { return slots_; }
  bool read(size_t, V30JournalRecord&) override;
  bool isEmpty(size_t) const override;
  bool write(size_t, const V30JournalRecord&) override;
  bool clear(size_t) override;
  bool flush() override;
 private:
  std::string path_; size_t slots_; std::FILE* file_;
  bool ensureOpen() const;
  static uint32_t checksum(const V30JournalRecord&);
  static bool valid(const V30JournalRecord&);
};
}
