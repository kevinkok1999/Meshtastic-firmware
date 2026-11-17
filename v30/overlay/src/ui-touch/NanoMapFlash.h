#pragma once
#include "NanoMapImage.h"

#if defined(ESP32)
namespace meshtastic::nanomap {
// Fast boot guard: only four bytes are read; no mount/erase/write occurs.
TileStorage classifyInternalTiles();

// Read-only exact partition mapping and framed pack inspection. Intended for
// a background worker only; a callback can copy/query the verified view while
// it is mapped. No authenticity claim: production needs signed asset policy.
// Not invoked automatically by the mount guard.
using RawPackVisitor = bool (*)(const uint8_t*, size_t, void*);
bool inspectInternalTiles(RawPackVisitor visitor, void* context);
}
#endif
