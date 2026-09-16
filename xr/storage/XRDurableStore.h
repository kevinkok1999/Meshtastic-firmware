#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

enum class XRRecordType : uint8_t {
    MESSAGE = 0,
    RX_DEDUP,
    COURIER_CUSTODY,
    DELIVERY_RECEIPT,
    TOMBSTONE,
    ROUTE_MODEL,
    ENCOUNTER_MODEL,
    CONTACT,
    WAYPOINT,
    BREADCRUMB,
    DIAGNOSTIC,
    SETTINGS,
    COUNT,
};

enum class XRJournalOp : uint8_t { UPSERT = 1, DELETE_RECORD = 2, COMMIT = 3 };

struct XRObjectKey {
    uint32_t namespaceId = 0;
    uint32_t objectId = 0;
    bool operator==(const XRObjectKey &other) const { return namespaceId == other.namespaceId && objectId == other.objectId; }
};

#pragma pack(push, 1)
struct XRJournalHeader {
    static constexpr uint32_t MAGIC = 0x58524A31u;
    uint32_t magic = MAGIC;
    uint8_t formatVersion = 1;
    XRRecordType type = XRRecordType::MESSAGE;
    XRJournalOp op = XRJournalOp::UPSERT;
    uint8_t flags = 0;
    XRObjectKey key{};
    uint32_t generation = 0;
    uint32_t payloadBytes = 0;
    uint32_t payloadCrc32 = 0;
};
#pragma pack(pop)

struct XRStorageBudget {
    uint32_t totalBytes = 512u * 1024u;
    uint32_t messagesBytes = 32u * 1024u;
    uint32_t courierBytes = 48u * 1024u;
    uint32_t receiptsAndDedupBytes = 48u * 1024u;
    uint32_t routeEncounterBytes = 48u * 1024u;
    uint32_t contactsWaypointsBytes = 48u * 1024u;
    uint32_t breadcrumbsBytes = 128u * 1024u;
    uint32_t diagnosticsBytes = 64u * 1024u;
    uint32_t settingsAndJournalReserveBytes = 96u * 1024u;
};

struct XRStorageUsage {
    uint32_t totalBytes = 0;
    uint32_t messagesBytes = 0;
    uint32_t courierBytes = 0;
    uint32_t receiptsAndDedupBytes = 0;
    uint32_t routeEncounterBytes = 0;
    uint32_t contactsWaypointsBytes = 0;
    uint32_t breadcrumbsBytes = 0;
    uint32_t diagnosticsBytes = 0;
    uint32_t settingsAndJournalReserveBytes = 0;
};

class IXRStorageBackend {
  public:
    virtual ~IXRStorageBackend() = default;
    virtual bool append(const void *data, size_t bytes) = 0;
    virtual bool flush() = 0;
    virtual uint32_t availableBytes() const = 0;
};

class XRDurableStore {
  public:
    explicit XRDurableStore(const XRStorageBudget &budget = XRStorageBudget{});
    const XRStorageBudget &budget() const { return budget_; }
    const XRStorageUsage &usage() const { return usage_; }
    bool validBudget() const;
    bool canAllocate(XRRecordType type, uint32_t payloadBytes) const;
    bool appendMutation(IXRStorageBackend &backend, XRRecordType type, XRJournalOp op, const XRObjectKey &key,
                        uint32_t generation, const void *payload, uint32_t payloadBytes, uint32_t payloadCrc32);
    static bool validHeader(const XRJournalHeader &header);
    static uint32_t crc32(const void *data, size_t bytes);
    void setRecoveredUsage(const XRStorageUsage &usage);

  private:
    XRStorageBudget budget_{};
    XRStorageUsage usage_{};
    uint32_t classUsage(XRRecordType type) const;
    uint32_t classBudget(XRRecordType type) const;
};

} // namespace meshoffgrid::xr
