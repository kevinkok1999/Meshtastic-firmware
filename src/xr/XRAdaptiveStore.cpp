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
    return loadAt(engine, MODEL_PATH);
}

bool XRAdaptiveStore::loadAt(XRAdaptiveIntelligence &engine, const char *path)
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
    File file = FSCom.open(path, FILE_O_READ);
    if (!file)
        return false;

    const size_t fileSize = static_cast<size_t>(file.size());
    if (fileSize != sizeof(snapshot)) {
        file.close();
        return false;
    }

    const size_t bytesRead = static_cast<size_t>(file.read(reinterpret_cast<uint8_t *>(&snapshot), sizeof(snapshot)));
    file.close();
    if (bytesRead != sizeof(snapshot))
        return false;

    return engine.restore(snapshot);
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
