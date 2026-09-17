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
    XRAdaptiveIntelligence::Snapshot snapshot{};

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        size_t outLen = 0;
        if (!EncryptedStorage::readAndDecrypt(MODEL_PATH, reinterpret_cast<uint8_t *>(&snapshot), sizeof(snapshot), outLen))
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
    File file = FSCom.open(MODEL_PATH, FILE_O_READ);
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
    if (!engine.shouldPersist(nowMs))
        return true;

    const auto snapshot = engine.snapshot();

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;

        if (!EncryptedStorage::encryptAndWrite(MODEL_PATH, reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot), true))
            return false;
        engine.markPersisted(nowMs);
        return true;
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);

    File file = FSCom.open(MODEL_TEMP_PATH, FILE_O_WRITE);
    if (!file)
        return false;

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot));
    file.flush();
    file.close();
    if (written != sizeof(snapshot)) {
        FSCom.remove(MODEL_TEMP_PATH);
        return false;
    }

    if (FSCom.exists(MODEL_PATH) && !FSCom.remove(MODEL_PATH)) {
        FSCom.remove(MODEL_TEMP_PATH);
        return false;
    }
    if (!FSCom.rename(MODEL_TEMP_PATH, MODEL_PATH)) {
        FSCom.remove(MODEL_TEMP_PATH);
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
