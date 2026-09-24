#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def die(msg: str) -> None:
    raise SystemExit("V27 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <V27-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    main_src = (root / "src/main.cpp").read_text()
    mesh_cpp = (root / "src/MyMesh.cpp").read_text()
    mesh_h = (root / "src/MyMesh.h").read_text()
    bridge_h = (root / "src/helpers/esp32/V11GlobalBridge.h").read_text()
    bridge_cpp = (root / "src/helpers/esp32/V11GlobalBridge.cpp").read_text()

    # V27 must remain a strict extension of the proven chain.
    for flag in (
        "MESH_OFFGRIDNL_V11=1",
        "MESH_OFFGRIDNL_V12=1",
        "MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1",
        "MESH_OFFGRIDNL_V16=1",
        "MESH_OFFGRIDNL_V17=1",
        "MESH_OFFGRIDNL_V18=1",
        "MESH_OFFGRIDNL_V19=1",
        "MESH_OFFGRIDNL_V26=1",
        "MESH_OFFGRIDNL_V27=1",
        "V27_PRIVACY_PRO=1",
        "V27_P1_V8_COMPAT=1",
        "V27_ZERO_CONFIG=1",
        "V27_WIFI_BROAD_COMPAT=1",
        "V27_RELAY_PROFILE_DEV_PUBLIC=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0:
        die("T-Deck target section missing")
    tdeck = pio[start:end]

    # P1 Pro V8 radio contract. CR5 is the WadaMesh/MeshCore default used by
    # this pinned profile; V27 may not introduce an override.
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
            die("P1 V8/T-Deck RF contract missing " + marker)
    if "#define LORA_CR 5" not in mesh_h:
        die("P1 V8/T-Deck RF contract missing CR5 default")

    # V27 must not add a competing radio profile.
    forbidden_rf = (
        "V27_LORA_FREQ",
        "V27_LORA_BW",
        "V27_LORA_SF",
        "V27_LORA_CR",
        "V27_TX_POWER",
    )
    for marker in forbidden_rf:
        if marker in "\n".join((pio, main_src, mesh_h, mesh_cpp, bridge_h, bridge_cpp)):
            die("V27 must not override RF via " + marker)

    # Existing hybrid behavior must survive: one logical DM mirrors to Internet,
    # LoRa remains the compatibility path and duplicate UI delivery is suppressed.
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.noteLoRaDM",
        "v11_global_bridge.begin",
        'STALL_SCOPE("v11-global"',
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
        "v27GetContactCount",
        "v27GetContactByIndex",
        "v27CalcSharedSecretCached",
        "MOG27-DM-KEY",
        "MOG27-DM-ROUTE",
        "MOG27-ID-DM",
        "PLAIN_LEN = 32 + 64 + 4 + 2 + MAX_TEXT",
        "PENDING_CAP = 16",
        "PENDING_TTL_MS",
        "RETRY_MAX_MS = 60000",
        "_mqtt.publish(topic, wire, (unsigned int)MAX_WIRE, false)",
        "mirrorChannelPacket",
        "noteLoRaChannel",
        "v27GetChannelByIndex",
        "v27InjectGlobalChannel",
        "MOG27-CH-KEY",
        "MOG27-ID-CH",
        "MOG27-CH-SIGN",
        "v27SignGlobal",
        "signer.verify(signature, signData",
        "routeTopicForChannel",
        "mesh::Utils::MACThenDecrypt",
        "PENDING_CHANNEL_CAP = 8",
    ):
        if marker not in "\n".join((main_src, mesh_cpp, bridge_h, bridge_cpp)):
            die("global/off-grid compatibility marker missing " + marker)



    # Broad 2.4-GHz compatibility ladder. The ESP32-S3 is physically 2.4 GHz
    # only; V27 broadens supported AP/security behavior rather than pretending
    # firmware can add a 5-GHz RF front-end.
    for marker in (
        "V27_WIFI_BROAD_COMPAT=1",
        "[V27][wifi] profile=1 standard",
        "[V27][wifi] profile=2 modern-transition",
        "[V27][wifi] profile=3 legacy-ht20",
        "[V27][wifi] profile=4 eu13-mesh-rescue",
        "WiFi.setMinSecurity(v27_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);",
        "cfg.sta.threshold.authmode = v27_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;",
        "cfg.sta.threshold.authmode = v27_pwd ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;",
        "cfg.sta.pmf_cfg.capable = true;",
        "cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;",
        "WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N",
        "WIFI_BW_HT20",
        'esp_wifi_set_country_code("NL", false);',
        "wifiConfigGetApHint",
        "cfg.sta.bssid_set = true;",
        "hint_channel >= 1 && hint_channel <= 13",
        "wifiConfigClearApHint();",
        "g_v16_wifi_attempt >= 4",
    ):
        if marker not in pio + "\n" + main_src:
            die("broad Wi-Fi compatibility marker missing " + marker)

    ui = (root / "src/ui-touch/UITask.cpp").read_text()
    for marker in (
        "Connecting... (1/4)",
        "Compatibility... (2/4)",
        "Compatibility... (3/4)",
        "Router recovery... (4/4)",
    ):
        if marker not in ui:
            die("four-profile zero-config Wi-Fi UI marker missing " + marker)

    # Security floor: modern profiles start at WPA2; WPA-only is permitted
    # only inside the explicit legacy compatibility profile. WEP stays off.
    if "WIFI_AUTH_WEP" in main_src:
        die("V27 broad Wi-Fi compatibility must not enable WEP")

    # RC2 secure transport experiment: server-authenticated TLS only, and
    # the public broker profile must be unmistakably development-only.
    if "#include <WiFiClientSecure.h>" not in bridge_h:
        die("V27 global relay must use WiFiClientSecure")
    if "WiFiClientSecure _wc;" not in bridge_h:
        die("V27 global relay client must be the secure client")
    for marker in (
        '#define V27_GLOBAL_PORT 8883',
        "V27_EMQX_ROOT_CA",
        "_wc.setCACert(V27_EMQX_ROOT_CA);",
        "DigiCert Global Root G2",
        "RC2 public relay bridge is development-only",
        "[V27][DEV] Public TLS relay transport ready (not production)",
    ):
        if marker not in bridge_cpp:
            die("verified TLS transport marker missing " + marker)
    for forbidden in (
        "#define V27_GLOBAL_PORT 1883",
        "setInsecure(",
        "WiFiClient _wc;",
    ):
        if forbidden in bridge_h + "\n" + bridge_cpp:
            die("insecure relay transport forbidden: " + forbidden)

    # V26 RF guard remains untouched.
    for marker in (
        "V26_RF_EVAL_MS = 15000",
        "radio_driver.resetAGC();",
        "radio_driver.triggerNoiseFloorCalibrate(0);",
    ):
        if marker not in main_src:
            die("V26 RF guard missing " + marker)

    # Privacy Pro envelope invariants: exact timestamp, full sender key and exact
    # text length stay inside fixed-size authenticated ciphertext.
    if "memcpy(wire + 14, _selfPub, 8)" in bridge_cpp:
        die("Privacy Pro must not expose a sender public-key prefix in the relay header")
    if "esp_fill_random(wire + 14, 8)" not in bridge_cpp:
        die("Privacy Pro relay header identity-hint bytes must be randomized")
    if "MOG27-DM-ROUTE" not in bridge_cpp or "deriveDmKey(pub, pairKey)" not in bridge_cpp:
        die("DM relay route must derive from the pair-wise shared key")
    if "v27CalcSharedSecretCached(peerPub, shared)" not in bridge_cpp:
        die("pair-wise DM routing must reuse the main-thread contact shared-secret cache")
    if "for (int i = 0; i < 16; ++i)" not in bridge_cpp:
        die("DM opaque route token must remain 128 bits")
    if "_dmTopic" in bridge_cpp or "_dmTopic" in bridge_h:
        die("single public-key-derived DM inbox must not return")
    ch_route_start = bridge_cpp.find("void V11GlobalBridge::routeTopicForChannel")
    ch_route_end = bridge_cpp.find("bool V11GlobalBridge::deriveDmKey", ch_route_start)
    if ch_route_start < 0 or ch_route_end < 0:
        die("group route function missing")
    ch_route = bridge_cpp[ch_route_start:ch_route_end]
    if "char tag[33]" not in ch_route or "for (int i = 0; i < 16; ++i)" not in ch_route:
        die("group opaque route token must remain 128 bits")
    if "findContactForTopic(topic, contact)" not in bridge_cpp:
        die("DM receive must map pair-wise secret route back to a local contact")
    if "memcpy(plain, _selfPub, 32)" not in bridge_cpp:
        die("Privacy Pro full sender identity must stay inside ciphertext")
    if "memcpy(plain + 32, &timestamp, 4)" not in bridge_cpp:
        die("Privacy Pro timestamp must stay inside ciphertext")
    if "wire[62]" in bridge_cpp or "wire[63]" in bridge_cpp:
        die("legacy plaintext exact-length fields leaked into V27 wire header")
    if 'Serial.printf("[V11] Global DM published bytes=' in bridge_cpp:
        die("legacy per-message metadata logging leaked into V27")
    if 'broker=%s' in bridge_cpp:
        die("broker/route metadata must not be printed in normal V27 logs")
    if "return allowQueue ? enqueue(recipient.id.pub_key, timestamp, text) : false;" not in bridge_cpp:
        die("zero-config DM mirror must only queue when the accepted RF path allows it")
    if "result != MSG_SEND_FAILED" not in mesh_cpp or "mirrorDM(recipient, timestamp, text," not in mesh_cpp:
        die("DM UI send state must not hide total RF+Internet failure behind a RAM queue")
    if "return enqueueChannel(channel.secret, timestamp, text);" not in bridge_cpp:
        die("zero-config offline global channel mirror queue missing")
    if 'snprintf(out, outCap, "mog27/v2/r/%s", tag);' not in bridge_cpp:
        die("DM/channel relay topics must share the opaque V27 routing namespace")
    if 'snprintf(out, outCap, "mog27/v2/d/%s", tag);' in bridge_cpp:
        die("legacy DM-labelled routing namespace leaks message class")
    if "seenOrRemember(msgId);" not in bridge_cpp:
        die("global channel self-echo suppression missing")
    if "v11_global_bridge.noteLoRaChannel(channel, timestamp, text)" not in mesh_cpp:
        die("RF/global channel dedup hook missing")
    if "v11_global_bridge.mirrorChannelPacket(channel, pkt)" not in mesh_cpp:
        die("shared channel send choke-point mirror missing")
    if "memcpy(plain, secret" in bridge_cpp or "memcpy(wire, secret" in bridge_cpp:
        die("channel secret must never be copied into relay payloads")
    if "memcpy(plain, _selfPub, 32)" not in bridge_cpp or "memcpy(plain + 32, signature, SIGNATURE_SIZE)" not in bridge_cpp:
        die("global channel sender identity/signature must remain encrypted in the fixed envelope")
    if "const bool signatureOk = signer.verify(signature, signData" not in bridge_cpp:
        die("global channel post must verify Ed25519 sender signature before UI insertion")

    if "subscribeContacts()" in bridge_cpp or "subscribeChannels()" in bridge_cpp:
        die("background connect path must not bulk-walk contact/channel tables")
    for marker in (
        "bool V11GlobalBridge::subscribeStep()",
        "void V11GlobalBridge::resetSubscriptions()",
        "if (!_subscriptionsReady)",
        "const uint32_t idx = _subContactIdx++",
        "const uint8_t idx = _subChannelIdx++",
    ):
        if marker not in bridge_cpp:
            die("incremental main-loop subscription guard missing " + marker)

    for marker in (
        "RX_RATE_WINDOW_MS = 10000",
        "RX_RATE_MAX_PER_WINDOW = 100",
        "static_assert(MAX_WIRE == 312",
        "static_assert(MAX_WIRE < 400",
        "bool V11GlobalBridge::allowInbound()",
        "if (!allowInbound()) return;",
    ):
        if marker not in bridge_h + "\n" + bridge_cpp:
            die("inbound abuse/resource guard missing " + marker)

    prefs = (root / "src/helpers/esp32/TouchPrefsStore.cpp").read_text()
    # Low-level MQTT remains hidden from the normal user surface by default.
    if "APPHIDE_MQTT" not in prefs or "app_hide" not in prefs:
        die("zero-config UX lost the hidden-by-default MQTT guard")
    # Keep the legacy unsandboxed WadaMesh open-auto-join path disabled.
    # Final V27 opportunistic open Wi-Fi is a separate sandboxed module and
    # must never be implemented by flipping this legacy preference.
    if "c.boot_wifi_open    = 0" not in prefs:
        die("legacy unsandboxed open Wi-Fi auto-join must remain disabled")

    print("V27 contracts OK: V26 + P1 V8 preserved, zero-config global DM/channels, fixed-size Privacy Pro envelopes and cross-transport dedup preserved")

if __name__ == "__main__":
    main()
