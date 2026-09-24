#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

class MyMesh;
struct ContactInfo;

class V11GlobalBridge {
public:
    void begin(MyMesh* mesh);
    void loop();

    // Keep LoRa untouched and mirror the same logical direct message over Wi-Fi.
    // True means the internet copy was published or accepted into the bounded RAM queue.
    bool mirrorDM(const ContactInfo& recipient, uint32_t timestamp, const char* text);

    // Dedup when the same message arrives first over internet and later over LoRa.
    bool noteLoRaDM(const uint8_t senderPub[32], uint32_t timestamp, const char* text);

    bool connected() { return _mqtt.connected(); }

private:
    static constexpr uint8_t PROTOCOL_VERSION = 1;
    static constexpr uint8_t KIND_DM = 1;
    static constexpr size_t MAX_TEXT = 160;
    static constexpr size_t HEADER_LEN = 64;
    static constexpr size_t TAG_LEN = 16;
    static constexpr size_t MAX_WIRE = HEADER_LEN + MAX_TEXT + TAG_LEN;
    static constexpr int PENDING_CAP = 8;
    static constexpr int DEDUP_CAP = 32;

    struct Pending {
        bool used = false;
        uint8_t recipient[32] = {};
        uint32_t timestamp = 0;
        char text[MAX_TEXT + 1] = {};
    };

    MyMesh* _mesh = nullptr;
    WiFiClient _wc;
    PubSubClient _mqtt{_wc};
    bool _started = false;
    volatile bool _connecting = false;
    uint32_t _lastConnectAttempt = 0;
    uint8_t _selfPub[32] = {};
    char _dmTopic[64] = {};
    Pending _pending[PENDING_CAP];
    uint8_t _pendingHead = 0;
    uint8_t _pendingCount = 0;
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

    void routeTopicFor(const uint8_t pub[32], char* out, size_t outCap) const;
    void messageIdFor(const uint8_t recipient[32], const uint8_t sender[32],
                      uint32_t timestamp, const char* text, uint8_t out[8]) const;
    bool deriveDmKey(const uint8_t peerPub[32], uint8_t key[32]) const;
    bool seenOrRemember(const uint8_t id[8]);
};

extern V11GlobalBridge v11_global_bridge;

#endif
