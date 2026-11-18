#include "NanoMapPresentation.h"
#include "NanoMapTrustPin.h"
#include <cassert>
#include <cmath>
#include <vector>

using namespace meshtastic::nanomap;
int main() {
  // Default Netherlands overview covers all mainland and European island bounds;
  // zoom and dimensions remain compatible with the native marker formula.
  ViewportSpec v{};
  assert(centeredViewport(52.2,5.4,6,320,240,v));
  assert(v.minLon<3.2&&v.maxLon>7.2&&v.minLat<50.7&&v.maxLat>53.6);
  assert(v.width==320&&v.height==240&&v.zoom==6);
  ViewportSpec z{};
  assert(centeredViewport(52.2,5.4,12,320,240,z));
  assert(z.maxLon-z.minLon < 0.12);
  assert(std::abs((5.4-z.minLon)/(z.maxLon-z.minLon)*319-160)<1e-8);
  assert(!centeredViewport(90,5,6,320,240,z));
  assert(!centeredViewport(52,179.9,6,320,240,z));
  assert(!centeredViewport(52,5,20,320,240,z));
  assert(!centeredViewport(52,5,6,321,240,z));

  // Render low-priority road first, then point on top. Independently chosen
  // literal colors check endpoint order, background, clipping and guard words.
  RasterPalette p{};
  p.background=0x1234;p.layer[static_cast<unsigned>(MapLayer::Local)]=0x4321;
  p.layer[static_cast<unsigned>(MapLayer::Place)]=0xABCD;
  PixelPrimitive features[]={{0,0,4,4,0,MapLayer::Local,false},{2,2,2,2,0,MapLayer::Place,true}};
  uint16_t pixels[27];for(auto& px:pixels)px=0x7777;
  assert(rasterizeViewport(features,2,5,5,p,pixels+1,25));
  assert(pixels[0]==0x7777&&pixels[26]==0x7777);
  assert(pixels[1]==0x4321&&pixels[25]==0x4321);
  assert(pixels[13]==0xABCD&&pixels[5]==0x1234);
  features[1].x0=5;
  assert(!rasterizeViewport(features,2,5,5,p,pixels+1,25));
  assert(!rasterizeViewport(features,2,5,5,p,pixels+1,24));
  assert(pixels[1]==0x4321&&pixels[25]==0x4321);
  assert(!rasterizeViewport(features,513,5,5,p,pixels+1,25));

  // External pin is independent reference SHA-256 of the exact input bytes.
  const uint8_t abc[]={'a','b','c'};
  const char* good="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const uint8_t corrupt[]={'a','b','d'};
  assert(validTrustPin(good));
  assert(!validTrustPin(nullptr)&&!validTrustPin("")&&!validTrustPin("ba78"));
  assert(verifyTrustPin(abc,sizeof abc,good));
  assert(!verifyTrustPin(corrupt,sizeof corrupt,good));
  assert(!verifyTrustPin(abc,sizeof abc,""));
  assert(!verifyTrustPin(abc,sizeof abc,good,{[](void*){return false;},nullptr}));
  assert(!verifyTrustPin(nullptr,sizeof abc,good));
  return 0;
}
