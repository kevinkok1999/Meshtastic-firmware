#!/usr/bin/env python3
from pathlib import Path
import shutil, sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_v7.py <saitama_checkout> <overlay_dir>")

root = Path(sys.argv[1]).resolve()
overlay = Path(sys.argv[2]).resolve()

def replace_once(path, old, new):
    p = root / path
    s = p.read_text()
    if old not in s:
        raise SystemExit(f"patch anchor missing in {path}: {old[:80]!r}")
    p.write_text(s.replace(old, new, 1))

# Add the V7 transport implementation.
dst = root / "src/mesh"
shutil.copy2(overlay / "HybridTransport.h", dst / "HybridTransport.h")
shutil.copy2(overlay / "HybridTransport.cpp", dst / "HybridTransport.cpp")

# MeshService: include and singleton.
replace_once("src/mesh/MeshService.cpp",
    '#include "Fhss.h"\n',
    '#include "Fhss.h"\n#include "HybridTransport.h"\n')

replace_once("src/mesh/MeshService.cpp",
    'static SimpleMeshTables          mesh_tables;\n',
    'static SimpleMeshTables          mesh_tables;\nstatic HybridTransport              hybrid_transport;\n')

# Cross-transport duplicate suppression on the normal LoRa receive path.
replace_once("src/mesh/MeshService.cpp",
    '    void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {\n        _upsertPeer(from);',
    '    void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {\n'
    '        if (!hybrid_transport.acceptApplicationMessage(from.id.pub_key, text, false)) {\n'
    '            OPS_LOG("Hybrid", "Dropped cross-transport duplicate from %s", from.name);\n'
    '            return;\n'
    '        }\n'
    '        _upsertPeer(from);')

# Add a public injection point into the existing Saitama receive/UI queue.
anchor = '''    bool dequeueRx(RxMessage& out) {
        if (_rxCount == 0) return false;
        out = _rxBuf[_rxHead];
        _rxHead = (_rxHead + 1) % RX_QUEUE_SIZE;
        _rxCount--;
        return true;
    }
'''
insert = anchor + '''
    void injectHybridMessage(const uint8_t* pubKey32, const char* name,
                             uint32_t timestamp, const char* text) {
        if (!pubKey32 || !text || !text[0]) return;
        if (!hybrid_transport.acceptApplicationMessage(pubKey32, text, true)) {
            OPS_LOG("Hybrid", "Dropped ESP-NOW/LoRa duplicate");
            return;
        }

        RxMessage msg{};
        msg.timestamp = timestamp;
        strncpy(msg.senderName, (name && name[0]) ? name : "V7 peer", 31);
        strncpy(msg.text, text, 159);
        msg.isDirect = true;
        msg.hops = 0;
        msg.rssi = 0;
        msg.snr = 0;
        memcpy(msg.pubKeyPrefix, pubKey32, 4);
        strncpy(msg.pathStr, "ESP-NOW-LR", sizeof(msg.pathStr)-1);
        _enqueueRx(msg);

        for (int i = 0; i < _peerCount; i++) {
            if (memcmp(_peers[i].pubKeyPrefix, pubKey32, 4) == 0) {
                if (name && name[0]) strncpy(_peers[i].name, name, 31);
                memcpy(_peers[i].pubKey, pubKey32, 32);
                _peers[i].lastSeen = getRTCClock()->getCurrentTime();
                _peerSerial++;
                return;
            }
        }
        if (_peerCount < MAX_PEERS) {
            PeerInfo& p = _peers[_peerCount++];
            memset(&p, 0, sizeof(p));
            strncpy(p.name, (name && name[0]) ? name : "V7 peer", 31);
            memcpy(p.pubKeyPrefix, pubKey32, 4);
            memcpy(p.pubKey, pubKey32, 32);
            p.type = 1;
            p.lastSeen = getRTCClock()->getCurrentTime();
            _peerSerial++;
            _autoAddPeer(p);
        }
    }
'''
replace_once("src/mesh/MeshService.cpp", anchor, insert)

# Start ESP-NOW only after the MeshCore identity is loaded.
replace_once("src/mesh/MeshService.cpp",
    '    _initialized = true;\n    OPS_LOG("Mesh", "MeshCore ready");',
    '    _initialized = true;\n'
    '    uint8_t v7Pub[32], v7Prv[64];\n'
    '    the_mesh.getSelfPubKey(v7Pub);\n'
    '    the_mesh.getSelfPrvKey(v7Prv);\n'
    '    if (hybrid_transport.begin(v7Pub, v7Prv, cfg.callsign[0] ? cfg.callsign : "OMS-NODE"))\n'
    '        OPS_LOG("Hybrid", "ESP-NOW LR ready");\n'
    '    else\n'
    '        OPS_LOG("Hybrid", "ESP-NOW LR unavailable; LoRa fallback remains active");\n'
    '    OPS_LOG("Mesh", "MeshCore ready");')

# Drain ESP-NOW messages and perform delayed LoRa fallback in the normal loop.
replace_once("src/mesh/MeshService.cpp",
    '    the_mesh.checkSerialInterface();\n    _tickFhss();',
    '    the_mesh.checkSerialInterface();\n'
    '    hybrid_transport.tick();\n'
    '    HybridRxMessage hmsg;\n'
    '    int hybridDrain = 0;\n'
    '    while (hybridDrain++ < 4 && hybrid_transport.dequeue(hmsg))\n'
    '        the_mesh.injectHybridMessage(hmsg.senderPubKey, hmsg.senderName, hmsg.timestamp, hmsg.text);\n'
    '    HybridFallback fb;\n'
    '    if (hybrid_transport.pollFallback(fb)) {\n'
    '        OPS_LOG("Hybrid", "ACK timeout; falling back to MeshCore/LoRa");\n'
    '        the_mesh.sendDirectMsg(fb.destPrefix, fb.text);\n'
    '    }\n'
    '    _tickFhss();')

# Prevent a successful hybrid send from inheriting a stale MeshCore ACK id in the UI.
replace_once("src/mesh/MeshService.cpp",
    '    bool getPeerInfo(int idx, PeerInfo& out) const {',
    '    void clearLastExpectedAck() { _lastExpectedAck = 0; }\\n\\n'
    '    bool getPeerInfo(int idx, PeerInfo& out) const {')

# AUTO routing: verified V7 peer first, normal MeshCore/LoRa otherwise.
replace_once("src/mesh/MeshService.cpp",
    'bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {\n'
    '    return _initialized && the_mesh.sendDirectMsg(pubKeyPrefix4, text);\n'
    '}',
    'bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {\n'
    '    if (!_initialized) return false;\n'
    '    if (hybrid_transport.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) {\n'
    '        the_mesh.clearLastExpectedAck();\n'
    '        OPS_LOG("Hybrid", "DM queued over ESP-NOW LR");\n'
    '        return true;\n'
    '    }\n'
    '    return the_mesh.sendDirectMsg(pubKeyPrefix4, text);\n'
    '}')

# Brand the derivative release as Version 7 while keeping upstream provenance in docs.
v = root / "src/version.h"
v.write_text('''#pragma once
#define OPS_VERSION_MAJOR 7
#define OPS_VERSION_MINOR 0
#define OPS_VERSION_PATCH 0
#define OPS_VERSION_PRE   ""
#define OPS_VERSION_STRING "7.0.0"
''')

print("V7 overlay applied successfully")
