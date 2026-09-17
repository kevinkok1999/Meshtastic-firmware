#include "XRDeferredPacketStore.h"

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

#include <memory>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid::xr {

uint32_t XRDeferredPacketStore::checksumSnapshot(const Snapshot &snapshot)
{
    constexpr uint32_t FNV_OFFSET = 2166136261u;
    constexpr uint32_t FNV_PRIME = 16777619u;

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&snapshot);
    const size_t length = sizeof(Snapshot) - sizeof(snapshot.checksum);

    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

bool XRDeferredPacketStore::encodeQueue(const XRDeferredPacketQueue &queue, Snapshot &snapshot)
{
    snapshot = Snapshot{};

    uint16_t outIndex = 0;
    for (size_t i = 0; i < XRDeferredPacketQueue::MAX_ENTRIES; ++i) {
        const auto *entry = queue.entry(i);
        if (!entry || !entry->used)
            continue;
        if (entry->packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
            continue;
        if (outIndex >= XRDeferredPacketQueue::MAX_ENTRIES)
            break;

        auto &record = snapshot.records[outIndex];
        pb_ostream_t stream = pb_ostream_from_buffer(record.bytes, sizeof(record.bytes));
        if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &entry->packet))
            return false;
        if (stream.bytes_written == 0 || stream.bytes_written > UINT16_MAX)
            return false;

        record.length = static_cast<uint16_t>(stream.bytes_written);
        ++outIndex;
    }

    snapshot.count = outIndex;
    snapshot.checksum = checksumSnapshot(snapshot);
    return true;
}

bool XRDeferredPacketStore::decodeQueue(const Snapshot &snapshot, XRDeferredPacketQueue &queue, uint32_t nowMs)
{
    if (snapshot.magic != 0x58525131 || snapshot.version != 1 ||
        snapshot.count > XRDeferredPacketQueue::MAX_ENTRIES ||
        snapshot.checksum != checksumSnapshot(snapshot))
        return false;

    queue.clear();

    for (uint16_t i = 0; i < snapshot.count; ++i) {
        const auto &record = snapshot.records[i];
        if (record.length == 0 || record.length > sizeof(record.bytes))
            return false;

        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(record.bytes, record.length);
        if (!pb_decode(&stream, meshtastic_MeshPacket_fields, &packet))
            return false;

        // Fail closed. Deferred storage never turns decoded/plaintext data into
        // a background transport.
        if (packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag || packet.id == 0 || packet.to == 0)
            return false;

        if (!queue.enqueue(packet, nowMs))
            return false;
    }

    queue.markPersisted();
    return true;
}

bool XRDeferredPacketStore::load(XRDeferredPacketQueue &queue, uint32_t nowMs)
{
    auto snapshot = std::unique_ptr<Snapshot>(new (std::nothrow) Snapshot{});
    if (!snapshot)
        return false;

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        size_t outLen = 0;
        if (!EncryptedStorage::readAndDecrypt(PATH, reinterpret_cast<uint8_t *>(snapshot.get()), sizeof(Snapshot), outLen))
            return false;
        if (outLen != sizeof(Snapshot))
            return false;

        const bool ok = decodeQueue(*snapshot, queue, nowMs);
        if (ok)
            lastPersistMs_ = nowMs;
        return ok;
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);
    if (!FSCom.exists(PATH)) {
        queue.markPersisted();
        lastPersistMs_ = nowMs;
        return true;
    }

    File file = FSCom.open(PATH, FILE_O_READ);
    if (!file)
        return false;

    const size_t fileSize = static_cast<size_t>(file.size());
    if (fileSize != sizeof(Snapshot)) {
        file.close();
        return false;
    }

    const size_t bytesRead = static_cast<size_t>(
        file.read(reinterpret_cast<uint8_t *>(snapshot.get()), sizeof(Snapshot)));
    file.close();

    if (bytesRead != sizeof(Snapshot))
        return false;

    const bool ok = decodeQueue(*snapshot, queue, nowMs);
    if (ok)
        lastPersistMs_ = nowMs;
    return ok;
#else
    (void)queue;
    (void)nowMs;
    return false;
#endif
}

bool XRDeferredPacketStore::service(XRDeferredPacketQueue &queue, uint32_t nowMs, bool force)
{
    if (!queue.dirty())
        return true;

    if (!force && lastPersistMs_ != 0 && (nowMs - lastPersistMs_) < MIN_PERSIST_INTERVAL_MS)
        return true;

    auto snapshot = std::unique_ptr<Snapshot>(new (std::nothrow) Snapshot{});
    if (!snapshot || !encodeQueue(queue, *snapshot))
        return false;

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        if (!EncryptedStorage::encryptAndWrite(PATH, reinterpret_cast<const uint8_t *>(snapshot.get()), sizeof(Snapshot), true))
            return false;

        queue.markPersisted();
        lastPersistMs_ = nowMs;
        return true;
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);
    FSCom.mkdir("/prefs");

    File file = FSCom.open(TEMP_PATH, FILE_O_WRITE);
    if (!file)
        return false;

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(snapshot.get()), sizeof(Snapshot));
    file.flush();
    file.close();

    if (written != sizeof(Snapshot)) {
        FSCom.remove(TEMP_PATH);
        return false;
    }

    if (FSCom.exists(PATH) && !FSCom.remove(PATH)) {
        FSCom.remove(TEMP_PATH);
        return false;
    }

    if (!FSCom.rename(TEMP_PATH, PATH)) {
        FSCom.remove(TEMP_PATH);
        return false;
    }

    queue.markPersisted();
    lastPersistMs_ = nowMs;
    return true;
#else
    return false;
#endif
}

} // namespace meshoffgrid::xr
