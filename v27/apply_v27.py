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
    mesh_path = root / "src/MyMesh.cpp"
    mesh_h_path = root / "src/MyMesh.h"
    bridge_h = root / "src/helpers/esp32/V11GlobalBridge.h"
    bridge_cpp = root / "src/helpers/esp32/V11GlobalBridge.cpp"

    for p in (pio_path, main_path, mesh_path, mesh_h_path, bridge_h, bridge_cpp):
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
        "  -D V27_ZERO_CONFIG=1\n",
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
    ):
        if marker not in joined:
            fail("hybrid/privacy baseline missing " + marker)

    print("V27 applied: zero-config Privacy Pro global DM + channels + V26/P1-V8 RF compatibility preserved")

if __name__ == "__main__":
    main()
