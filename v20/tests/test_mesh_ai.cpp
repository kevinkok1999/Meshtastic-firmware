#include <cassert>
#include <cstring>
#include <iostream>
#include "mesh-ai/MeshAiCore.h"

static bool has(const char* s, const char* n){ return std::strstr(s,n)!=nullptr; }

int main() {
  MeshAi::Context c{};
  std::strcpy(c.node_name, "TDeck");
  c.contacts_total = 9;
  c.contacts_recent = 3;
  c.contacts_direct = 2;
  c.wifi_supported = true;
  c.wifi_connected = true;
  c.wifi_status = 3;
  c.wifi_rssi = -51;
  std::strcpy(c.wifi_ssid, "S21");
  std::strcpy(c.ip, "192.168.43.20");
  c.gps_known = true;
  c.gps_lat = 51.87;
  c.gps_lon = 4.69;
  c.lora_freq_mhz = 869.618f;
  c.lora_bw_khz = 62.5f;
  c.lora_sf = 8;
  c.lora_tx_dbm = 22;
  c.internal_heap_free = 120*1024;
  c.psram_free = 5*1024*1024;

  char out[512];
  assert(MeshAi::isCommand("ai status"));
  assert(MeshAi::isCommand("Mesh AI waarom wifi?"));
  assert(!MeshAi::isCommand("wifi"));

  assert(MeshAi::answer("ai wifi", c, out, sizeof(out)));
  assert(has(out, "S21"));
  assert(has(out, "-51"));

  assert(MeshAi::answer("ai hoe is mijn radio?", c, out, sizeof(out)));
  assert(has(out, "LoRa"));
  assert(has(out, "869.618"));

  assert(MeshAi::answer("ai wie is bereikbaar?", c, out, sizeof(out)));
  assert(has(out, "3"));
  assert(has(out, "9"));

  assert(MeshAi::answer("ai gps", c, out, sizeof(out)));
  assert(has(out, "51.87000"));

  assert(MeshAi::answer("ai route", c, out, sizeof(out)));
  assert(has(out, "LoRa/mesh"));
  assert(has(out, "geen internet-berichtenroute"));

  assert(MeshAi::answer("ai warum funktioniert mein wlan nicht", c, out, sizeof(out)));
  assert(has(out, "Status:"));

  assert(MeshAi::answer("ai help", c, out, sizeof(out)));
  assert(has(out, "Mesh AI LOCAL"));

  std::cout << "Mesh AI parser tests OK\n";
  return 0;
}
