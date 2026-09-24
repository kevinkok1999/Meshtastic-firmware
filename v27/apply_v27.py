#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys

def fail(msg: str) -> None:
    raise SystemExit("V27 patch failed: " + msg)

def section(source: str, heading: str, next_heading: str, old: str, new: str, label: str) -> str:
    start = source.find(heading)
    if start < 0:
        fail(label + ": section missing")
    end = source.find(next_heading, start + len(heading))
    if end < 0:
        end = len(source)
    chunk = source[start:end]
    count = chunk.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    chunk = chunk.replace(old, new, 1)
    return source[:start] + chunk + source[end:]

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v27.py <V26-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_path = root / "platformio.ini"
    main_path = root / "src/main.cpp"
    ui_path = root / "src/ui-touch/UITask.cpp"
    mesh_path = root / "src/MyMesh.cpp"
    mesh_h_path = root / "src/MyMesh.h"
    bridge_h = root / "src/helpers/esp32/V11GlobalBridge.h"
    bridge_cpp = root / "src/helpers/esp32/V11GlobalBridge.cpp"

    for p in (pio_path, main_path, ui_path, mesh_path, mesh_h_path, bridge_h, bridge_cpp):
        if not p.exists():
            fail("missing " + str(p))

    pio = pio_path.read_text()
    for marker in ("MESH_OFFGRIDNL_V19=1", "MESH_OFFGRIDNL_V26=1"):
        if marker not in pio:
            fail("required baseline missing " + marker)

    # V27 is T-Deck-only and additive. These flags gate privacy/global work
    # without changing the radio waveform used by P1 Pro V8.
    pio = section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V26=1\n",
        "  -D MESH_OFFGRIDNL_V26=1\n"
        "  -D MESH_OFFGRIDNL_V27=1\n"
        "  -D V27_PRIVACY_PRO=1\n"
        "  -D V27_P1_V8_COMPAT=1\n"
        "  -D V27_ZERO_CONFIG=1\n"
        "  -D V27_WIFI_BROAD_COMPAT=1\n"
        "  -D V27_RELAY_PROFILE_DEV_PUBLIC=1\n",
        "V27 T-Deck flags",
    )
    pio_path.write_text(pio)

    # Fail closed if the known P1-V8-compatible RF profile drifted before V27.
    tdeck_start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    tdeck_end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", tdeck_start)
    tdeck = pio[tdeck_start:tdeck_end]
    for marker in (
        "LORA_FREQ=869.618",
        "LORA_BW=62.5",
        "LORA_SF=8",
        "LORA_TX_POWER=22",
        "MAX_LORA_TX_POWER=22",
        "SX126X_RX_BOOSTED_GAIN=1",
        "SX126X_DIO2_AS_RF_SWITCH=true",
    ):
        if marker not in tdeck:
            fail("P1 Pro V8 compatibility marker missing " + marker)


    # V27 broad 2.4-GHz compatibility engine. ESP32-S3 has no 5-GHz radio, so
    # broaden association behavior instead of pretending firmware can add a band.
    main_text = main_path.read_text()

    v19_old = """#if defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
  // Runtime joining is intentionally boring: no erase-AP, no forced PMF/SAE,
  // no BSSID/channel pin and no PHY/bandwidth override.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  wifiConfigClearApHint();
  const char* v19_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  if (g_v16_wifi_attempt >= 3) {
    Serial.printf("[V19][wifi] attempt=%u safe STA restart ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(150);
    WiFi.mode(WIFI_OFF);
    delay(350);
    WiFi.mode(WIFI_STA);
    delay(200);
  } else {
    Serial.printf("[V19][wifi] attempt=%u standard join ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(g_v16_wifi_attempt <= 1 ? 120 : 300);
    WiFi.mode(WIFI_STA);
  }

  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid, v19_pwd);
  return;
#elif defined(MESH_OFFGRIDNL_V18)"""

    v27_new = """#if defined(MESH_OFFGRIDNL_V27)
  // V27 adaptive 2.4-GHz association ladder. One SSID/password, four automatic
  // profiles. No normal-user Wi-Fi mode selector is introduced.
  //
  // Profile 1: modern secure Arduino/IDF defaults (WPA2 or better).
  // Profile 2: explicit WPA/WPA2/WPA3 transition + optional PMF + SAE H2E.
  // Profile 3: legacy/IoT-friendly HT20 with PMF disabled.
  // Profile 4: EU868/NL rescue path, channels 1-13 + optional BSSID/channel
  //            hint for mesh/extender APs and hidden/ch12-13 edge cases.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  const char* v27_pwd = (pwd && pwd[0]) ? pwd : nullptr;
  const uint8_t profile =
      g_v16_wifi_attempt <= 1 ? 1 :
      g_v16_wifi_attempt == 2 ? 2 :
      g_v16_wifi_attempt == 3 ? 3 : 4;

  WiFi.disconnect(false, false);
  delay(profile == 1 ? 120 : 220);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  // Modern-first security floor. WPA-only compatibility is tried only by
  // profile 3; WEP is never enabled.
  WiFi.setMinSecurity(v27_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);

  if (profile == 1) {
    Serial.printf("[V27][wifi] profile=1 standard ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.begin(ssid, v27_pwd);
    return;
  }

  wifi_config_t v27_cfg = {};
  strlcpy(reinterpret_cast<char*>(v27_cfg.sta.ssid), ssid, sizeof(v27_cfg.sta.ssid));
  if (v27_pwd)
    strlcpy(reinterpret_cast<char*>(v27_cfg.sta.password), v27_pwd, sizeof(v27_cfg.sta.password));
  v27_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  v27_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  v27_cfg.sta.bssid_set = false;
  v27_cfg.sta.channel = 0;
  v27_cfg.sta.threshold.rssi = -127;
  v27_cfg.sta.threshold.authmode = v27_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  v27_cfg.sta.failure_retry_cnt = profile == 4 ? 4 : 3;

  if (profile == 2) {
    // Modern mixed WPA2/WPA3 routers/hotspots. PMF is advertised but optional;
    // WPA3 APs can still require it. Both SAE element methods are accepted.
    v27_cfg.sta.pmf_cfg.capable = true;
    v27_cfg.sta.pmf_cfg.required = false;
    v27_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    Serial.printf("[V27][wifi] profile=2 modern-transition ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
  } else if (profile == 3) {
    // Older/quirky 2.4-GHz routers. Only this explicit fallback lowers the
    // auth threshold to WPA so modern profiles never silently weaken security.
    v27_cfg.sta.threshold.authmode = v27_pwd ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    v27_cfg.sta.pmf_cfg.capable = false;
    v27_cfg.sta.pmf_cfg.required = false;
    esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    Serial.printf("[V27][wifi] profile=3 legacy-ht20 ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
  } else {
    // This firmware is the MeshOffGridNL EU868/NL build. Enable active 1-13
    // operation for the final rescue attempt; this also covers hidden SSIDs on
    // channels 12/13 that world-safe passive scanning can miss.
    esp_wifi_set_country_code("NL", false);
    v27_cfg.sta.pmf_cfg.capable = true;
    v27_cfg.sta.pmf_cfg.required = false;
    v27_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    int32_t hint_channel = 0;
    uint8_t hint_bssid[6] = {};
    uint8_t hint_auth = 0;
    const bool have_hint =
        wifiConfigGetApHint(ssid, &hint_channel, hint_bssid, &hint_auth) &&
        hint_channel >= 1 && hint_channel <= 13;
    if (have_hint) {
      v27_cfg.sta.bssid_set = true;
      v27_cfg.sta.channel = (uint8_t)hint_channel;
      memcpy(v27_cfg.sta.bssid, hint_bssid, sizeof(hint_bssid));
    }
    esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    Serial.printf("[V27][wifi] profile=4 eu13-mesh-rescue ssid='%s' ch=%ld hint=%d reason=%u\\n",
                  ssid, (long)hint_channel, (int)have_hint,
                  (unsigned)g_wifi_last_disc_reason);
    // The hint is one-shot; later background retries must be able to roam.
    wifiConfigClearApHint();
  }

  const esp_err_t v27_cfg_rc = esp_wifi_set_config(WIFI_IF_STA, &v27_cfg);
  const esp_err_t v27_con_rc = (v27_cfg_rc == ESP_OK) ? esp_wifi_connect() : v27_cfg_rc;
  Serial.printf("[V27][wifi] profile=%u cfg=%d connect=%d\\n",
                (unsigned)profile, (int)v27_cfg_rc, (int)v27_con_rc);
  return;
#elif defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
  // Runtime joining is intentionally boring: no erase-AP, no forced PMF/SAE,
  // no BSSID/channel pin and no PHY/bandwidth override.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  wifiConfigClearApHint();
  const char* v19_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  if (g_v16_wifi_attempt >= 3) {
    Serial.printf("[V19][wifi] attempt=%u safe STA restart ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(150);
    WiFi.mode(WIFI_OFF);
    delay(350);
    WiFi.mode(WIFI_STA);
    delay(200);
  } else {
    Serial.printf("[V19][wifi] attempt=%u standard join ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(g_v16_wifi_attempt <= 1 ? 120 : 300);
    WiFi.mode(WIFI_STA);
  }

  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid, v19_pwd);
  return;
#elif defined(MESH_OFFGRIDNL_V18)"""

    if main_text.count(v19_old) != 1:
        fail("V27 adaptive Wi-Fi anchor drifted")
    main_text = main_text.replace(v19_old, v27_new, 1)

    # V16 originally backs off after three foreground attempts. V27 has four
    # compatibility profiles, so allow all four before entering quiet backoff.
    for old_retry, new_retry in (
        ("(!g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3)",
         "(!g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 4)"),
        ("if (g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3)",
         "if (g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 4)"),
    ):
        if main_text.count(old_retry) != 1:
            fail("V27 four-profile retry anchor drifted: " + old_retry)
        main_text = main_text.replace(old_retry, new_retry, 1)
    main_path.write_text(main_text)

    ui_text = ui_path.read_text()
    ui_old = """#if defined(MESH_OFFGRIDNL_V19)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 fresh connect (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 plain retry (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "V19 radio retry (3/3)");
#elif defined(MESH_OFFGRIDNL_V18)"""
    ui_new = """#if defined(MESH_OFFGRIDNL_V27)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "Connecting... (1/4)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "Compatibility... (2/4)");
      else if (g_v16_wifi_attempt == 3)
        snprintf(s_v16_join, sizeof s_v16_join, "Compatibility... (3/4)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "Router recovery... (4/4)");
#elif defined(MESH_OFFGRIDNL_V19)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 fresh connect (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 plain retry (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "V19 radio retry (3/3)");
#elif defined(MESH_OFFGRIDNL_V18)"""
    if ui_text.count(ui_old) != 1:
        fail("V27 Wi-Fi status UI anchor drifted")
    ui_text = ui_text.replace(ui_old, ui_new, 1)
    ui_path.write_text(ui_text)

    mesh_h_text = mesh_h_path.read_text()
    if "#define LORA_CR 5" not in mesh_h_text:
        fail("P1 Pro V8 compatibility marker missing #define LORA_CR 5")

    # Extend only the narrow V11 contact hook surface needed by the Privacy Pro
    # envelope. 8-byte public-key prefixes are used solely as a local lookup
    # hint; the full key stays inside authenticated ciphertext on the Internet.
    old_h = """  bool v11LookupChatContact(const uint8_t pub[32], ContactInfo& out);
  void v11InjectGlobalDm(const uint8_t senderPub[32], uint32_t timestamp, const char* text);
"""
    new_h = """  bool v11LookupChatContact(const uint8_t pub[32], ContactInfo& out);
#if defined(MESH_OFFGRIDNL_V27)
  uint32_t v27GetContactCount();
  bool v27GetContactByIndex(uint32_t idx, ContactInfo& out);
  bool v27CalcSharedSecretCached(const uint8_t peerPub[32], uint8_t out[32]);
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
  void v27SignGlobal(const uint8_t* data, size_t len, uint8_t sig[SIGNATURE_SIZE]);
  void v27InjectGlobalChannel(const mesh::GroupChannel& channel, uint32_t timestamp, const char* text);
#endif
  void v11InjectGlobalDm(const uint8_t senderPub[32], uint32_t timestamp, const char* text);
"""
    if mesh_h_text.count(old_h) != 1:
        fail("MyMesh V27 contact hook anchor drifted")
    mesh_h_text = mesh_h_text.replace(old_h, new_h, 1)
    mesh_h_path.write_text(mesh_h_text)

    mesh_cpp_text = mesh_path.read_text()
    old_cpp = """bool MyMesh::v11LookupChatContact(const uint8_t pub[32], ContactInfo& out) {
  if (!pub) return false;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  out = *c;
  return true;
}

void MyMesh::v11InjectGlobalDm"""
    new_cpp = """bool MyMesh::v11LookupChatContact(const uint8_t pub[32], ContactInfo& out) {
  if (!pub) return false;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  out = *c;
  return true;
}

#if defined(MESH_OFFGRIDNL_V27)
static bool s_v27_global_channel_inject = false;

uint32_t MyMesh::v27GetContactCount() {
  return getNumContacts();
}

bool MyMesh::v27GetContactByIndex(uint32_t idx, ContactInfo& out) {
  return getContactByIdx(idx, out);
}

bool MyMesh::v27CalcSharedSecretCached(const uint8_t peerPub[32], uint8_t out[32]) {
  if (!peerPub || !out) return false;
  ContactInfo* contact = lookupContactByPubKey(peerPub, PUB_KEY_SIZE);
  if (!contact || contact->type != ADV_TYPE_CHAT) return false;
  memcpy(out, contact->getSharedSecret(self_id), PUB_KEY_SIZE);
  return true;
}

bool MyMesh::v27GetChannelByIndex(uint8_t idx, ChannelDetails& out) {
  if (idx >= MAX_GROUP_CHANNELS) return false;
  if (!getChannel(idx, out)) return false;
  return channelSlotConfigured(out);
}

void MyMesh::v27SignGlobal(const uint8_t* data, size_t len, uint8_t sig[SIGNATURE_SIZE]) {
  if (!data || !sig || len == 0 || len > 512) {
    if (sig) memset(sig, 0, SIGNATURE_SIZE);
    return;
  }
  self_id.sign(sig, data, (int)len);
}

void MyMesh::v27InjectGlobalChannel(const mesh::GroupChannel& channel,
                                    uint32_t timestamp,
                                    const char* text) {
  if (!text) return;
  mesh::Packet synthetic;
  synthetic.header = (PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;
  synthetic.payload_len = 0;
  synthetic.path_len = 0;
  synthetic._snr = 0;
  s_v27_global_channel_inject = true;
  onChannelMessageRecv(channel, &synthetic, timestamp, text);
  s_v27_global_channel_inject = false;
}
#endif

void MyMesh::v11InjectGlobalDm"""
    if mesh_cpp_text.count(old_cpp) != 1:
        fail("MyMesh V27 contact lookup anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(old_cpp, new_cpp, 1)
    # V27 global-first for already-proven V27 peers. The early return happens
    # before MeshCore queues an RF packet. Unknown/legacy/P1 peers still take
    # the existing RF path and are mirrored globally for capability discovery.
    dm_entry_old = """  if (!text) {
    expected_ack = 0;
    est_timeout = 0;
    return MSG_SEND_FAILED;
  }

  mesh::Packet* held[COMPANION_TEXT_QUEUE_CAPACITY] = {};
"""
    dm_entry_new = """  if (!text) {
    expected_ack = 0;
    est_timeout = 0;
    return MSG_SEND_FAILED;
  }

#if defined(MESH_OFFGRIDNL_V27)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif

  mesh::Packet* held[COMPANION_TEXT_QUEUE_CAPACITY] = {};
"""
    if mesh_cpp_text.count(dm_entry_old) != 1:
        fail("V27 global-first send entry anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(dm_entry_old, dm_entry_new, 1)

    # V11 considered an Internet RAM-queue acceptance equivalent to a send.
    # V27 keeps the UI honest: if RF failed, the global path only counts when
    # it was published immediately. Hidden delayed delivery is never reported
    # as a successful send. If RF succeeded, a bounded global mirror may queue.
    dm_old = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text)
          : false;"""
    dm_new = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text,
                                       result != MSG_SEND_FAILED)
          : false;"""
    if mesh_cpp_text.count(dm_old) != 1:
        fail("V27 DM honest-send-state anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(dm_old, dm_new, 1)

    # Mirror every locally-originated group text at the single shared MeshCore
    # choke point used by touch UI, companion apps and other senders.
    group_send_old = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
"""
    group_send_new = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
#if defined(MESH_OFFGRIDNL_V27)
  if (pkt && pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT) {
    v11_global_bridge.mirrorChannelPacket(channel, pkt);
  }
#endif
"""
    if mesh_cpp_text.count(group_send_old) != 1:
        fail("V27 global channel send anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(group_send_old, group_send_new, 1)

    group_rx_old = """void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  // Clock bootstrap"""
    group_rx_new = """void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
#if defined(MESH_OFFGRIDNL_V27)
  // If the same logical channel post arrives by Internet and RF, keep one
  // bubble in the one shared conversation. Global injection bypasses this
  // check because the bridge already recorded its keyed message ID.
  if (!s_v27_global_channel_inject &&
      v11_global_bridge.noteLoRaChannel(channel, timestamp, text)) {
    return;
  }
#endif
  // Clock bootstrap"""
    if mesh_cpp_text.count(group_rx_old) != 1:
        fail("V27 global channel receive anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(group_rx_old, group_rx_new, 1)

    mesh_path.write_text(mesh_cpp_text)

    # Replace only the Internet bridge implementation. MyMesh chat, V26 RF and
    # the P1 V8-compatible radio path remain untouched.
    here = pathlib.Path(__file__).resolve().parent
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # Existing hybrid DM behavior remains, but its network-side wire format is
    # now V27 Privacy Pro.
    joined = "\n".join((main_path.read_text(), mesh_path.read_text(),
                         mesh_h_path.read_text(), bridge_h.read_text(), bridge_cpp.read_text()))
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.noteLoRaDM",
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
        "MOG27-DM-KEY",
        "MOG27-DM-ROUTE",
        "PLAIN_LEN = 32 + 64 + 4 + 2 + MAX_TEXT",
        "v27GetContactCount",
        "v27GetContactByIndex",
        "v27CalcSharedSecretCached",
        "v27GetChannelByIndex",
        "v27SignGlobal",
        "v27InjectGlobalChannel",
        "mirrorChannelPacket",
        "noteLoRaChannel",
        "MOG27-CH-KEY",
        "MOG27-CH-SIGN",
        "[V27][wifi] profile=1 standard",
        "[V27][wifi] profile=2 modern-transition",
        "[V27][wifi] profile=3 legacy-ht20",
        "[V27][wifi] profile=4 eu13-mesh-rescue",
    ):
        if marker not in joined:
            fail("hybrid/privacy baseline missing " + marker)

    print("V27 applied: zero-config Privacy Pro global DM + channels + V26/P1-V8 RF compatibility preserved")

if __name__ == "__main__":
    main()
