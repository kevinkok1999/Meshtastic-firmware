#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11) && defined(MESH_OFFGRIDNL_V27)

#include "V11GlobalBridge.h"
#include "../../MyMesh.h"
#include <WiFi.h>
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
    routeTopicFor(_selfPub, _dmTopic, sizeof(_dmTopic));

    _mqtt.setServer(V27_GLOBAL_BROKER, V27_GLOBAL_PORT);
    _mqtt.setKeepAlive(45);
    _mqtt.setSocketTimeout(2);
    _mqtt.setBufferSize(512);
    _mqtt.setCallback(mqttThunk);

    // Deliberately do not log broker host, route topic, public-key prefix or
    // message metadata. Normal users never need to see transport internals.
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

    if (!_mqtt.subscribe(_dmTopic, 1)) {
        _mqtt.disconnect();
        _nextConnectAt = millis() + _retryDelayMs + (esp_random() % 1200U);
        return false;
    }

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
        // Fresh Wi-Fi edge: attempt immediately rather than making the user
        // wait for a previous backoff window.
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

    if (!_connecting) {
        if (!_mqtt.loop()) {
            _nextConnectAt = millis() + RETRY_MIN_MS;
            return;
        }
        flushOne();
    }
}

void V11GlobalBridge::routeTopicFor(const uint8_t pub[32], char* out, size_t outCap) const {
    uint8_t input[64] = {};
    static const char ctx[] = "MOG27-INBOX";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    memcpy(input + n, pub, 32);
    n += 32;

    uint8_t digest[32] = {};
    if (!sha256(input, n, digest)) {
        if (outCap) out[0] = '\0';
        return;
    }

    char tag[17] = {};
    for (int i = 0; i < 8; ++i) snprintf(tag + i * 2, 3, "%02x", digest[i]);
    snprintf(out, outCap, "mog27/v2/d/%s", tag);
    memset(digest, 0, sizeof(digest));
    memset(input, 0, sizeof(input));
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

    // Canonical public-key ordering makes the KDF identical at both peers.
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
        // Prefer keeping the newest user intent. RF has already had its own
        // independent attempt, so discarding the oldest Internet mirror cannot
        // block local/off-grid delivery.
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

bool V11GlobalBridge::mirrorDM(const ContactInfo& recipient, uint32_t timestamp, const char* text) {
    if (!_started || !_mesh || !text || recipient.type != ADV_TYPE_CHAT) return false;

    // Zero-config behavior: even when Wi-Fi is currently absent, accept a
    // bounded Internet mirror for later. The existing RF send path remains
    // completely independent and immediate.
    if (WiFi.status() != WL_CONNECTED || _connecting || !_mqtt.connected()) {
        return enqueue(recipient.id.pub_key, timestamp, text);
    }

    if (publishDMNow(recipient.id.pub_key, timestamp, text)) return true;
    return enqueue(recipient.id.pub_key, timestamp, text);
}

bool V11GlobalBridge::publishDMNow(const uint8_t recipient[32],
                                   uint32_t timestamp,
                                   const char* text) {
    if (!_mqtt.connected() || !recipient || !text) return false;

    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    uint8_t wire[MAX_WIRE] = {};
    wire[0] = 'M';
    wire[1] = 'G';
    wire[2] = '2';
    wire[3] = '7';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_DM;

    uint8_t msgId[8] = {};
    if (!messageIdFor(recipient, timestamp, text, msgId)) return false;
    memcpy(wire + 6, msgId, 8);

    // Only a short routing hint is visible. The full sender public key is
    // carried inside the authenticated ciphertext.
    memcpy(wire + 14, _selfPub, 8);
    esp_fill_random(wire + 22, 12);

    uint8_t plain[PLAIN_LEN] = {};
    esp_fill_random(plain, sizeof(plain)); // randomized padding hides exact text length
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
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm,
            MBEDTLS_GCM_ENCRYPT,
            PLAIN_LEN,
            wire + 22,
            12,
            wire,
            AAD_LEN,
            plain,
            wire + HEADER_LEN,
            TAG_LEN,
            tag);
    }

    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(plain, 0, sizeof(plain));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + PLAIN_LEN, tag, TAG_LEN);

    char topic[64] = {};
    routeTopicFor(recipient, topic, sizeof(topic));
    if (!topic[0]) return false;

    // Constant-size publish: the broker cannot learn exact message length.
    return _mqtt.publish(topic, wire, (unsigned int)MAX_WIRE, false);
}

void V11GlobalBridge::onMqtt(char* topic, uint8_t* payload, unsigned int len) {
    if (!_mesh || !topic || !payload || strcmp(topic, _dmTopic) != 0) return;
    if (len != MAX_WIRE) return;

    if (payload[0] != 'M' || payload[1] != 'G' ||
        payload[2] != '2' || payload[3] != '7') return;
    if (payload[4] != PROTOCOL_VERSION || payload[5] != KIND_DM) return;

    ContactInfo contact{};
    if (!_mesh->v27LookupChatContactByPrefix(payload + 14, contact)) return;

    uint8_t key[32] = {};
    if (!deriveDmKey(contact.id.pub_key, key)) return;

    uint8_t plain[PLAIN_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm,
            PLAIN_LEN,
            payload + 22,
            12,
            payload,
            AAD_LEN,
            payload + HEADER_LEN + PLAIN_LEN,
            TAG_LEN,
            payload + HEADER_LEN,
            plain);
    }

    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    if (rc != 0) {
        memset(plain, 0, sizeof(plain));
        return;
    }

    // Full identity is encrypted and must match the contact selected by the
    // short routing hint.
    if (memcmp(plain, contact.id.pub_key, 32) != 0) {
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
    if (!messageIdFor(contact.id.pub_key, timestamp, text, expectedId) ||
        memcmp(expectedId, payload + 6, 8) != 0 ||
        seenOrRemember(expectedId)) {
        memset(plain, 0, sizeof(plain));
        memset(text, 0, sizeof(text));
        return;
    }

    memset(plain, 0, sizeof(plain));
    _mesh->v11InjectGlobalDm(contact.id.pub_key, timestamp, text);
    memset(text, 0, sizeof(text));
}

bool V11GlobalBridge::noteLoRaDM(const uint8_t senderPub[32],
                                 uint32_t timestamp,
                                 const char* text) {
    if (!_started || !senderPub || !text) return false;

    uint8_t id[8] = {};
    if (!messageIdFor(senderPub, timestamp, text, id)) return false;
    return seenOrRemember(id);
}

#endif
