#pragma once
#include "NanoMapProgress.h"
#include <cstddef>
#include <cstdint>

namespace meshtastic::nanomap {
bool validTrustPin(const char* hex);
bool verifyTrustPin(const uint8_t* bytes,size_t size,const char* hex,MapProgress progress={});
}
