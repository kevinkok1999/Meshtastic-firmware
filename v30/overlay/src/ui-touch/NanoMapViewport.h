#pragma once
#include "NanoMapPack.h"

namespace meshtastic::nanomap {
// Bounds are geographic, projection is Web Mercator, with north at top.
struct ViewportSpec { double minLon,minLat,maxLon,maxLat; uint16_t width,height; uint8_t zoom; };
enum class MapLayer : uint8_t { Boundary, Place, Motorway, Trunk, Primary, Secondary, Water, Tertiary, Local };
struct PixelPrimitive { uint16_t x0,y0,x1,y1,nameIndex; MapLayer layer; bool point; };
enum class ViewportStatus : uint8_t { Ready, Truncated, InvalidViewport, Cancelled, CorruptPack };
constexpr size_t kMaxViewportPrimitives=512;

// Background-only staging: bounded caller-owned frame, no allocation for
// primitives, highest-priority layers retained. Output is committed (count>0)
// ONLY after full geometry validation. Truncated means missing detail.
// The caller must independently enforce signed image provenance.
ViewportStatus stageViewport(const NanoMapPack&, const ViewportSpec&, PixelPrimitive*, size_t capacity,
                             size_t& count, MapProgress progress={});
}
