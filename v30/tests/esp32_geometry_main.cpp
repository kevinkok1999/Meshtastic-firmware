#include <Arduino.h>
#include "NanoMapPack.h"
meshtastic::nanomap::NanoMapPack pack;
void setup() {
  pack.open(nullptr, 0);
  pack.forEachFeature(3.0, 50.0, 8.0, 54.0, 19,
                      [](const meshtastic::nanomap::FeatureRecord &) { return true; });
}
void loop() { delay(100); }
