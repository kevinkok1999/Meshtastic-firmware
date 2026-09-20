#pragma once

#include <Arduino.h>
#include <esp_now.h>

namespace ops {

struct HybridRxMessage {
    uint32_t timestamp = 0;
    uint32_t messageId = 0;
    uint8_t senderPubKey[32] = {};
    char senderName[17] = {};
    char text[160] = {};
};

struct HybridFallback {
    uint8_t destPrefix[4] = {};
    char text[160] = {};
};

struct HybridStats {
    uint32_t helloTx = 0;
    uint32_t helloRx = 0;
    uint32_t dataTx = 0;
    uint32_t dataRx = 0;
    uint32_t ackTx = 0;
    uint32_t ackRx = 0;
    uint32_t retries = 0;
    uint32_t fallbacks = 0;
    uint32_t duplicateDrops = 0;
    uint32_t invalidFrames = 0;
    uint32_t queueDrops = 0;
};

class HybridTransport {
public:
    bool begin(const uint8_t selfPub[32], const uint8_t selfPrv[64], const char* callsign);
    void end();
    void tick();

    // Returns true only when the message was accepted by ESP-NOW and is now
    // awaiting an application-level ACK. A false return lets MeshService use LoRa.
    bool sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp);

    bool dequeue(HybridRxMessage& out);
    bool pollFallback(HybridFallback& out);

    // Shared cross-transport dedup filter. Call before delivering either a
    // LoRa or ESP-NOW application message to the UI.
    bool acceptApplicationMessage(const uint8_t senderPub[32], const char* text, bool fromHybrid);

    bool initialized() const { return _initialized; }
    int peerCount() const;
    const HybridStats& stats() const { return _stats; }

private:
    static constexpr uint8_t PROTO_VERSION = 1;
    static constexpr uint8_t TYPE_HELLO = 1;
    static constexpr uint8_t TYPE_DATA  = 2;
    static constexpr uint8_t TYPE_ACK   = 3;
    static constexpr uint8_t WIFI_CHANNEL = 1;
    static constexpr size_t MAX_FRAME = 250;
    static constexpr int MAX_PEERS = 24;
    static constexpr int RAW_QUEUE = 10;
    static constexpr int RX_QUEUE = 8;
    static constexpr int RECENT_CACHE = 16;
    static constexpr uint32_t HELLO_INTERVAL_MS = 15000;
    static constexpr uint32_t PEER_TTL_MS = 120000;
    static constexpr uint32_t ACK_TIMEOUT_MS = 900;
    static constexpr uint8_t MAX_ATTEMPTS = 3;
    static constexpr uint32_t DUP_WINDOW_MS = 8000;

    struct Peer {
        bool used = false;
        bool verified = false;
        uint8_t mac[6] = {};
        uint8_t pub[32] = {};
        char name[17] = {};
        uint32_t lastSeenMs = 0;
    };

    struct RawFrame {
        uint8_t mac[6] = {};
        uint8_t len = 0;
        uint8_t data[MAX_FRAME] = {};
    };

    struct Recent {
        bool used = false;
        uint8_t hash[8] = {};
        uint32_t seenMs = 0;
        uint8_t route = 0; // 1=LoRa, 2=ESP-NOW; only cross-route repeats are suppressed
    };

    struct Pending {
        bool active = false;
        uint32_t id = 0;
        uint32_t sentAtMs = 0;
        uint8_t attempts = 0;
        uint8_t destPrefix[4] = {};
        uint8_t peerPub[32] = {};
        uint8_t peerMac[6] = {};
        uint8_t frame[MAX_FRAME] = {};
        uint8_t frameLen = 0;
        char text[160] = {};
    };

    bool _initialized = false;
    uint8_t _selfPub[32] = {};
    uint8_t _selfPrv[64] = {};
    char _callsign[17] = {};
    uint32_t _lastHelloMs = 0;
    uint32_t _nextMessageId = 1;

    Peer _peers[MAX_PEERS];
    RawFrame _raw[RAW_QUEUE];
    volatile uint8_t _rawHead = 0, _rawTail = 0, _rawCount = 0;
    HybridRxMessage _rx[RX_QUEUE];
    uint8_t _rxHead = 0, _rxTail = 0, _rxCount = 0;
    Recent _recent[RECENT_CACHE];
    uint8_t _recentCursor = 0;
    Pending _pending;
    HybridFallback _fallback;
    bool _fallbackReady = false;
    HybridStats _stats;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    static HybridTransport* _instance;
    static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
    static void onSendStatic(const uint8_t* mac, esp_now_send_status_t status);

    void onRecv(const uint8_t* mac, const uint8_t* data, int len);
    void processFrame(const RawFrame& f);
    void processHello(const RawFrame& f);
    void processData(const RawFrame& f);
    void processAck(const RawFrame& f);
    void sendHello();
    void sendAck(const uint8_t mac[6], const uint8_t peerPub[32], uint32_t messageId);
    bool ensurePeer(const uint8_t mac[6]);
    Peer* findPeerByPrefix(const uint8_t prefix[4]);
    Peer* upsertPeer(const uint8_t mac[6], const uint8_t pub[32], const char* name, bool verified);
    void sharedSecret(const uint8_t peerPub[32], uint8_t out[32]) const;
    bool queueRx(const HybridRxMessage& msg);
    void scheduleFallback();
    static bool magicOk(const uint8_t* p, int len);
};

} // namespace ops
