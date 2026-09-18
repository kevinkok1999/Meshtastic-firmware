#include "XREspNowTransport.h"
#include "XRTransportTeam.h"

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR)

#include "NodeDB.h"
#include "Router.h"
#include "UptimeClock.h"
#include "configuration.h"

#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid::xr {

XREspNowTransport *XREspNowTransport::instance_ = nullptr;
XREspNowTransport *xrEspNowTransport = nullptr;

namespace {
constexpr uint8_t BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr UBaseType_t RX_QUEUE_DEPTH = 8;
constexpr UBaseType_t TX_QUEUE_DEPTH = 4;
constexpr UBaseType_t TX_STATUS_QUEUE_DEPTH = 4;
constexpr uint32_t SERVICE_INTERVAL_MS = 50;

bool macEqual(const uint8_t a[6], const uint8_t b[6])
{
    return std::memcmp(a, b, 6) == 0;
}

bool addEspNowPeerIfNeeded(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac))
        return true;

    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = 0; // Follow the currently active Wi-Fi/ESP-NOW channel.
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false; // XR transports Meshtastic ciphertext, not plaintext.

    const esp_err_t result = esp_now_add_peer(&peer);
    return result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
}
} // namespace

XREspNowTransport::XREspNowTransport() : concurrency::OSThread("xr-espnow", SERVICE_INTERVAL_MS)
{
    instance_ = this;
    (void)XRDeliveryEvents::addSink(this);
}

XREspNowTransport::~XREspNowTransport()
{
    XRDeliveryEvents::removeSink(this);
    shutdown();
    if (instance_ == this)
        instance_ = nullptr;
}

bool XREspNowTransport::initialize()
{
    if (initialized_)
        return true;

    if (!rxQueue_)
        rxQueue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxFrame));
    if (!txQueue_)
        txQueue_ = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxPacket));
    if (!txStatusQueue_)
        txStatusQueue_ = xQueueCreate(TX_STATUS_QUEUE_DEPTH, sizeof(TxStatus));
    if (!deliveryEventQueue_)
        deliveryEventQueue_ = xQueueCreate(DELIVERY_EVENT_QUEUE_DEPTH, sizeof(DeliveryEvent));
    if (!rxQueue_ || !txQueue_ || !txStatusQueue_ || !deliveryEventQueue_) {
        LOG_ERROR("XR ESP-NOW queue allocation failed");
        return false;
    }

    // ESP-NOW uses the ESP32 Wi-Fi PHY but needs no AP, WAN or Internet.
    // Preserve an existing AP role rather than replacing it.
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL) {
        if (!WiFi.mode(WIFI_STA)) {
            LOG_ERROR("XR ESP-NOW could not start Wi-Fi STA PHY");
            return false;
        }
    } else if (mode == WIFI_MODE_AP) {
        if (!WiFi.mode(WIFI_AP_STA)) {
            LOG_ERROR("XR ESP-NOW could not add STA PHY to AP mode");
            return false;
        }
    }

    const esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK) {
        LOG_ERROR("XR ESP-NOW init failed: %d", static_cast<int>(initResult));
        return false;
    }

    if (esp_now_register_recv_cb(&XREspNowTransport::onReceive) != ESP_OK) {
        LOG_ERROR("XR ESP-NOW receive callback registration failed");
        esp_now_deinit();
        return false;
    }
    if (esp_now_register_send_cb(&XREspNowTransport::onSend) != ESP_OK) {
        LOG_ERROR("XR ESP-NOW send callback registration failed");
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        return false;
    }

    if (!addEspNowPeerIfNeeded(BROADCAST_MAC)) {
        LOG_ERROR("XR ESP-NOW broadcast peer setup failed");
        esp_now_unregister_send_cb();
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        return false;
    }

    coordinator_.begin();
    const uint32_t nowMs = Time::getMillis();
    (void)deferredStore_.load(deferred_, nowMs);
    initialized_ = true;
    lastHelloMs_ = 0;
    LOG_INFO("XR ESP-NOW sidecar ready (LoRa remains primary/fallback)");
    return true;
}

void XREspNowTransport::shutdown()
{
    if (initialized_) {
        esp_now_unregister_send_cb();
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        initialized_ = false;
    }
    if (rxQueue_) {
        vQueueDelete(rxQueue_);
        rxQueue_ = nullptr;
    }
    if (txQueue_) {
        vQueueDelete(txQueue_);
        txQueue_ = nullptr;
    }
    if (txStatusQueue_) {
        vQueueDelete(txStatusQueue_);
        txStatusQueue_ = nullptr;
    }
    if (deliveryEventQueue_) {
        vQueueDelete(deliveryEventQueue_);
        deliveryEventQueue_ = nullptr;
    }
    mirrorCandidate_ = {};
}

int32_t XREspNowTransport::runOnce()
{
    if (!initAttempted_) {
        initAttempted_ = true;
        if (!initialize())
            return 5000;
    }
    if (!initialized_)
        return 5000;

    const uint32_t nowMs = Time::getMillis();

    RxFrame received{};
    while (xQueueReceive(rxQueue_, &received, 0) == pdTRUE)
        processRx(received, nowMs);

    // Process one released packet before consuming a possible reliable-LoRa
    // failure event for the same packet. This ensures the ciphertext reaches
    // the cache before it can be promoted into persistent recovery storage.
    bool processedImmediate = false;
    TxPacket outgoing{};
    if (xQueueReceive(txQueue_, &outgoing, 0) == pdTRUE) {
        processTx(outgoing, nowMs);
        processedImmediate = true;
    }

    drainDeliveryEvents(nowMs);

    // Each ESP-NOW send waits for its callback, so bound recovery work to one
    // packet per service pass rather than blocking this task on a backlog.
    if (!processedImmediate)
        serviceDeferred(nowMs);

    if (!lastHelloMs_ || nowMs - lastHelloMs_ >= HELLO_INTERVAL_MS)
        sendHello(nowMs);

    expireState(nowMs);
    expireOutboundCache(nowMs);
    (void)deferredStore_.service(deferred_, nowMs, false);
    coordinator_.service(nowMs);
    return SERVICE_INTERVAL_MS;
}

RadioTxHook::PreTxAction XREspNowTransport::beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet)
{
    // Another hook may still hold/drop the LoRa TX. Keep only a bounded
    // encrypted candidate here and hand it to the ESP-NOW task after
    // packetReleased() confirms the radio path consumed the packet.
    mirrorCandidate_ = {};
    if (initialized_.load() && packet && txQueue_ &&
        packet->which_payload_variant == meshtastic_MeshPacket_encrypted_tag && isFromUs(packet) &&
        !isBroadcast(packet->to) && !packet->via_mqtt) {
        mirrorCandidate_.valid = true;
        mirrorCandidate_.packet = *packet;
    }
    return PRETX_SEND;
}

void XREspNowTransport::packetReleased(RadioInterface *, const meshtastic_MeshPacket *packet)
{
    if (!packet || !txQueue_ || !mirrorCandidate_.valid || mirrorCandidate_.packet.id != packet->id) {
        mirrorCandidate_ = {};
        return;
    }

    TxPacket queued{};
    queued.packet = mirrorCandidate_.packet;
    (void)xQueueSend(txQueue_, &queued, 0);
    mirrorCandidate_ = {};
}


bool XREspNowTransport::enqueueDeliveryEvent(DeliveryEventType type, uint32_t peer, uint32_t packetId, uint32_t whenMs)
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

void XREspNowTransport::onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Failed, destination, packetId, nowMs);
}

void XREspNowTransport::onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Acked, peer, packetId, nowMs);
}

void XREspNowTransport::onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    (void)enqueueDeliveryEvent(DeliveryEventType::Naked, peer, packetId, nowMs);
}

void XREspNowTransport::drainDeliveryEvents(uint32_t nowMs)
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

void XREspNowTransport::handleDeliveryEvent(const DeliveryEvent &event, uint32_t nowMs)
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
            LOG_INFO("XR ESP-NOW recovery queued encrypted packet id=0x%08x to=0x%08x", event.packetId, event.peer);
            (void)deferredStore_.service(deferred_, nowMs, true);
        }
        break;
    }
    case DeliveryEventType::Acked:
    case DeliveryEventType::Naked:
        if (event.type == DeliveryEventType::Acked)
            XRTransportTeam::shared().markDelivered(event.peer, event.packetId);
        else
            XRTransportTeam::shared().markCancelled(event.peer, event.packetId);
        deferred_.markDelivered(event.packetId, event.peer);
        clearCachedOutbound(event.peer, event.packetId);
        (void)deferredStore_.service(deferred_, nowMs, true);
        break;
    }
}

void XREspNowTransport::rememberOutbound(const meshtastic_MeshPacket &packet, uint32_t nowMs)
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

XREspNowTransport::CachedOutbound *XREspNowTransport::findCachedOutbound(uint32_t destination, uint32_t packetId)
{
    for (auto &entry : outboundCache_) {
        if (entry.used && entry.packet.to == destination && entry.packet.id == packetId)
            return &entry;
    }
    return nullptr;
}

void XREspNowTransport::clearCachedOutbound(uint32_t destination, uint32_t packetId)
{
    if (CachedOutbound *entry = findCachedOutbound(destination, packetId))
        *entry = {};
}

void XREspNowTransport::expireOutboundCache(uint32_t nowMs)
{
    for (auto &entry : outboundCache_) {
        if (entry.used && (nowMs - entry.cachedAtMs) > OUTBOUND_CACHE_TTL_MS)
            entry = {};
    }
}

void XREspNowTransport::serviceDeferred(uint32_t nowMs)
{
    for (size_t i = 0; i < XRDeferredPacketQueue::MAX_ENTRIES; ++i) {
        auto *entry = deferred_.entry(i);
        if (!entry || !entry->used)
            continue;
        if (static_cast<int32_t>(nowMs - entry->nextAttemptMs) < 0)
            continue;

        Peer *peer = findPeer(entry->packet.to);
        if (!peer || nowMs - peer->lastSeenMs > PEER_FRESH_MS)
            continue;

        (void)attemptPacket(entry->packet, nowMs, true);
        return; // Bound work per service pass; retry/backoff handles the rest.
    }
}

uint8_t XREspNowTransport::peerCount() const
{
    uint8_t count = 0;
    for (const auto &peer : peers_)
        if (peer.used)
            ++count;
    return count;
}

uint8_t XREspNowTransport::linkScoreFor(uint32_t nodeNum) const
{
    const Peer *peer = findPeer(nodeNum);
    if (!peer)
        return 0;

    int score = (static_cast<int>(peer->rssiEwma) + 100) * 2;
    score = std::clamp(score, 0, 100);
    if (peer->sends) {
        const int failurePenalty = (100 * peer->sendFailures / peer->sends) / 2;
        score -= failurePenalty;
    }
    return static_cast<uint8_t>(std::clamp(score, 0, 100));
}

void XREspNowTransport::sendHello(uint32_t nowMs)
{
    if (!router)
        return;

    FrameHeader header{};
    header.type = FrameType::HELLO;
    header.fromNode = router->getNodeNum();
    if (!header.fromNode)
        return;

    if (sendFrame(BROADCAST_MAC, header, nullptr, 0))
        lastHelloMs_ = nowMs;
}

void XREspNowTransport::processRx(const RxFrame &frame, uint32_t nowMs)
{
    if (frame.length < sizeof(FrameHeader))
        return;

    FrameHeader header{};
    std::memcpy(&header, frame.bytes, sizeof(header));
    if (header.magic != WIRE_MAGIC || header.version != WIRE_VERSION || !header.fromNode)
        return;
    if (router && header.fromNode == router->getNodeNum())
        return;

    if (header.type == FrameType::HELLO)
        processHello(header, frame, nowMs);
    else if (header.type == FrameType::DATA)
        processData(header, frame, nowMs);
}

void XREspNowTransport::processHello(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs)
{
    // Sidecar discovery is supplementary to the Meshtastic identity layer.
    // Do not let an arbitrary 2.4 GHz sender create a new node identity.
    if (!nodeDB || nodeDB->getMeshNode(header.fromNode) == nullptr)
        return;

    // Keep a fresh node->MAC binding sticky. A different MAC may take over
    // only after the old peer record has gone stale.
    if (Peer *existing = findPeer(header.fromNode)) {
        if (!macEqual(existing->mac, frame.mac) && nowMs - existing->lastSeenMs <= PEER_FRESH_MS)
            return;
    }

    rememberPeer(header.fromNode, frame.mac, frame.rssi, nowMs);
    (void)addEspNowPeerIfNeeded(frame.mac);
}

void XREspNowTransport::processData(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs)
{
    if (header.fragmentCount == 0 || header.fragmentCount > MAX_FRAGMENTS || header.fragmentIndex >= header.fragmentCount ||
        header.fragmentLength > FRAGMENT_BYTES || header.totalLength == 0 || header.totalLength > MAX_PACKET_BYTES ||
        static_cast<size_t>(header.fragmentOffset) + header.fragmentLength > header.totalLength ||
        frame.length != sizeof(FrameHeader) + header.fragmentLength)
        return;

    Reassembly &assembly = getReassembly(header, frame.mac, nowMs);
    if (!assembly.used || assembly.totalLength != header.totalLength || assembly.fragmentCount != header.fragmentCount)
        return;

    const uint8_t *payload = frame.bytes + sizeof(FrameHeader);
    std::memcpy(assembly.data.data() + header.fragmentOffset, payload, header.fragmentLength);
    assembly.receivedMask |= static_cast<uint8_t>(1u << header.fragmentIndex);
    assembly.updatedMs = nowMs;

    const uint8_t completeMask = static_cast<uint8_t>((1u << header.fragmentCount) - 1u);
    if (assembly.receivedMask != completeMask)
        return;

    if (checksum32(assembly.data.data(), assembly.totalLength) != header.checksum) {
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

    // Fail closed: XR ESP-NOW ingress accepts only a valid encrypted Meshtastic
    // packet whose immutable identity matches the carrier header.
    if (!decoded || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag || packet->from != header.fromNode ||
        packet->id != header.packetId || isBroadcast(packet->from) || !nodeDB ||
        nodeDB->getMeshNode(packet->from) == nullptr) {
        packetPool.release(packet);
        return;
    }

    // Bind the transport address only after a complete MeshPacket passed all
    // carrier and Meshtastic identity checks.
    if (Peer *existing = findPeer(packet->from)) {
        if (!macEqual(existing->mac, frame.mac) && nowMs - existing->lastSeenMs <= PEER_FRESH_MS) {
            packetPool.release(packet);
            return;
        }
    }
    rememberPeer(packet->from, frame.mac, frame.rssi, nowMs);
    (void)addEspNowPeerIfNeeded(frame.mac);

    packet->via_mqtt = false;
    // Keep local ingress metadata distinct from both primary LoRa and XBee.
    // The encrypted Meshtastic payload and packet identity remain unchanged.
    packet->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA_ALT2;
    if (router)
        router->enqueueReceivedMessage(packet);
    else
        packetPool.release(packet);
}

void XREspNowTransport::processTx(const TxPacket &queued, uint32_t nowMs)
{
    // Cache the already-encrypted wire packet inside the sidecar thread. If
    // normal reliable LoRa later exhausts retries, the delivery-event path can
    // promote this exact ciphertext into persistent store/carry/forward.
    rememberOutbound(queued.packet, nowMs);
    (void)attemptPacket(queued.packet, nowMs, false);
}

bool XREspNowTransport::attemptPacket(const meshtastic_MeshPacket &packet, uint32_t nowMs, bool fromDeferredQueue)
{
    Peer *peer = findPeer(packet.to);
    if (!peer || !shouldMirror(packet, *peer, nowMs))
        return false;

    auto &team = XRTransportTeam::shared();
    const uint8_t score = linkScoreFor(peer->nodeNum);
    team.reportRoute(XRTeamTransport::EspNow, peer->nodeNum, score, true, nowMs);

    if (fromDeferredQueue) {
        if (!team.claimRecovery(XRTeamTransport::EspNow, packet.to, packet.id, nowMs))
            return false;
    } else if (!team.allowAssist(XRTeamTransport::EspNow, packet.to, packet.id, nowMs)) {
        return false;
    }

    ++peer->sends;
    const bool accepted = sendPacketToPeer(*peer, packet, nowMs);
    if (!accepted)
        ++peer->sendFailures;

    if (fromDeferredQueue) {
        if (accepted)
            deferred_.markTransportAccepted(packet.id, packet.to, nowMs);
        else
            deferred_.markFailure(packet.id, packet.to, nowMs);
        team.reportRecoveryResult(XRTeamTransport::EspNow, packet.to, packet.id, accepted, nowMs);
    } else {
        team.reportAssistResult(XRTeamTransport::EspNow, packet.to, packet.id, accepted, nowMs);
    }

    XRAdaptiveContext context{};
    context.espNowLinkScore = linkScoreFor(peer->nodeNum);
    context.peerSeenRecently = true;
    context.directMessage = true;
    context.privatePayload = true;

    XRAdaptiveCapabilities capabilities{};
    capabilities.loraAvailable = true;
    capabilities.espNowAvailable = true;
    capabilities.espNowPrivacyApproved = true;

    const XRAdaptivePlan plan = coordinator_.plan(context, capabilities, nowMs, packet.id);
    XRAdaptiveOutcome outcome{};
    outcome.transportAccepted = accepted;
    outcome.transportFailed = !accepted;
    coordinator_.report(plan, outcome, nowMs);
    return accepted;
}

bool XREspNowTransport::sendPacketToPeer(const Peer &peer, const meshtastic_MeshPacket &packet, uint32_t)
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    std::array<uint8_t, MAX_PACKET_BYTES> encoded{};
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), encoded.size());
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &packet) || stream.bytes_written == 0)
        return false;

    const size_t totalLength = stream.bytes_written;
    const size_t fragmentCount = (totalLength + FRAGMENT_BYTES - 1) / FRAGMENT_BYTES;
    if (fragmentCount == 0 || fragmentCount > MAX_FRAGMENTS)
        return false;

    const uint32_t checksum = checksum32(encoded.data(), totalLength);
    for (size_t index = 0; index < fragmentCount; ++index) {
        const size_t offset = index * FRAGMENT_BYTES;
        const size_t length = std::min(FRAGMENT_BYTES, totalLength - offset);

        FrameHeader header{};
        header.type = FrameType::DATA;
        header.fromNode = packet.from;
        header.packetId = packet.id;
        header.totalLength = static_cast<uint16_t>(totalLength);
        header.fragmentOffset = static_cast<uint16_t>(offset);
        header.fragmentIndex = static_cast<uint8_t>(index);
        header.fragmentCount = static_cast<uint8_t>(fragmentCount);
        header.fragmentLength = static_cast<uint16_t>(length);
        header.checksum = checksum;

        if (!sendFrame(peer.mac, header, encoded.data() + offset, length))
            return false;
    }
    return true;
}

bool XREspNowTransport::sendFrame(const uint8_t *mac, FrameHeader header, const uint8_t *payload, size_t payloadLength)
{
    if (!initialized_.load() || !txStatusQueue_ || !mac || payloadLength > FRAGMENT_BYTES ||
        sizeof(FrameHeader) + payloadLength > ESP_NOW_SAFE_FRAME)
        return false;
    if (!addEspNowPeerIfNeeded(mac))
        return false;

    // This sidecar owns one ESP-NOW send at a time. Drain any stale callback
    // record before starting the next frame so a previous timeout cannot be
    // mistaken for the current fragment.
    TxStatus stale{};
    while (xQueueReceive(txStatusQueue_, &stale, 0) == pdTRUE) {
    }

    std::array<uint8_t, ESP_NOW_SAFE_FRAME> frame{};
    std::memcpy(frame.data(), &header, sizeof(header));
    if (payloadLength && payload)
        std::memcpy(frame.data() + sizeof(header), payload, payloadLength);

    if (esp_now_send(mac, frame.data(), sizeof(header) + payloadLength) != ESP_OK)
        return false;

    TxStatus result{};
    if (xQueueReceive(txStatusQueue_, &result, pdMS_TO_TICKS(SEND_STATUS_TIMEOUT_MS)) != pdTRUE)
        return false;
    if (!macEqual(result.mac, mac))
        return false;

    return result.status == ESP_NOW_SEND_SUCCESS;
}

XREspNowTransport::Peer *XREspNowTransport::findPeer(uint32_t nodeNum)
{
    for (auto &peer : peers_)
        if (peer.used && peer.nodeNum == nodeNum)
            return &peer;
    return nullptr;
}

const XREspNowTransport::Peer *XREspNowTransport::findPeer(uint32_t nodeNum) const
{
    for (const auto &peer : peers_)
        if (peer.used && peer.nodeNum == nodeNum)
            return &peer;
    return nullptr;
}

XREspNowTransport::Peer &XREspNowTransport::rememberPeer(uint32_t nodeNum, const uint8_t mac[6], int8_t rssi, uint32_t nowMs)
{
    Peer *slot = findPeer(nodeNum);
    if (!slot) {
        for (auto &peer : peers_) {
            if (!peer.used) {
                slot = &peer;
                break;
            }
        }
    }
    if (!slot) {
        slot = &peers_[0];
        for (auto &peer : peers_)
            if (peer.lastSeenMs < slot->lastSeenMs)
                slot = &peer;
    }

    const bool sameIdentity = slot->used && slot->nodeNum == nodeNum && macEqual(slot->mac, mac);
    if (!sameIdentity)
        *slot = Peer{};

    slot->used = true;
    slot->nodeNum = nodeNum;
    std::memcpy(slot->mac, mac, sizeof(slot->mac));
    slot->rssiEwma = sameIdentity ? static_cast<int16_t>((slot->rssiEwma * 3 + rssi) / 4) : rssi;
    slot->lastSeenMs = nowMs;
    XRTransportTeam::shared().reportRoute(XRTeamTransport::EspNow, nodeNum, linkScoreFor(nodeNum), true, nowMs);
    return *slot;
}

XREspNowTransport::Reassembly &XREspNowTransport::getReassembly(const FrameHeader &header, const uint8_t mac[6], uint32_t nowMs)
{
    for (auto &item : reassembly_) {
        if (item.used && item.fromNode == header.fromNode && item.packetId == header.packetId && macEqual(item.mac, mac))
            return item;
    }

    Reassembly *slot = nullptr;
    for (auto &item : reassembly_) {
        if (!item.used || nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS) {
            slot = &item;
            break;
        }
    }
    if (!slot) {
        slot = &reassembly_[0];
        for (auto &item : reassembly_)
            if (item.updatedMs < slot->updatedMs)
                slot = &item;
    }

    *slot = Reassembly{};
    slot->used = true;
    slot->fromNode = header.fromNode;
    slot->packetId = header.packetId;
    std::memcpy(slot->mac, mac, sizeof(slot->mac));
    slot->totalLength = header.totalLength;
    slot->fragmentCount = header.fragmentCount;
    slot->updatedMs = nowMs;
    return *slot;
}

void XREspNowTransport::expireState(uint32_t nowMs)
{
    for (auto &peer : peers_) {
        if (peer.used && nowMs - peer.lastSeenMs > PEER_FRESH_MS) {
            XRTransportTeam::shared().reportRoute(XRTeamTransport::EspNow, peer.nodeNum, 0, false, nowMs);
            peer.used = false;
        }
    }
    for (auto &item : reassembly_) {
        if (item.used && nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS)
            item.used = false;
    }
}

bool XREspNowTransport::shouldMirror(const meshtastic_MeshPacket &packet, const Peer &peer, uint32_t nowMs)
{
    return initialized_ && peer.used && nowMs - peer.lastSeenMs <= PEER_FRESH_MS && packet.to == peer.nodeNum &&
           packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag && isFromUs(&packet) && !isBroadcast(packet.to) &&
           !packet.via_mqtt;
}

uint32_t XREspNowTransport::checksum32(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void XREspNowTransport::onSend(const esp_now_send_info_t *txInfo, esp_now_send_status_t status)
{
    const uint8_t *mac = txInfo != nullptr ? txInfo->des_addr : nullptr;
#else
void XREspNowTransport::onSend(const uint8_t *mac, esp_now_send_status_t status)
{
#endif
    XREspNowTransport *self = instance_;
    if (!self || !self->initialized_.load() || !self->txStatusQueue_ || !mac)
        return;

    TxStatus result{};
    std::memcpy(result.mac, mac, sizeof(result.mac));
    result.status = status;
    (void)xQueueSend(self->txStatusQueue_, &result, 0);
}

void XREspNowTransport::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    XREspNowTransport *self = instance_;
    if (!self || !self->initialized_.load() || !self->rxQueue_ || !info || !data || length <= 0 ||
        length > static_cast<int>(ESP_NOW_SAFE_FRAME))
        return;

    RxFrame frame{};
    std::memcpy(frame.mac, info->src_addr, sizeof(frame.mac));
    frame.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    frame.length = static_cast<uint16_t>(length);
    std::memcpy(frame.bytes, data, static_cast<size_t>(length));
    (void)xQueueSend(self->rxQueue_, &frame, 0);
}

} // namespace meshoffgrid::xr

#endif // ARCH_ESP32 && T_DECK
