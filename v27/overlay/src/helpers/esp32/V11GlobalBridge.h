#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11) && defined(MESH_OFFGRIDNL_V27)

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <Mesh.h>

class MyMesh;
struct ContactInfo;

// V27 keeps the existing V11 integration points so the proven chat/UI path does
// not change, but replaces the Internet wire format with the V27 Privacy Pro
// envelope. The class name is intentionally retained to avoid touching the
// V19/V26 chat path.
class V11GlobalBridge {
public:
    void begin(MyMesh* mesh);
    void loop();

    // One logical chat action can use RF and the global path. This call is
    // non-blocking; it publishes immediately or accepts into a bounded RAM queue.
    bool mirrorDM(const ContactInfo& recipient, uint32_t timestamp, const char* text);

    // Channel/group messages use the existing MeshCore channel secret as the
    // E2E trust root. The normal RF packet is never modified.
    bool mirrorChannelPacket(const mesh::GroupChannel& channel, const mesh::Packet* packet);
    bool noteLoRaChannel(const mesh::GroupChannel& channel, uint32_t timestamp, const char* text);

    // Cross-transport dedup: the same logical DM arriving later over RF is not
    // inserted into the visible chat twice.
    bool noteLoRaDM(const uint8_t senderPub[32], uint32_t timestamp, const char* text);

    bool connected() { return _mqtt.connected(); }

private:
    static constexpr uint8_t PROTOCOL_VERSION = 2;
    static constexpr uint8_t KIND_DM = 1;
    static constexpr uint8_t KIND_CHANNEL = 2;
    static constexpr size_t MAX_TEXT = 160;

    // Privacy Pro wire format:
    // 0..3   magic MG27
    // 4      protocol version
    // 5      kind
    // 6..13  keyed opaque message id
    // 14..21 truncated sender contact prefix (routing hint only)
    // 22..33 random GCM nonce
    // 34..   fixed-size ciphertext:
    //        sender pub[32] | timestamp[4] | text_len[2] | text/padding[160]
    // tail   GCM tag[16]
    //
    // Exact timestamp, full sender key and exact text length are therefore not
    // visible to the broker. Every DM has the same on-wire payload size.
    static constexpr size_t HEADER_LEN = 34;
    static constexpr size_t AAD_LEN = 22;
    static constexpr size_t PLAIN_LEN = 32 + 4 + 2 + MAX_TEXT;
    static constexpr size_t TAG_LEN = 16;
    static constexpr size_t MAX_WIRE = HEADER_LEN + PLAIN_LEN + TAG_LEN;

    static constexpr int PENDING_CAP = 16;
    static constexpr int PENDING_CHANNEL_CAP = 8;
    static constexpr int DEDUP_CAP = 64;
    static constexpr uint32_t PENDING_TTL_MS = 6UL * 60UL * 60UL * 1000UL;
    static constexpr uint32_t RETRY_MIN_MS = 3000;
    static constexpr uint32_t RETRY_MAX_MS = 60000;
    static constexpr uint32_t RX_RATE_WINDOW_MS = 10000;
    static constexpr uint16_t RX_RATE_MAX_PER_WINDOW = 100;
    static_assert(MAX_WIRE == 248, "V27 privacy envelope size changed unexpectedly");
    static_assert(MAX_WIRE < 400, "V27 envelope must stay comfortably inside the MQTT client buffer");

    struct Pending {
        bool used = false;
        uint8_t recipient[32] = {};
        uint32_t timestamp = 0;
        uint32_t queuedMs = 0;
        char text[MAX_TEXT + 1] = {};
    };

    struct PendingChannel {
        bool used = false;
        uint8_t secret[PUB_KEY_SIZE] = {};
        uint32_t timestamp = 0;
        uint32_t queuedMs = 0;
        char text[MAX_TEXT + 1] = {};
    };

    MyMesh* _mesh = nullptr;
    WiFiClient _wc;
    PubSubClient _mqtt{_wc};
    bool _started = false;
    volatile bool _connecting = false;
    bool _wifiWasConnected = false;
    uint32_t _nextConnectAt = 0;
    uint32_t _retryDelayMs = RETRY_MIN_MS;
    uint8_t _selfPub[32] = {};
    char _dmTopic[64] = {};
    Pending _pending[PENDING_CAP];
    uint8_t _pendingHead = 0;
    uint8_t _pendingCount = 0;
    PendingChannel _pendingChannel[PENDING_CHANNEL_CAP];
    uint8_t _pendingChannelHead = 0;
    uint8_t _pendingChannelCount = 0;
    uint32_t _lastChannelCheckMs = 0;
    uint32_t _channelFingerprint = 0;
    uint32_t _rxWindowStartMs = 0;
    uint16_t _rxWindowCount = 0;
    uint64_t _dedup[DEDUP_CAP] = {};
    uint8_t _dedupNext = 0;

    static V11GlobalBridge* s_instance;
    static void mqttThunk(char* topic, uint8_t* payload, unsigned int len);
    static void connectTask(void* arg);

    bool connectNow();
    void onMqtt(char* topic, uint8_t* payload, unsigned int len);
    bool publishDMNow(const uint8_t recipient[32], uint32_t timestamp, const char* text);
    bool enqueue(const uint8_t recipient[32], uint32_t timestamp, const char* text);
    void flushOne();

    bool publishChannelNow(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text);
    bool enqueueChannel(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text);
    void flushOneChannel();
    bool subscribeChannels();
    uint32_t channelFingerprint() const;
    bool findChannelForTopic(const char* topic, mesh::GroupChannel& out) const;
    bool channelStillConfigured(const uint8_t secret[PUB_KEY_SIZE]) const;

    void routeTopicFor(const uint8_t pub[32], char* out, size_t outCap) const;
    void routeTopicForChannel(const uint8_t secret[PUB_KEY_SIZE], char* out, size_t outCap) const;
    bool messageIdFor(const uint8_t peerPub[32], uint32_t timestamp, const char* text, uint8_t out[8]) const;
    bool channelMessageIdFor(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text, uint8_t out[8]) const;
    bool deriveDmKey(const uint8_t peerPub[32], uint8_t key[32]) const;
    bool deriveChannelKey(const uint8_t secret[PUB_KEY_SIZE], uint8_t key[32]) const;
    bool allowInbound();
    bool seenOrRemember(const uint8_t id[8]);
};

extern V11GlobalBridge v11_global_bridge;

#endif
