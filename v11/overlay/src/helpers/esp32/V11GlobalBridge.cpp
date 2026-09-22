#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)

#include "V11GlobalBridge.h"
#include "../../MyMesh.h"
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

#ifndef V11_GLOBAL_BROKER
#define V11_GLOBAL_BROKER "broker.emqx.io"
#endif

#ifndef V11_GLOBAL_PORT
#define V11_GLOBAL_PORT 1883
#endif

V11GlobalBridge v11_global_bridge;
V11GlobalBridge* V11GlobalBridge::s_instance = nullptr;

namespace {
bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md(info, data, len, out) == 0;
}

uint64_t idToU64(const uint8_t id[8]) {
    uint64_t v = 0;
    memcpy(&v, id, sizeof(v));
    return v;
}
}  // namespace

void V11GlobalBridge::begin(MyMesh* mesh) {
    _mesh = mesh;
    if (!_mesh || _started) return;

    _started = true;
    s_instance = this;
    memcpy(_selfPub, _mesh->getSelfPubKey(), sizeof(_selfPub));
    routeTopicFor(_selfPub, _dmTopic, sizeof(_dmTopic));

    _mqtt.setServer(V11_GLOBAL_BROKER, V11_GLOBAL_PORT);
    _mqtt.setKeepAlive(45);
    _mqtt.setSocketTimeout(2);
    _mqtt.setBufferSize(512);
    _mqtt.setCallback(mqttThunk);

    Serial.printf("[V11] Global DM ready broker=%s:%u\n",
                  V11_GLOBAL_BROKER, (unsigned)V11_GLOBAL_PORT);
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

    char clientId[32];
    const uint16_t rnd = (uint16_t)(esp_random() & 0xFFFFu);
    snprintf(clientId, sizeof(clientId), "mog11-%02x%02x%02x-%04x",
             _selfPub[0], _selfPub[1], _selfPub[2], (unsigned)rnd);

    if (!_mqtt.connect(clientId)) {
        Serial.printf("[V11] broker connect failed rc=%d\n", _mqtt.state());
        return false;
    }

    if (!_mqtt.subscribe(_dmTopic, 1)) {
        Serial.println("[V11] broker subscribe failed");
        _mqtt.disconnect();
        return false;
    }

    Serial.println("[V11] Global broker connected");
    return true;
}

void V11GlobalBridge::loop() {
    if (!_started || !_mesh) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (_mqtt.connected()) _mqtt.disconnect();
        return;
    }

    if (!_mqtt.connected()) {
        const uint32_t now = millis();
        if (!_connecting && (uint32_t)(now - _lastConnectAttempt) >= 15000UL) {
            _lastConnectAttempt = now;
            _connecting = true;
            if (xTaskCreatePinnedToCore(connectTask, "v11_mqtt", 6144, this,
                                        1, nullptr, 0) != pdPASS) {
                _connecting = false;
            }
        }
        return;
    }

    if (!_connecting) {
        if (!_mqtt.loop()) return;
        flushOne();
    }
}

void V11GlobalBridge::routeTopicFor(const uint8_t pub[32], char* out, size_t outCap) const {
    uint8_t input[64] = {};
    static const char ctx[] = "MOG11-ROUTE";
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
    snprintf(out, outCap, "mog11/v1/d/%s", tag);
}

void V11GlobalBridge::messageIdFor(const uint8_t recipient[32], const uint8_t sender[32],
                                   uint32_t timestamp, const char* text, uint8_t out[8]) const {
    uint8_t buf[256] = {};
    static const char ctx[] = "MOG11-ID-DM";
    size_t n = 0;
    memcpy(buf + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    memcpy(buf + n, recipient, 32);
    n += 32;
    memcpy(buf + n, sender, 32);
    n += 32;
    memcpy(buf + n, &timestamp, 4);
    n += 4;

    const size_t tlen = text ? strnlen(text, MAX_TEXT) : 0;
    if (tlen) {
        memcpy(buf + n, text, tlen);
        n += tlen;
    }

    uint8_t digest[32] = {};
    sha256(buf, n, digest);
    memcpy(out, digest, 8);
}

bool V11GlobalBridge::deriveDmKey(const uint8_t peerPub[32], uint8_t key[32]) const {
    if (!_mesh) return false;

    uint8_t shared[32] = {};
    if (!_mesh->v11CalcSharedSecret(peerPub, shared)) return false;

    uint8_t input[64] = {};
    static const char ctx[] = "MOG11-DM-KEY";
    size_t n = 0;
    memcpy(input + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;
    memcpy(input + n, shared, sizeof(shared));
    n += sizeof(shared);

    const bool ok = sha256(input, n, key);
    memset(shared, 0, sizeof(shared));
    memset(input, 0, sizeof(input));
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
    if (_pendingCount >= PENDING_CAP || !text) return false;

    const uint8_t slot = (uint8_t)((_pendingHead + _pendingCount) % PENDING_CAP);
    Pending& p = _pending[slot];
    p.used = true;
    memcpy(p.recipient, recipient, 32);
    p.timestamp = timestamp;
    strncpy(p.text, text, MAX_TEXT);
    p.text[MAX_TEXT] = '\0';
    ++_pendingCount;
    return true;
}

void V11GlobalBridge::flushOne() {
    if (_pendingCount == 0 || !_mqtt.connected()) return;

    Pending& p = _pending[_pendingHead];
    if (!p.used) {
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
    if (WiFi.status() != WL_CONNECTED) return false;

    if (_connecting || !_mqtt.connected()) {
        return enqueue(recipient.id.pub_key, timestamp, text);
    }

    if (publishDMNow(recipient.id.pub_key, timestamp, text)) return true;
    return enqueue(recipient.id.pub_key, timestamp, text);
}

bool V11GlobalBridge::publishDMNow(const uint8_t recipient[32],
                                   uint32_t timestamp,
                                   const char* text) {
    if (!_mqtt.connected() || !text) return false;

    const size_t tlen = strnlen(text, MAX_TEXT);
    if (tlen == 0 || tlen > MAX_TEXT) return false;

    uint8_t wire[MAX_WIRE] = {};
    wire[0] = 'M';
    wire[1] = 'G';
    wire[2] = '1';
    wire[3] = '1';
    wire[4] = PROTOCOL_VERSION;
    wire[5] = KIND_DM;

    uint8_t msgId[8] = {};
    messageIdFor(recipient, _selfPub, timestamp, text, msgId);
    memcpy(wire + 6, msgId, 8);
    memcpy(wire + 14, &timestamp, 4);
    memcpy(wire + 18, _selfPub, 32);
    esp_fill_random(wire + 50, 12);
    wire[62] = (uint8_t)(tlen & 0xFF);
    wire[63] = (uint8_t)((tlen >> 8) & 0xFF);

    uint8_t key[32] = {};
    if (!deriveDmKey(recipient, key)) return false;

    uint8_t tag[TAG_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm,
            MBEDTLS_GCM_ENCRYPT,
            tlen,
            wire + 50,
            12,
            wire,
            50,
            reinterpret_cast<const uint8_t*>(text),
            wire + HEADER_LEN,
            TAG_LEN,
            tag);
    }

    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    if (rc != 0) return false;

    memcpy(wire + HEADER_LEN + tlen, tag, TAG_LEN);

    char topic[64] = {};
    routeTopicFor(recipient, topic, sizeof(topic));
    if (!topic[0]) return false;

    const bool ok = _mqtt.publish(
        topic,
        wire,
        (unsigned int)(HEADER_LEN + tlen + TAG_LEN),
        false);

    if (ok) {
        Serial.printf("[V11] Global DM published bytes=%u\n",
                      (unsigned)(HEADER_LEN + tlen + TAG_LEN));
    }
    return ok;
}

void V11GlobalBridge::onMqtt(char* topic, uint8_t* payload, unsigned int len) {
    if (!_mesh || !topic || !payload || strcmp(topic, _dmTopic) != 0) return;
    if (len < HEADER_LEN + TAG_LEN || len > MAX_WIRE) return;

    if (payload[0] != 'M' || payload[1] != 'G' ||
        payload[2] != '1' || payload[3] != '1') return;
    if (payload[4] != PROTOCOL_VERSION || payload[5] != KIND_DM) return;

    const uint16_t clen = (uint16_t)payload[62] | ((uint16_t)payload[63] << 8);
    if (clen == 0 || clen > MAX_TEXT) return;
    if (HEADER_LEN + clen + TAG_LEN != len) return;

    uint8_t sender[32] = {};
    memcpy(sender, payload + 18, 32);
    if (memcmp(sender, _selfPub, 32) == 0) return;

    ContactInfo contact{};
    if (!_mesh->v11LookupChatContact(sender, contact)) return;

    uint8_t key[32] = {};
    if (!deriveDmKey(sender, key)) return;

    char text[MAX_TEXT + 1] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm,
            clen,
            payload + 50,
            12,
            payload,
            50,
            payload + HEADER_LEN + clen,
            TAG_LEN,
            payload + HEADER_LEN,
            reinterpret_cast<uint8_t*>(text));
    }

    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    if (rc != 0) return;

    text[clen] = '\0';

    uint32_t timestamp = 0;
    memcpy(&timestamp, payload + 14, 4);

    uint8_t expectedId[8] = {};
    messageIdFor(_selfPub, sender, timestamp, text, expectedId);
    if (memcmp(expectedId, payload + 6, 8) != 0) return;
    if (seenOrRemember(expectedId)) return;

    _mesh->v11InjectGlobalDm(sender, timestamp, text);
}

bool V11GlobalBridge::noteLoRaDM(const uint8_t senderPub[32],
                                 uint32_t timestamp,
                                 const char* text) {
    if (!_started || !senderPub || !text) return false;

    uint8_t id[8] = {};
    messageIdFor(_selfPub, senderPub, timestamp, text, id);
    return seenOrRemember(id);
}

#endif
