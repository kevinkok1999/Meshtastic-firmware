#include "XRXBeeTransport.h"

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR) && defined(MESHOFFGRID_ENABLE_XBEE_XR868) && \
    defined(MESHOFFGRID_XBEE_RX_PIN) && defined(MESHOFFGRID_XBEE_TX_PIN)

#include "NodeDB.h"
#include "Router.h"
#include "UptimeClock.h"
#include "configuration.h"

#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid::xr {

XRXBeeTransport *XRXBeeTransport::instance_ = nullptr;
XRXBeeTransport *xrXBeeTransport = nullptr;

namespace {
constexpr UBaseType_t TX_QUEUE_DEPTH = 6;
constexpr uint8_t XBEE_DELIVERY_SUCCESS = 0x00;
}

XRXBeeTransport::XRXBeeTransport() : concurrency::OSThread("xr-xbee", SERVICE_INTERVAL_MS)
{
    instance_ = this;
    (void)XRDeliveryEvents::addSink(this);
}

XRXBeeTransport::~XRXBeeTransport()
{
    XRDeliveryEvents::removeSink(this);
    shutdown();
    if (instance_ == this)
        instance_ = nullptr;
}

bool XRXBeeTransport::initialize()
{
    if (initialized_)
        return true;

    if (!txQueue_)
        txQueue_ = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxPacket));
    if (!txQueue_) {
        LOG_ERROR("XR XBee TX queue allocation failed");
        return false;
    }

    link_.onReceive(&XRXBeeTransport::onReceiveStatic);
    link_.onTxStatus(&XRXBeeTransport::onTxStatusStatic);
    link_.onModemStatus(&XRXBeeTransport::onModemStatusStatic);
    link_.onAtResponse(&XRXBeeTransport::onAtResponseStatic);

    if (!link_.begin(serial_, XBEE_BAUD, MESHOFFGRID_XBEE_RX_PIN, MESHOFFGRID_XBEE_TX_PIN)) {
        LOG_ERROR("XR XBee UART initialization failed");
        return false;
    }

    link_.queryModuleInfo();
    lastInfoQueryMs_ = Time::getMillis();
    coordinator_.begin();

    if (!deferredStore_.load(deferred_, lastInfoQueryMs_))
        LOG_WARN("XR XBee deferred queue could not be restored; starting with current RAM state");

    initialized_ = true;
    LOG_INFO("XR XBee sidecar initialized; LoRa remains available");
    return true;
}

void XRXBeeTransport::shutdown()
{
    if (initialized_) {
        (void)deferredStore_.service(deferred_, Time::getMillis(), true);
        link_.end();
        initialized_ = false;
    }
    if (txQueue_) {
        vQueueDelete(txQueue_);
        txQueue_ = nullptr;
    }
    activeTx_ = {};
    mirrorCandidate_ = {};
    deferred_.clear();
}

int32_t XRXBeeTransport::runOnce()
{
    if (!initAttempted_) {
        initAttempted_ = true;
        if (!initialize())
            return 5000;
    }
    if (!initialized_)
        return 5000;

    const uint32_t nowMs = Time::getMillis();
    link_.poll();

    deferred_.expire(nowMs);
    expireOutboundCache(nowMs);

    if (nowMs - lastInfoQueryMs_ >= 5u * 60u * 1000u) {
        link_.queryModuleInfo();
        lastInfoQueryMs_ = nowMs;
    }

    serviceOutgoing(nowMs);

    if (!activeTx_.used && coexistence_.canUseSecondary(nowMs) &&
        (!lastHelloMs_ || nowMs - lastHelloMs_ >= HELLO_INTERVAL_MS)) {
        sendHello(nowMs);
    }

    expireState(nowMs);
    (void)deferredStore_.service(deferred_, nowMs);
    coordinator_.service(nowMs);
    return SERVICE_INTERVAL_MS;
}

bool XRXBeeTransport::eligibleForMirror(const meshtastic_MeshPacket &packet) const
{
    return packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag && isFromUs(&packet) &&
           !isBroadcast(packet.to) && packet.to != 0;
}

RadioTxHook::PreTxAction XRXBeeTransport::beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet)
{
    const uint32_t nowMs = Time::getMillis();
    coexistence_.onLoRaTxStart(nowMs);

    mirrorCandidate_ = {};
    if (initialized_ && packet && eligibleForMirror(*packet)) {
        // Cache only the already-encrypted wire packet. If normal LoRa reliable
        // delivery later exhausts its retries, this exact ciphertext can enter
        // store/carry/forward without ever persisting a decoded/plaintext copy.
        rememberOutbound(*packet, nowMs);

        mirrorCandidate_.valid = true;
        mirrorCandidate_.packet = *packet;
        mirrorCandidate_.ackExpected = packet->want_ack;
    }
    return PRETX_SEND;
}

void XRXBeeTransport::packetReleased(RadioInterface *, const meshtastic_MeshPacket *packet)
{
    const uint32_t nowMs = Time::getMillis();
    const bool ackExpected = packet ? packet->want_ack : false;
    coexistence_.onLoRaTxEnd(nowMs, ackExpected);

    if (!txQueue_ || !packet || !mirrorCandidate_.valid || mirrorCandidate_.packet.id != packet->id) {
        mirrorCandidate_ = {};
        return;
    }

    TxPacket queued{};
    queued.packet = mirrorCandidate_.packet;
    queued.ackExpected = mirrorCandidate_.ackExpected;
    (void)xQueueSend(txQueue_, &queued, 0);
    mirrorCandidate_ = {};
}

void XRXBeeTransport::onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    // LoRa has exhausted its reliable-delivery budget. If the encrypted packet
    // is in our bounded sidecar spool, make it eligible immediately instead of
    // waiting for the normal grace/backoff window.
    deferred_.makeDue(packetId, destination, nowMs);
}

void XRXBeeTransport::onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    // A real Meshtastic ACK is stronger evidence than any sidecar transmit
    // status. Cancel stale fallback work so the same chat message is not
    // needlessly replayed when the peer reappears later.
    deferred_.markSuccess(packetId, peer);

    if (activeTx_.used && activeTx_.packetId == packetId && activeTx_.nodeNum == peer)
        activeTx_ = {};

    (void)deferredStore_.service(deferred_, nowMs);
}

void XRXBeeTransport::onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    // A remote NAK is an explicit application/routing rejection, not a weak RF
    // failure. Drop alternative retries for that same packet.
    deferred_.markSuccess(packetId, peer);

    if (activeTx_.used && activeTx_.packetId == packetId && activeTx_.nodeNum == peer)
        activeTx_ = {};

    (void)deferredStore_.service(deferred_, nowMs);
}


void XRXBeeTransport::rememberOutbound(const meshtastic_MeshPacket &packet, uint32_t nowMs)
{
    CachedOutbound *slot = findCachedOutbound(packet.to, packet.id);
    if (!slot) {
        for (auto &candidate : outboundCache_) {
            if (!candidate.used) {
                slot = &candidate;
                break;
            }
        }
    }

    if (!slot) {
        slot = &outboundCache_[0];
        for (auto &candidate : outboundCache_) {
            if ((nowMs - candidate.cachedAtMs) > (nowMs - slot->cachedAtMs))
                slot = &candidate;
        }
    }

    *slot = {};
    slot->used = true;
    slot->packet = packet;
    slot->cachedAtMs = nowMs;
}

XRXBeeTransport::CachedOutbound *XRXBeeTransport::findCachedOutbound(uint32_t destination, uint32_t packetId)
{
    for (auto &entry : outboundCache_) {
        if (entry.used && entry.packet.to == destination && entry.packet.id == packetId)
            return &entry;
    }
    return nullptr;
}

void XRXBeeTransport::clearCachedOutbound(uint32_t destination, uint32_t packetId)
{
    if (CachedOutbound *entry = findCachedOutbound(destination, packetId))
        *entry = {};
}

void XRXBeeTransport::expireOutboundCache(uint32_t nowMs)
{
    for (auto &entry : outboundCache_) {
        if (entry.used && (nowMs - entry.cachedAtMs) > OUTBOUND_CACHE_TTL_MS)
            entry = {};
    }
}

void XRXBeeTransport::onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    CachedOutbound *cached = findCachedOutbound(destination, packetId);
    if (!cached)
        return;

    if (deferred_.enqueue(cached->packet, nowMs)) {
        LOG_INFO("XR XBee recovery queued encrypted packet id=0x%08x to=0x%08x", packetId, destination);
        (void)deferredStore_.service(deferred_, nowMs, true);
    }
}

void XRXBeeTransport::onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    deferred_.markDelivered(packetId, peer);
    clearCachedOutbound(peer, packetId);
    (void)deferredStore_.service(deferred_, nowMs, true);
}

void XRXBeeTransport::onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    deferred_.markDelivered(packetId, peer);
    clearCachedOutbound(peer, packetId);
    (void)deferredStore_.service(deferred_, nowMs, true);
}

uint8_t XRXBeeTransport::peerCount() const
{
    uint8_t count = 0;
    for (const auto &peer : peers_) {
        if (peer.used)
            ++count;
    }
    return count;
}

uint8_t XRXBeeTransport::linkScoreFor(uint32_t nodeNum) const
{
    const Peer *peer = findPeer(nodeNum);
    if (!peer)
        return 0;

    const uint32_t nowMs = Time::getMillis();
    if (nowMs - peer->lastSeenMs > PEER_FRESH_MS)
        return 0;

    uint32_t attempts = static_cast<uint32_t>(peer->fragmentsOk) + peer->fragmentsFailed;
    int score = 65;
    if (attempts != 0) {
        const int successPct = static_cast<int>((100u * peer->fragmentsOk) / attempts);
        score = 25 + successPct * 3 / 4;
    }
    if (nowMs - peer->lastSeenMs < 30000u)
        score += 10;

    return static_cast<uint8_t>(std::clamp(score, 0, 100));
}

void XRXBeeTransport::sendHello(uint32_t nowMs)
{
    if (!router)
        return;

    FrameHeader header{};
    header.type = FrameType::HELLO;
    header.fromNode = router->getNodeNum();
    if (!header.fromNode)
        return;

    const uint8_t frameId = link_.send(meshoffgrid::xbee::BROADCAST_64, reinterpret_cast<const uint8_t *>(&header),
                                       sizeof(header));
    if (frameId != 0)
        lastHelloMs_ = nowMs;
}

uint16_t XRXBeeTransport::fragmentBudget() const
{
    const size_t np = link_.maxTxPayload();
    if (np <= sizeof(FrameHeader))
        return 0;
    return static_cast<uint16_t>(np - sizeof(FrameHeader));
}

void XRXBeeTransport::serviceOutgoing(uint32_t nowMs)
{
    if (activeTx_.used) {
        if (activeTx_.waitingFrameId != 0) {
            if (nowMs - activeTx_.waitingSinceMs > TX_STATUS_TIMEOUT_MS)
                finishActiveTx(false, nowMs);
            return;
        }
        if (coexistence_.canUseSecondary(nowMs))
            sendNextFragment(nowMs);
        return;
    }

    if (!coexistence_.canUseSecondary(nowMs))
        return;

    // First, try a one-shot mirror of the packet that just went over LoRa.
    // If the XBee peer is not currently reachable, discard this immediate
    // mirror request. It is NOT persisted yet; only a later reliable-LoRa
    // failure event is allowed to promote the cached ciphertext into the
    // store/carry/forward queue. This prevents delayed duplicate messages after
    // a normal LoRa delivery succeeded.
    TxPacket immediate{};
    while (txQueue_ && xQueueReceive(txQueue_, &immediate, 0) == pdTRUE) {
        Peer *peer = findPeer(immediate.packet.to);
        if (!peer || nowMs - peer->lastSeenMs > PEER_FRESH_MS)
            continue;

        if (prepareActiveTx(immediate.packet, nowMs, false)) {
            sendNextFragment(nowMs);
            return;
        }
    }

    // Recovery path: only packets whose normal reliable LoRa delivery actually
    // exhausted its retries enter this persistent queue. A later XBee HELLO can
    // therefore carry them opportunistically without creating background
    // duplicates for packets that were already ACKed normally.
    for (size_t i = 0; i < XRDeferredPacketQueue::MAX_ENTRIES; ++i) {
        auto *queued = deferred_.entry(i);
        if (!queued || !queued->used)
            continue;
        if (static_cast<int32_t>(nowMs - queued->nextAttemptMs) < 0)
            continue;

        Peer *peer = findPeer(queued->packet.to);
        if (!peer || nowMs - peer->lastSeenMs > PEER_FRESH_MS)
            continue;

        if (prepareActiveTx(queued->packet, nowMs, true)) {
            sendNextFragment(nowMs);
            return;
        }

        deferred_.markFailure(queued->packet.id, queued->packet.to, nowMs);
    }
}

bool XRXBeeTransport::prepareActiveTx(const meshtastic_MeshPacket &packet, uint32_t nowMs, bool fromDeferredQueue)
{
    Peer *peer = findPeer(packet.to);
    if (!peer || nowMs - peer->lastSeenMs > PEER_FRESH_MS)
        return false;

    const uint16_t budget = fragmentBudget();
    if (budget == 0)
        return false;

    std::array<uint8_t, meshtastic_MeshPacket_size> encoded{};
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), encoded.size());
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &packet) || stream.bytes_written == 0)
        return false;

    const size_t count = (stream.bytes_written + budget - 1u) / budget;
    if (count == 0 || count > MAX_FRAGMENTS)
        return false;

    activeTx_ = {};
    activeTx_.used = true;
    activeTx_.nodeNum = peer->nodeNum;
    activeTx_.destination64 = peer->address64;
    activeTx_.packetId = packet.id;
    activeTx_.totalLength = static_cast<uint16_t>(stream.bytes_written);
    activeTx_.checksum = checksum32(encoded.data(), stream.bytes_written);
    activeTx_.fragmentCount = static_cast<uint8_t>(count);
    activeTx_.fragmentPayloadBytes = budget;
    activeTx_.fromDeferredQueue = fromDeferredQueue;
    std::memcpy(activeTx_.encoded.data(), encoded.data(), stream.bytes_written);

    if (peer->packetsStarted != UINT16_MAX)
        ++peer->packetsStarted;

    return true;
}

void XRXBeeTransport::sendNextFragment(uint32_t nowMs)
{
    if (!activeTx_.used)
        return;

    if (activeTx_.nextFragment >= activeTx_.fragmentCount) {
        finishActiveTx(true, nowMs);
        return;
    }

    const size_t offset = static_cast<size_t>(activeTx_.nextFragment) * activeTx_.fragmentPayloadBytes;
    const size_t remaining = activeTx_.totalLength - offset;
    const size_t length = std::min<size_t>(activeTx_.fragmentPayloadBytes, remaining);
    if (length > UINT8_MAX) {
        finishActiveTx(false, nowMs);
        return;
    }

    FrameHeader header{};
    header.type = FrameType::DATA;
    header.fromNode = router ? router->getNodeNum() : 0;
    header.packetId = activeTx_.packetId;
    header.totalLength = activeTx_.totalLength;
    header.fragmentOffset = static_cast<uint16_t>(offset);
    header.fragmentIndex = activeTx_.nextFragment;
    header.fragmentCount = activeTx_.fragmentCount;
    header.fragmentLength = static_cast<uint8_t>(length);
    header.checksum = activeTx_.checksum;

    std::array<uint8_t, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD_HARD> frame{};
    std::memcpy(frame.data(), &header, sizeof(header));
    std::memcpy(frame.data() + sizeof(header), activeTx_.encoded.data() + offset, length);

    const uint8_t frameId = link_.send(activeTx_.destination64, frame.data(), sizeof(header) + length);
    if (frameId == 0) {
        finishActiveTx(false, nowMs);
        return;
    }

    activeTx_.waitingFrameId = frameId;
    activeTx_.waitingSinceMs = nowMs;
}

void XRXBeeTransport::finishActiveTx(bool success, uint32_t nowMs)
{
    const uint32_t nodeNum = activeTx_.nodeNum;
    const uint32_t packetId = activeTx_.packetId;

    if (activeTx_.fromDeferredQueue) {
        if (success)
            deferred_.markTransportAccepted(packetId, nodeNum, nowMs);
        else
            deferred_.markFailure(packetId, nodeNum, nowMs);
    }

    activeTx_ = {};

    XRAdaptiveContext context{};
    context.xbeeLinkScore = linkScoreFor(nodeNum);
    context.peerSeenRecently = true;
    context.directMessage = true;
    context.privatePayload = true;

    XRAdaptiveCapabilities capabilities{};
    capabilities.loraAvailable = true;
    capabilities.xbeeAvailable = true;
    capabilities.xbeePrivacyApproved = true;

    const XRAdaptivePlan plan = coordinator_.plan(context, capabilities, nowMs, packetId);
    XRAdaptiveOutcome outcome{};
    outcome.transportAccepted = success;
    outcome.transportFailed = !success;
    coordinator_.report(plan, outcome, nowMs);
}

void XRXBeeTransport::processRx(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t, uint32_t nowMs)
{
    if (!payload || payloadLength < sizeof(FrameHeader))
        return;

    FrameHeader header{};
    std::memcpy(&header, payload, sizeof(header));
    if (header.magic != WIRE_MAGIC || header.version != WIRE_VERSION || header.fromNode == 0)
        return;
    if (router && header.fromNode == router->getNodeNum())
        return;

    if (header.type == FrameType::HELLO)
        processHello(header, source64, nowMs);
    else if (header.type == FrameType::DATA)
        processData(header, source64, payload + sizeof(FrameHeader), payloadLength - sizeof(FrameHeader), nowMs);
}

void XRXBeeTransport::processHello(const FrameHeader &header, uint64_t source64, uint32_t nowMs)
{
    rememberPeer(header.fromNode, source64, nowMs);
}

void XRXBeeTransport::processData(const FrameHeader &header, uint64_t source64, const uint8_t *payload,
                                  size_t payloadLength, uint32_t nowMs)
{
    if (header.fragmentCount == 0 || header.fragmentCount > MAX_FRAGMENTS ||
        header.fragmentIndex >= header.fragmentCount || header.totalLength == 0 ||
        header.totalLength > meshtastic_MeshPacket_size || header.fragmentLength != payloadLength ||
        static_cast<size_t>(header.fragmentOffset) + payloadLength > header.totalLength)
        return;

    rememberPeer(header.fromNode, source64, nowMs);
    Reassembly &assembly = getReassembly(header, source64, nowMs);
    if (!assembly.used || assembly.totalLength != header.totalLength || assembly.fragmentCount != header.fragmentCount ||
        assembly.checksum != header.checksum)
        return;

    std::memcpy(assembly.data.data() + header.fragmentOffset, payload, payloadLength);
    assembly.receivedMask |= static_cast<uint16_t>(1u << header.fragmentIndex);
    assembly.updatedMs = nowMs;

    const uint16_t completeMask =
        header.fragmentCount == 16 ? 0xffffu : static_cast<uint16_t>((1u << header.fragmentCount) - 1u);
    if (assembly.receivedMask != completeMask)
        return;

    if (checksum32(assembly.data.data(), assembly.totalLength) != assembly.checksum) {
        assembly.used = false;
        return;
    }

    meshtastic_MeshPacket *packet = packetPool.allocZeroed();
    if (!packet) {
        assembly.used = false;
        return;
    }

    pb_istream_t stream = pb_istream_from_buffer(assembly.data.data(), assembly.totalLength);
    const bool decoded = pb_decode(&stream, meshtastic_MeshPacket_fields, packet);
    assembly.used = false;

    if (!decoded || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag ||
        packet->from != header.fromNode || packet->id != header.packetId || isBroadcast(packet->from)) {
        packetPool.release(packet);
        return;
    }

    packet->via_mqtt = false;
    if (router)
        router->enqueueReceivedMessage(packet);
    else
        packetPool.release(packet);
}

XRXBeeTransport::Peer *XRXBeeTransport::findPeer(uint32_t nodeNum)
{
    for (auto &peer : peers_)
        if (peer.used && peer.nodeNum == nodeNum)
            return &peer;
    return nullptr;
}

const XRXBeeTransport::Peer *XRXBeeTransport::findPeer(uint32_t nodeNum) const
{
    for (const auto &peer : peers_)
        if (peer.used && peer.nodeNum == nodeNum)
            return &peer;
    return nullptr;
}

XRXBeeTransport::Peer *XRXBeeTransport::findPeerByAddress(uint64_t address64)
{
    for (auto &peer : peers_)
        if (peer.used && peer.address64 == address64)
            return &peer;
    return nullptr;
}

XRXBeeTransport::Peer &XRXBeeTransport::rememberPeer(uint32_t nodeNum, uint64_t address64, uint32_t nowMs)
{
    if (Peer *existing = findPeer(nodeNum)) {
        existing->address64 = address64;
        existing->lastSeenMs = nowMs;
        return *existing;
    }

    if (Peer *sameAddress = findPeerByAddress(address64)) {
        sameAddress->nodeNum = nodeNum;
        sameAddress->lastSeenMs = nowMs;
        return *sameAddress;
    }

    Peer *slot = nullptr;
    for (auto &peer : peers_) {
        if (!peer.used) {
            slot = &peer;
            break;
        }
        if (!slot || peer.lastSeenMs < slot->lastSeenMs)
            slot = &peer;
    }

    *slot = {};
    slot->used = true;
    slot->nodeNum = nodeNum;
    slot->address64 = address64;
    slot->lastSeenMs = nowMs;
    return *slot;
}

XRXBeeTransport::Reassembly &XRXBeeTransport::getReassembly(const FrameHeader &header, uint64_t source64, uint32_t nowMs)
{
    for (auto &item : reassembly_) {
        if (item.used && item.fromNode == header.fromNode && item.packetId == header.packetId &&
            item.source64 == source64)
            return item;
    }

    Reassembly *slot = nullptr;
    for (auto &item : reassembly_) {
        if (!item.used) {
            slot = &item;
            break;
        }
        if (!slot || item.updatedMs < slot->updatedMs)
            slot = &item;
    }

    *slot = {};
    slot->used = true;
    slot->fromNode = header.fromNode;
    slot->packetId = header.packetId;
    slot->source64 = source64;
    slot->totalLength = header.totalLength;
    slot->fragmentCount = header.fragmentCount;
    slot->checksum = header.checksum;
    slot->updatedMs = nowMs;
    return *slot;
}

void XRXBeeTransport::expireState(uint32_t nowMs)
{
    for (auto &item : reassembly_) {
        if (item.used && nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS)
            item = {};
    }

    for (auto &peer : peers_) {
        if (peer.used && nowMs - peer.lastSeenMs > 24u * 60u * 60u * 1000u)
            peer = {};
    }
}

uint32_t XRXBeeTransport::checksum32(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void XRXBeeTransport::onReceiveStatic(uint64_t source64, const uint8_t *payload, size_t payloadLength,
                                      uint8_t receiveOptions)
{
    if (instance_)
        instance_->processRx(source64, payload, payloadLength, receiveOptions, Time::getMillis());
}

void XRXBeeTransport::onTxStatusStatic(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount,
                                       uint8_t discoveryStatus)
{
    if (instance_)
        instance_->onTxStatus(frameId, deliveryStatus, retryCount, discoveryStatus);
}

void XRXBeeTransport::onModemStatusStatic(uint8_t status)
{
    if (instance_)
        instance_->onModemStatus(status);
}

void XRXBeeTransport::onAtResponseStatic(uint8_t frameId, char command0, char command1, uint8_t status,
                                         const uint8_t *value, size_t valueLength)
{
    if (instance_)
        instance_->onAtResponse(frameId, command0, command1, status, value, valueLength);
}

void XRXBeeTransport::onTxStatus(uint8_t frameId, uint8_t deliveryStatus, uint8_t, uint8_t)
{
    if (!activeTx_.used || activeTx_.waitingFrameId != frameId)
        return;

    Peer *peer = findPeer(activeTx_.nodeNum);
    if (deliveryStatus == XBEE_DELIVERY_SUCCESS) {
        if (peer && peer->fragmentsOk != UINT16_MAX)
            ++peer->fragmentsOk;
        activeTx_.waitingFrameId = 0;
        activeTx_.waitingSinceMs = 0;
        ++activeTx_.nextFragment;

        if (activeTx_.nextFragment >= activeTx_.fragmentCount)
            finishActiveTx(true, Time::getMillis());
    } else {
        if (peer && peer->fragmentsFailed != UINT16_MAX)
            ++peer->fragmentsFailed;
        finishActiveTx(false, Time::getMillis());
    }
}

void XRXBeeTransport::onModemStatus(uint8_t status)
{
    LOG_DEBUG("XR XBee modem status: 0x%02x", status);
}

void XRXBeeTransport::onAtResponse(uint8_t, char command0, char command1, uint8_t status, const uint8_t *value,
                                   size_t valueLength)
{
    if (status != 0) {
        LOG_WARN("XR XBee AT %c%c failed status=0x%02x", command0, command1, status);
        return;
    }

    if (command0 == 'A' && command1 == 'P' && valueLength && value[0] != 1)
        LOG_WARN("XR XBee requires AP=1; module reports AP=%u", static_cast<unsigned>(value[0]));
    if (command0 == 'A' && command1 == 'O' && valueLength && value[0] != 0)
        LOG_WARN("XR XBee integration expects AO=0; module reports AO=%u", static_cast<unsigned>(value[0]));
}

} // namespace meshoffgrid::xr

#endif
