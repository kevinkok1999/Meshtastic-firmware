#include "HybridTransport.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <Identity.h>
#include <Utils.h>
#define ED25519_NO_SEED 1
#include <ed_25519.h>

namespace ops {

HybridTransport* HybridTransport::_instance = nullptr;

static constexpr uint8_t MAGIC0 = 0x4D; // M
static constexpr uint8_t MAGIC1 = 0x37; // 7

static uint32_t read32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void write32(uint8_t* p, uint32_t v) {
    memcpy(p, &v, sizeof(v));
}

bool HybridTransport::begin(const uint8_t selfPub[32], const uint8_t selfPrv[64], const char* callsign) {
    if (_initialized) return true;
    memcpy(_selfPub, selfPub, 32);
    memcpy(_selfPrv, selfPrv, 64);
    strncpy(_callsign, callsign && callsign[0] ? callsign : "V7-NODE", 16);
    _callsign[16] = 0;

    WiFi.mode(WIFI_STA);
    delay(20);

    if (esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
    // Espressif Long Range mode. V7 traffic is intentionally ESP32-to-ESP32.
    if (esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR) != ESP_OK) {
        return false;
    }
    esp_wifi_set_max_tx_power(80); // quarter-dBm units => 20 dBm

    if (esp_now_init() != ESP_OK) return false;

    _instance = this;
    esp_now_register_recv_cb(onRecvStatic);
    esp_now_register_send_cb(onSendStatic);

    const uint8_t bc[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    if (!ensurePeer(bc)) {
        esp_now_deinit();
        _instance = nullptr;
        return false;
    }

    _initialized = true;
    _lastHelloMs = millis() - HELLO_INTERVAL_MS;
    sendHello();
    return true;
}

void HybridTransport::end() {
    if (!_initialized) return;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    _initialized = false;
    _instance = nullptr;
}

void HybridTransport::onRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
    if (_instance) _instance->onRecv(mac, data, len);
}

void HybridTransport::onSendStatic(const uint8_t*, esp_now_send_status_t) {
    // Link-layer completion is deliberately not treated as delivery success.
    // Delivery is acknowledged by a signed-identity/ECDH-protected TYPE_ACK.
}

void HybridTransport::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (!mac || !data || len <= 0 || len > (int)MAX_FRAME) {
        _stats.invalidFrames++;
        return;
    }

    portENTER_CRITICAL(&_mux);
    if (_rawCount >= RAW_QUEUE) {
        _stats.queueDrops++;
        portEXIT_CRITICAL(&_mux);
        return;
    }
    RawFrame& slot = _raw[_rawTail];
    memcpy(slot.mac, mac, 6);
    slot.len = (uint8_t)len;
    memcpy(slot.data, data, len);
    _rawTail = (uint8_t)((_rawTail + 1) % RAW_QUEUE);
    _rawCount++;
    portEXIT_CRITICAL(&_mux);
}

bool HybridTransport::magicOk(const uint8_t* p, int len) {
    return len >= 4 && p[0] == MAGIC0 && p[1] == MAGIC1 && p[2] == PROTO_VERSION;
}

void HybridTransport::tick() {
    if (!_initialized) return;

    for (int n = 0; n < RAW_QUEUE; n++) {
        RawFrame f;
        bool have = false;
        portENTER_CRITICAL(&_mux);
        if (_rawCount) {
            f = _raw[_rawHead];
            _rawHead = (uint8_t)((_rawHead + 1) % RAW_QUEUE);
            _rawCount--;
            have = true;
        }
        portEXIT_CRITICAL(&_mux);
        if (!have) break;
        processFrame(f);
    }

    const uint32_t now = millis();
    if ((uint32_t)(now - _lastHelloMs) >= HELLO_INTERVAL_MS) sendHello();

    if (_pending.active && (uint32_t)(now - _pending.sentAtMs) >= ACK_TIMEOUT_MS) {
        if (_pending.attempts < MAX_ATTEMPTS) {
            if (ensurePeer(_pending.peerMac) &&
                esp_now_send(_pending.peerMac, _pending.frame, _pending.frameLen) == ESP_OK) {
                _pending.attempts++;
                _pending.sentAtMs = now;
                _stats.retries++;
                _stats.dataTx++;
            } else {
                scheduleFallback();
            }
        } else {
            scheduleFallback();
        }
    }
}

void HybridTransport::processFrame(const RawFrame& f) {
    if (!magicOk(f.data, f.len)) {
        _stats.invalidFrames++;
        return;
    }
    switch (f.data[3]) {
        case TYPE_HELLO: processHello(f); break;
        case TYPE_DATA:  processData(f);  break;
        case TYPE_ACK:   processAck(f);   break;
        default: _stats.invalidFrames++; break;
    }
}

bool HybridTransport::ensurePeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = WIFI_CHANNEL;
    pi.encrypt = false; // application payload is ECDH/AES/HMAC protected
    return esp_now_add_peer(&pi) == ESP_OK;
}

HybridTransport::Peer* HybridTransport::upsertPeer(const uint8_t mac[6], const uint8_t pub[32],
                                                    const char* name, bool verified) {
    Peer* freeSlot = nullptr;
    for (auto& p : _peers) {
        if (p.used && (memcmp(p.pub, pub, 32) == 0 || memcmp(p.mac, mac, 6) == 0)) {
            memcpy(p.mac, mac, 6);
            memcpy(p.pub, pub, 32);
            if (name && name[0]) {
                strncpy(p.name, name, 16);
                p.name[16] = 0;
            }
            p.verified = p.verified || verified;
            p.lastSeenMs = millis();
            ensurePeer(mac);
            return &p;
        }
        if (!p.used && !freeSlot) freeSlot = &p;
    }
    if (!freeSlot) return nullptr;
    freeSlot->used = true;
    freeSlot->verified = verified;
    memcpy(freeSlot->mac, mac, 6);
    memcpy(freeSlot->pub, pub, 32);
    if (name) {
        strncpy(freeSlot->name, name, 16);
        freeSlot->name[16] = 0;
    }
    freeSlot->lastSeenMs = millis();
    ensurePeer(mac);
    return freeSlot;
}

HybridTransport::Peer* HybridTransport::findPeerByPrefix(const uint8_t prefix[4]) {
    const uint32_t now = millis();
    for (auto& p : _peers) {
        if (p.used && p.verified && memcmp(p.pub, prefix, 4) == 0 &&
            (uint32_t)(now - p.lastSeenMs) <= PEER_TTL_MS) return &p;
    }
    return nullptr;
}

void HybridTransport::sharedSecret(const uint8_t peerPub[32], uint8_t out[32]) const {
    ed25519_key_exchange(out, peerPub, _selfPrv);
}

void HybridTransport::sendHello() {
    if (!_initialized) return;
    uint8_t frame[122] = {};
    frame[0] = MAGIC0; frame[1] = MAGIC1; frame[2] = PROTO_VERSION; frame[3] = TYPE_HELLO;

    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    memcpy(&frame[4], mac, 6);
    memcpy(&frame[10], _selfPub, 32);
    memcpy(&frame[42], _callsign, 16);

    ed25519_sign(&frame[58], frame, 58, _selfPub, _selfPrv);

    const uint8_t bc[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    if (ensurePeer(bc) && esp_now_send(bc, frame, sizeof(frame)) == ESP_OK) {
        _stats.helloTx++;
        _lastHelloMs = millis();
    }
}

void HybridTransport::processHello(const RawFrame& f) {
    if (f.len != 122) { _stats.invalidFrames++; return; }
    if (memcmp(&f.data[4], f.mac, 6) != 0) { _stats.invalidFrames++; return; }
    const uint8_t* pub = &f.data[10];
    if (memcmp(pub, _selfPub, 32) == 0) return;

    mesh::Identity identity(pub);
    if (!identity.verify(&f.data[58], f.data, 58)) {
        _stats.invalidFrames++;
        return;
    }

    char name[17] = {};
    memcpy(name, &f.data[42], 16);
    upsertPeer(f.mac, pub, name, true);
    _stats.helloRx++;
}

bool HybridTransport::sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp) {
    if (!_initialized || !text || !text[0] || _pending.active) return false;
    Peer* peer = findPeerByPrefix(destPrefix);
    if (!peer) return false;

    size_t textLen = strnlen(text, 150);
    uint32_t msgId = _nextMessageId++;
    if (_nextMessageId == 0) _nextMessageId = 1;

    uint8_t plain[160] = {};
    write32(&plain[0], timestamp);
    write32(&plain[4], msgId);
    plain[8] = (uint8_t)textLen;
    memcpy(&plain[9], text, textLen);
    const int plainLen = 9 + (int)textLen;

    uint8_t secret[32];
    sharedSecret(peer->pub, secret);

    uint8_t enc[180] = {};
    int encLen = mesh::Utils::encryptThenMAC(secret, enc, plain, plainLen);
    if (encLen <= 0 || encLen > 180) return false;

    uint8_t frame[MAX_FRAME] = {};
    frame[0] = MAGIC0; frame[1] = MAGIC1; frame[2] = PROTO_VERSION; frame[3] = TYPE_DATA;
    memcpy(&frame[4], _selfPub, 32);
    memcpy(&frame[36], destPrefix, 4);
    write32(&frame[40], msgId);
    frame[44] = (uint8_t)encLen;
    memcpy(&frame[45], enc, encLen);
    const int frameLen = 45 + encLen;
    if (frameLen > (int)MAX_FRAME) return false;

    if (!ensurePeer(peer->mac) || esp_now_send(peer->mac, frame, frameLen) != ESP_OK) return false;

    _pending.active = true;
    _pending.id = msgId;
    _pending.sentAtMs = millis();
    _pending.attempts = 1;
    memcpy(_pending.destPrefix, destPrefix, 4);
    memcpy(_pending.peerPub, peer->pub, 32);
    memcpy(_pending.peerMac, peer->mac, 6);
    memcpy(_pending.frame, frame, frameLen);
    _pending.frameLen = (uint8_t)frameLen;
    strncpy(_pending.text, text, sizeof(_pending.text)-1);
    _stats.dataTx++;
    return true;
}

void HybridTransport::processData(const RawFrame& f) {
    if (f.len < 63) { _stats.invalidFrames++; return; }
    const uint8_t* srcPub = &f.data[4];
    if (memcmp(&f.data[36], _selfPub, 4) != 0) return;
    const uint32_t msgId = read32(&f.data[40]);
    const uint8_t encLen = f.data[44];
    if (45 + encLen != f.len || encLen < 18) { _stats.invalidFrames++; return; }

    uint8_t secret[32];
    sharedSecret(srcPub, secret);
    uint8_t plain[192] = {};
    int decLen = mesh::Utils::MACThenDecrypt(secret, plain, &f.data[45], encLen);
    if (decLen < 9) { _stats.invalidFrames++; return; }

    const uint32_t timestamp = read32(&plain[0]);
    const uint32_t innerId = read32(&plain[4]);
    const uint8_t textLen = plain[8];
    if (innerId != msgId || textLen > 150 || 9 + textLen > decLen) {
        _stats.invalidFrames++;
        return;
    }

    char text[160] = {};
    memcpy(text, &plain[9], textLen);
    text[textLen] = 0;

    Peer* peer = upsertPeer(f.mac, srcPub, nullptr, true);

    // Message-ID dedup at the transport layer.
    uint8_t idMaterial[36];
    memcpy(idMaterial, srcPub, 32);
    write32(&idMaterial[32], msgId);
    uint8_t idHash[8];
    mesh::Utils::sha256(idHash, sizeof(idHash), idMaterial, sizeof(idMaterial));
    for (auto& r : _recent) {
        if (r.used && memcmp(r.hash, idHash, 8) == 0 &&
            (uint32_t)(millis() - r.seenMs) <= DUP_WINDOW_MS) {
            sendAck(f.mac, srcPub, msgId);
            _stats.duplicateDrops++;
            return;
        }
    }

    HybridRxMessage m;
    m.timestamp = timestamp;
    m.messageId = msgId;
    memcpy(m.senderPubKey, srcPub, 32);
    if (peer && peer->name[0]) strncpy(m.senderName, peer->name, 16);
    else strncpy(m.senderName, "V7 peer", 16);
    strncpy(m.text, text, sizeof(m.text)-1);

    if (queueRx(m)) {
        Recent& rr = _recent[_recentCursor++ % RECENT_CACHE];
        rr.used = true; memcpy(rr.hash, idHash, 8); rr.seenMs = millis();
        _stats.dataRx++;
        sendAck(f.mac, srcPub, msgId);
    }
}

void HybridTransport::sendAck(const uint8_t mac[6], const uint8_t peerPub[32], uint32_t messageId) {
    uint8_t secret[32];
    sharedSecret(peerPub, secret);
    uint8_t plain[4];
    write32(plain, messageId);
    uint8_t enc[32] = {};
    int encLen = mesh::Utils::encryptThenMAC(secret, enc, plain, sizeof(plain));

    uint8_t frame[96] = {};
    frame[0] = MAGIC0; frame[1] = MAGIC1; frame[2] = PROTO_VERSION; frame[3] = TYPE_ACK;
    memcpy(&frame[4], _selfPub, 32);
    memcpy(&frame[36], peerPub, 4);
    write32(&frame[40], messageId);
    frame[44] = (uint8_t)encLen;
    memcpy(&frame[45], enc, encLen);
    if (ensurePeer(mac) && esp_now_send(mac, frame, 45 + encLen) == ESP_OK) _stats.ackTx++;
}

void HybridTransport::processAck(const RawFrame& f) {
    if (!_pending.active || f.len < 63) return;
    const uint8_t* srcPub = &f.data[4];
    if (memcmp(&f.data[36], _selfPub, 4) != 0) return;
    const uint32_t msgId = read32(&f.data[40]);
    if (msgId != _pending.id || memcmp(srcPub, _pending.peerPub, 32) != 0) return;
    const uint8_t encLen = f.data[44];
    if (45 + encLen != f.len) return;

    uint8_t secret[32], plain[32] = {};
    sharedSecret(srcPub, secret);
    int decLen = mesh::Utils::MACThenDecrypt(secret, plain, &f.data[45], encLen);
    if (decLen < 4 || read32(plain) != msgId) {
        _stats.invalidFrames++;
        return;
    }

    _pending.active = false;
    _stats.ackRx++;
    upsertPeer(f.mac, srcPub, nullptr, true);
}

bool HybridTransport::queueRx(const HybridRxMessage& msg) {
    if (_rxCount >= RX_QUEUE) { _stats.queueDrops++; return false; }
    _rx[_rxTail] = msg;
    _rxTail = (uint8_t)((_rxTail + 1) % RX_QUEUE);
    _rxCount++;
    return true;
}

bool HybridTransport::dequeue(HybridRxMessage& out) {
    if (!_rxCount) return false;
    out = _rx[_rxHead];
    _rxHead = (uint8_t)((_rxHead + 1) % RX_QUEUE);
    _rxCount--;
    return true;
}

void HybridTransport::scheduleFallback() {
    if (!_pending.active) return;
    memcpy(_fallback.destPrefix, _pending.destPrefix, 4);
    strncpy(_fallback.text, _pending.text, sizeof(_fallback.text)-1);
    _fallbackReady = true;
    _pending.active = false;
    _stats.fallbacks++;
}

bool HybridTransport::pollFallback(HybridFallback& out) {
    if (!_fallbackReady) return false;
    out = _fallback;
    _fallbackReady = false;
    return true;
}

bool HybridTransport::acceptApplicationMessage(const uint8_t senderPub[32], const char* text) {
    if (!senderPub || !text) return false;
    uint8_t h[8];
    mesh::Utils::sha256(h, sizeof(h), senderPub, 32,
                        reinterpret_cast<const uint8_t*>(text), (int)strnlen(text, 159));
    const uint32_t now = millis();
    for (auto& r : _recent) {
        if (r.used && memcmp(r.hash, h, sizeof(h)) == 0 &&
            (uint32_t)(now - r.seenMs) <= DUP_WINDOW_MS) {
            _stats.duplicateDrops++;
            return false;
        }
    }
    Recent& r = _recent[_recentCursor++ % RECENT_CACHE];
    r.used = true;
    memcpy(r.hash, h, sizeof(h));
    r.seenMs = now;
    return true;
}

int HybridTransport::peerCount() const {
    int n = 0;
    for (const auto& p : _peers) if (p.used && p.verified) n++;
    return n;
}

} // namespace ops
