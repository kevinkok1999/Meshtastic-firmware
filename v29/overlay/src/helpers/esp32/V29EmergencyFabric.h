#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include <Arduino.h>
#include <Mesh.h>

class MyMesh;

class V29EmergencyFabric {
public:
    enum class Kind : uint8_t {
        CheckIn = 1,
        HelpRequest = 2,
        MeetingPoint = 3,
        Household = 4,
        System = 5,
    };

    enum class Priority : uint8_t {
        Critical = 0,
        High = 1,
        Normal = 2,
        Bulk = 3,
    };

    enum class CheckInState : uint8_t {
        Safe = 1,
        NeedHelp = 2,
        Moving = 3,
        AtMeetingPoint = 4,
    };

    enum class PowerMode : uint8_t {
        Normal = 0,
        Emergency = 1,
        Critical = 2,
    };

    struct Event {
        Kind kind = Kind::System;
        Priority priority = Priority::Normal;
        uint8_t origin[PUB_KEY_SIZE] = {};
        uint8_t body[24] = {};
        uint8_t bodyLen = 0;
    };

    struct MemoryStats {
        uint32_t psramTotal = 0;
        uint32_t psramFree = 0;
        uint32_t psramLargest = 0;
        uint32_t psramLowWater = 0;
        uint32_t internalFree = 0;
        uint32_t internalLargest = 0;
        uint32_t storageTotal = 0;
        uint32_t storageUsed = 0;
        uint16_t queueCapacity = 0;
        uint16_t queueUsed = 0;
    };

    void begin(MyMesh* mesh);
    void loop();

    bool sendStructured(const uint8_t recipient[PUB_KEY_SIZE], Kind kind,
                        Priority priority, const uint8_t* body, size_t len);
    bool sendCheckIn(const uint8_t recipient[PUB_KEY_SIZE], CheckInState state);
    bool sendHelpRequest(const uint8_t recipient[PUB_KEY_SIZE], uint8_t helpType,
                         uint8_t severity);
    uint8_t emergencyContactCount() const;
    uint8_t sendCheckInToEmergencyContacts(CheckInState state);
    uint8_t sendHelpToEmergencyContacts(uint8_t helpType, uint8_t severity);

    bool onRawFrame(const uint8_t* data, size_t len);
    bool takeEvent(Event& out);

    void setEmergencyMode(bool enabled);
    bool emergencyMode() const { return _emergencyMode; }
    void setPowerMode(PowerMode mode);
    PowerMode powerMode() const { return _powerMode; }
    MemoryStats memoryStats() const;

private:
    static constexpr uint8_t PROTOCOL_VERSION = 2;
    static constexpr size_t WIRE_MAX = MAX_PACKET_PAYLOAD;
    static constexpr size_t HEADER_LEN = 82;
    static constexpr size_t TAG_LEN = 16;
    static constexpr size_t SIG_LEN = SIGNATURE_SIZE;
    static constexpr size_t BODY_MAX = WIRE_MAX - HEADER_LEN - TAG_LEN - SIG_LEN;
    static constexpr size_t ID_LEN = 16;
    static constexpr size_t RECIPIENT_HINT_LEN = 8;

    static constexpr size_t AGE_MINUTES_OFFSET = 12;
    static constexpr size_t MSG_ID_OFFSET = 14;
    static constexpr size_t ORIGIN_OFFSET = 30;
    static constexpr size_t RECIPIENT_HINT_OFFSET = 62;
    static constexpr size_t NONCE_OFFSET = 70;

    static constexpr uint8_t DEFAULT_TTL_HOURS = 192;
    static constexpr uint8_t LOCAL_FORWARD_LIMIT = 3;
    static constexpr uint16_t MIN_QUEUE_RECORDS = 24;
    static constexpr uint16_t MAX_QUEUE_RECORDS = 512;
    static constexpr uint16_t SEEN_CAP = 256;
    static constexpr uint16_t EVENT_CAP = 8;
    static constexpr uint8_t MAX_CRITICAL_PER_ORIGIN = 8;
    static constexpr uint32_t STORAGE_TARGET_PERMILLE = 800;
    static constexpr uint32_t MEMORY_TARGET_PERMILLE = 800;
    static constexpr uint32_t MEMORY_RESERVE_PERMILLE = 200;

    static_assert(WIRE_MAX == 184, "V29 wire contract assumes MeshCore 184-byte payload");
    static_assert(HEADER_LEN == NONCE_OFFSET + 12, "V29 header/nonce layout changed");
    static_assert(BODY_MAX == 22, "V29 structured emergency body contract changed");

    static uint16_t wireAgeMinutes(const uint8_t* wire) {
        return wire ? (uint16_t)wire[AGE_MINUTES_OFFSET] |
                      ((uint16_t)wire[AGE_MINUTES_OFFSET + 1] << 8) : 0;
    }
    static void setWireAgeMinutes(uint8_t* wire, uint16_t age) {
        if (!wire) return;
        wire[AGE_MINUTES_OFFSET] = (uint8_t)(age & 0xff);
        wire[AGE_MINUTES_OFFSET + 1] = (uint8_t)(age >> 8);
    }
    static uint8_t carryBudgetFor(Priority p) {
        switch (p) {
            case Priority::Critical: return 8;
            case Priority::High: return 6;
            case Priority::Normal: return 4;
            case Priority::Bulk: return 2;
        }
        return 2;
    }

    struct Record {
        bool used = false;
        uint8_t len = 0;
        uint8_t forwards = 0;
        uint16_t ageMinutesBase = 0;
        uint32_t firstSeenMs = 0;
        uint32_t lastForwardMs = 0;
        uint8_t wire[WIRE_MAX] = {};
    };

#pragma pack(push, 1)
    struct PackedRecord {
        uint8_t len = 0;
        uint8_t forwards = 0;
        uint16_t ageMinutes = 0;
        uint8_t wire[WIRE_MAX] = {};
    };

    struct SnapshotHeader {
        uint8_t magic[4] = {};
        uint8_t version = 0;
        uint8_t reserved = 0;
        uint16_t count = 0;
        uint32_t generation = 0;
        uint32_t checksum = 0;
    };
#pragma pack(pop)

    MyMesh* _mesh = nullptr;
    Record* _records = nullptr;
    uint16_t _capacity = 0;
    uint16_t _used = 0;
    bool _started = false;
    bool _emergencyMode = false;
    PowerMode _powerMode = PowerMode::Normal;
    bool _dirty = false;
    uint32_t _generation = 0;
    uint32_t _lastFlushMs = 0;
    uint32_t _lastLoopMs = 0;

    uint8_t _seen[SEEN_CAP][ID_LEN] = {};
    uint16_t _seenNext = 0;
    Event _events[EVENT_CAP];
    uint8_t _eventHead = 0;
    uint8_t _eventCount = 0;

    bool allocateQueue();
    void freeQueue();
    bool queueWire(const uint8_t* wire, size_t len);
    void trimCriticalOrigin(const uint8_t* wire);
    bool evictFor(Priority incoming);
    int findRecordById(const uint8_t id[ID_LEN]) const;
    int selectForwardRecord(uint32_t now) const;
    void removeRecord(uint16_t idx);
    void expireRecords(uint32_t now);
    uint32_t recordAgeMinutes(const Record& r, uint32_t now) const;
    uint32_t forwardIntervalMs(const Record& r) const;

    bool buildDirectWire(const uint8_t recipient[PUB_KEY_SIZE], Kind kind,
                         Priority priority, const uint8_t* body, size_t len,
                         uint8_t out[WIRE_MAX], size_t& outLen);
    bool verifyWire(const uint8_t* wire, size_t len) const;
    bool decryptForSelf(const uint8_t* wire, size_t len, Event& event) const;
    bool deriveDirectKey(const uint8_t peer[PUB_KEY_SIZE], uint8_t key[32]) const;
    bool normalizedAuthData(const uint8_t* wire, size_t signedLen,
                            uint8_t* out, size_t outCap) const;
    bool recipientIsSelf(const uint8_t* wire) const;
    bool seenOrRemember(const uint8_t id[ID_LEN]);

    void pushEvent(const Event& event);

    bool loadSnapshots();
    bool snapshotValid(const char* path, uint32_t& generation,
                       uint16_t& count) const;
    bool loadSnapshot(const char* path);
    bool flushSnapshot(uint32_t now);
    uint32_t snapshotChecksum(uint32_t now) const;
    uint32_t checksumUpdate(uint32_t h, const uint8_t* data, size_t len) const;
    bool storageWithinBudget(size_t projectedWrite) const;

    static bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
        return (uint32_t)(now - then) >= interval;
    }
};

extern V29EmergencyFabric v29_emergency_fabric;

#endif
