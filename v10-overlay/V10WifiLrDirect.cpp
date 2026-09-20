#include "V10WifiLrDirect.h"

#include <esp_wifi.h>
#include <Identity.h>
#include <Utils.h>
#define ED25519_NO_SEED 1
#include <ed_25519.h>

namespace ops {
namespace v10 {

static const uint8_t W_MAGIC0 = 'W';
static const uint8_t W_MAGIC1 = '1';
static const uint8_t W_MAGIC2 = '0';

uint32_t WifiLrDirect::read32(const uint8_t* p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

void WifiLrDirect::write32(uint8_t* p, uint32_t value) {
    memcpy(p, &value, sizeof(value));
}

bool WifiLrDirect::begin(const uint8_t selfPub[32], const uint8_t selfPrv[64]) {
    if (!selfPub || !selfPrv) return false;
    memcpy(_selfPub, selfPub, 32);
    memcpy(_selfPrv, selfPrv, 64);
    _started = true;
    return true;
}

void WifiLrDirect::stopNetwork() {
    if (_udpStarted) {
        _udp.stop();
        _udpStarted = false;
    }
    if (_apRole) {
        WiFi.softAPdisconnect(true);
    } else {
        WiFi.disconnect(false, false);
    }
    _peerIpValid = false;
}

void WifiLrDirect::derivePairMaterial() {
    uint8_t pairMaterial[64];
    if (memcmp(_selfPub, _peerPub, 32) < 0) {
        memcpy(pairMaterial, _selfPub, 32);
        memcpy(pairMaterial + 32, _peerPub, 32);
        _apRole = true;
    } else {
        memcpy(pairMaterial, _peerPub, 32);
        memcpy(pairMaterial + 32, _selfPub, 32);
        _apRole = false;
    }

    uint8_t pairHash[16];
    mesh::Utils::sha256(pairHash, sizeof(pairHash), pairMaterial, sizeof(pairMaterial));
    snprintf(_ssid, sizeof(_ssid), "MOG10-%02X%02X%02X%02X",
             pairHash[0], pairHash[1], pairHash[2], pairHash[3]);

    ed25519_key_exchange(_secret, _peerPub, _selfPrv);
    snprintf(_password, sizeof(_password),
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             _secret[0], _secret[1], _secret[2], _secret[3], _secret[4],
             _secret[5], _secret[6], _secret[7], _secret[8], _secret[9]);
}

void WifiLrDirect::startNetwork() {
    if (!_peerConfigured) return;

    if (_apRole) {
        WiFi.mode(WIFI_AP_STA);
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR);
        if (WiFi.softAP(_ssid, _password, 1, false, 1)) {
            _udpStarted = _udp.begin(PORT);
        }
    } else {
        WiFi.mode(WIFI_STA);
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
        WiFi.begin(_ssid, _password, 1);
    }
}

bool WifiLrDirect::configurePeer(const uint8_t peerPub[32]) {
    if (!_started || !peerPub || memcmp(peerPub, _selfPub, 32) == 0) return false;
    if (_peerConfigured && memcmp(peerPub, _peerPub, 32) == 0) return true;

    stopNetwork();
    memcpy(_peerPub, peerPub, 32);
    _peerConfigured = true;
    derivePairMaterial();
    startNetwork();
    _lastProbeMs = 0;
    _lastRxMs = 0;
    return true;
}

bool WifiLrDirect::sendFrame(const uint8_t* frame, size_t len) {
    if (!_udpStarted || !_peerIpValid || !frame || len == 0 || len > MAX_FRAME) return false;
    if (!_udp.beginPacket(_peerIp, PORT)) return false;
    const size_t written = _udp.write(frame, len);
    return written == len && _udp.endPacket() == 1;
}

void WifiLrDirect::sendProbe(bool ack) {
    if (!_peerConfigured || !_udpStarted || !_peerIpValid) return;

    uint8_t plain[4];
    write32(plain, millis());

    uint8_t enc[32] = {};
    const int encLen = mesh::Utils::encryptThenMAC(_secret, enc, plain, sizeof(plain));
    if (encLen <= 0 || encLen > 32) return;

    uint8_t frame[80] = {};
    frame[0] = W_MAGIC0;
    frame[1] = W_MAGIC1;
    frame[2] = W_MAGIC2;
    frame[3] = ack ? TYPE_PROBE_ACK : TYPE_PROBE;
    memcpy(&frame[4], _selfPub, 32);
    memcpy(&frame[36], _peerPub, 4);
    write32(&frame[40], 0);
    frame[44] = static_cast<uint8_t>(encLen);
    memcpy(&frame[45], enc, encLen);

    if (sendFrame(frame, static_cast<size_t>(45 + encLen))) {
        ++_stats.probeTx;
        _lastProbeMs = millis();
    }
}

void WifiLrDirect::tick() {
    if (!_peerConfigured) return;

    if (!_apRole && !_udpStarted && WiFi.status() == WL_CONNECTED) {
        _udpStarted = _udp.begin(PORT);
        _peerIp = WiFi.gatewayIP();
        _peerIpValid = static_cast<uint32_t>(_peerIp) != 0;
    }

    if (_udpStarted) {
        for (int i = 0; i < 4; ++i) {
            const int packetLen = _udp.parsePacket();
            if (packetLen <= 0) break;
            if (packetLen > static_cast<int>(MAX_FRAME)) {
                while (_udp.available()) _udp.read();
                ++_stats.invalid;
                continue;
            }
            uint8_t frame[MAX_FRAME];
            const IPAddress remote = _udp.remoteIP();
            const int got = _udp.read(frame, sizeof(frame));
            if (got > 0) processPacket(frame, got, remote);
        }
    }

    const uint32_t now = millis();
    if (_peerIpValid && static_cast<uint32_t>(now - _lastProbeMs) >= PROBE_INTERVAL_MS) {
        sendProbe(false);
    }

    if (_pending.active && static_cast<uint32_t>(now - _pending.sentAtMs) >= ACK_TIMEOUT_MS) {
        if (_pending.attempts < MAX_ATTEMPTS && sendFrame(_pending.frame, _pending.frameLen)) {
            ++_pending.attempts;
            _pending.sentAtMs = now;
            ++_stats.retries;
            ++_stats.dataTx;
        } else {
            scheduleFallback();
        }
    }
}

bool WifiLrDirect::canReach(const uint8_t destPrefix[4]) const {
    if (!_peerConfigured || !_udpStarted || !_peerIpValid || !destPrefix) return false;
    if (memcmp(destPrefix, _peerPub, 4) != 0) return false;
    return _lastRxMs != 0 && static_cast<uint32_t>(millis() - _lastRxMs) <= PEER_TTL_MS;
}

bool WifiLrDirect::sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp) {
    if (!canReach(destPrefix) || !text || !text[0] || _pending.active) return false;

    const size_t textLen = strnlen(text, 150);
    uint32_t msgId = _nextMessageId++;
    if (_nextMessageId == 0) _nextMessageId = 1;

    uint8_t plain[160] = {};
    write32(&plain[0], timestamp);
    write32(&plain[4], msgId);
    plain[8] = static_cast<uint8_t>(textLen);
    memcpy(&plain[9], text, textLen);
    const int plainLen = 9 + static_cast<int>(textLen);

    uint8_t enc[180] = {};
    const int encLen = mesh::Utils::encryptThenMAC(_secret, enc, plain, plainLen);
    if (encLen <= 0 || encLen > 180) return false;

    uint8_t frame[MAX_FRAME] = {};
    frame[0] = W_MAGIC0;
    frame[1] = W_MAGIC1;
    frame[2] = W_MAGIC2;
    frame[3] = TYPE_DATA;
    memcpy(&frame[4], _selfPub, 32);
    memcpy(&frame[36], destPrefix, 4);
    write32(&frame[40], msgId);
    frame[44] = static_cast<uint8_t>(encLen);
    memcpy(&frame[45], enc, encLen);
    const int frameLen = 45 + encLen;

    if (!sendFrame(frame, static_cast<size_t>(frameLen))) return false;

    _pending.active = true;
    _pending.id = msgId;
    _pending.sentAtMs = millis();
    _pending.attempts = 1;
    memcpy(_pending.destPrefix, destPrefix, 4);
    memcpy(_pending.frame, frame, frameLen);
    _pending.frameLen = static_cast<uint8_t>(frameLen);
    strncpy(_pending.text, text, sizeof(_pending.text) - 1);
    ++_stats.dataTx;
    return true;
}

void WifiLrDirect::processPacket(uint8_t* frame, int len, const IPAddress& remoteIp) {
    if (len < 4 || frame[0] != W_MAGIC0 || frame[1] != W_MAGIC1 || frame[2] != W_MAGIC2) {
        ++_stats.invalid;
        return;
    }
    if (len < 45 || memcmp(&frame[4], _peerPub, 32) != 0 ||
        memcmp(&frame[36], _selfPub, 4) != 0) {
        ++_stats.invalid;
        return;
    }

    _peerIp = remoteIp;
    _peerIpValid = true;
    _lastRxMs = millis();

    switch (frame[3]) {
        case TYPE_PROBE: processProbe(frame, len, remoteIp, false); break;
        case TYPE_PROBE_ACK: processProbe(frame, len, remoteIp, true); break;
        case TYPE_DATA: processData(frame, len, remoteIp); break;
        case TYPE_ACK: processAck(frame, len, remoteIp); break;
        default: ++_stats.invalid; break;
    }
}

void WifiLrDirect::processProbe(const uint8_t* frame, int len, const IPAddress&, bool ack) {
    const uint8_t encLen = frame[44];
    if (45 + encLen != len) {
        ++_stats.invalid;
        return;
    }
    uint8_t plain[32] = {};
    const int decLen = mesh::Utils::MACThenDecrypt(_secret, plain, &frame[45], encLen);
    if (decLen < 4) {
        ++_stats.invalid;
        return;
    }
    ++_stats.probeRx;
    if (!ack) sendProbe(true);
}

void WifiLrDirect::processData(const uint8_t* frame, int len, const IPAddress& remoteIp) {
    const uint32_t msgId = read32(&frame[40]);
    const uint8_t encLen = frame[44];
    if (45 + encLen != len || encLen < 18) {
        ++_stats.invalid;
        return;
    }

    uint8_t plain[192] = {};
    const int decLen = mesh::Utils::MACThenDecrypt(_secret, plain, &frame[45], encLen);
    if (decLen < 9) {
        ++_stats.invalid;
        return;
    }

    const uint32_t timestamp = read32(&plain[0]);
    const uint32_t innerId = read32(&plain[4]);
    const uint8_t textLen = plain[8];
    if (innerId != msgId || textLen > 150 || 9 + textLen > decLen) {
        ++_stats.invalid;
        return;
    }

    WifiRxMessage msg;
    memset(&msg, 0, sizeof(msg));
    memcpy(msg.senderPubKey, _peerPub, 32);
    msg.timestamp = timestamp;
    msg.messageId = msgId;
    memcpy(msg.text, &plain[9], textLen);
    msg.text[textLen] = 0;

    if (queueRx(msg)) {
        ++_stats.dataRx;
        sendAck(msgId, remoteIp);
    }
}

void WifiLrDirect::sendAck(uint32_t messageId, const IPAddress& remoteIp) {
    uint8_t plain[4];
    write32(plain, messageId);
    uint8_t enc[32] = {};
    const int encLen = mesh::Utils::encryptThenMAC(_secret, enc, plain, sizeof(plain));
    if (encLen <= 0 || encLen > 32) return;

    uint8_t frame[80] = {};
    frame[0] = W_MAGIC0;
    frame[1] = W_MAGIC1;
    frame[2] = W_MAGIC2;
    frame[3] = TYPE_ACK;
    memcpy(&frame[4], _selfPub, 32);
    memcpy(&frame[36], _peerPub, 4);
    write32(&frame[40], messageId);
    frame[44] = static_cast<uint8_t>(encLen);
    memcpy(&frame[45], enc, encLen);

    const IPAddress oldIp = _peerIp;
    const bool oldValid = _peerIpValid;
    _peerIp = remoteIp;
    _peerIpValid = true;
    if (sendFrame(frame, static_cast<size_t>(45 + encLen))) ++_stats.ackTx;
    _peerIp = oldIp;
    _peerIpValid = oldValid;
}

void WifiLrDirect::processAck(const uint8_t* frame, int len, const IPAddress&) {
    if (!_pending.active) return;
    const uint32_t msgId = read32(&frame[40]);
    if (msgId != _pending.id) return;

    const uint8_t encLen = frame[44];
    if (45 + encLen != len) return;

    uint8_t plain[32] = {};
    const int decLen = mesh::Utils::MACThenDecrypt(_secret, plain, &frame[45], encLen);
    if (decLen < 4 || read32(plain) != msgId) {
        ++_stats.invalid;
        return;
    }

    _pending.active = false;
    ++_stats.ackRx;
}

bool WifiLrDirect::queueRx(const WifiRxMessage& msg) {
    if (_rxCount >= RX_QUEUE) return false;
    _rx[_rxTail] = msg;
    _rxTail = static_cast<uint8_t>((_rxTail + 1) % RX_QUEUE);
    ++_rxCount;
    return true;
}

bool WifiLrDirect::dequeue(WifiRxMessage& out) {
    if (_rxCount == 0) return false;
    out = _rx[_rxHead];
    _rxHead = static_cast<uint8_t>((_rxHead + 1) % RX_QUEUE);
    --_rxCount;
    return true;
}

void WifiLrDirect::scheduleFallback() {
    if (!_pending.active) return;
    _fallback.ready = true;
    memcpy(_fallback.destPrefix, _pending.destPrefix, 4);
    strncpy(_fallback.text, _pending.text, sizeof(_fallback.text) - 1);
    _pending.active = false;
    ++_stats.fallbacks;
}

bool WifiLrDirect::pollFallback(WifiFallback& out) {
    if (!_fallback.ready) return false;
    out = _fallback;
    _fallback.ready = false;
    return true;
}

} // namespace v10
} // namespace ops
