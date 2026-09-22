#pragma once
// MeshOffGridNL V20 Local AI Core
// Deterministic, offline-first intent/diagnostic engine for ESP32-S3.
// No cloud, no LLM, no radio/Wi-Fi driver ownership.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace MeshAi {

struct Context {
  char node_name[32] = {0};

  uint32_t contacts_total = 0;
  uint32_t contacts_recent = 0;
  uint32_t contacts_direct = 0;

  bool wifi_supported = false;
  bool wifi_connected = false;
  int wifi_status = 0;
  int wifi_rssi = 0;
  char wifi_ssid[33] = {0};
  char ip[24] = {0};

  bool gps_known = false;
  double gps_lat = 0.0;
  double gps_lon = 0.0;

  float lora_freq_mhz = 0.0f;
  float lora_bw_khz = 0.0f;
  int lora_sf = 0;
  int lora_tx_dbm = 0;

  uint32_t internal_heap_free = 0;
  uint32_t psram_free = 0;
};

inline void lowerCopy(const char* in, char* out, size_t cap) {
  if (!out || cap == 0) return;
  size_t i = 0;
  if (in) {
    for (; in[i] && i + 1 < cap; ++i) {
      unsigned char c = (unsigned char)in[i];
      out[i] = (char)tolower(c);
    }
  }
  out[i] = '\0';
}

inline const char* skipSpace(const char* p) {
  if (!p) return "";
  while (*p == ' ' || *p == '\t') ++p;
  return p;
}

inline bool startsWithNoCase(const char* text, const char* prefix) {
  if (!text || !prefix) return false;
  while (*prefix) {
    if (!*text) return false;
    if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return false;
    ++text; ++prefix;
  }
  return true;
}

inline bool isCommand(const char* cmd) {
  const char* p = skipSpace(cmd);
  return startsWithNoCase(p, "ai") ||
         startsWithNoCase(p, "meshai") ||
         startsWithNoCase(p, "mesh ai");
}

inline const char* payload(const char* cmd) {
  const char* p = skipSpace(cmd);
  if (startsWithNoCase(p, "meshai")) p += 6;
  else if (startsWithNoCase(p, "mesh ai")) p += 7;
  else if (startsWithNoCase(p, "ai")) p += 2;
  return skipSpace(p);
}

inline bool has(const char* lowered, const char* word) {
  return lowered && word && strstr(lowered, word) != nullptr;
}

inline const char* wifiStateName(int status, bool connected) {
  if (connected) return "verbonden";
  switch (status) {
    case 0: return "idle";
    case 1: return "geen SSID";
    case 2: return "scan voltooid";
    case 4: return "verbinding verloren";
    case 5: return "verbinding mislukt";
    case 6: return "losgekoppeld";
    default: return "niet verbonden";
  }
}

inline bool answer(const char* command, const Context& c, char* out, size_t cap) {
  if (!out || cap < 32 || !isCommand(command)) return false;
  const char* raw = payload(command);
  char q[192];
  lowerCopy(raw, q, sizeof(q));

  if (!q[0] || has(q, "help") || has(q, "hulp") || has(q, "hilfe") || has(q, "?")) {
    snprintf(out, cap,
      "Mesh AI LOCAL. Probeer: ai status | ai wifi | ai radio | ai bereikbaar | ai gps | ai route | ai geheugen | ai diagnose");
    return true;
  }

  const bool ask_wifi =
      has(q, "wifi") || has(q, "wi-fi") || has(q, "wlan") ||
      has(q, "internet") || has(q, "hotspot");
  const bool ask_radio =
      has(q, "radio") || has(q, "lora") || has(q, "funk") ||
      has(q, "zend") || has(q, "send") || has(q, "rf");
  const bool ask_reach =
      has(q, "bereik") || has(q, "bereikbaar") || has(q, "reachable") ||
      has(q, "erreich") || has(q, "nodes") || has(q, "contact");
  const bool ask_gps =
      has(q, "gps") || has(q, "locatie") || has(q, "location") ||
      has(q, "standort") || has(q, "positie");
  const bool ask_mem =
      has(q, "geheugen") || has(q, "memory") || has(q, "speicher") ||
      has(q, "heap") || has(q, "psram");
  const bool ask_route =
      has(q, "route") || has(q, "pad") || has(q, "path") ||
      has(q, "weg") || has(q, "transport");
  const bool ask_diag =
      has(q, "diagnose") || has(q, "waarom") || has(q, "why") ||
      has(q, "problem") || has(q, "probleem") || has(q, "fehler");
  const bool ask_status =
      has(q, "status") || has(q, "health") || has(q, "gezondheid") ||
      has(q, "zustand") || has(q, "overzicht");

  if (ask_wifi && !ask_status && !ask_diag) {
    if (!c.wifi_supported) {
      snprintf(out, cap, "Wi-Fi: niet beschikbaar in deze build.");
    } else if (c.wifi_connected) {
      snprintf(out, cap, "Wi-Fi: verbonden met %s, RSSI %d dBm, IP %s.",
               c.wifi_ssid[0] ? c.wifi_ssid : "(SSID verborgen)",
               c.wifi_rssi, c.ip[0] ? c.ip : "onbekend");
    } else {
      snprintf(out, cap, "Wi-Fi: %s (status %d). LoRa/mesh blijft onafhankelijk beschikbaar.",
               wifiStateName(c.wifi_status, false), c.wifi_status);
    }
    return true;
  }

  if (ask_radio && !ask_status && !ask_diag) {
    snprintf(out, cap,
      "Radio: LoRa %.3f MHz, BW %.1f kHz, SF%d, TX-config %d dBm. Recent %u contacten, %u met directe bekende padstatus.",
      (double)c.lora_freq_mhz, (double)c.lora_bw_khz, c.lora_sf, c.lora_tx_dbm,
      (unsigned)c.contacts_recent, (unsigned)c.contacts_direct);
    return true;
  }

  if (ask_reach && !ask_status && !ask_diag) {
    snprintf(out, cap,
      "Bereik: %u contacten bekend; %u recent gehoord (laatste 2 uur); %u hebben een directe bekende route.",
      (unsigned)c.contacts_total, (unsigned)c.contacts_recent, (unsigned)c.contacts_direct);
    return true;
  }

  if (ask_gps && !ask_status && !ask_diag) {
    if (c.gps_known) {
      snprintf(out, cap, "Locatie: %.5f, %.5f. Deze positie blijft lokaal tenzij jij hem expliciet verzendt.",
               c.gps_lat, c.gps_lon);
    } else {
      snprintf(out, cap, "Locatie: nog geen bruikbare positie in de lokale context.");
    }
    return true;
  }

  if (ask_mem && !ask_status && !ask_diag) {
    snprintf(out, cap, "Geheugen: interne heap vrij %u KB, PSRAM vrij %u KB.",
             (unsigned)(c.internal_heap_free / 1024U),
             (unsigned)(c.psram_free / 1024U));
    return true;
  }

  if (ask_route && !ask_status && !ask_diag) {
    if (c.contacts_recent > 0) {
      snprintf(out, cap,
        "Routeadvies: LoRa/mesh is nu de bevestigde berichtroute (%u recent gehoorde contacten). Wi-Fi%s verbonden, maar V20 claimt geen internet-berichtenroute zonder echte backend.",
        (unsigned)c.contacts_recent, c.wifi_connected ? " is" : " is niet");
    } else {
      snprintf(out, cap,
        "Routeadvies: geen recente mesh-contacten bevestigd. LoRa blijft actief; Wi-Fi%s verbonden. Internet-messaging is pas een route zodra een echte backend bestaat.",
        c.wifi_connected ? " is" : " is niet");
    }
    return true;
  }

  if (ask_diag || ask_status) {
    const char* wifi = c.wifi_supported ? wifiStateName(c.wifi_status, c.wifi_connected) : "niet beschikbaar";
    const char* radio = c.contacts_recent ? "mesh-activiteit gezien" : "geen recente peers bevestigd";
    snprintf(out, cap,
      "%s%sStatus: Wi-Fi %s; radio %s; contacten %u/%u recent; GPS %s; heap %u KB; PSRAM %u KB.",
      c.node_name[0] ? c.node_name : "MeshOffGrid",
      c.node_name[0] ? " - " : " - ",
      wifi, radio,
      (unsigned)c.contacts_recent, (unsigned)c.contacts_total,
      c.gps_known ? "positie bekend" : "geen positie",
      (unsigned)(c.internal_heap_free / 1024U),
      (unsigned)(c.psram_free / 1024U));
    return true;
  }

  snprintf(out, cap,
    "Ik werk volledig lokaal en ben gespecialiseerd in dit apparaat. Vraag bijvoorbeeld: 'ai waarom werkt mijn wifi niet?' of 'ai hoe is mijn radio?'.");
  return true;
}

} // namespace MeshAi
