#include <Arduino.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_idf_version.h>

#ifndef GOLDEN_WIFI_SSID
#define GOLDEN_WIFI_SSID "MeshOffGrid21"
#endif
#ifndef GOLDEN_WIFI_PASS
#define GOLDEN_WIFI_PASS "MeshOffGrid21Test"
#endif
#ifndef GOLDEN_STACK_NAME
#define GOLDEN_STACK_NAME "unknown"
#endif

enum class LinkPhase : uint8_t {
  Boot = 0,
  StaStarted,
  Associating,
  Associated,
  GotIp,
  Disconnected
};

static volatile LinkPhase g_phase = LinkPhase::Boot;
static volatile uint8_t g_last_reason = 0;
static volatile uint32_t g_disconnect_count = 0;
static volatile bool g_event_dirty = false;
static uint32_t g_retry_at_ms = 0;
static uint32_t g_last_heartbeat_ms = 0;
static bool g_retry_armed = false;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
using MogEventId = arduino_event_id_t;
using MogEventInfo = arduino_event_info_t;
#else
using MogEventId = WiFiEvent_t;
using MogEventInfo = WiFiEventInfo_t;
#endif

static const char* phaseName(LinkPhase p) {
  switch (p) {
    case LinkPhase::Boot: return "BOOT";
    case LinkPhase::StaStarted: return "STA_STARTED";
    case LinkPhase::Associating: return "ASSOCIATING";
    case LinkPhase::Associated: return "ASSOCIATED_WAIT_DHCP";
    case LinkPhase::GotIp: return "GOT_IP";
    case LinkPhase::Disconnected: return "DISCONNECTED";
    default: return "?";
  }
}

// Callback rule: record tiny pieces of state only.
// No reconnect, no scans, no mode changes, no blocking work from the event task.
static void onWiFiEvent(MogEventId event, MogEventInfo info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      g_phase = LinkPhase::StaStarted;
      g_event_dirty = true;
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      g_phase = LinkPhase::Associated;
      g_event_dirty = true;
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_phase = LinkPhase::GotIp;
      g_retry_armed = false;
      g_event_dirty = true;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_last_reason = info.wifi_sta_disconnected.reason;
      ++g_disconnect_count;
      g_phase = LinkPhase::Disconnected;
      g_retry_armed = true;
      g_retry_at_ms = millis() + 5000U;
      g_event_dirty = true;
      break;

    default:
      break;
  }
}

static void startAssociation(const char* why) {
  Serial.printf("[GOLDEN] begin reason=%s ssid='%s'\n", why, GOLDEN_WIFI_SSID);
  g_phase = LinkPhase::Associating;

  // Intentionally minimal:
  // - no scan
  // - no disconnect()
  // - no BSSID/channel hint
  // - no min-security override
  // - no PMF/SAE override
  // - no Wi-Fi OFF/ON recovery
  WiFi.begin(GOLDEN_WIFI_SSID, GOLDEN_WIFI_PASS);
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println("=== MeshOffGridNL V21 Golden Wi-Fi ===");
  Serial.printf("[GOLDEN] stack=%s\n", GOLDEN_STACK_NAME);
  Serial.printf("[GOLDEN] arduino=%d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  Serial.printf("[GOLDEN] idf=%d.%d.%d\n",
                ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
  Serial.println("[GOLDEN] BLE=NOT_STARTED LoRa=ABSENT scan=ABSENT custom-security=ABSENT");

  // Register events before starting the station.
  WiFi.onEvent(onWiFiEvent);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  const bool mode_ok = WiFi.mode(WIFI_STA);
  Serial.printf("[GOLDEN] WIFI_STA mode=%s\n", mode_ok ? "OK" : "FAIL");

  if (mode_ok) {
    startAssociation("initial");
  }
}

void loop() {
  if (g_event_dirty) {
    noInterrupts();
    const LinkPhase phase = g_phase;
    const uint8_t reason = g_last_reason;
    const uint32_t drops = g_disconnect_count;
    g_event_dirty = false;
    interrupts();

    Serial.printf("[GOLDEN] event phase=%s status=%d reason=%u drops=%lu\n",
                  phaseName(phase), (int)WiFi.status(), (unsigned)reason,
                  (unsigned long)drops);

    if (phase == LinkPhase::Associated) {
      Serial.println("[GOLDEN] association succeeded; waiting for DHCP/GOT_IP");
    } else if (phase == LinkPhase::GotIp) {
      Serial.printf("[GOLDEN] SUCCESS ip=%s rssi=%d channel=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    } else if (phase == LinkPhase::Disconnected) {
      Serial.printf("[GOLDEN] disconnect_reason=%u; retry scheduled in 5s\n",
                    (unsigned)reason);
    }
  }

  if (g_retry_armed && (int32_t)(millis() - g_retry_at_ms) >= 0) {
    g_retry_armed = false;
    startAssociation("scheduled-retry");
  }

  if ((uint32_t)(millis() - g_last_heartbeat_ms) >= 5000U) {
    g_last_heartbeat_ms = millis();
    Serial.printf("[GOLDEN] heartbeat phase=%s status=%d ip=%s rssi=%d reason=%u\n",
                  phaseName(g_phase), (int)WiFi.status(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  (unsigned)g_last_reason);
  }

  delay(10);
}
