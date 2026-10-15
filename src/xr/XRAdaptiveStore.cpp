#include "XRAdaptiveStore.h"

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

namespace meshoffgrid::xr {

bool XRAdaptiveStore::load(XRAdaptiveIntelligence &engine)
{
    return loadAt(engine, MODEL_PATH, MODEL_TEMP_PATH);
}

bool XRAdaptiveStore::loadAt(XRAdaptiveIntelligence &engine, const char *path, const char *tempPath)
{
    if (path == nullptr || path[0] == '\0')
        return false;

    XRAdaptiveIntelligence::Snapshot snapshot{};

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        size_t outLen = 0;
        if (!EncryptedStorage::readAndDecrypt(path, reinterpret_cast<uint8_t *>(&snapshot), sizeof(snapshot), outLen))
            return false;
        if (outLen != sizeof(snapshot))
            return false;
        return engine.restore(snapshot);
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);

    auto readSnapshot = [](const char *candidate, XRAdaptiveIntelligence::Snapshot &out) -> bool {
        if (candidate == nullptr || candidate[0] == '\0' || !FSCom.exists(candidate))
            return false;

        File file = FSCom.open(candidate, FILE_O_READ);
        if (!file)
            return false;

        const size_t fileSize = static_cast<size_t>(file.size());
        if (fileSize != sizeof(out)) {
            file.close();
            return false;
        }

        const size_t bytesRead = static_cast<size_t>(file.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)));
        file.close();
        return bytesRead == sizeof(out);
    };

    if (readSnapshot(path, snapshot) && engine.restore(snapshot)) {
        // A stale temp file from an older successful save is no longer needed.
        if (tempPath != nullptr && tempPath[0] != '\0' && FSCom.exists(tempPath))
            FSCom.remove(tempPath);
        return true;
    }

    // Power-loss recovery: saveAt() writes the complete validated snapshot to
    // temp before replacing the primary file. If power was lost between those
    // steps, recover the validated temp snapshot instead of discarding learning.
    XRAdaptiveIntelligence::Snapshot recovered{};
    if (readSnapshot(tempPath, recovered) && engine.restore(recovered)) {
        if (FSCom.exists(path))
            FSCom.remove(path);
        if (tempPath != nullptr && tempPath[0] != '\0')
            (void)FSCom.rename(tempPath, path);
        return true;
    }

    return false;
#else
    return false;
#endif
}

bool XRAdaptiveStore::save(XRAdaptiveIntelligence &engine, uint32_t nowMs)
{
    return saveAt(engine, nowMs, MODEL_PATH, MODEL_TEMP_PATH);
}

bool XRAdaptiveStore::saveAt(XRAdaptiveIntelligence &engine, uint32_t nowMs, const char *path, const char *tempPath)
{
    if (path == nullptr || path[0] == '\0' || tempPath == nullptr || tempPath[0] == '\0')
        return false;

    if (!engine.shouldPersist(nowMs))
        return true;

    const auto snapshot = engine.snapshot();

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        if (!EncryptedStorage::encryptAndWrite(path, reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot), true))
            return false;
        engine.markPersisted(nowMs);
        return true;
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);

    File file = FSCom.open(tempPath, FILE_O_WRITE);
    if (!file)
        return false;

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot));
    file.flush();
    file.close();
    if (written != sizeof(snapshot)) {
        FSCom.remove(tempPath);
        return false;
    }

    if (FSCom.exists(path) && !FSCom.remove(path)) {
        FSCom.remove(tempPath);
        return false;
    }
    if (!FSCom.rename(tempPath, path)) {
        FSCom.remove(tempPath);
        return false;
    }

    engine.markPersisted(nowMs);
    return true;
#else
    (void)snapshot;
    return false;
#endif
}

} // namespace meshoffgrid::xr
