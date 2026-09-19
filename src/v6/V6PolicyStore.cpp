#include "V6PolicyStore.h"

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

namespace meshoffgrid::v6 {

bool V6PolicyStore::load(PolicyState &state)
{
    PolicyState candidate{};

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;
        size_t outLen = 0;
        if (!EncryptedStorage::readAndDecrypt(PATH, reinterpret_cast<uint8_t *>(&candidate), sizeof(candidate), outLen))
            return false;
        if (outLen != sizeof(candidate) || !V6Policy::valid(candidate))
            return false;
        state = candidate;
        return true;
    }
#endif

#ifdef FSCom
    if (!spiLock)
        return false;

    concurrency::LockGuard guard(spiLock);
    FSCom.mkdir("/prefs");

    auto readState = [](const char *path, PolicyState &out) -> bool {
        if (!path || !path[0] || !FSCom.exists(path))
            return false;
        File file = FSCom.open(path, FILE_O_READ);
        if (!file)
            return false;
        if (static_cast<size_t>(file.size()) != sizeof(out)) {
            file.close();
            return false;
        }
        const size_t n = static_cast<size_t>(file.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)));
        file.close();
        return n == sizeof(out) && V6Policy::valid(out);
    };

    if (readState(PATH, candidate)) {
        state = candidate;
        if (FSCom.exists(TEMP_PATH))
            FSCom.remove(TEMP_PATH);
        return true;
    }

    PolicyState recovered{};
    if (readState(TEMP_PATH, recovered)) {
        if (FSCom.exists(PATH))
            FSCom.remove(PATH);
        if (FSCom.rename(TEMP_PATH, PATH)) {
            state = recovered;
            return true;
        }
    }

    return false;
#else
    return false;
#endif
}

bool V6PolicyStore::save(const PolicyState &input)
{
    PolicyState state = input;
    V6Policy::seal(state);
    if (!V6Policy::valid(state))
        return false;

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (EncryptedStorage::isLockdownActive()) {
        if (!EncryptedStorage::isUnlocked())
            return false;
        return EncryptedStorage::encryptAndWrite(PATH, reinterpret_cast<const uint8_t *>(&state), sizeof(state), true);
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

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&state), sizeof(state));
    file.flush();
    file.close();
    if (written != sizeof(state)) {
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

    return true;
#else
    return false;
#endif
}

} // namespace meshoffgrid::v6
