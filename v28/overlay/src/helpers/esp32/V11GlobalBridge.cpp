#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V28)

#include "V11GlobalBridge.h"
#include "../../MyMesh.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Identity.h>
#include <Utils.h>
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

#ifndef V28_RELAY_URL
#define V28_RELAY_URL "https://meshoffgridnl.vercel.app/api/v28-relay"
#endif

static const char V28_RELAY_ROOT_CA[] PROGMEM = R"PEM(
__V28_CA_BUNDLE__
)PEM";

V11GlobalBridge v11_global_bridge;
V11GlobalBridge* V11GlobalBridge::s_instance = nullptr;

namespace {

bool hmac256(const uint8_t* key, size_t keyLen,
             const uint8_t* data, size_t len,
             uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, keyLen, data, len, out) == 0;
}

bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md(info, data, len, out) == 0;
}

bool due(uint32_t now, uint32_t at) {
    return at == 0 || (int32_t)(now - at) >= 0;
}

} // namespace

void V11GlobalBridge::begin(MyMesh* mesh) {
    _mesh = mesh;
    if (!_mesh || _started) return;

    _started = true;
    s_instance = this;
    memcpy(_selfPub, _mesh->getSelfPubKey(), PUB_KEY_SIZE);

    // Verified TLS only. V28 deliberately has no setInsecure() fallback.
    _wc.setCACert(V28_RELAY_ROOT_CA);
    _nextPollAt = millis() + 1000;
    Serial.println("[V28] RF-first + signed HTTPS relay route ready");
}

bool V11GlobalBridge::connected() const {
    return _started &&
           WiFi.status() == WL_CONNECTED &&
           _lastRelayOkMs != 0 &&
           (uint32_t)(millis() - _lastRelayOkMs) <= RELAY_HEALTH_MS;
}

bool V11GlobalBridge::normalizeJoinCode(const char* in, char out[9]) {
    if (!in || !out) return false;
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    size_t n = 0;
    for (const char* p = in; *p; ++p) {
        char ch = *p;
        if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (!strchr(alphabet, ch) || n >= 8) return false;
        out[n++] = ch;
    }
    out[n] = '\0';
    return n == 8;
}

void V11GlobalBridge::emitEvent(UiEventType type, const char* message,
                                const char* code, const char* channel) {
    memset(&_uiEvent, 0, sizeof(_uiEvent));
    _uiEvent.type = type;
    if (message) strncpy(_uiEvent.message, message, sizeof(_uiEvent.message) - 1);
    if (code) strncpy(_uiEvent.code, code, sizeof(_uiEvent.code) - 1);
    if (channel) strncpy(_uiEvent.channel, channel, sizeof(_uiEvent.channel) - 1);
}

bool V11GlobalBridge::takeUiEvent(UiEvent& out) {
    if (_uiEvent.type == UI_NONE) return false;
    out = _uiEvent;
    memset(&_uiEvent, 0, sizeof(_uiEvent));
    return true;
}

bool V11GlobalBridge::getJoinRequest(uint8_t idx, JoinRequest& out) const {
    if (idx >= _joinRequestCount) return false;
    out = _joinRequests[idx];
    return true;
}

bool V11GlobalBridge::createChannelInvite(uint8_t channelSlot) {
    if (!_started || !_mesh || _control.used) return false;
    ChannelDetails cd{};
    if (!_mesh->v27GetChannelByIndex(channelSlot, cd)) return false;
    memset(&_control, 0, sizeof(_control));
    _control.used = true;
    _control.kind = WORK_INVITE_CREATE;
    _control.channelSlot = channelSlot;
    _nextPollAt = 0;
    return true;
}

bool V11GlobalBridge::requestChannelJoin(const char* code) {
    if (!_started || !_mesh || _control.used) return false;
    char normalized[9] = {};
    if (!normalizeJoinCode(code, normalized)) return false;
    memset(&_control, 0, sizeof(_control));
    _control.used = true;
    _control.kind = WORK_INVITE_REQUEST;
    strncpy(_control.code, normalized, sizeof(_control.code) - 1);
    _nextPollAt = 0;
    return true;
}

bool V11GlobalBridge::refreshJoinRequests() {
    if (!_started || !_mesh || _control.used) return false;
    memset(&_control, 0, sizeof(_control));
    _control.used = true;
    _control.kind = WORK_INVITE_LIST;
    _nextPollAt = 0;
    return true;
}

bool V11GlobalBridge::decideJoinRequest(uint32_t inviteId,
                                        const uint8_t requester[PUB_KEY_SIZE],
                                        bool approve) {
    if (!_started || !_mesh || _control.used || inviteId == 0 || !requester) return false;
    memset(&_control, 0, sizeof(_control));
    _control.used = true;
    _control.kind = WORK_INVITE_DECIDE;
    _control.inviteId = inviteId;
    memcpy(_control.requester, requester, PUB_KEY_SIZE);
    _control.approve = approve;
    _nextPollAt = 0;
    return true;
}

void V11GlobalBridge::httpTask(void* arg) {
    V11GlobalBridge* self = static_cast<V11GlobalBridge*>(arg);
    self->_httpOk = self->performHttp();
    self->_httpDone = true;
    self->_httpBusy = false;
    vTaskDelete(nullptr);
}

void V11GlobalBridge::loop() {
    if (!_started || !_mesh) return;

    if (_httpDone) {
        completeHttp();
        _httpDone = false;
    }
    if (_httpBusy) return;
    if (WiFi.status() != WL_CONNECTED) {
        _lastRelayOkMs = 0;
        return;
    }

    const uint32_t now = millis();
    if (!due(now, _nextPollAt)) return;

    // Delivery ACKs first, then queued route-2 copies, then inbox polling.
    if (startAck()) return;
    if (startPushDm()) return;
    if (startPushChannel()) return;
    if (startPoll()) return;

    _nextPollAt = now + POLL_INTERVAL_MS;
}

bool V11GlobalBridge::mirrorDM(const ContactInfo& recipient,
                               uint32_t timestamp,
                               const char* text,
                               bool allowQueue) {
    if (!_started || !_mesh || !text || recipient.type != ADV_TYPE_CHAT) return false;

    // RF is route 1 and has already been attempted by MyMesh in V28.
    // Route 2 is independent: if Wi-Fi exists, queue the encrypted Internet
    // copy even when RF could not queue. Never queue behind absent Wi-Fi.
    if (!allowQueue || WiFi.status() != WL_CONNECTED) return false;
    const bool queued = enqueue(recipient.id.pub_key, timestamp, text);
    // Return "globally accepted" to MyMesh only when the relay was recently
    // healthy. The queue itself still survives a temporary relay outage.
    return queued && connected();
}

bool V11GlobalBridge::mirrorChannelPacket(const mesh::GroupChannel& channel,
                                          const mesh::Packet* packet) {
    if (!_started || !_mesh || !packet ||
        packet->getPayloadType() != PAYLOAD_TYPE_GRP_TXT) return false;
    if (packet->payload_len <= PATH_HASH_SIZE + CIPHER_MAC_SIZE) return false;

    uint8_t plain[MAX_PACKET_PAYLOAD] = {};
    const int plen = mesh::Utils::MACThenDecrypt(
        channel.secret,
        plain,
        packet->payload + PATH_HASH_SIZE,
        packet->payload_len - PATH_HASH_SIZE);
    if (plen <= 5 || plain[4] != 0) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    uint32_t timestamp = 0;
    memcpy(&timestamp, plain, 4);
    const size_t avail = (size_t)(plen - 5);
    const size_t tlen = strnlen(reinterpret_cast<const char*>(plain + 5), avail);
    if (tlen == 0) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    char text[MAX_TEXT + 1] = {};
    const size_t keep = tlen > MAX_TEXT ? MAX_TEXT : tlen;
    memcpy(text, plain + 5, keep);
    text[keep] = '\0';
    memset(plain, 0, sizeof(plain));

    return enqueueChannel(channel.secret, timestamp, text);
}

bool V11GlobalBridge::enqueue(const uint8_t recipient[32],
                              uint32_t timestamp,
                              const char* text) {
    if (!recipient || !text) return false;

    if (_pendingCount >= PENDING_CAP) {
        dropDmHead();
    }

    const uint8_t slot = (uint8_t)((_pendingHead + _pendingCount) % PENDING_CAP);
    Pending& p = _pending[slot];
    memset(&p, 0, sizeof(p));
    p.used = true;
    memcpy(p.recipient, recipient, PUB_KEY_SIZE);
    p.timestamp = timestamp;
    p.queuedMs = millis();
    strncpy(p.text, text, MAX_TEXT);
    p.text[MAX_TEXT] = '\0';
    ++_pendingCount;
    return true;
}

bool V11GlobalBridge::enqueueChannel(const uint8_t secret[PUB_KEY_SIZE],
                                     uint32_t timestamp,
                                     const char* text) {
    if (!secret || !text) return false;

    if (_pendingChannelCount >= PENDING_CHANNEL_CAP) {
        dropChannelHead();
    }

    const uint8_t slot =
        (uint8_t)((_pendingChannelHead + _pendingChannelCount) % PENDING_CHANNEL_CAP);
    PendingChannel& p = _pendingChannel[slot];
    memset(&p, 0, sizeof(p));
    p.used = true;
    memcpy(p.secret, secret, PUB_KEY_SIZE);
    p.timestamp = timestamp;
    p.queuedMs = millis();
    strncpy(p.text, text, MAX_TEXT);
    p.text[MAX_TEXT] = '\0';
    ++_pendingChannelCount;
    return true;
}

bool V11GlobalBridge::enqueueAck(const char* route, const char* message) {
    if (!route || !message || strlen(route) != 32 || strlen(message) != 32) return false;
    if (_ackCount >= ACK_CAP) dropAckHead();

    const uint8_t slot = (uint8_t)((_ackHead + _ackCount) % ACK_CAP);
    PendingAck& a = _acks[slot];
    memset(&a, 0, sizeof(a));
    a.used = true;
    strncpy(a.route, route, sizeof(a.route) - 1);
    strncpy(a.message, message, sizeof(a.message) - 1);
    ++_ackCount;
    return true;
}

void V11GlobalBridge::dropDmHead() {
    if (_pendingCount == 0) return;
    memset(&_pending[_pendingHead], 0, sizeof(Pending));
    _pendingHead = (uint8_t)((_pendingHead + 1) % PENDING_CAP);
    --_pendingCount;
}

void V11GlobalBridge::dropChannelHead() {
    if (_pendingChannelCount == 0) return;
    memset(&_pendingChannel[_pendingChannelHead], 0, sizeof(PendingChannel));
    _pendingChannelHead =
        (uint8_t)((_pendingChannelHead + 1) % PENDING_CHANNEL_CAP);
    --_pendingChannelCount;
}

void V11GlobalBridge::dropAckHead() {
    if (_ackCount == 0) return;
    memset(&_acks[_ackHead], 0, sizeof(PendingAck));
    _ackHead = (uint8_t)((_ackHead + 1) % ACK_CAP);
    --_ackCount;
}

bool V11GlobalBridge::startPushDm() {
    while (_pendingCount > 0) {
        Pending& p = _pending[_pendingHead];
        if (!p.used ||
            (uint32_t)(millis() - p.queuedMs) > PENDING_TTL_MS) {
            dropDmHead();
            continue;
        }

        uint8_t wire[MAX_WIRE] = {};
        uint8_t msgId[MSG_ID_LEN] = {};
        if (!buildDmEnvelope(p.recipient, p.timestamp, p.text, wire, msgId)) {
            dropDmHead();
            continue;
        }

        char route[33] = {};
        char message[33] = {};
        if (!routeHexFor(p.recipient, route)) {
            dropDmHead();
            continue;
        }
        hexEncode(msgId, sizeof(msgId), message);
        return startHttp(WORK_PUSH_DM, route, message, wire, p.recipient, nullptr);
    }
    return false;
}

bool V11GlobalBridge::startPushChannel() {
    while (_pendingChannelCount > 0) {
        PendingChannel& p = _pendingChannel[_pendingChannelHead];
        if (!p.used ||
            (uint32_t)(millis() - p.queuedMs) > PENDING_TTL_MS ||
            !channelStillConfigured(p.secret)) {
            dropChannelHead();
            continue;
        }

        uint8_t wire[MAX_WIRE] = {};
        uint8_t msgId[MSG_ID_LEN] = {};
        if (!buildChannelEnvelope(p.secret, p.timestamp, p.text, wire, msgId)) {
            dropChannelHead();
            continue;
        }

        char route[33] = {};
        char message[33] = {};
        if (!routeHexForChannel(p.secret, route)) {
            dropChannelHead();
            continue;
        }
        hexEncode(msgId, sizeof(msgId), message);
        return startHttp(WORK_PUSH_CHANNEL, route, message, wire, nullptr, p.secret);
    }
    return false;
}

bool V11GlobalBridge::startAck() {
    while (_ackCount > 0) {
        PendingAck& a = _acks[_ackHead];
        if (!a.used) {
            dropAckHead();
            continue;
        }
        return startHttp(WORK_ACK, a.route, a.message, nullptr);
    }
    return false;
}

bool V11GlobalBridge::startPoll() {
    const uint32_t contacts = _mesh->v27GetContactCount();

    for (int pass = 0; pass < 2; ++pass) {
        const bool channelPass = _pollChannelsNext;
        _pollChannelsNext = !_pollChannelsNext;

        if (!channelPass && contacts > 0) {
            for (uint32_t tries = 0; tries < contacts; ++tries) {
                const uint32_t idx = _pollContactIdx++ % contacts;
                ContactInfo c{};
                if (!_mesh->v27GetContactByIndex(idx, c) ||
                    c.type != ADV_TYPE_CHAT) continue;
                char route[33] = {};
                if (!routeHexFor(c.id.pub_key, route)) continue;
                return startHttp(WORK_POLL_DM, route, "", nullptr, c.id.pub_key, nullptr);
            }
        }

        if (channelPass) {
            for (int tries = 0; tries < MAX_GROUP_CHANNELS; ++tries) {
                const uint8_t idx = _pollChannelIdx++ % MAX_GROUP_CHANNELS;
                ChannelDetails cd{};
                if (!_mesh->v27GetChannelByIndex(idx, cd)) continue;
                char route[33] = {};
                if (!routeHexForChannel(cd.channel.secret, route)) continue;
                return startHttp(WORK_POLL_CHANNEL, route, "", nullptr, nullptr,
                                 cd.channel.secret);
            }
        }
    }

    return false;
}

bool V11GlobalBridge::startHttp(WorkKind kind,
                                const char* route,
                                const char* message,
                                const uint8_t* envelope,
                                const uint8_t* peerPub,
                                const uint8_t* channelSecret) {
    if (_httpBusy || !_started || WiFi.status() != WL_CONNECTED ||
        !route || strlen(route) != 32) return false;

    memset(&_work, 0, sizeof(_work));
    _work.kind = kind;
    strncpy(_work.route, route, sizeof(_work.route) - 1);
    if (message) strncpy(_work.message, message, sizeof(_work.message) - 1);
    if (envelope) {
        memcpy(_work.envelope, envelope, MAX_WIRE);
        _work.hasEnvelope = true;
    }
    if (peerPub) memcpy(_work.peerPub, peerPub, PUB_KEY_SIZE);
    if (channelSecret) memcpy(_work.channelSecret, channelSecret, PUB_KEY_SIZE);

    const char* action =
        (kind == WORK_PUSH_DM || kind == WORK_PUSH_CHANNEL) ? "push" :
        (kind == WORK_ACK) ? "ack" : "poll";

    if (!buildSignedRequest(action, _work.route, _work.message,
                            _work.hasEnvelope ? _work.envelope : nullptr,
                            _httpBody)) {
        memset(&_work, 0, sizeof(_work));
        return false;
    }

    _httpResponse = "";
    _httpOk = false;
    _httpDone = false;
    _httpBusy = true;

    if (xTaskCreatePinnedToCore(httpTask, "v28_https", 8192, this,
                                1, nullptr, 0) != pdPASS) {
        _httpBusy = false;
        memset(&_work, 0, sizeof(_work));
        _httpBody = "";
        return false;
    }
    return true;
}

bool V11GlobalBridge::buildSignedRequest(const char* action,
                                         const char* route,
                                         const char* message,
                                         const uint8_t* envelope,
                                         String& out) {
    if (!_mesh || !action || !route) return false;

    uint8_t nonce[16] = {};
    esp_fill_random(nonce, sizeof(nonce));
    char nonceHex[33] = {};
    char senderHex[PUB_KEY_SIZE * 2 + 1] = {};
    hexEncode(nonce, sizeof(nonce), nonceHex);
    hexEncode(_selfPub, PUB_KEY_SIZE, senderHex);

    char digestHex[65] = {};
    if (envelope) {
        uint8_t digest[32] = {};
        if (!sha256(envelope, MAX_WIRE, digest)) return false;
        hexEncode(digest, sizeof(digest), digestHex);
        memset(digest, 0, sizeof(digest));
    }

    String canonical;
    canonical.reserve(220);
    canonical += "MOG28-RELAY-V1|";
    canonical += action;
    canonical += "|";
    canonical += route;
    canonical += "|";
    canonical += message ? message : "";
    canonical += "|";
    canonical += nonceHex;
    canonical += "|";
    canonical += digestHex;
    canonical += "|";  // empty extraDigest for push/poll/ack

    uint8_t signature[SIGNATURE_SIZE] = {};
    _mesh->v27SignGlobal(
        reinterpret_cast<const uint8_t*>(canonical.c_str()),
        canonical.length(),
        signature);

    bool signatureNonZero = false;
    for (size_t i = 0; i < sizeof(signature); ++i)
        signatureNonZero = signatureNonZero || signature[i] != 0;
    if (!signatureNonZero) {
        memset(signature, 0, sizeof(signature));
        return false;
    }

    String sig64;
    if (!base64Encode(signature, sizeof(signature), sig64)) {
        memset(signature, 0, sizeof(signature));
        return false;
    }
    memset(signature, 0, sizeof(signature));

    String env64;
    if (envelope && !base64Encode(envelope, MAX_WIRE, env64)) return false;

    out = "";
    out.reserve(envelope ? 850 : 380);
    out += "{\"action\":\"";
    out += action;
    out += "\",\"route\":\"";
    out += route;
    out += "\",\"nonce\":\"";
    out += nonceHex;
    out += "\",\"sender\":\"";
    out += senderHex;
    out += "\",\"signature\":\"";
    out += sig64;
    out += "\"";
    if (message && message[0]) {
        out += ",\"message\":\"";
        out += message;
        out += "\"";
    }
    if (envelope) {
        out += ",\"envelope\":\"";
        out += env64;
        out += "\"";
    }
    out += "}";
    return true;
}

bool V11GlobalBridge::performHttp() {
    if (WiFi.status() != WL_CONNECTED || _httpBody.length() == 0) return false;

    HTTPClient http;
    http.setTimeout(3500);
    http.setReuse(false);

    if (!http.begin(_wc, V28_RELAY_URL)) return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "MeshOffGridNL-V28/1");

    const int code = http.POST(_httpBody);
    if (code > 0) {
        _httpResponse = http.getString();
    } else {
        _httpResponse = "";
    }
    http.end();

    return code >= 200 && code < 300;
}

void V11GlobalBridge::completeHttp() {
    const bool ok = _httpOk;
    if (ok) _lastRelayOkMs = millis();

    switch (_work.kind) {
        case WORK_PUSH_DM:
            if (ok) dropDmHead();
            break;
        case WORK_PUSH_CHANNEL:
            if (ok) dropChannelHead();
            break;
        case WORK_ACK:
            if (ok) dropAckHead();
            break;
        case WORK_POLL_DM:
        case WORK_POLL_CHANNEL:
            if (ok) processPollResponse();
            break;
        default:
            break;
    }

    if (!ok) {
        _nextPollAt = millis() + 3000;
    } else if (_work.kind == WORK_POLL_DM || _work.kind == WORK_POLL_CHANNEL) {
        _nextPollAt = millis() + POLL_INTERVAL_MS;
    } else {
        _nextPollAt = millis() + 250;
    }

    _httpBody = "";
    _httpResponse = "";
    memset(&_work, 0, sizeof(_work));
}

bool V11GlobalBridge::buildDmEnvelope(const uint8_t recipient[32],
                                      uint32_t timestamp,
                                      const char* text,
                                      uint8_t wire[MAX_WIRE],
                                      uint8_t msgId[MSG_ID_LEN]) {
    if (!recipient || !text || !wire || !msgId) return false;
    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    memset(wire, 0, MAX_WIRE);
    wire[0] = 'M'; wire[1] = 'G'; wire[2] = '2'; wire[3] = '7';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_DM;

    if (!messageIdFor(recipient, timestamp, text, msgId)) return false;
    memcpy(wire + 6, msgId, MSG_ID_LEN);
    esp_fill_random(wire + 22, 8);
    esp_fill_random(wire + 30, 12);

    uint8_t plain[PLAIN_LEN] = {};
    esp_fill_random(plain, sizeof(plain));
    memcpy(plain, _selfPub, PUB_KEY_SIZE);
    memcpy(plain + 32, &timestamp, 4);
    plain[36] = (uint8_t)(tlen & 0xff);
    plain[37] = (uint8_t)((tlen >> 8) & 0xff);
    memcpy(plain + 38, text, tlen);

    uint8_t key[32] = {};
    if (!deriveDmKey(recipient, key)) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    uint8_t tag[TAG_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, PLAIN_LEN,
            wire + 30, 12,
            wire, AAD_LEN,
            plain, wire + HEADER_LEN,
            TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(plain, 0, sizeof(plain));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + PLAIN_LEN, tag, TAG_LEN);
    return true;
}

bool V11GlobalBridge::buildChannelEnvelope(
    const uint8_t secret[PUB_KEY_SIZE],
    uint32_t timestamp,
    const char* text,
    uint8_t wire[MAX_WIRE],
    uint8_t msgId[MSG_ID_LEN]) {
    if (!secret || !text || !wire || !msgId) return false;
    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    memset(wire, 0, MAX_WIRE);
    wire[0] = 'M'; wire[1] = 'G'; wire[2] = '2'; wire[3] = '7';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_CHANNEL;

    if (!channelMessageIdFor(secret, timestamp, text, msgId)) return false;
    memcpy(wire + 6, msgId, MSG_ID_LEN);
    esp_fill_random(wire + 22, 8);
    esp_fill_random(wire + 30, 12);

    uint8_t signData[192] = {};
    static const char signCtx[] = "MOG27-CH-SIGN";
    size_t signLen = 0;
    memcpy(signData + signLen, signCtx, sizeof(signCtx) - 1);
    signLen += sizeof(signCtx) - 1;
    memcpy(signData + signLen, msgId, MSG_ID_LEN);
    signLen += MSG_ID_LEN;
    memcpy(signData + signLen, &timestamp, 4);
    signLen += 4;
    signData[signLen++] = (uint8_t)(tlen & 0xff);
    signData[signLen++] = (uint8_t)((tlen >> 8) & 0xff);
    memcpy(signData + signLen, text, tlen);
    signLen += tlen;

    uint8_t signature[SIGNATURE_SIZE] = {};
    _mesh->v27SignGlobal(signData, signLen, signature);

    uint8_t plain[PLAIN_LEN] = {};
    esp_fill_random(plain, sizeof(plain));
    memcpy(plain, _selfPub, PUB_KEY_SIZE);
    memcpy(plain + 32, signature, SIGNATURE_SIZE);
    memcpy(plain + 96, &timestamp, 4);
    plain[100] = (uint8_t)(tlen & 0xff);
    plain[101] = (uint8_t)((tlen >> 8) & 0xff);
    memcpy(plain + 102, text, tlen);

    memset(signature, 0, sizeof(signature));
    memset(signData, 0, sizeof(signData));

    uint8_t key[32] = {};
    if (!deriveChannelKey(secret, key)) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    uint8_t tag[TAG_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, PLAIN_LEN,
            wire + 30, 12,
            wire, AAD_LEN,
            plain, wire + HEADER_LEN,
            TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(plain, 0, sizeof(plain));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + PLAIN_LEN, tag, TAG_LEN);
    return true;
}

void V11GlobalBridge::processPollResponse() {
    if (_httpResponse.length() == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, _httpResponse)) return;
    JsonArrayConst items = doc["items"].as<JsonArrayConst>();
    if (items.isNull()) return;

    for (JsonObjectConst item : items) {
        const char* message = item["message"] | "";
        const char* envelope64 = item["envelope"] | "";
        if (strlen(message) != 32 || !envelope64[0]) continue;

        uint8_t wire[MAX_WIRE] = {};
        size_t wireLen = 0;
        if (!base64Decode(envelope64, wire, sizeof(wire), wireLen) ||
            wireLen != MAX_WIRE) {
            memset(wire, 0, sizeof(wire));
            continue;
        }

        bool accepted = false;
        if (_work.kind == WORK_POLL_DM) {
            accepted = processDmEnvelope(_work.peerPub, wire);
        } else if (_work.kind == WORK_POLL_CHANNEL) {
            accepted = processChannelEnvelope(_work.channelSecret, wire);
        }
        memset(wire, 0, sizeof(wire));

        // ACK only envelopes that authenticate for this route (including
        // cross-transport duplicates). Invalid ciphertext simply expires server-side.
        if (accepted) enqueueAck(_work.route, message);
    }
}

bool V11GlobalBridge::processDmEnvelope(
    const uint8_t peerPub[PUB_KEY_SIZE],
    const uint8_t wire[MAX_WIRE]) {
    if (!peerPub || !wire || !allowInbound()) return false;
    if (wire[0] != 'M' || wire[1] != 'G' ||
        wire[2] != '2' || wire[3] != '7' ||
        wire[4] != PROTOCOL_VERSION || wire[5] != KIND_DM) return false;

    uint8_t key[32] = {};
    if (!deriveDmKey(peerPub, key)) return false;

    uint8_t plain[PLAIN_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, PLAIN_LEN,
            wire + 30, 12,
            wire, AAD_LEN,
            wire + HEADER_LEN + PLAIN_LEN, TAG_LEN,
            wire + HEADER_LEN, plain);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));

    if (rc != 0 || memcmp(plain, peerPub, PUB_KEY_SIZE) != 0) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    uint32_t timestamp = 0;
    memcpy(&timestamp, plain + 32, 4);
    const uint16_t tlen =
        (uint16_t)plain[36] | ((uint16_t)plain[37] << 8);
    if (tlen == 0 || tlen > MAX_TEXT) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    char text[MAX_TEXT + 1] = {};
    memcpy(text, plain + 38, tlen);
    text[tlen] = '\0';

    uint8_t expected[MSG_ID_LEN] = {};
    const bool valid =
        messageIdFor(peerPub, timestamp, text, expected) &&
        memcmp(expected, wire + 6, MSG_ID_LEN) == 0;
    memset(plain, 0, sizeof(plain));
    if (!valid) {
        memset(text, 0, sizeof(text));
        return false;
    }

    const bool duplicate = seenOrRemember(expected);
    if (!duplicate) _mesh->v11InjectGlobalDm(peerPub, timestamp, text);
    memset(text, 0, sizeof(text));
    return true;
}

bool V11GlobalBridge::processChannelEnvelope(
    const uint8_t secret[PUB_KEY_SIZE],
    const uint8_t wire[MAX_WIRE]) {
    if (!secret || !wire || !allowInbound()) return false;
    if (wire[0] != 'M' || wire[1] != 'G' ||
        wire[2] != '2' || wire[3] != '7' ||
        wire[4] != PROTOCOL_VERSION || wire[5] != KIND_CHANNEL) return false;

    uint8_t key[32] = {};
    if (!deriveChannelKey(secret, key)) return false;

    uint8_t plain[PLAIN_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, PLAIN_LEN,
            wire + 30, 12,
            wire, AAD_LEN,
            wire + HEADER_LEN + PLAIN_LEN, TAG_LEN,
            wire + HEADER_LEN, plain);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    if (rc != 0) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    uint8_t senderPub[PUB_KEY_SIZE] = {};
    uint8_t signature[SIGNATURE_SIZE] = {};
    memcpy(senderPub, plain, PUB_KEY_SIZE);
    memcpy(signature, plain + 32, SIGNATURE_SIZE);

    uint32_t timestamp = 0;
    memcpy(&timestamp, plain + 96, 4);
    const uint16_t tlen =
        (uint16_t)plain[100] | ((uint16_t)plain[101] << 8);
    if (tlen == 0 || tlen > MAX_TEXT) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    char text[MAX_TEXT + 1] = {};
    memcpy(text, plain + 102, tlen);
    text[tlen] = '\0';

    uint8_t expected[MSG_ID_LEN] = {};
    if (!channelMessageIdFor(secret, timestamp, text, expected) ||
        memcmp(expected, wire + 6, MSG_ID_LEN) != 0) {
        memset(plain, 0, sizeof(plain));
        memset(text, 0, sizeof(text));
        return false;
    }

    uint8_t signData[192] = {};
    static const char signCtx[] = "MOG27-CH-SIGN";
    size_t signLen = 0;
    memcpy(signData + signLen, signCtx, sizeof(signCtx) - 1);
    signLen += sizeof(signCtx) - 1;
    memcpy(signData + signLen, expected, MSG_ID_LEN);
    signLen += MSG_ID_LEN;
    memcpy(signData + signLen, &timestamp, 4);
    signLen += 4;
    signData[signLen++] = (uint8_t)(tlen & 0xff);
    signData[signLen++] = (uint8_t)((tlen >> 8) & 0xff);
    memcpy(signData + signLen, text, tlen);
    signLen += tlen;

    mesh::Identity signer(senderPub);
    const bool signatureOk = signer.verify(signature, signData, (int)signLen);

    memset(signature, 0, sizeof(signature));
    memset(signData, 0, sizeof(signData));
    memset(senderPub, 0, sizeof(senderPub));
    memset(plain, 0, sizeof(plain));

    if (!signatureOk) {
        memset(text, 0, sizeof(text));
        return false;
    }

    const bool duplicate = seenOrRemember(expected);
    if (!duplicate) {
        mesh::GroupChannel channel{};
        memcpy(channel.secret, secret, PUB_KEY_SIZE);
        _mesh->v27InjectGlobalChannel(channel, timestamp, text);
    }
    memset(text, 0, sizeof(text));
    return true;
}

bool V11GlobalBridge::deriveDmKey(const uint8_t peerPub[32],
                                  uint8_t key[32]) const {
    if (!_mesh || !peerPub || !key) return false;
    uint8_t shared[32] = {};
    if (!_mesh->v27CalcSharedSecretCached(peerPub, shared)) return false;

    uint8_t info[96] = {};
    static const char ctx[] = "MOG27-DM-KEY";
    size_t n = 0;
    memcpy(info + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    if (memcmp(_selfPub, peerPub, PUB_KEY_SIZE) <= 0) {
        memcpy(info + n, _selfPub, PUB_KEY_SIZE); n += PUB_KEY_SIZE;
        memcpy(info + n, peerPub, PUB_KEY_SIZE); n += PUB_KEY_SIZE;
    } else {
        memcpy(info + n, peerPub, PUB_KEY_SIZE); n += PUB_KEY_SIZE;
        memcpy(info + n, _selfPub, PUB_KEY_SIZE); n += PUB_KEY_SIZE;
    }

    const bool ok = hmac256(shared, sizeof(shared), info, n, key);
    memset(shared, 0, sizeof(shared));
    memset(info, 0, sizeof(info));
    return ok;
}

bool V11GlobalBridge::deriveChannelKey(
    const uint8_t secret[PUB_KEY_SIZE],
    uint8_t key[32]) const {
    static const uint8_t ctx[] = "MOG27-CH-KEY";
    return secret && key &&
           hmac256(secret, PUB_KEY_SIZE, ctx, sizeof(ctx) - 1, key);
}

bool V11GlobalBridge::routeHexFor(const uint8_t pub[32], char out[33]) const {
    if (!pub || !out) return false;
    uint8_t pairKey[32] = {};
    if (!deriveDmKey(pub, pairKey)) return false;

    static const uint8_t ctx[] = "MOG27-DM-ROUTE";
    uint8_t digest[32] = {};
    const bool ok = hmac256(pairKey, sizeof(pairKey),
                            ctx, sizeof(ctx) - 1, digest);
    memset(pairKey, 0, sizeof(pairKey));
    if (!ok) return false;
    hexEncode(digest, 16, out);
    memset(digest, 0, sizeof(digest));
    return true;
}

bool V11GlobalBridge::routeHexForChannel(
    const uint8_t secret[PUB_KEY_SIZE],
    char out[33]) const {
    if (!secret || !out) return false;
    static const uint8_t ctx[] = "MOG27-CH-INBOX";
    uint8_t digest[32] = {};
    if (!hmac256(secret, PUB_KEY_SIZE, ctx, sizeof(ctx) - 1, digest))
        return false;
    hexEncode(digest, 16, out);
    memset(digest, 0, sizeof(digest));
    return true;
}

bool V11GlobalBridge::messageIdFor(const uint8_t peerPub[32],
                                   uint32_t timestamp,
                                   const char* text,
                                   uint8_t out[MSG_ID_LEN]) const {
    if (!peerPub || !text || !out) return false;

    uint8_t key[32] = {};
    if (!deriveDmKey(peerPub, key)) return false;

    uint8_t input[192] = {};
    static const char ctx[] = "MOG27-ID-DM";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1); n += sizeof(ctx) - 1;
    memcpy(input + n, &timestamp, sizeof(timestamp)); n += sizeof(timestamp);
    const size_t tlen = strnlen(text, MAX_TEXT);
    input[n++] = (uint8_t)(tlen & 0xff);
    input[n++] = (uint8_t)((tlen >> 8) & 0xff);
    if (tlen) { memcpy(input + n, text, tlen); n += tlen; }

    uint8_t digest[32] = {};
    const bool ok = hmac256(key, sizeof(key), input, n, digest);
    if (ok) memcpy(out, digest, MSG_ID_LEN);
    memset(key, 0, sizeof(key));
    memset(input, 0, sizeof(input));
    memset(digest, 0, sizeof(digest));
    return ok;
}

bool V11GlobalBridge::channelMessageIdFor(
    const uint8_t secret[PUB_KEY_SIZE],
    uint32_t timestamp,
    const char* text,
    uint8_t out[MSG_ID_LEN]) const {
    if (!secret || !text || !out) return false;

    uint8_t key[32] = {};
    if (!deriveChannelKey(secret, key)) return false;

    uint8_t input[192] = {};
    static const char ctx[] = "MOG27-ID-CH";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1); n += sizeof(ctx) - 1;
    memcpy(input + n, &timestamp, sizeof(timestamp)); n += sizeof(timestamp);
    const size_t tlen = strnlen(text, MAX_TEXT);
    input[n++] = (uint8_t)(tlen & 0xff);
    input[n++] = (uint8_t)((tlen >> 8) & 0xff);
    if (tlen) { memcpy(input + n, text, tlen); n += tlen; }

    uint8_t digest[32] = {};
    const bool ok = hmac256(key, sizeof(key), input, n, digest);
    if (ok) memcpy(out, digest, MSG_ID_LEN);
    memset(key, 0, sizeof(key));
    memset(input, 0, sizeof(input));
    memset(digest, 0, sizeof(digest));
    return ok;
}

bool V11GlobalBridge::allowInbound() {
    const uint32_t now = millis();
    if (_rxWindowStartMs == 0 ||
        (uint32_t)(now - _rxWindowStartMs) >= RX_RATE_WINDOW_MS) {
        _rxWindowStartMs = now;
        _rxWindowCount = 0;
    }
    if (_rxWindowCount >= RX_RATE_MAX_PER_WINDOW) return false;
    ++_rxWindowCount;
    return true;
}

bool V11GlobalBridge::seenOrRemember(const uint8_t id[MSG_ID_LEN]) {
    if (!id) return false;
    bool any = false;
    for (size_t j = 0; j < MSG_ID_LEN; ++j) any = any || id[j] != 0;
    if (!any) return false;

    for (int i = 0; i < DEDUP_CAP; ++i) {
        if (memcmp(_dedup[i], id, MSG_ID_LEN) == 0) return true;
    }

    memcpy(_dedup[_dedupNext], id, MSG_ID_LEN);
    _dedupNext = (uint8_t)((_dedupNext + 1) % DEDUP_CAP);
    return false;
}

bool V11GlobalBridge::channelStillConfigured(
    const uint8_t secret[PUB_KEY_SIZE]) const {
    if (!_mesh || !secret) return false;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (_mesh->v27GetChannelByIndex((uint8_t)i, cd) &&
            memcmp(cd.channel.secret, secret, PUB_KEY_SIZE) == 0) return true;
    }
    return false;
}

bool V11GlobalBridge::noteLoRaDM(const uint8_t senderPub[32],
                                 uint32_t timestamp,
                                 const char* text) {
    if (!_started || !senderPub || !text) return false;
    uint8_t id[MSG_ID_LEN] = {};
    if (!messageIdFor(senderPub, timestamp, text, id)) return false;
    return seenOrRemember(id);
}

bool V11GlobalBridge::noteLoRaChannel(const mesh::GroupChannel& channel,
                                      uint32_t timestamp,
                                      const char* text) {
    if (!_started || !text) return false;
    uint8_t id[MSG_ID_LEN] = {};
    if (!channelMessageIdFor(channel.secret, timestamp, text, id)) return false;
    return seenOrRemember(id);
}

void V11GlobalBridge::hexEncode(const uint8_t* in, size_t len, char* out) {
    static const char h[] = "0123456789abcdef";
    if (!in || !out) return;
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = h[in[i] >> 4];
        out[i * 2 + 1] = h[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

bool V11GlobalBridge::hexDecode(const char* in, uint8_t* out, size_t len) {
    if (!in || !out || strlen(in) != len * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < len; ++i) {
        const int hi = nibble(in[i * 2]);
        const int lo = nibble(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool V11GlobalBridge::base64Encode(const uint8_t* in, size_t len, String& out) {
    if (!in) return false;
    char buf[460] = {};
    size_t olen = 0;
    if (mbedtls_base64_encode(
            reinterpret_cast<unsigned char*>(buf),
            sizeof(buf) - 1, &olen,
            in, len) != 0) return false;
    buf[olen] = '\0';
    out = buf;
    return true;
}

bool V11GlobalBridge::base64Decode(const char* in,
                                   uint8_t* out,
                                   size_t cap,
                                   size_t& outLen) {
    outLen = 0;
    if (!in || !out) return false;
    return mbedtls_base64_decode(
               out, cap, &outLen,
               reinterpret_cast<const unsigned char*>(in),
               strlen(in)) == 0;
}

#endif
