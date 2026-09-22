#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys


def fail(message: str) -> None:
    raise SystemExit("V11 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_section(text: str, header: str, next_header: str,
                  old: str, new: str, label: str) -> str:
    start = text.find(header)
    if start < 0:
        fail(f"{label}: section start missing")
    end = text.find(next_header, start + len(header))
    if end < 0:
        end = len(text)
    section = text[start:end]
    patched = replace_once(section, old, new, label)
    return text[:start] + patched + text[end:]


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v11.py <wadamesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    if not (root / "src/MyMesh.cpp").exists():
        fail("target is not a WadaMesh checkout")

    # Copy additive V11 source files first.
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # Phase 1: T-Deck build flags. Preserve the upstream radio profile and make
    # the hardware ceiling explicit. No hidden over-power path is added.
    p = root / "platformio.ini"
    s = p.read_text()
    s = patch_section(
        s,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D WADAMESH_FORK_BUILD=1\n",
        "  -D WADAMESH_FORK_BUILD=1\n"
        "  -D MESH_OFFGRIDNL_V11=1\n"
        "  -D MAX_LORA_TX_POWER=22\n"
        "  -D V11_GLOBAL_BROKER='\\\"broker.emqx.io\\\"'\n"
        "  -D V11_GLOBAL_PORT=1883\n",
        "T-Deck build flags",
    )
    p.write_text(s)

    # Phase 2: expose only the narrow identity/contact/UI hooks the bridge needs.
    p = root / "src/MyMesh.h"
    s = p.read_text()
    anchor = """  int sendCommandData(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt,
                      const char* text, uint32_t& est_timeout,
                      uint32_t* out_packet_hash4 = nullptr, TxtTxDebugInfo* out_dbg = nullptr);

"""
    replacement = anchor + """#if defined(MESH_OFFGRIDNL_V11)
  bool v11CalcSharedSecret(const uint8_t peerPub[32], uint8_t out[32]);
  bool v11LookupChatContact(const uint8_t pub[32], ContactInfo& out);
  void v11InjectGlobalDm(const uint8_t senderPub[32], uint32_t timestamp, const char* text);
#endif

"""
    s = replace_once(s, anchor, replacement, "MyMesh public V11 hooks")
    p.write_text(s)

    p = root / "src/MyMesh.cpp"
    s = p.read_text()
    s = replace_once(
        s,
        '#include "MyMesh.h"\n',
        '#include "MyMesh.h"\n'
        '#if defined(MESH_OFFGRIDNL_V11)\n'
        '#include "helpers/esp32/V11GlobalBridge.h"\n'
        '#endif\n',
        "MyMesh V11 include",
    )

    send_def = "int MyMesh::sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt,\n"
    pos = s.find(send_def)
    if pos < 0:
        fail("sendMessage definition missing")

    helpers = r'''#if defined(MESH_OFFGRIDNL_V11)
bool MyMesh::v11CalcSharedSecret(const uint8_t peerPub[32], uint8_t out[32]) {
  if (!peerPub || !out) return false;
  self_id.calcSharedSecret(out, peerPub);
  return true;
}

bool MyMesh::v11LookupChatContact(const uint8_t pub[32], ContactInfo& out) {
  if (!pub) return false;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  out = *c;
  return true;
}

void MyMesh::v11InjectGlobalDm(const uint8_t senderPub[32],
                               uint32_t timestamp,
                               const char* text) {
  if (!senderPub || !text || !_ui) return;

  ContactInfo* c = lookupContactByPubKey(senderPub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return;

  if (timestamp > 1700000000UL && timestamp < 2000000000UL &&
      getRTCClock()->getCurrentTime() < 1700000000UL) {
    getRTCClock()->setCurrentTime(timestamp);
  }

  _ui->notify(UIEventType::contactMessage);
  _last_sender_ts = timestamp;
  _ui->newMsgFromPub(0xFF, c->id.pub_key, c->name, text, history_count);
}
#endif

'''
    s = s[:pos] + helpers + s[pos:]

    old_tail = """  if (out_packet_hash4) *out_packet_hash4 = packet_hash4;
  if (result != MSG_SEND_FAILED) {
    _last_plain_tx_ack = expected_ack;
    memcpy(_last_plain_tx_retry_key, retry_key, sizeof(_last_plain_tx_retry_key));
    mesh::Utils::sha256(_last_plain_tx_fingerprint,
                        sizeof(_last_plain_tx_fingerprint),
                        recipient.id.pub_key, PUB_KEY_SIZE,
                        reinterpret_cast<const uint8_t*>(text), strlen(text));
    _last_plain_tx_meta_valid = true;
  }
  return result;
}

int MyMesh::sendCommandData"""
    new_tail = """  if (out_packet_hash4) *out_packet_hash4 = packet_hash4;
  if (result != MSG_SEND_FAILED) {
    _last_plain_tx_ack = expected_ack;
    memcpy(_last_plain_tx_retry_key, retry_key, sizeof(_last_plain_tx_retry_key));
    mesh::Utils::sha256(_last_plain_tx_fingerprint,
                        sizeof(_last_plain_tx_fingerprint),
                        recipient.id.pub_key, PUB_KEY_SIZE,
                        reinterpret_cast<const uint8_t*>(text), strlen(text));
    _last_plain_tx_meta_valid = true;
  }

#if defined(MESH_OFFGRIDNL_V11)
  // One logical send, two independent routes. MeshCore/LoRa remains the
  // compatibility path; Wi-Fi adds a worldwide encrypted copy for V11 peers.
  // Do this only on the first semantic attempt so retry trains cannot multiply
  // the internet copy.
  const bool v11_global_ok =
      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text)
          : false;

  // If the radio queue is unavailable but Wi-Fi accepted the encrypted message,
  // report an accepted send to the UI without inventing a MeshCore ACK.
  if (result == MSG_SEND_FAILED && v11_global_ok) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif

  return result;
}

int MyMesh::sendCommandData"""
    s = replace_once(s, old_tail, new_tail, "sendMessage global mirror")

    queue_anchor = """  if (sender_timestamp > 1700000000UL && sender_timestamp < 2000000000UL &&
      getRTCClock()->getCurrentTime() < 1700000000UL) {
    getRTCClock()->setCurrentTime(sender_timestamp);
  }
  int i = 0;
"""
    queue_replacement = """  if (sender_timestamp > 1700000000UL && sender_timestamp < 2000000000UL &&
      getRTCClock()->getCurrentTime() < 1700000000UL) {
    getRTCClock()->setCurrentTime(sender_timestamp);
  }
#if defined(MESH_OFFGRIDNL_V11)
  const bool v11_suppress_ui =
      (txt_type == TXT_TYPE_PLAIN)
          ? v11_global_bridge.noteLoRaDM(from.id.pub_key, sender_timestamp, text)
          : false;
#else
  const bool v11_suppress_ui = false;
#endif
  int i = 0;
"""
    s = replace_once(s, queue_anchor, queue_replacement, "LoRa/global dedup marker")
    s = replace_once(
        s,
        "  if (should_display && _ui) {\n",
        "  if (should_display && _ui && !v11_suppress_ui) {\n",
        "LoRa/global duplicate UI suppression",
    )
    p.write_text(s)

    # Phase 3: lifecycle. Start after prefs exist; service once per loop.
    p = root / "src/main.cpp"
    s = p.read_text()
    s = replace_once(
        s,
        '    #include "helpers/esp32/MqttBridge.h"\n',
        '    #include "helpers/esp32/MqttBridge.h"\n'
        '    #if defined(MESH_OFFGRIDNL_V11)\n'
        '      #include "helpers/esp32/V11GlobalBridge.h"\n'
        '    #endif\n',
        "main V11 include",
    )

    s = replace_once(
        s,
        '  wifiConfigBegin();\n  Serial.println("[BOOT] wifiConfig ok");\n',
        '  wifiConfigBegin();\n'
        '  Serial.println("[BOOT] wifiConfig ok");\n'
        '#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)\n'
        '  v11_global_bridge.begin(&the_mesh);\n'
        '#endif\n',
        "main V11 begin",
    )

    loop_anchor = """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("mqtt", mqtt_bridge.loop());
#else
  mqtt_bridge.loop();
#endif
#endif
"""
    loop_replacement = loop_anchor + """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("v11-global", v11_global_bridge.loop());
#else
  v11_global_bridge.loop();
#endif
#endif
"""
    s = replace_once(s, loop_anchor, loop_replacement, "main V11 loop")
    p.write_text(s)

    # Fast pre-build checks. Upstream drift fails here instead of wasting the
    # single runner on a doomed PlatformIO build.
    required = {
        "platformio.ini": [
            "MESH_OFFGRIDNL_V11=1",
            "MAX_LORA_TX_POWER=22",
            "LORA_TX_POWER=22",
            "SX126X_DIO2_AS_RF_SWITCH=true",
            "SX126X_RX_BOOSTED_GAIN=1",
        ],
        "src/MyMesh.cpp": [
            "v11_global_bridge.mirrorDM",
            "v11_global_bridge.noteLoRaDM",
            "v11InjectGlobalDm",
        ],
        "src/main.cpp": [
            "v11_global_bridge.begin",
            'STALL_SCOPE("v11-global"',
        ],
        "src/helpers/esp32/V11GlobalBridge.cpp": [
            "mbedtls_gcm_crypt_and_tag",
            "mbedtls_gcm_auth_decrypt",
            "recipient.type != ADV_TYPE_CHAT",
        ],
    }

    for rel, markers in required.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("V11 overlay applied successfully")


if __name__ == "__main__":
    main()
