#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace ops {
namespace v10 {

struct WifiRxMessage {
    uint8_t senderPubKey[32];
    uint32_t timestamp;
    uint32_t messageId;
    char text[160];
};

struct WifiFallback {
    bool ready;
    uint8_t destPrefix[4];
    char text[160];

    WifiFallback() : ready(false) {
        memset(destPrefix, 0, sizeof(destPrefix));
        memset(text, 0, sizeof(text));
    }
};

struct WifiStats {
    uint32_t probeTx;
    uint32_t probeRx;
    uint32_t dataTx;
    uint32_t dataRx;
    uint32_t ackTx;
    uint32_t ackRx;
    uint32_t retries;
    uint32_t fallbacks;
    uint32_t invalid;

    WifiStats()
        : probeTx(0), probeRx(0), dataTx(0), dataRx(0), ackTx(0), ackRx(0),
          retries(0), fallbacks(0), invalid(0) {}
};

class WifiLrDirect {
public:
    bool begin(const uint8_t selfPub[32], const uint8_t selfPrv[64]);
    bool configurePeer(const uint8_t peerPub[32]);
    void tick();

    bool configured() const { return _peerConfigured; }
    bool canReach(const uint8_t destPrefix[4]) const;
    bool sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp);

    bool dequeue(WifiRxMessage& out);
    bool pollFallback(WifiFallback& out);

    const WifiStats& stats() const { return _stats; }

private:
    static const uint16_t PORT = 39210;
    static const uint8_t TYPE_PROBE = 1;
    static const uint8_t TYPE_DATA = 2;
    static const uint8_t TYPE_ACK = 3;
    static const uint8_t TYPE_PROBE_ACK = 4;
    static const size_t MAX_FRAME = 250;
    static const uint32_t PROBE_INTERVAL_MS = 3000;
    static const uint32_t PEER_TTL_MS = 15000;
    static const uint32_t ACK_TIMEOUT_MS = 1200;
    static const uint8_t MAX_ATTEMPTS = 2;
    static const uint8_t RX_QUEUE = 6;

    struct Pending {
        bool active;
        uint32_t id;
        uint32_t sentAtMs;
        uint8_t attempts;
        uint8_t destPrefix[4];
        uint8_t frame[MAX_FRAME];
        uint8_t frameLen;
        char text[160];

        Pending() : active(false), id(0), sentAtMs(0), attempts(0), frameLen(0) {
            memset(destPrefix, 0, sizeof(destPrefix));
            memset(frame, 0, sizeof(frame));
            memset(text, 0, sizeof(text));
        }
    };

    bool _started = false;
    bool _peerConfigured = false;
    bool _apRole = false;
    bool _udpStarted = false;
    bool _peerIpValid = false;
    uint8_t _selfPub[32] = {};
    uint8_t _selfPrv[64] = {};
    uint8_t _peerPub[32] = {};
    uint8_t _secret[32] = {};
    char _ssid[33] = {};
    char _password[25] = {};
    IPAddress _peerIp;
    WiFiUDP _udp;
    uint32_t _lastProbeMs = 0;
    uint32_t _lastRxMs = 0;
    uint32_t _nextMessageId = 1;
    Pending _pending;
    WifiFallback _fallback;
    WifiStats _stats;

    WifiRxMessage _rx[RX_QUEUE];
    uint8_t _rxHead = 0;
    uint8_t _rxTail = 0;
    uint8_t _rxCount = 0;

    void stopNetwork();
    void derivePairMaterial();
    void startNetwork();
    bool sendFrame(const uint8_t* frame, size_t len);
    void sendProbe(bool ack);
    void processPacket(uint8_t* frame, int len, const IPAddress& remoteIp);
    void processProbe(const uint8_t* frame, int len, const IPAddress& remoteIp, bool ack);
    void processData(const uint8_t* frame, int len, const IPAddress& remoteIp);
    void processAck(const uint8_t* frame, int len, const IPAddress& remoteIp);
    void sendAck(uint32_t messageId, const IPAddress& remoteIp);
    bool queueRx(const WifiRxMessage& msg);
    void scheduleFallback();

    static uint32_t read32(const uint8_t* p);
    static void write32(uint8_t* p, uint32_t value);
};

} // namespace v10
} // namespace ops
