#pragma once
#include "NanoMapViewport.h"

namespace meshtastic::nanomap {
struct RasterPalette { uint16_t background; uint16_t layer[9]; };
bool centeredViewport(double lat,double lon,uint8_t zoom,uint16_t width,uint16_t height,ViewportSpec& out);
bool rasterizeViewport(const PixelPrimitive* features,size_t count,uint16_t width,uint16_t height,
                       const RasterPalette& palette,uint16_t* pixels,size_t pixelCapacity);
}
