#include "XRXBeeTransport.h"
#include "XRTransportTeam.h"

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR) && defined(MESHOFFGRID_ENABLE_XBEE_XR868) && \
    defined(MESHOFFGRID_XBEE_RX_PIN) && defined(MESHOFFGRID_XBEE_TX_PIN)

#include "NodeDB.h"
#include "PowerStatus.h"
#include "Router.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "configuration.h"

#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid::xr {

XRXBeeTransport *XRXBeeTransport::instance_ = nullptr;
XRXBeeTransport *xrXBeeTransport = nullptr;

namespace {
void reportHiddenRfEnvironment(uint32_t nowMs)
{
    static uint32_t lastReportMs = 0;
    if (lastReportMs != 0 && nowMs - lastReportMs < 1000u)
        return;
    lastReportMs = nowMs;

    uint8_t batteryPercent = 100;
    if (powerStatus && powerStatus->getHasBattery())
        batteryPercent = powerStatus->getBatteryChargePercent();

    float utilization = airTime ? airTime->smoothedChannelUtilizationPercent() : 0.0f;
    if (utilization < 0.0f)
        utilization = 0.0f;
    if (utilization > 100.0f)
        utilization = 100.0f;

    int16_t noiseFloorDbm = -120;
    if (router && router->getRadioIface())
        noiseFloorDbm = static_cast<int16_t>(router->getRadioIface()->getNoiseFloor());

    XRTransportTeam::shared().reportEnvironment(
        batteryPercent, static_cast<uint8_t>(utilization + 0.5f), noiseFloorDbm, nowMs);
}

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
    if (!deliveryEventQueue_)
        deliveryEventQueue_ = xQueueCreate(DELIVERY_EVENT_QUEUE_DEPTH, sizeof(DeliveryEvent));
    if (!txQueue_ || !deliveryEventQueue_) {
        LOG_ERROR("XR XBee queue allocation failed");
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
    if (deliveryEventQueue_) {
        vQueueDelete(deliveryEventQueue_);
        deliveryEventQueue_ = nullptr;
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
    reportHiddenRfEnvironment(nowMs);
    link_.poll();
    drainDeliveryEvents(nowMs);

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
        // Only copy into the bounded handoff record here. Cache/store mutations
        // are performed by the XBee OSThread after packetReleased() queues it.
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

bool XRXBeeTransport::enqueueDeliveryEvent(DeliveryEventType type, uint32_t peer, uint32_t packetId,
                                              uint32_t whenMs)
{
    if (!deliveryEventQueue_ || peer == 0 || packetId == 0)
        return false;

    DeliveryEvent event{};
    event.type = type;
    event.peer = peer;
    event.packetId = packetId;
    event.whenMs = whenMs;
    return xQueueSend(deliveryEventQueue_, &event, 0) == pdTRUE;
}

void XRXBeeTransport::onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Failed, destination, packetId, nowMs);
}

void XRXBeeTransport::onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Acked, peer, packetId, nowMs);
}

void XRXBeeTransport::onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Naked, peer, packetId, nowMs);
}

void XRXBeeTransport::drainDeliveryEvents(uint32_t nowMs)
{
    if (!deliveryEventQueue_)
        return;

    // Snapshot the current depth so a failure event re-queued while its
    // ciphertext handoff is still crossing threads is retried on the next
    // service pass instead of spinning in this one.
    UBaseType_t remaining = uxQueueMessagesWaiting(deliveryEventQueue_);
    DeliveryEvent event{};
    while (remaining-- > 0 && xQueueReceive(deliveryEventQueue_, &event, 0) == pdTRUE)
        handleDeliveryEvent(event, nowMs);
}

void XRXBeeTransport::handleDeliveryEvent(const DeliveryEvent &event, uint32_t nowMs)
{
    switch (event.type) {
    case DeliveryEventType::Failed: {
        CachedOutbound *cached = findCachedOutbound(event.peer, event.packetId);
        if (!cached) {
            // Reliable-LoRa failure can be published from a different task a
            // few milliseconds before the sidecar has consumed packetReleased.
            // Give that encrypted handoff a short bounded grace window.
            if (nowMs - event.whenMs < 5000u)
                (void)xQueueSend(deliveryEventQueue_, &event, 0);
            return;
        }

        if (deferred_.enqueue(cached->packet, nowMs)) {
            deferred_.makeDue(event.packetId, event.peer, nowMs);
            LOG_INFO("XR XBee recovery queued encrypted packet id=0x%08x to=0x%08x", event.packetId, event.peer);
            (void)deferredStore_.service(deferred_, nowMs, true);
        }
        break;
    }
    case DeliveryEventType::Acked:
    case DeliveryEventType::Naked:
        if (event.type == DeliveryEventType::Acked)
            XRTransportTeam::shared().markDelivered(event.peer, event.packetId, nowMs);
        else
            XRTransportTeam::shared().markCancelled(event.peer, event.packetId, nowMs);
        deferred_.markDelivered(event.packetId, event.peer);
        clearCachedOutbound(event.peer, event.packetId);
        if (activeTx_.used && activeTx_.packetId == event.packetId && activeTx_.nodeNum == event.peer)
            activeTx_ = {};
        (void)deferredStore_.service(deferred_, nowMs, true);
        break;
    }
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
        rememberOutbound(immediate.packet, nowMs);
        Peer *peer = findPeer(immediate.packet.to);
        if (!peer || nowMs - peer->lastSeenMs > PEER_FRESH_MS)
            continue;

        auto &team = XRTransportTeam::shared();
        team.reportRoute(XRTeamTransport::XBee, peer->nodeNum, linkScoreFor(peer->nodeNum), true, nowMs);
        if (!team.allowAssist(XRTeamTransport::XBee, immediate.packet.to, immediate.packet.id, nowMs))
            continue;

        if (prepareActiveTx(immediate.packet, nowMs, false, false)) {
            sendNextFragment(nowMs);
            return;
        }
        team.reportAssistResult(XRTeamTransport::XBee, immediate.packet.to, immediate.packet.id, false, nowMs);
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

        Peer *peer = selectRecoveryPeer(queued->packet.to, nowMs, queued->packet.hop_limit > 0);
        if (!peer)
            continue;

        auto &team = XRTransportTeam::shared();
        team.reportRoute(XRTeamTransport::XBee, queued->packet.to, linkScoreFor(peer->nodeNum), true, nowMs);
        if (!team.claimRecovery(XRTeamTransport::XBee, queued->packet.to, queued->packet.id, nowMs))
            continue;

        if (prepareActiveTx(queued->packet, nowMs, true, queued->packet.hop_limit > 0)) {
            sendNextFragment(nowMs);
            return;
        }

        team.reportRecoveryResult(XRTeamTransport::XBee, queued->packet.to, queued->packet.id, false, nowMs);
        deferred_.markFailure(queued->packet.id, queued->packet.to, nowMs);
    }
}

bool XRXBeeTransport::prepareActiveTx(const meshtastic_MeshPacket &packet, uint32_t nowMs, bool fromDeferredQueue,
                                      bool allowBridge)
{
    Peer *peer = allowBridge ? selectRecoveryPeer(packet.to, nowMs, true) : findPeer(packet.to);
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
    activeTx_.nodeNum = packet.to;
    activeTx_.carrierNodeNum = peer->nodeNum;
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
    const uint32_t carrierNodeNum = activeTx_.carrierNodeNum;
    const uint32_t packetId = activeTx_.packetId;

    if (activeTx_.fromDeferredQueue) {
        if (success)
            deferred_.markTransportAccepted(packetId, nodeNum, nowMs);
        else
            deferred_.markFailure(packetId, nodeNum, nowMs);
        XRTransportTeam::shared().reportRecoveryResult(XRTeamTransport::XBee, nodeNum, packetId, success, nowMs);
    } else {
        XRTransportTeam::shared().reportAssistResult(XRTeamTransport::XBee, nodeNum, packetId, success, nowMs);
    }

    activeTx_ = {};

    XRAdaptiveContext context{};
    context.xbeeLinkScore = linkScoreFor(carrierNodeNum);
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
    // XBee addresses are transport identifiers, not Meshtastic identities.
    // Only bind a sidecar address to a node already known by the normal mesh.
    if (!nodeDB || nodeDB->getMeshNode(header.fromNode) == nullptr)
        return;

    if (Peer *existing = findPeer(header.fromNode)) {
        if (existing->address64 != source64 && nowMs - existing->lastSeenMs <= PEER_FRESH_MS)
            return;
    }

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
        packet->from != header.fromNode || packet->id != header.packetId || isBroadcast(packet->from) || !nodeDB ||
        nodeDB->getMeshNode(packet->from) == nullptr) {
        packetPool.release(packet);
        return;
    }

    if (Peer *existing = findPeer(packet->from)) {
        if (existing->address64 != source64 && nowMs - existing->lastSeenMs <= PEER_FRESH_MS) {
            packetPool.release(packet);
            return;
        }
    }
    rememberPeer(packet->from, source64, nowMs);

    packet->via_mqtt = false;
    // Meshtastic currently has no generic sidecar transport enum. Mark this
    // as the first secondary-radio path so it is never mistaken for an
    // internally generated packet or for primary-LoRa RF metadata.
    packet->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA_ALT1;
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

XRXBeeTransport::Peer *XRXBeeTransport::selectRecoveryPeer(uint32_t destination, uint32_t nowMs, bool allowBridge)
{
    if (Peer *direct = findPeer(destination)) {
        if (direct->used && nowMs - direct->lastSeenMs <= PEER_FRESH_MS)
            return direct;
    }

    if (!allowBridge)
        return nullptr;

    Peer *best = nullptr;
    uint8_t bestScore = 0;
    for (auto &peer : peers_) {
        if (!peer.used || nowMs - peer.lastSeenMs > PEER_FRESH_MS)
            continue;
        const uint8_t score = linkScoreFor(peer.nodeNum);
        if (!best || score > bestScore) {
            best = &peer;
            bestScore = score;
        }
    }
    return best;
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
        XRTransportTeam::shared().reportRoute(XRTeamTransport::XBee, nodeNum, linkScoreFor(nodeNum), true, nowMs);
        return *existing;
    }

    if (Peer *sameAddress = findPeerByAddress(address64)) {
        sameAddress->nodeNum = nodeNum;
        sameAddress->lastSeenMs = nowMs;
        XRTransportTeam::shared().reportRoute(XRTeamTransport::XBee, nodeNum, linkScoreFor(nodeNum), true, nowMs);
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
    XRTransportTeam::shared().reportRoute(XRTeamTransport::XBee, nodeNum, linkScoreFor(nodeNum), true, nowMs);
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
        if (peer.used && nowMs - peer.lastSeenMs > 24u * 60u * 60u * 1000u) {
            XRTransportTeam::shared().reportRoute(XRTeamTransport::XBee, peer.nodeNum, 0, false, nowMs);
            peer = {};
        }
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

    Peer *peer = findPeer(activeTx_.carrierNodeNum);
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
    // XR868 reports both informational and disruptive modem events. Only
    // reset/sleep/power/fault states invalidate the shared team route.
    const bool disruptive =
        status == 0x00 || // hardware reset / power-up
        status == 0x01 || // watchdog reset
        status == 0x0C || // network went to sleep
        status == 0x0D || // supply limit exceeded
        status == 0x13 || // fatal error
        status == 0x42 || // network watchdog timeout
        status >= 0x80;   // stack error range

    if (!disruptive) {
        LOG_DEBUG("XR XBee modem status: 0x%02x", status);
        return;
    }

    const uint32_t nowMs = Time::getMillis();
    LOG_WARN("XR XBee disruptive modem status: 0x%02x", status);

    if (activeTx_.used)
        finishActiveTx(false, nowMs);

    for (auto &peer : peers_) {
        if (peer.used)
            XRTransportTeam::shared().reportRoute(XRTeamTransport::XBee, peer.nodeNum, 0, false, nowMs);
    }

    // Force the normal service loop to refresh module identity/configuration
    // on its next pass instead of waiting for the periodic five-minute probe.
    lastInfoQueryMs_ = nowMs - 5u * 60u * 1000u;
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
