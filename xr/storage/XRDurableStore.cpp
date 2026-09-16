#include "XRDurableStore.h"

#include <algorithm>

namespace meshoffgrid::xr {

XRDurableStore::XRDurableStore(const XRStorageBudget &budget) : budget_(budget) {}

bool XRDurableStore::validBudget() const
{
    const uint64_t sum = static_cast<uint64_t>(budget_.messagesBytes) + budget_.courierBytes +
                         budget_.receiptsAndDedupBytes + budget_.routeEncounterBytes +
                         budget_.contactsWaypointsBytes + budget_.breadcrumbsBytes + budget_.diagnosticsBytes +
                         budget_.settingsAndJournalReserveBytes;
    return sum <= budget_.totalBytes;
}

uint32_t XRDurableStore::classUsage(XRRecordType type) const
{
    switch (type) {
    case XRRecordType::MESSAGE: return usage_.messagesBytes;
    case XRRecordType::COURIER_CUSTODY: return usage_.courierBytes;
    case XRRecordType::RX_DEDUP:
    case XRRecordType::DELIVERY_RECEIPT:
    case XRRecordType::TOMBSTONE: return usage_.receiptsAndDedupBytes;
    case XRRecordType::ROUTE_MODEL:
    case XRRecordType::ENCOUNTER_MODEL: return usage_.routeEncounterBytes;
    case XRRecordType::CONTACT:
    case XRRecordType::WAYPOINT: return usage_.contactsWaypointsBytes;
    case XRRecordType::BREADCRUMB: return usage_.breadcrumbsBytes;
    case XRRecordType::DIAGNOSTIC: return usage_.diagnosticsBytes;
    case XRRecordType::SETTINGS:
    default: return usage_.settingsAndJournalReserveBytes;
    }
}

uint32_t XRDurableStore::classBudget(XRRecordType type) const
{
    switch (type) {
    case XRRecordType::MESSAGE: return budget_.messagesBytes;
    case XRRecordType::COURIER_CUSTODY: return budget_.courierBytes;
    case XRRecordType::RX_DEDUP:
    case XRRecordType::DELIVERY_RECEIPT:
    case XRRecordType::TOMBSTONE: return budget_.receiptsAndDedupBytes;
    case XRRecordType::ROUTE_MODEL:
    case XRRecordType::ENCOUNTER_MODEL: return budget_.routeEncounterBytes;
    case XRRecordType::CONTACT:
    case XRRecordType::WAYPOINT: return budget_.contactsWaypointsBytes;
    case XRRecordType::BREADCRUMB: return budget_.breadcrumbsBytes;
    case XRRecordType::DIAGNOSTIC: return budget_.diagnosticsBytes;
    case XRRecordType::SETTINGS:
    default: return budget_.settingsAndJournalReserveBytes;
    }
}

bool XRDurableStore::canAllocate(XRRecordType type, uint32_t payloadBytes) const
{
    if (!validBudget() || type >= XRRecordType::COUNT)
        return false;
    if (payloadBytes > classBudget(type) - std::min(classUsage(type), classBudget(type)))
        return false;
    return payloadBytes <= budget_.totalBytes - std::min(usage_.totalBytes, budget_.totalBytes);
}

bool XRDurableStore::validHeader(const XRJournalHeader &header)
{
    if (header.magic != XRJournalHeader::MAGIC || header.formatVersion != 1 || header.type >= XRRecordType::COUNT)
        return false;
    switch (header.op) {
    case XRJournalOp::UPSERT:
        return header.key.namespaceId != 0 && header.key.objectId != 0 && header.generation != 0 && header.payloadBytes > 0;
    case XRJournalOp::DELETE_RECORD:
    case XRJournalOp::COMMIT:
        return header.key.namespaceId != 0 && header.key.objectId != 0 && header.generation != 0 && header.payloadBytes == 0;
    default:
        return false;
    }
}

uint32_t XRDurableStore::crc32(const void *data, size_t bytes)
{
    if (!data && bytes != 0)
        return 0;
    const auto *p = static_cast<const uint8_t *>(data);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool XRDurableStore::appendMutation(IXRStorageBackend &backend, XRRecordType type, XRJournalOp op,
                                    const XRObjectKey &key, uint32_t generation, const void *payload,
                                    uint32_t payloadBytes, uint32_t payloadCrc32)
{
    if (op != XRJournalOp::UPSERT && op != XRJournalOp::DELETE_RECORD)
        return false;
    if (op == XRJournalOp::UPSERT) {
        if (!payload || payloadBytes == 0 || !canAllocate(type, payloadBytes))
            return false;
        if (payloadCrc32 != crc32(payload, payloadBytes))
            return false;
    } else if (payloadBytes != 0 || payload != nullptr) {
        return false;
    }

    XRJournalHeader mutation{};
    mutation.type = type;
    mutation.op = op;
    mutation.key = key;
    mutation.generation = generation;
    mutation.payloadBytes = payloadBytes;
    mutation.payloadCrc32 = payloadCrc32;

    XRJournalHeader commit{};
    commit.type = type;
    commit.op = XRJournalOp::COMMIT;
    commit.key = key;
    commit.generation = generation;

    if (!validHeader(mutation) || !validHeader(commit))
        return false;

    const uint32_t bytesNeeded = static_cast<uint32_t>(sizeof(mutation) + payloadBytes + sizeof(commit));
    if (backend.availableBytes() < bytesNeeded)
        return false;
    if (!backend.append(&mutation, sizeof(mutation)))
        return false;
    if (payloadBytes && !backend.append(payload, payloadBytes))
        return false;
    if (!backend.flush())
        return false;
    if (!backend.append(&commit, sizeof(commit)))
        return false;
    return backend.flush();
}

void XRDurableStore::setRecoveredUsage(const XRStorageUsage &usage) { usage_ = usage; }

} // namespace meshoffgrid::xr
