#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11) && defined(MESH_OFFGRIDNL_V27)

#include "V11GlobalBridge.h"
#include "../../MyMesh.h"
#include <WiFi.h>
#include <Utils.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

#ifndef V27_GLOBAL_BROKER
#define V27_GLOBAL_BROKER "broker.emqx.io"
#endif

#ifndef V27_GLOBAL_PORT
#define V27_GLOBAL_PORT 1883
#endif

V11GlobalBridge v11_global_bridge;
V11GlobalBridge* V11GlobalBridge::s_instance = nullptr;

namespace {
bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md(info, data, len, out) == 0;
}

bool hmac256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len, uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, keyLen, data, len, out) == 0;
}

uint64_t idToU64(const uint8_t id[8]) {
    uint64_t v = 0;
    memcpy(&v, id, sizeof(v));
    return v;
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
    memcpy(_selfPub, _mesh->getSelfPubKey(), sizeof(_selfPub));

    _mqtt.setServer(V27_GLOBAL_BROKER, V27_GLOBAL_PORT);
    _mqtt.setKeepAlive(45);
    _mqtt.setSocketTimeout(2);
    _mqtt.setBufferSize(512);
    _mqtt.setCallback(mqttThunk);

    Serial.println("[V27] Global Privacy transport ready");
}

void V11GlobalBridge::mqttThunk(char* topic, uint8_t* payload, unsigned int len) {
    if (s_instance) s_instance->onMqtt(topic, payload, len);
}

void V11GlobalBridge::connectTask(void* arg) {
    V11GlobalBridge* self = static_cast<V11GlobalBridge*>(arg);
    self->connectNow();
    self->_connecting = false;
    vTaskDelete(nullptr);
}

bool V11GlobalBridge::connectNow() {
    if (!_started || WiFi.status() != WL_CONNECTED) return false;

    char clientId[40];
    snprintf(clientId, sizeof(clientId), "mog27-%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());

    if (!_mqtt.connect(clientId)) {
        const uint32_t now = millis();
        const uint32_t jitter = esp_random() % 1200U;
        _nextConnectAt = now + _retryDelayMs + jitter;
        _retryDelayMs = (_retryDelayMs >= RETRY_MAX_MS / 2)
                            ? RETRY_MAX_MS
                            : _retryDelayMs * 2;
        Serial.printf("[V27] Global transport retry rc=%d\n", _mqtt.state());
        return false;
    }

    if (!subscribeContacts() || !subscribeChannels()) {
        _mqtt.disconnect();
        _nextConnectAt = millis() + _retryDelayMs + (esp_random() % 1200U);
        return false;
    }

    _contactFingerprint = contactFingerprint();
    _channelFingerprint = channelFingerprint();
    _lastConfigCheckMs = millis();
    _retryDelayMs = RETRY_MIN_MS;
    _nextConnectAt = 0;
    Serial.println("[V27] Global transport connected");
    return true;
}

void V11GlobalBridge::loop() {
    if (!_started || !_mesh) return;

    const bool wifiUp = WiFi.status() == WL_CONNECTED;
    if (!wifiUp) {
        _wifiWasConnected = false;
        if (_mqtt.connected()) _mqtt.disconnect();
        return;
    }

    if (!_wifiWasConnected) {
        _wifiWasConnected = true;
        _nextConnectAt = 0;
        _retryDelayMs = RETRY_MIN_MS;
    }

    if (!_mqtt.connected()) {
        const uint32_t now = millis();
        if (!_connecting && due(now, _nextConnectAt)) {
            _connecting = true;
            if (xTaskCreatePinnedToCore(connectTask, "v27_global", 6144, this,
                                        1, nullptr, 0) != pdPASS) {
                _connecting = false;
                _nextConnectAt = now + RETRY_MIN_MS;
            }
        }
        return;
    }

    if (_connecting) return;

    if (!_mqtt.loop()) {
        _nextConnectAt = millis() + RETRY_MIN_MS;
        return;
    }

    const uint32_t now = millis();
    if ((uint32_t)(now - _lastConfigCheckMs) >= 30000UL) {
        _lastConfigCheckMs = now;
        const uint32_t contactFp = contactFingerprint();
        const uint32_t channelFp = channelFingerprint();
        if (contactFp != _contactFingerprint || channelFp != _channelFingerprint) {
            // Reconnect atomically refreshes pair-wise DM routes and group
            // subscriptions after contacts/channels are added, removed or changed.
            _contactFingerprint = contactFp;
            _channelFingerprint = channelFp;
            _mqtt.disconnect();
            _nextConnectAt = 0;
            return;
        }
    }

    flushOne();
    flushOneChannel();
}

void V11GlobalBridge::routeTopicFor(const uint8_t pub[32], char* out, size_t outCap) const {
    if (!pub || !out || outCap == 0) return;

    uint8_t pairKey[32] = {};
    if (!deriveDmKey(pub, pairKey)) {
        out[0] = '\0';
        return;
    }

    static const uint8_t ctx[] = "MOG27-DM-ROUTE";
    uint8_t digest[32] = {};
    if (!hmac256(pairKey, sizeof(pairKey), ctx, sizeof(ctx) - 1, digest)) {
        out[0] = '\0';
        memset(pairKey, 0, sizeof(pairKey));
        return;
    }

    // 128-bit opaque route token: not derivable from a public key alone and
    // identical at both peers because deriveDmKey() is pair-wise canonical.
    char tag[33] = {};
    for (int i = 0; i < 16; ++i) snprintf(tag + i * 2, 3, "%02x", digest[i]);
    snprintf(out, outCap, "mog27/v2/r/%s", tag);

    memset(digest, 0, sizeof(digest));
    memset(pairKey, 0, sizeof(pairKey));
}

void V11GlobalBridge::routeTopicForChannel(const uint8_t secret[PUB_KEY_SIZE], char* out, size_t outCap) const {
    static const uint8_t ctx[] = "MOG27-CH-INBOX";
    uint8_t digest[32] = {};
    if (!hmac256(secret, PUB_KEY_SIZE, ctx, sizeof(ctx) - 1, digest)) {
        if (outCap) out[0] = '\0';
        return;
    }
    char tag[17] = {};
    for (int i = 0; i < 8; ++i) snprintf(tag + i * 2, 3, "%02x", digest[i]);
    snprintf(out, outCap, "mog27/v2/r/%s", tag);
    memset(digest, 0, sizeof(digest));
}

bool V11GlobalBridge::deriveDmKey(const uint8_t peerPub[32], uint8_t key[32]) const {
    if (!_mesh || !peerPub || !key) return false;

    uint8_t shared[32] = {};
    if (!_mesh->v11CalcSharedSecret(peerPub, shared)) return false;

    uint8_t info[96] = {};
    static const char ctx[] = "MOG27-DM-KEY";
    size_t n = 0;
    memcpy(info + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;

    if (memcmp(_selfPub, peerPub, 32) <= 0) {
        memcpy(info + n, _selfPub, 32); n += 32;
        memcpy(info + n, peerPub, 32); n += 32;
    } else {
        memcpy(info + n, peerPub, 32); n += 32;
        memcpy(info + n, _selfPub, 32); n += 32;
    }

    const bool ok = hmac256(shared, sizeof(shared), info, n, key);
    memset(shared, 0, sizeof(shared));
    memset(info, 0, sizeof(info));
    return ok;
}

bool V11GlobalBridge::deriveChannelKey(const uint8_t secret[PUB_KEY_SIZE], uint8_t key[32]) const {
    static const uint8_t ctx[] = "MOG27-CH-KEY";
    return secret && key && hmac256(secret, PUB_KEY_SIZE, ctx, sizeof(ctx) - 1, key);
}

bool V11GlobalBridge::messageIdFor(const uint8_t peerPub[32], uint32_t timestamp,
                                   const char* text, uint8_t out[8]) const {
    if (!peerPub || !text || !out) return false;

    uint8_t key[32] = {};
    if (!deriveDmKey(peerPub, key)) return false;

    uint8_t input[192] = {};
    static const char ctx[] = "MOG27-ID-DM";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    memcpy(input + n, &timestamp, sizeof(timestamp));
    n += sizeof(timestamp);

    const size_t tlen = strnlen(text, MAX_TEXT);
    input[n++] = (uint8_t)(tlen & 0xff);
    input[n++] = (uint8_t)((tlen >> 8) & 0xff);
    if (tlen) {
        memcpy(input + n, text, tlen);
        n += tlen;
    }

    uint8_t digest[32] = {};
    const bool ok = hmac256(key, sizeof(key), input, n, digest);
    if (ok) memcpy(out, digest, 8);
    memset(key, 0, sizeof(key));
    memset(input, 0, sizeof(input));
    memset(digest, 0, sizeof(digest));
    return ok;
}

bool V11GlobalBridge::channelMessageIdFor(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp,
                                          const char* text, uint8_t out[8]) const {
    if (!secret || !text || !out) return false;

    uint8_t key[32] = {};
    if (!deriveChannelKey(secret, key)) return false;

    uint8_t input[192] = {};
    static const char ctx[] = "MOG27-ID-CH";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    memcpy(input + n, &timestamp, sizeof(timestamp));
    n += sizeof(timestamp);
    const size_t tlen = strnlen(text, MAX_TEXT);
    input[n++] = (uint8_t)(tlen & 0xff);
    input[n++] = (uint8_t)((tlen >> 8) & 0xff);
    if (tlen) {
        memcpy(input + n, text, tlen);
        n += tlen;
    }

    uint8_t digest[32] = {};
    const bool ok = hmac256(key, sizeof(key), input, n, digest);
    if (ok) memcpy(out, digest, 8);
    memset(key, 0, sizeof(key));
    memset(input, 0, sizeof(input));
    memset(digest, 0, sizeof(digest));
    return ok;
}

bool V11GlobalBridge::allowInbound() {
    const uint32_t now = millis();
    if (_rxWindowStartMs == 0 || (uint32_t)(now - _rxWindowStartMs) >= RX_RATE_WINDOW_MS) {
        _rxWindowStartMs = now;
        _rxWindowCount = 0;
    }
    if (_rxWindowCount >= RX_RATE_MAX_PER_WINDOW) return false;
    ++_rxWindowCount;
    return true;
}

bool V11GlobalBridge::seenOrRemember(const uint8_t id[8]) {
    const uint64_t v = idToU64(id);
    if (v == 0) return false;

    for (int i = 0; i < DEDUP_CAP; ++i) {
        if (_dedup[i] == v) return true;
    }

    _dedup[_dedupNext] = v;
    _dedupNext = (uint8_t)((_dedupNext + 1) % DEDUP_CAP);
    return false;
}

bool V11GlobalBridge::enqueue(const uint8_t recipient[32], uint32_t timestamp, const char* text) {
    if (!recipient || !text) return false;

    if (_pendingCount >= PENDING_CAP) {
        memset(&_pending[_pendingHead], 0, sizeof(Pending));
        _pendingHead = (uint8_t)((_pendingHead + 1) % PENDING_CAP);
        --_pendingCount;
    }

    const uint8_t slot = (uint8_t)((_pendingHead + _pendingCount) % PENDING_CAP);
    Pending& p = _pending[slot];
    memset(&p, 0, sizeof(p));
    p.used = true;
    memcpy(p.recipient, recipient, 32);
    p.timestamp = timestamp;
    p.queuedMs = millis();
    strncpy(p.text, text, MAX_TEXT);
    p.text[MAX_TEXT] = '\0';
    ++_pendingCount;
    return true;
}

bool V11GlobalBridge::enqueueChannel(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text) {
    if (!secret || !text) return false;

    if (_pendingChannelCount >= PENDING_CHANNEL_CAP) {
        memset(&_pendingChannel[_pendingChannelHead], 0, sizeof(PendingChannel));
        _pendingChannelHead = (uint8_t)((_pendingChannelHead + 1) % PENDING_CHANNEL_CAP);
        --_pendingChannelCount;
    }

    const uint8_t slot = (uint8_t)((_pendingChannelHead + _pendingChannelCount) % PENDING_CHANNEL_CAP);
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

void V11GlobalBridge::flushOne() {
    if (_pendingCount == 0 || !_mqtt.connected()) return;

    Pending& p = _pending[_pendingHead];
    if (!p.used || (uint32_t)(millis() - p.queuedMs) > PENDING_TTL_MS) {
        memset(&p, 0, sizeof(p));
        _pendingHead = (uint8_t)((_pendingHead + 1) % PENDING_CAP);
        --_pendingCount;
        return;
    }

    if (!publishDMNow(p.recipient, p.timestamp, p.text)) return;

    memset(&p, 0, sizeof(p));
    _pendingHead = (uint8_t)((_pendingHead + 1) % PENDING_CAP);
    --_pendingCount;
}

bool V11GlobalBridge::channelStillConfigured(const uint8_t secret[PUB_KEY_SIZE]) const {
    if (!_mesh || !secret) return false;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (_mesh->v27GetChannelByIndex((uint8_t)i, cd) &&
            memcmp(cd.channel.secret, secret, PUB_KEY_SIZE) == 0) return true;
    }
    return false;
}

void V11GlobalBridge::flushOneChannel() {
    if (_pendingChannelCount == 0 || !_mqtt.connected()) return;

    PendingChannel& p = _pendingChannel[_pendingChannelHead];
    if (!p.used || (uint32_t)(millis() - p.queuedMs) > PENDING_TTL_MS ||
        !channelStillConfigured(p.secret)) {
        memset(&p, 0, sizeof(p));
        _pendingChannelHead = (uint8_t)((_pendingChannelHead + 1) % PENDING_CHANNEL_CAP);
        --_pendingChannelCount;
        return;
    }

    if (!publishChannelNow(p.secret, p.timestamp, p.text)) return;

    memset(&p, 0, sizeof(p));
    _pendingChannelHead = (uint8_t)((_pendingChannelHead + 1) % PENDING_CHANNEL_CAP);
    --_pendingChannelCount;
}

bool V11GlobalBridge::mirrorDM(const ContactInfo& recipient, uint32_t timestamp,
                               const char* text, bool allowQueue) {
    if (!_started || !_mesh || !text || recipient.type != ADV_TYPE_CHAT) return false;

    if (WiFi.status() != WL_CONNECTED || _connecting || !_mqtt.connected()) {
        return allowQueue ? enqueue(recipient.id.pub_key, timestamp, text) : false;
    }

    if (publishDMNow(recipient.id.pub_key, timestamp, text)) return true;
    return allowQueue ? enqueue(recipient.id.pub_key, timestamp, text) : false;
}

bool V11GlobalBridge::mirrorChannelPacket(const mesh::GroupChannel& channel, const mesh::Packet* packet) {
    if (!_started || !_mesh || !packet || packet->getPayloadType() != PAYLOAD_TYPE_GRP_TXT) return false;
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

    if (WiFi.status() != WL_CONNECTED || _connecting || !_mqtt.connected()) {
        return enqueueChannel(channel.secret, timestamp, text);
    }
    if (publishChannelNow(channel.secret, timestamp, text)) return true;
    return enqueueChannel(channel.secret, timestamp, text);
}

bool V11GlobalBridge::publishDMNow(const uint8_t recipient[32],
                                   uint32_t timestamp,
                                   const char* text) {
    if (!_mqtt.connected() || !recipient || !text) return false;

    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    uint8_t wire[MAX_WIRE] = {};
    wire[0] = 'M'; wire[1] = 'G'; wire[2] = '2'; wire[3] = '7';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_DM;

    uint8_t msgId[8] = {};
    if (!messageIdFor(recipient, timestamp, text, msgId)) return false;
    memcpy(wire + 6, msgId, 8);
    // Pair-wise secret topic identifies the conversation to the two endpoints;
    // keep the visible header unlinkable to the sender identity.
    esp_fill_random(wire + 14, 8);
    esp_fill_random(wire + 22, 12);

    uint8_t plain[PLAIN_LEN] = {};
    esp_fill_random(plain, sizeof(plain));
    memcpy(plain, _selfPub, 32);
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
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, PLAIN_LEN,
                                       wire + 22, 12, wire, AAD_LEN, plain,
                                       wire + HEADER_LEN, TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(plain, 0, sizeof(plain));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + PLAIN_LEN, tag, TAG_LEN);
    char topic[64] = {};
    routeTopicFor(recipient, topic, sizeof(topic));
    return topic[0] && _mqtt.publish(topic, wire, (unsigned int)MAX_WIRE, false);
}

bool V11GlobalBridge::publishChannelNow(const uint8_t secret[PUB_KEY_SIZE],
                                        uint32_t timestamp,
                                        const char* text) {
    if (!_mqtt.connected() || !secret || !text) return false;

    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    uint8_t wire[MAX_WIRE] = {};
    wire[0] = 'M'; wire[1] = 'G'; wire[2] = '2'; wire[3] = '7';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_CHANNEL;

    uint8_t msgId[8] = {};
    if (!channelMessageIdFor(secret, timestamp, text, msgId)) return false;
    memcpy(wire + 6, msgId, 8);
    // We subscribe to the same channel topic we publish to. Remember our own
    // keyed ID before publish so broker loopback cannot create a second bubble.
    seenOrRemember(msgId);
    esp_fill_random(wire + 14, 8);
    esp_fill_random(wire + 22, 12);

    uint8_t plain[PLAIN_LEN] = {};
    esp_fill_random(plain, sizeof(plain));
    memcpy(plain, &timestamp, 4);
    plain[4] = (uint8_t)(tlen & 0xff);
    plain[5] = (uint8_t)((tlen >> 8) & 0xff);
    memcpy(plain + 6, text, tlen);

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
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, PLAIN_LEN,
                                       wire + 22, 12, wire, AAD_LEN, plain,
                                       wire + HEADER_LEN, TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(plain, 0, sizeof(plain));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + PLAIN_LEN, tag, TAG_LEN);
    char topic[64] = {};
    routeTopicForChannel(secret, topic, sizeof(topic));
    return topic[0] && _mqtt.publish(topic, wire, (unsigned int)MAX_WIRE, false);
}

bool V11GlobalBridge::subscribeContacts() {
    if (!_mesh || !_mqtt.connected()) return false;
    const uint32_t count = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < count; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact) || contact.type != ADV_TYPE_CHAT) continue;
        char topic[80] = {};
        routeTopicFor(contact.id.pub_key, topic, sizeof(topic));
        if (!topic[0] || !_mqtt.subscribe(topic, 1)) return false;
    }
    return true;
}

uint32_t V11GlobalBridge::contactFingerprint() const {
    if (!_mesh) return 0;
    uint32_t fp = 2166136261UL;
    const uint32_t count = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < count; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact) || contact.type != ADV_TYPE_CHAT) continue;
        for (size_t j = 0; j < PUB_KEY_SIZE; ++j) {
            fp ^= contact.id.pub_key[j];
            fp *= 16777619UL;
        }
    }
    return fp;
}

bool V11GlobalBridge::findContactForTopic(const char* topic, ContactInfo& out) const {
    if (!_mesh || !topic) return false;
    const uint32_t count = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < count; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact) || contact.type != ADV_TYPE_CHAT) continue;
        char candidate[80] = {};
        routeTopicFor(contact.id.pub_key, candidate, sizeof(candidate));
        if (candidate[0] && strcmp(candidate, topic) == 0) {
            out = contact;
            return true;
        }
    }
    return false;
}

bool V11GlobalBridge::subscribeChannels() {
    if (!_mesh || !_mqtt.connected()) return false;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (!_mesh->v27GetChannelByIndex((uint8_t)i, cd)) continue;
        char topic[64] = {};
        routeTopicForChannel(cd.channel.secret, topic, sizeof(topic));
        if (!topic[0] || !_mqtt.subscribe(topic, 1)) return false;
    }
    return true;
}

uint32_t V11GlobalBridge::channelFingerprint() const {
    if (!_mesh) return 0;
    uint32_t fp = 2166136261UL;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (!_mesh->v27GetChannelByIndex((uint8_t)i, cd)) continue;
        for (size_t j = 0; j < PUB_KEY_SIZE; ++j) {
            fp ^= cd.channel.secret[j];
            fp *= 16777619UL;
        }
        for (size_t j = 0; cd.name[j] && j < sizeof(cd.name); ++j) {
            fp ^= (uint8_t)cd.name[j];
            fp *= 16777619UL;
        }
    }
    return fp;
}

bool V11GlobalBridge::findChannelForTopic(const char* topic, mesh::GroupChannel& out) const {
    if (!_mesh || !topic) return false;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (!_mesh->v27GetChannelByIndex((uint8_t)i, cd)) continue;
        char candidate[64] = {};
        routeTopicForChannel(cd.channel.secret, candidate, sizeof(candidate));
        if (candidate[0] && strcmp(candidate, topic) == 0) {
            out = cd.channel;
            return true;
        }
    }
    return false;
}

void V11GlobalBridge::onMqtt(char* topic, uint8_t* payload, unsigned int len) {
    if (!_mesh || !topic || !payload || len != MAX_WIRE) return;
    if (!allowInbound()) return;
    if (payload[0] != 'M' || payload[1] != 'G' ||
        payload[2] != '2' || payload[3] != '7' ||
        payload[4] != PROTOCOL_VERSION) return;

    if (payload[5] == KIND_DM) {
        ContactInfo contact{};
        if (!findContactForTopic(topic, contact)) return;

        uint8_t key[32] = {};
        if (!deriveDmKey(contact.id.pub_key, key)) return;

        uint8_t plain[PLAIN_LEN] = {};
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
        if (rc == 0) {
            rc = mbedtls_gcm_auth_decrypt(&gcm, PLAIN_LEN, payload + 22, 12,
                                          payload, AAD_LEN,
                                          payload + HEADER_LEN + PLAIN_LEN, TAG_LEN,
                                          payload + HEADER_LEN, plain);
        }
        mbedtls_gcm_free(&gcm);
        memset(key, 0, sizeof(key));
        if (rc != 0 || memcmp(plain, contact.id.pub_key, 32) != 0) {
            memset(plain, 0, sizeof(plain));
            return;
        }

        uint32_t timestamp = 0;
        memcpy(&timestamp, plain + 32, 4);
        const uint16_t tlen = (uint16_t)plain[36] | ((uint16_t)plain[37] << 8);
        if (tlen == 0 || tlen > MAX_TEXT) {
            memset(plain, 0, sizeof(plain));
            return;
        }

        char text[MAX_TEXT + 1] = {};
        memcpy(text, plain + 38, tlen);
        text[tlen] = '\0';

        uint8_t expectedId[8] = {};
        const bool reject =
            !messageIdFor(contact.id.pub_key, timestamp, text, expectedId) ||
            memcmp(expectedId, payload + 6, 8) != 0 ||
            seenOrRemember(expectedId);
        memset(plain, 0, sizeof(plain));
        if (reject) {
            memset(text, 0, sizeof(text));
            return;
        }

        _mesh->v11InjectGlobalDm(contact.id.pub_key, timestamp, text);
        memset(text, 0, sizeof(text));
        return;
    }

    if (payload[5] == KIND_CHANNEL) {
        mesh::GroupChannel channel{};
        if (!findChannelForTopic(topic, channel)) return;

        uint8_t key[32] = {};
        if (!deriveChannelKey(channel.secret, key)) return;

        uint8_t plain[PLAIN_LEN] = {};
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
        if (rc == 0) {
            rc = mbedtls_gcm_auth_decrypt(&gcm, PLAIN_LEN, payload + 22, 12,
                                          payload, AAD_LEN,
                                          payload + HEADER_LEN + PLAIN_LEN, TAG_LEN,
                                          payload + HEADER_LEN, plain);
        }
        mbedtls_gcm_free(&gcm);
        memset(key, 0, sizeof(key));
        if (rc != 0) {
            memset(plain, 0, sizeof(plain));
            return;
        }

        uint32_t timestamp = 0;
        memcpy(&timestamp, plain, 4);
        const uint16_t tlen = (uint16_t)plain[4] | ((uint16_t)plain[5] << 8);
        if (tlen == 0 || tlen > MAX_TEXT) {
            memset(plain, 0, sizeof(plain));
            return;
        }

        char text[MAX_TEXT + 1] = {};
        memcpy(text, plain + 6, tlen);
        text[tlen] = '\0';

        uint8_t expectedId[8] = {};
        const bool reject =
            !channelMessageIdFor(channel.secret, timestamp, text, expectedId) ||
            memcmp(expectedId, payload + 6, 8) != 0 ||
            seenOrRemember(expectedId);
        memset(plain, 0, sizeof(plain));
        if (reject) {
            memset(text, 0, sizeof(text));
            return;
        }

        _mesh->v27InjectGlobalChannel(channel, timestamp, text);
        memset(text, 0, sizeof(text));
    }
}

bool V11GlobalBridge::noteLoRaDM(const uint8_t senderPub[32],
                                 uint32_t timestamp,
                                 const char* text) {
    if (!_started || !senderPub || !text) return false;
    uint8_t id[8] = {};
    if (!messageIdFor(senderPub, timestamp, text, id)) return false;
    return seenOrRemember(id);
}

bool V11GlobalBridge::noteLoRaChannel(const mesh::GroupChannel& channel,
                                      uint32_t timestamp,
                                      const char* text) {
    if (!_started || !text) return false;
    uint8_t id[8] = {};
    if (!channelMessageIdFor(channel.secret, timestamp, text, id)) return false;
    return seenOrRemember(id);
}

#endif
