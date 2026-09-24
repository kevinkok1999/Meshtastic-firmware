#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V28)

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <Mesh.h>

class MyMesh;
struct ContactInfo;

// V28 keeps the proven V11/V27 chat integration surface but replaces the
// development MQTT transport with MeshOffGridNL's signed HTTPS store-and-forward
// relay. RF remains route 1; this class is only route 2.
class V11GlobalBridge {
public:
    void begin(MyMesh* mesh);
    void loop();

    bool mirrorDM(const ContactInfo& recipient, uint32_t timestamp, const char* text, bool allowQueue = true);
    bool mirrorChannelPacket(const mesh::GroupChannel& channel, const mesh::Packet* packet);
    bool noteLoRaChannel(const mesh::GroupChannel& channel, uint32_t timestamp, const char* text);
    bool noteLoRaDM(const uint8_t senderPub[32], uint32_t timestamp, const char* text);

    bool connected() const;
    bool tryGlobalFirstDM(const ContactInfo&, uint32_t, const char*) { return false; }

private:
    static constexpr uint8_t PROTOCOL_VERSION = 3;
    static constexpr size_t MSG_ID_LEN = 16;
    static constexpr uint8_t KIND_DM = 1;
    static constexpr uint8_t KIND_CHANNEL = 2;
    static constexpr size_t MAX_TEXT = 160;
    static constexpr size_t HEADER_LEN = 42;
    static constexpr size_t AAD_LEN = 30;
    static constexpr size_t PLAIN_LEN = 32 + 64 + 4 + 2 + MAX_TEXT;
    static constexpr size_t TAG_LEN = 16;
    static constexpr size_t MAX_WIRE = HEADER_LEN + PLAIN_LEN + TAG_LEN;

    static constexpr int PENDING_CAP = 16;
    static constexpr int PENDING_CHANNEL_CAP = 8;
    static constexpr int ACK_CAP = 8;
    static constexpr int DEDUP_CAP = 64;
    static constexpr uint32_t PENDING_TTL_MS = 6UL * 60UL * 60UL * 1000UL;
    static constexpr uint32_t POLL_INTERVAL_MS = 1800;
    static constexpr uint32_t RELAY_HEALTH_MS = 120000;
    static constexpr uint32_t RX_RATE_WINDOW_MS = 10000;
    static constexpr uint16_t RX_RATE_MAX_PER_WINDOW = 100;
    static_assert(MAX_WIRE == 320, "V28 encrypted envelope size changed unexpectedly");

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

    struct PendingAck {
        bool used = false;
        char route[33] = {};
        char message[33] = {};
    };

    enum WorkKind : uint8_t {
        WORK_NONE = 0,
        WORK_PUSH_DM,
        WORK_PUSH_CHANNEL,
        WORK_POLL_DM,
        WORK_POLL_CHANNEL,
        WORK_ACK,
    };

    struct Work {
        WorkKind kind = WORK_NONE;
        char route[33] = {};
        char message[33] = {};
        uint8_t envelope[MAX_WIRE] = {};
        bool hasEnvelope = false;
        uint8_t peerPub[PUB_KEY_SIZE] = {};
        uint8_t channelSecret[PUB_KEY_SIZE] = {};
    };

    MyMesh* _mesh = nullptr;
    WiFiClientSecure _wc;
    bool _started = false;
    volatile bool _httpBusy = false;
    volatile bool _httpDone = false;
    volatile bool _httpOk = false;
    uint32_t _lastRelayOkMs = 0;
    uint32_t _nextPollAt = 0;
    uint32_t _rxWindowStartMs = 0;
    uint16_t _rxWindowCount = 0;

    uint8_t _selfPub[PUB_KEY_SIZE] = {};
    uint8_t _dedup[DEDUP_CAP][MSG_ID_LEN] = {};
    uint8_t _dedupNext = 0;

    Pending _pending[PENDING_CAP];
    uint8_t _pendingHead = 0;
    uint8_t _pendingCount = 0;
    PendingChannel _pendingChannel[PENDING_CHANNEL_CAP];
    uint8_t _pendingChannelHead = 0;
    uint8_t _pendingChannelCount = 0;
    PendingAck _acks[ACK_CAP];
    uint8_t _ackHead = 0;
    uint8_t _ackCount = 0;

    uint32_t _pollContactIdx = 0;
    uint8_t _pollChannelIdx = 0;
    bool _pollChannelsNext = false;

    Work _work;
    String _httpBody;
    String _httpResponse;

    static V11GlobalBridge* s_instance;
    static void httpTask(void* arg);

    bool enqueue(const uint8_t recipient[32], uint32_t timestamp, const char* text);
    bool enqueueChannel(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text);
    bool enqueueAck(const char* route, const char* message);
    void dropDmHead();
    void dropChannelHead();
    void dropAckHead();

    bool startPushDm();
    bool startPushChannel();
    bool startPoll();
    bool startAck();
    bool startHttp(WorkKind kind, const char* route, const char* message,
                   const uint8_t* envelope,
                   const uint8_t* peerPub = nullptr,
                   const uint8_t* channelSecret = nullptr);
    bool buildSignedRequest(const char* action, const char* route, const char* message,
                            const uint8_t* envelope, String& out);
    bool performHttp();
    void completeHttp();

    bool buildDmEnvelope(const uint8_t recipient[32], uint32_t timestamp, const char* text,
                         uint8_t wire[MAX_WIRE], uint8_t msgId[MSG_ID_LEN]);
    bool buildChannelEnvelope(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text,
                              uint8_t wire[MAX_WIRE], uint8_t msgId[MSG_ID_LEN]);

    bool processDmEnvelope(const uint8_t peerPub[PUB_KEY_SIZE], const uint8_t wire[MAX_WIRE]);
    bool processChannelEnvelope(const uint8_t secret[PUB_KEY_SIZE], const uint8_t wire[MAX_WIRE]);
    void processPollResponse();

    bool routeHexFor(const uint8_t pub[32], char out[33]) const;
    bool routeHexForChannel(const uint8_t secret[PUB_KEY_SIZE], char out[33]) const;
    bool messageIdFor(const uint8_t peerPub[32], uint32_t timestamp, const char* text, uint8_t out[MSG_ID_LEN]) const;
    bool channelMessageIdFor(const uint8_t secret[PUB_KEY_SIZE], uint32_t timestamp, const char* text, uint8_t out[MSG_ID_LEN]) const;
    bool deriveDmKey(const uint8_t peerPub[32], uint8_t key[32]) const;
    bool deriveChannelKey(const uint8_t secret[PUB_KEY_SIZE], uint8_t key[32]) const;
    bool allowInbound();
    bool seenOrRemember(const uint8_t id[MSG_ID_LEN]);
    bool channelStillConfigured(const uint8_t secret[PUB_KEY_SIZE]) const;

    static void hexEncode(const uint8_t* in, size_t len, char* out);
    static bool hexDecode(const char* in, uint8_t* out, size_t len);
    static bool base64Encode(const uint8_t* in, size_t len, String& out);
    static bool base64Decode(const char* in, uint8_t* out, size_t cap, size_t& outLen);
};

extern V11GlobalBridge v11_global_bridge;

#endif
