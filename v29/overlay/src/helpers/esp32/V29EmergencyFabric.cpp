#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include "V29EmergencyFabric.h"
#include "../../MyMesh.h"

#include <Identity.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

V29EmergencyFabric v29_emergency_fabric;

namespace {

bool hmac256v29(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t len,
                uint8_t out[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, keyLen, data, len, out) == 0;
}

bool allZero(const uint8_t* data, size_t len) {
    uint8_t acc = 0;
    for (size_t i = 0; i < len; ++i) acc |= data[i];
    return acc == 0;
}

} // namespace

void V29EmergencyFabric::begin(MyMesh* mesh) {
    if (_started || !mesh) return;
    _mesh = mesh;
    if (!allocateQueue()) {
        Serial.println("[V29] emergency fabric disabled: no safe queue memory");
        return;
    }

    loadSnapshots();
    _started = true;

    const MemoryStats s = memoryStats();
    Serial.printf("[V29] offline emergency fabric ready queue=%u/%u psram=%lu/%lu storage=%lu/%lu\n",
                  (unsigned)s.queueUsed, (unsigned)s.queueCapacity,
                  (unsigned long)s.psramFree, (unsigned long)s.psramTotal,
                  (unsigned long)s.storageUsed, (unsigned long)s.storageTotal);
}

bool V29EmergencyFabric::allocateQueue() {
    const uint32_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const uint32_t psramReserve =
        (uint32_t)(((uint64_t)psramTotal * MEMORY_RESERVE_PERMILLE) / 1000ULL);

    uint16_t cap = MAX_QUEUE_RECORDS;
    while (cap >= MIN_QUEUE_RECORDS) {
        const size_t bytes = (size_t)cap * sizeof(Record);
        if (psramTotal && psramFree > psramReserve &&
            bytes <= (size_t)(psramFree - psramReserve) &&
            bytes <= psramLargest) {
            _records = static_cast<Record*>(
                heap_caps_calloc(cap, sizeof(Record), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (_records) {
                _capacity = cap;
                return true;
            }
        }
        if (cap == MIN_QUEUE_RECORDS) break;
        cap = (uint16_t)(cap / 2);
        if (cap < MIN_QUEUE_RECORDS) cap = MIN_QUEUE_RECORDS;
    }

    const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t internalTotal = heap_caps_get_total_size(internalCaps);
    const uint32_t internalFree = heap_caps_get_free_size(internalCaps);
    const uint32_t internalLargest = heap_caps_get_largest_free_block(internalCaps);
    const uint32_t internalReserve =
        (uint32_t)(((uint64_t)internalTotal * MEMORY_RESERVE_PERMILLE) / 1000ULL);
    const size_t bytes = (size_t)MIN_QUEUE_RECORDS * sizeof(Record);

    if (internalFree > internalReserve &&
        bytes <= (size_t)(internalFree - internalReserve) &&
        bytes <= internalLargest) {
        _records = static_cast<Record*>(
            heap_caps_calloc(MIN_QUEUE_RECORDS, sizeof(Record), internalCaps));
        if (_records) {
            _capacity = MIN_QUEUE_RECORDS;
            return true;
        }
    }
    return false;
}

void V29EmergencyFabric::freeQueue() {
    if (_records) free(_records);
    _records = nullptr;
    _capacity = 0;
    _used = 0;
}

V29EmergencyFabric::MemoryStats V29EmergencyFabric::memoryStats() const {
    MemoryStats s{};
    s.psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    s.psramLowWater = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    s.internalFree = heap_caps_get_free_size(internalCaps);
    s.internalLargest = heap_caps_get_largest_free_block(internalCaps);

    s.storageTotal = SPIFFS.totalBytes();
    s.storageUsed = SPIFFS.usedBytes();
    s.queueCapacity = _capacity;
    s.queueUsed = _used;
    return s;
}

bool V29EmergencyFabric::deriveDirectKey(const uint8_t peer[PUB_KEY_SIZE],
                                         uint8_t key[32]) const {
    if (!_mesh || !peer || !key) return false;

    uint8_t shared[PUB_KEY_SIZE] = {};
    if (!_mesh->v28CalcSharedSecretAny(peer, shared) || allZero(shared, sizeof(shared))) {
        memset(shared, 0, sizeof(shared));
        return false;
    }

    uint8_t info[96] = {};
    static const uint8_t ctx[] = "MOG29-DIRECT-V1";
    size_t n = 0;
    memcpy(info + n, ctx, sizeof(ctx) - 1);
    n += sizeof(ctx) - 1;

    const uint8_t* self = _mesh->getSelfPubKey();
    if (memcmp(self, peer, PUB_KEY_SIZE) <= 0) {
        memcpy(info + n, self, PUB_KEY_SIZE);
        n += PUB_KEY_SIZE;
        memcpy(info + n, peer, PUB_KEY_SIZE);
        n += PUB_KEY_SIZE;
    } else {
        memcpy(info + n, peer, PUB_KEY_SIZE);
        n += PUB_KEY_SIZE;
        memcpy(info + n, self, PUB_KEY_SIZE);
        n += PUB_KEY_SIZE;
    }

    const bool ok = hmac256v29(shared, sizeof(shared), info, n, key);
    memset(shared, 0, sizeof(shared));
    memset(info, 0, sizeof(info));
    return ok;
}

bool V29EmergencyFabric::normalizedAuthData(const uint8_t* wire, size_t signedLen,
                                            uint8_t* out, size_t outCap) const {
    if (!wire || !out || signedLen > outCap || signedLen < HEADER_LEN) return false;
    memcpy(out, wire, signedLen);
    out[5] = 0; // remaining carry budget is mutable; maxCarry at byte 8 is signed.
    return true;
}

bool V29EmergencyFabric::buildDirectWire(
    const uint8_t recipient[PUB_KEY_SIZE], Kind kind, Priority priority,
    const uint8_t* body, size_t len, uint8_t out[WIRE_MAX], size_t& outLen) {
    outLen = 0;
    if (!_mesh || !recipient || !body || len == 0 || len > BODY_MAX) return false;
    if ((uint8_t)kind < (uint8_t)Kind::CheckIn ||
        (uint8_t)kind > (uint8_t)Kind::System ||
        (uint8_t)priority > (uint8_t)Priority::Bulk) return false;

    memset(out, 0, WIRE_MAX);
    out[0] = 'M';
    out[1] = '2';
    out[2] = '9';
    out[3] = 'E';
    out[4] = PROTOCOL_VERSION;
    out[5] = DEFAULT_MAX_CARRY;
    out[6] = (uint8_t)kind;
    out[7] = (uint8_t)priority;
    out[8] = DEFAULT_MAX_CARRY;
    out[9] = DEFAULT_TTL_HOURS;
    out[10] = (uint8_t)len;
    out[11] = 0;

    esp_fill_random(out + 12, ID_LEN);
    memcpy(out + 28, _mesh->getSelfPubKey(), PUB_KEY_SIZE);
    memcpy(out + 60, recipient, RECIPIENT_HINT_LEN);
    esp_fill_random(out + 68, 12);

    uint8_t key[32] = {};
    if (!deriveDirectKey(recipient, key)) return false;

    uint8_t aad[HEADER_LEN] = {};
    memcpy(aad, out, HEADER_LEN);
    aad[5] = 0;

    uint8_t tag[TAG_LEN] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, len,
            out + 68, 12,
            aad, sizeof(aad),
            body, out + HEADER_LEN,
            TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(aad, 0, sizeof(aad));
    if (rc != 0) {
        memset(tag, 0, sizeof(tag));
        return false;
    }

    memcpy(out + HEADER_LEN + len, tag, TAG_LEN);
    memset(tag, 0, sizeof(tag));

    const size_t signedLen = HEADER_LEN + len + TAG_LEN;
    uint8_t auth[HEADER_LEN + BODY_MAX + TAG_LEN] = {};
    if (!normalizedAuthData(out, signedLen, auth, sizeof(auth))) return false;

    uint8_t signature[SIG_LEN] = {};
    _mesh->v27SignGlobal(auth, signedLen, signature);
    memset(auth, 0, sizeof(auth));
    if (allZero(signature, sizeof(signature))) {
        memset(signature, 0, sizeof(signature));
        return false;
    }

    memcpy(out + signedLen, signature, SIG_LEN);
    memset(signature, 0, sizeof(signature));
    outLen = signedLen + SIG_LEN;
    return outLen <= WIRE_MAX;
}

bool V29EmergencyFabric::verifyWire(const uint8_t* wire, size_t len) const {
    if (!wire || len < HEADER_LEN + TAG_LEN + SIG_LEN || len > WIRE_MAX) return false;
    if (wire[0] != 'M' || wire[1] != '2' || wire[2] != '9' || wire[3] != 'E' ||
        wire[4] != PROTOCOL_VERSION) return false;

    const uint8_t remaining = wire[5];
    const uint8_t kind = wire[6];
    const uint8_t priority = wire[7];
    const uint8_t maxCarry = wire[8];
    const uint8_t bodyLen = wire[10];

    if (maxCarry == 0 || maxCarry > 8) return false;
    if (!(remaining <= maxCarry)) return false;
    if (kind < (uint8_t)Kind::CheckIn || kind > (uint8_t)Kind::System) return false;
    if (priority > (uint8_t)Priority::Bulk) return false;
    if (bodyLen == 0 || bodyLen > BODY_MAX) return false;

    const size_t signedLen = HEADER_LEN + bodyLen + TAG_LEN;
    if (signedLen + SIG_LEN != len) return false;

    uint8_t auth[HEADER_LEN + BODY_MAX + TAG_LEN] = {};
    if (!normalizedAuthData(wire, signedLen, auth, sizeof(auth))) return false;

    mesh::Identity signer(wire + 28);
    const bool ok = signer.verify(wire + signedLen, auth, (int)signedLen);
    memset(auth, 0, sizeof(auth));
    return ok;
}

bool V29EmergencyFabric::recipientIsSelf(const uint8_t* wire) const {
    return _mesh && wire &&
           memcmp(wire + 60, _mesh->getSelfPubKey(), RECIPIENT_HINT_LEN) == 0;
}

bool V29EmergencyFabric::decryptForSelf(const uint8_t* wire, size_t len,
                                        Event& event) const {
    if (!_mesh || !verifyWire(wire, len) || !recipientIsSelf(wire)) return false;

    const uint8_t bodyLen = wire[10];
    uint8_t key[32] = {};
    if (!deriveDirectKey(wire + 28, key)) return false;

    uint8_t aad[HEADER_LEN] = {};
    memcpy(aad, wire, HEADER_LEN);
    aad[5] = 0;

    uint8_t plain[BODY_MAX] = {};
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, bodyLen,
            wire + 68, 12,
            aad, sizeof(aad),
            wire + HEADER_LEN + bodyLen, TAG_LEN,
            wire + HEADER_LEN, plain);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    memset(aad, 0, sizeof(aad));
    if (rc != 0) {
        memset(plain, 0, sizeof(plain));
        return false;
    }

    memset(&event, 0, sizeof(event));
    event.kind = (Kind)wire[6];
    event.priority = (Priority)wire[7];
    memcpy(event.origin, wire + 28, PUB_KEY_SIZE);
    memcpy(event.body, plain, bodyLen);
    event.bodyLen = bodyLen;
    memset(plain, 0, sizeof(plain));
    return true;
}

bool V29EmergencyFabric::sendStructured(
    const uint8_t recipient[PUB_KEY_SIZE], Kind kind, Priority priority,
    const uint8_t* body, size_t len) {
    if (!_started || !_mesh) return false;

    uint8_t wire[WIRE_MAX] = {};
    size_t wireLen = 0;
    if (!buildDirectWire(recipient, kind, priority, body, len, wire, wireLen))
        return false;

    const bool queued = queueWire(wire, wireLen);
    const bool sent = _mesh->v29SendEmergencyRaw(wire, wireLen);
    memset(wire, 0, sizeof(wire));
    return queued || sent;
}

bool V29EmergencyFabric::sendCheckIn(
    const uint8_t recipient[PUB_KEY_SIZE], CheckInState state) {
    const uint8_t body[2] = {1, (uint8_t)state};
    const Priority p = state == CheckInState::NeedHelp
        ? Priority::Critical
        : Priority::High;
    return sendStructured(recipient, Kind::CheckIn, p, body, sizeof(body));
}

bool V29EmergencyFabric::sendHelpRequest(
    const uint8_t recipient[PUB_KEY_SIZE], uint8_t helpType, uint8_t severity) {
    const uint8_t body[4] = {1, helpType, severity, 0};
    return sendStructured(recipient, Kind::HelpRequest, Priority::Critical,
                          body, sizeof(body));
}

uint8_t V29EmergencyFabric::emergencyContactCount() const {
    if (!_mesh) return 0;
    uint8_t count = 0;
    const uint32_t total = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < total; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact)) continue;
        if (contact.type != ADV_TYPE_CHAT) continue;
        if ((contact.flags & 0x01U) == 0) continue; // existing Favorite bit
        if (count < 0xff) ++count;
    }
    return count;
}

uint8_t V29EmergencyFabric::sendCheckInToEmergencyContacts(CheckInState state) {
    if (!_mesh) return 0;
    uint8_t sent = 0;
    const uint32_t total = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < total; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact)) continue;
        if (contact.type != ADV_TYPE_CHAT || (contact.flags & 0x01U) == 0) continue;
        if (sendCheckIn(contact.id.pub_key, state) && sent < 0xff) ++sent;
    }
    return sent;
}

uint8_t V29EmergencyFabric::sendHelpToEmergencyContacts(uint8_t helpType,
                                                         uint8_t severity) {
    if (!_mesh) return 0;
    uint8_t sent = 0;
    const uint32_t total = _mesh->v27GetContactCount();
    for (uint32_t i = 0; i < total; ++i) {
        ContactInfo contact{};
        if (!_mesh->v27GetContactByIndex(i, contact)) continue;
        if (contact.type != ADV_TYPE_CHAT || (contact.flags & 0x01U) == 0) continue;
        if (sendHelpRequest(contact.id.pub_key, helpType, severity) && sent < 0xff) ++sent;
    }
    return sent;
}

int V29EmergencyFabric::findRecordById(const uint8_t id[ID_LEN]) const {
    if (!_records || !id) return -1;
    for (uint16_t i = 0; i < _capacity; ++i) {
        if (_records[i].used && memcmp(_records[i].wire + 12, id, ID_LEN) == 0)
            return (int)i;
    }
    return -1;
}

bool V29EmergencyFabric::evictFor(Priority incoming) {
    if (!_records || _used < _capacity) return true;

    int victim = -1;
    uint8_t victimPriority = 0;
    uint32_t victimAge = 0;
    const uint32_t now = millis();

    for (uint16_t i = 0; i < _capacity; ++i) {
        const Record& r = _records[i];
        if (!r.used) continue;
        const uint8_t rp = r.wire[7];

        // Never evict a more important record for a less important arrival.
        if (rp < (uint8_t)incoming) continue;

        const uint32_t age = recordAgeMinutes(r, now);
        if (victim < 0 || rp > victimPriority ||
            (rp == victimPriority && age > victimAge)) {
            victim = (int)i;
            victimPriority = rp;
            victimAge = age;
        }
    }

    if (victim < 0) return false;
    removeRecord((uint16_t)victim);
    return true;
}

bool V29EmergencyFabric::queueWire(const uint8_t* wire, size_t len) {
    if (!_records || !verifyWire(wire, len)) return false;
    if (findRecordById(wire + 12) >= 0) return true;

    const Priority incoming = (Priority)wire[7];
    if (_used >= _capacity && !evictFor(incoming)) return false;

    for (uint16_t i = 0; i < _capacity; ++i) {
        Record& r = _records[i];
        if (r.used) continue;
        memset(&r, 0, sizeof(r));
        r.used = true;
        r.len = (uint8_t)len;
        r.firstSeenMs = millis();
        memcpy(r.wire, wire, len);
        ++_used;
        _dirty = true;
        return true;
    }
    return false;
}

void V29EmergencyFabric::removeRecord(uint16_t idx) {
    if (!_records || idx >= _capacity || !_records[idx].used) return;
    memset(&_records[idx], 0, sizeof(Record));
    if (_used) --_used;
    _dirty = true;
}

uint32_t V29EmergencyFabric::recordAgeMinutes(const Record& r, uint32_t now) const {
    const uint32_t liveMinutes = (uint32_t)(now - r.firstSeenMs) / 60000UL;
    return (uint32_t)r.ageMinutesBase + liveMinutes;
}

uint32_t V29EmergencyFabric::forwardIntervalMs(const Record& r) const {
    uint32_t base = 600000UL;
    switch ((Priority)r.wire[7]) {
        case Priority::Critical: base = 20000UL; break;
        case Priority::High: base = 60000UL; break;
        case Priority::Normal: base = 180000UL; break;
        case Priority::Bulk: base = 600000UL; break;
    }
    if (_emergencyMode && base > 30000UL) base /= 2;
    return base;
}

void V29EmergencyFabric::expireRecords(uint32_t now) {
    if (!_records) return;
    for (uint16_t i = 0; i < _capacity; ++i) {
        const Record& r = _records[i];
        if (!r.used || r.len < HEADER_LEN) continue;
        const uint32_t ttlMinutes = (uint32_t)r.wire[9] * 60UL;
        if (ttlMinutes && recordAgeMinutes(r, now) >= ttlMinutes)
            removeRecord(i);
    }
}

int V29EmergencyFabric::selectForwardRecord(uint32_t now) const {
    if (!_records) return -1;

    int best = -1;
    uint8_t bestPriority = 0xff;
    uint32_t bestAge = 0;

    for (uint16_t i = 0; i < _capacity; ++i) {
        const Record& r = _records[i];
        if (!r.used || recipientIsSelf(r.wire)) continue;

        uint32_t interval = forwardIntervalMs(r);
        if (r.forwards >= LOCAL_FORWARD_LIMIT)
            interval = 60UL * 60UL * 1000UL;

        uint32_t jitter = 0;
        for (size_t b = 0; b < ID_LEN; ++b) jitter = (jitter * 33U) ^ r.wire[12 + b];
        jitter %= 15000UL;

        if (r.lastForwardMs != 0 &&
            !elapsed(now, r.lastForwardMs, interval + jitter)) continue;

        const uint8_t p = r.wire[7];
        const uint32_t age = recordAgeMinutes(r, now);
        if (best < 0 || p < bestPriority ||
            (p == bestPriority && age > bestAge)) {
            best = (int)i;
            bestPriority = p;
            bestAge = age;
        }
    }
    return best;
}

bool V29EmergencyFabric::seenOrRemember(const uint8_t id[ID_LEN]) {
    if (!id) return true;
    for (uint16_t i = 0; i < SEEN_CAP; ++i) {
        if (memcmp(_seen[i], id, ID_LEN) == 0) return true;
    }
    memcpy(_seen[_seenNext], id, ID_LEN);
    _seenNext = (uint16_t)((_seenNext + 1) % SEEN_CAP);
    return false;
}

void V29EmergencyFabric::pushEvent(const Event& event) {
    if (_eventCount >= EVENT_CAP) {
        memset(&_events[_eventHead], 0, sizeof(Event));
        _eventHead = (uint8_t)((_eventHead + 1) % EVENT_CAP);
        --_eventCount;
    }
    const uint8_t slot = (uint8_t)((_eventHead + _eventCount) % EVENT_CAP);
    _events[slot] = event;
    ++_eventCount;
}

bool V29EmergencyFabric::takeEvent(Event& out) {
    if (_eventCount == 0) return false;
    out = _events[_eventHead];
    memset(&_events[_eventHead], 0, sizeof(Event));
    _eventHead = (uint8_t)((_eventHead + 1) % EVENT_CAP);
    --_eventCount;
    return true;
}

bool V29EmergencyFabric::onRawFrame(const uint8_t* data, size_t len) {
    if (!data || len < 4 ||
        data[0] != 'M' || data[1] != '2' || data[2] != '9' || data[3] != 'E')
        return false;

    // V29-looking data is consumed even when invalid so malformed emergency
    // frames never leak into the generic raw-data companion stream.
    if (!_started || !verifyWire(data, len)) return true;

    if (recipientIsSelf(data)) {
        if (seenOrRemember(data + 12)) return true;
        Event ev{};
        if (decryptForSelf(data, len, ev)) pushEvent(ev);
        return true;
    }

    if (data[5] == 0) return true;

    uint8_t carried[WIRE_MAX] = {};
    memcpy(carried, data, len);
    carried[5] = (uint8_t)(carried[5] - 1); // mutable carry budget; signature normalizes byte 5.
    queueWire(carried, len);
    memset(carried, 0, sizeof(carried));
    return true;
}

void V29EmergencyFabric::loop() {
    if (!_started || !_mesh || !_records) return;

    const uint32_t now = millis();
    if (!elapsed(now, _lastLoopMs, 500UL)) return;
    _lastLoopMs = now;

    expireRecords(now);

    const int idx = selectForwardRecord(now);
    if (idx >= 0) {
        Record& r = _records[idx];
        if (r.forwards >= LOCAL_FORWARD_LIMIT &&
            elapsed(now, r.lastForwardMs, 60UL * 60UL * 1000UL)) {
            r.forwards = 0;
        }

        if (_mesh->v29SendEmergencyRaw(r.wire, r.len)) {
            if (r.forwards < 0xff) ++r.forwards;
            r.lastForwardMs = now;
            _dirty = true;
        }
    }

    if (_dirty && elapsed(now, _lastFlushMs, 10000UL))
        flushSnapshot(now);
}

uint32_t V29EmergencyFabric::checksumUpdate(
    uint32_t h, const uint8_t* data, size_t len) const {
    if (h == 0) h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t V29EmergencyFabric::snapshotChecksum(uint32_t now) const {
    uint32_t h = 2166136261u;
    if (!_records) return h;

    for (uint16_t i = 0; i < _capacity; ++i) {
        const Record& r = _records[i];
        if (!r.used) continue;

        PackedRecord p{};
        p.len = r.len;
        p.forwards = r.forwards;
        uint32_t age = recordAgeMinutes(r, now);
        if (age > 0xffffU) age = 0xffffU;
        p.ageMinutes = (uint16_t)age;
        memcpy(p.wire, r.wire, r.len);
        h = checksumUpdate(h, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
        memset(&p, 0, sizeof(p));
    }
    return h;
}

bool V29EmergencyFabric::storageWithinBudget(size_t projectedWrite) const {
    const size_t total = SPIFFS.totalBytes();
    const size_t used = SPIFFS.usedBytes();
    if (total == 0) return false;

    const uint64_t target =
        ((uint64_t)total * STORAGE_TARGET_PERMILLE) / 1000ULL;
    return (uint64_t)used + projectedWrite <= target;
}

bool V29EmergencyFabric::flushSnapshot(uint32_t now) {
    if (!_records) return false;

    const size_t projected = sizeof(SnapshotHeader) +
                             (size_t)_used * sizeof(PackedRecord);
    if (!storageWithinBudget(projected)) {
        Serial.println("[V29] snapshot deferred: preserving 20% storage reserve");
        _lastFlushMs = now;
        return false;
    }

    const uint32_t nextGen = _generation + 1;
    const char* path = (nextGen & 1U) ? "/v29q1.bin" : "/v29q0.bin";

    SnapshotHeader hdr{};
    hdr.magic[0] = 'V'; hdr.magic[1] = '2'; hdr.magic[2] = '9'; hdr.magic[3] = 'Q';
    hdr.version = PROTOCOL_VERSION;
    hdr.count = _used;
    hdr.generation = nextGen;
    hdr.checksum = snapshotChecksum(now);

    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) {
        _lastFlushMs = now;
        return false;
    }

    bool ok = f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr);
    for (uint16_t i = 0; ok && i < _capacity; ++i) {
        const Record& r = _records[i];
        if (!r.used) continue;

        PackedRecord p{};
        p.len = r.len;
        p.forwards = r.forwards;
        uint32_t age = recordAgeMinutes(r, now);
        if (age > 0xffffU) age = 0xffffU;
        p.ageMinutes = (uint16_t)age;
        memcpy(p.wire, r.wire, r.len);
        ok = f.write(reinterpret_cast<const uint8_t*>(&p), sizeof(p)) == sizeof(p);
        memset(&p, 0, sizeof(p));
    }
    f.flush();
    f.close();

    _lastFlushMs = now;
    if (!ok) return false;

    _generation = nextGen;
    _dirty = false;
    return true;
}

bool V29EmergencyFabric::snapshotValid(
    const char* path, uint32_t& generation, uint16_t& count) const {
    generation = 0;
    count = 0;
    if (!_records || !SPIFFS.exists(path)) return false;

    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;

    SnapshotHeader hdr{};
    bool ok = f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr);
    if (!ok || hdr.magic[0] != 'V' || hdr.magic[1] != '2' ||
        hdr.magic[2] != '9' || hdr.magic[3] != 'Q' ||
        hdr.version != PROTOCOL_VERSION || hdr.count > _capacity) {
        f.close();
        return false;
    }

    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < hdr.count; ++i) {
        PackedRecord p{};
        if (f.read(reinterpret_cast<uint8_t*>(&p), sizeof(p)) != sizeof(p)) {
            f.close();
            return false;
        }
        if (p.len < HEADER_LEN + TAG_LEN + SIG_LEN || p.len > WIRE_MAX) {
            f.close();
            return false;
        }
        h = checksumUpdate(h, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
    }
    f.close();

    if (h != hdr.checksum) return false;
    generation = hdr.generation;
    count = hdr.count;
    return true;
}

bool V29EmergencyFabric::loadSnapshot(const char* path) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;

    SnapshotHeader hdr{};
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    for (uint16_t i = 0; i < _capacity; ++i) memset(&_records[i], 0, sizeof(Record));
    _used = 0;

    const uint32_t now = millis();
    for (uint16_t n = 0; n < hdr.count && n < _capacity; ++n) {
        PackedRecord p{};
        if (f.read(reinterpret_cast<uint8_t*>(&p), sizeof(p)) != sizeof(p)) {
            f.close();
            return false;
        }
        if (!verifyWire(p.wire, p.len)) continue;

        Record& r = _records[_used];
        r.used = true;
        r.len = p.len;
        r.forwards = p.forwards;
        r.ageMinutesBase = p.ageMinutes;
        r.firstSeenMs = now;
        memcpy(r.wire, p.wire, p.len);
        ++_used;
    }
    f.close();

    _generation = hdr.generation;
    _dirty = false;
    return true;
}

bool V29EmergencyFabric::loadSnapshots() {
    uint32_t g0 = 0, g1 = 0;
    uint16_t c0 = 0, c1 = 0;
    const bool v0 = snapshotValid("/v29q0.bin", g0, c0);
    const bool v1 = snapshotValid("/v29q1.bin", g1, c1);

    if (!v0 && !v1) return false;
    if (v0 && (!v1 || g0 >= g1)) return loadSnapshot("/v29q0.bin");
    return loadSnapshot("/v29q1.bin");
}

#endif
