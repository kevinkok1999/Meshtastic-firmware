#include "XRTransportTeamStore.h"

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

namespace meshoffgrid::xr {

XRTransportTeamStore &XRTransportTeamStore::shared()
{
    static XRTransportTeamStore store;
    return store;
}

bool XRTransportTeamStore::tryLock()
{
    return !operationLock_.test_and_set(std::memory_order_acquire);
}

void XRTransportTeamStore::unlock()
{
    operationLock_.clear(std::memory_order_release);
}

bool XRTransportTeamStore::loadOnce(XRTransportTeam &team, uint32_t nowMs)
{
    if (!tryLock())
        return false;
    if (loaded_) {
        unlock();
        return true;
    }

    XRTransportTeam::LearningSnapshot snapshot{};
    bool restored = false;

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked()) {
            unlock();
            return false;
        }

        size_t outLen = 0;
        if (EncryptedStorage::readAndDecrypt(PATH, reinterpret_cast<uint8_t *>(&snapshot), sizeof(snapshot), outLen) &&
            outLen == sizeof(snapshot)) {
            restored = team.restoreLearning(snapshot, nowMs);
        }

        // A missing/corrupt model must never block networking. Start clean and
        // avoid repeatedly hitting encrypted storage every service cycle.
        loaded_ = true;
        lastPersistMs_ = nowMs;
        unlock();
        return restored || !team.learningDirty();
    }
#endif

#ifdef FSCom
    if (!spiLock) {
        unlock();
        return false;
    }

    concurrency::LockGuard guard(spiLock);
    FSCom.mkdir("/prefs");

    auto readSnapshot = [](const char *path, XRTransportTeam::LearningSnapshot &out) -> bool {
        if (path == nullptr || path[0] == '\0' || !FSCom.exists(path))
            return false;

        File file = FSCom.open(path, FILE_O_READ);
        if (!file)
            return false;

        const size_t size = static_cast<size_t>(file.size());
        if (size != sizeof(out)) {
            file.close();
            return false;
        }

        const size_t bytesRead = static_cast<size_t>(file.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)));
        file.close();
        return bytesRead == sizeof(out);
    };

    if (readSnapshot(PATH, snapshot) && team.restoreLearning(snapshot, nowMs)) {
        restored = true;
        if (FSCom.exists(TEMP_PATH))
            FSCom.remove(TEMP_PATH);
    } else {
        XRTransportTeam::LearningSnapshot recovered{};
        if (readSnapshot(TEMP_PATH, recovered) && team.restoreLearning(recovered, nowMs)) {
            restored = true;
            if (FSCom.exists(PATH))
                FSCom.remove(PATH);
            (void)FSCom.rename(TEMP_PATH, PATH);
        }
    }

    loaded_ = true;
    lastPersistMs_ = nowMs;
    unlock();
    return restored || (!FSCom.exists(PATH) && !FSCom.exists(TEMP_PATH));
#else
    (void)team;
    (void)nowMs;
    loaded_ = true;
    unlock();
    return true;
#endif
}

bool XRTransportTeamStore::service(XRTransportTeam &team, uint32_t nowMs, bool force)
{
    if (!loaded_ && !loadOnce(team, nowMs))
        return false;

    // Never spin-wait in an XR radio task while another sidecar is writing
    // flash. Skipping one service pass is safe because dirty generations remain
    // pending and will be checkpointed on a later pass.
    if (!tryLock())
        return true;

    if (!team.learningDirty()) {
        unlock();
        return true;
    }

    if (!force && lastPersistMs_ != 0 && (nowMs - lastPersistMs_) < MIN_PERSIST_INTERVAL_MS) {
        unlock();
        return true;
    }

    uint32_t generation = 0;
    const auto snapshot = team.learningSnapshot(generation);

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked()) {
            unlock();
            return false;
        }

        const bool ok = EncryptedStorage::encryptAndWrite(
            PATH, reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot), true);
        if (ok) {
            team.markLearningPersisted(generation);
            lastPersistMs_ = nowMs;
        }
        unlock();
        return ok;
    }
#endif

#ifdef FSCom
    if (!spiLock) {
        unlock();
        return false;
    }

    concurrency::LockGuard guard(spiLock);
    FSCom.mkdir("/prefs");

    File file = FSCom.open(TEMP_PATH, FILE_O_WRITE);
    if (!file) {
        unlock();
        return false;
    }

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot));
    file.flush();
    file.close();
    if (written != sizeof(snapshot)) {
        FSCom.remove(TEMP_PATH);
        unlock();
        return false;
    }

    if (FSCom.exists(PATH) && !FSCom.remove(PATH)) {
        FSCom.remove(TEMP_PATH);
        unlock();
        return false;
    }
    if (!FSCom.rename(TEMP_PATH, PATH)) {
        FSCom.remove(TEMP_PATH);
        unlock();
        return false;
    }

    team.markLearningPersisted(generation);
    lastPersistMs_ = nowMs;
    unlock();
    return true;
#else
    (void)snapshot;
    unlock();
    return false;
#endif
}

} // namespace meshoffgrid::xr
