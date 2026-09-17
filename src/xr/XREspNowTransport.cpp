#include "XREspNowTransport.h"

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
constexpr UBaseType_t TX_QUEUE_DEPTH = 8;
constexpr UBaseType_t DELIVERY_EVENT_QUEUE_DEPTH = 12;
constexpr UBaseType_t SEND_STATUS_QUEUE_DEPTH = 8;
constexpr uint32_t SERVICE_INTERVAL_MS = 40;
constexpr uint32_t REINIT_INTERVAL_MS = 5000;
constexpr uint32_t SEND_CALLBACK_TIMEOUT_MS = 1500;

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
    peer.channel = 0; // Follow the current STA channel.
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false; // Payload itself is already Meshtastic ciphertext.

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
    if (!deliveryEventQueue_)
        deliveryEventQueue_ = xQueueCreate(DELIVERY_EVENT_QUEUE_DEPTH, sizeof(DeliveryEvent));
    if (!sendStatusQueue_)
        sendStatusQueue_ = xQueueCreate(SEND_STATUS_QUEUE_DEPTH, sizeof(SendStatus));

    if (!rxQueue_ || !txQueue_ || !deliveryEventQueue_ || !sendStatusQueue_) {
        LOG_ERROR("XR ESP-NOW queue allocation failed");
        return false;
    }

    if (!recoveryLoaded_) {
        const uint32_t nowMs = Time::getMillis();
        if (!deferredStore_.load(deferred_, nowMs))
            LOG_WARN("XR ESP-NOW deferred queue could not be restored; continuing with RAM state");
        recoveryLoaded_ = true;
    }

    // Espressif requires Wi-Fi to be started before ESP-NOW. No AP, WAN or
    // Internet is required. Preserve an existing AP by adding STA mode.
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL) {
        if (!WiFi.mode(WIFI_STA)) {
            LOG_WARN("XR ESP-NOW could not start Wi-Fi STA PHY");
            return false;
        }
    } else if (mode == WIFI_MODE_AP) {
        if (!WiFi.mode(WIFI_AP_STA)) {
            LOG_WARN("XR ESP-NOW could not add STA PHY to AP mode");
            return false;
        }
    }

    const esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK) {
        LOG_WARN("XR ESP-NOW init failed: %d", static_cast<int>(initResult));
        return false;
    }

    instance_ = this;

    if (esp_now_register_send_cb(&XREspNowTransport::onSend) != ESP_OK) {
        LOG_WARN("XR ESP-NOW send callback registration failed");
        esp_now_deinit();
        return false;
    }

    if (esp_now_register_recv_cb(&XREspNowTransport::onReceive) != ESP_OK) {
        LOG_WARN("XR ESP-NOW receive callback registration failed");
        esp_now_unregister_send_cb();
        esp_now_deinit();
        return false;
    }

    if (!addEspNowPeerIfNeeded(BROADCAST_MAC)) {
        LOG_WARN("XR ESP-NOW broadcast peer setup failed");
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        return false;
    }

    coordinator_.begin();
    initialized_ = true;
    lastHelloMs_ = 0;
    sendInFlight_ = false;
    activeTx_ = {};
    LOG_INFO("XR ESP-NOW sidecar ready; LoRa remains primary");
    return true;
}

void XREspNowTransport::shutdown()
{
    (void)deferredStore_.service(deferred_, Time::getMillis(), true);

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
    if (deliveryEventQueue_) {
        vQueueDelete(deliveryEventQueue_);
        deliveryEventQueue_ = nullptr;
    }
    if (sendStatusQueue_) {
        vQueueDelete(sendStatusQueue_);
        sendStatusQueue_ = nullptr;
    }

    sendInFlight_ = false;
    activeTx_ = {};
}

int32_t XREspNowTransport::runOnce()
{
    const uint32_t nowMs = Time::getMillis();

    if (!initialized_ && !initialize()) {
        // Initialization can fail temporarily while the Wi-Fi stack is changing.
        // Recovery queues remain bounded and the sidecar retries automatically.
        drainTxQueue(nowMs, false);
        processDeliveryEvents(nowMs);
        deferred_.expire(nowMs);
        expireOutboundCache(nowMs);
        (void)deferredStore_.service(deferred_, nowMs);
        return REINIT_INTERVAL_MS;
    }

    processSendStatus(nowMs);

    RxFrame received{};
    while (rxQueue_ && xQueueReceive(rxQueue_, &received, 0) == pdTRUE)
        processRx(received, nowMs);

    drainTxQueue(nowMs, true);
    processDeliveryEvents(nowMs);
    processDeferred(nowMs);
    serviceActiveTx(nowMs);

    if (!activeTx_.used && !sendInFlight_ &&
        (!lastHelloMs_ || nowMs - lastHelloMs_ >= HELLO_INTERVAL_MS))
        sendHello(nowMs);

    deferred_.expire(nowMs);
    expireOutboundCache(nowMs);
    expireState(nowMs);
    (void)deferredStore_.service(deferred_, nowMs);
    coordinator_.service(nowMs);
    return SERVICE_INTERVAL_MS;
}

RadioTxHook::PreTxAction XREspNowTransport::beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet)
{
    // This callback is on the LoRa path. Keep it deterministic: only copy a
    // fully encrypted local unicast packet into a bounded queue. All ESP-NOW
    // peer/cache/radio state is owned by the XR worker task.
    if (!packet || !txQueue_ || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag ||
        !isFromUs(packet) || isBroadcast(packet->to) || packet->to == 0 || packet->via_mqtt)
        return PRETX_SEND;

    TxPacket queued{};
    queued.packet = *packet;
    (void)xQueueSend(txQueue_, &queued, 0);
    return PRETX_SEND;
}

void XREspNowTransport::packetReleased(RadioInterface *, const meshtastic_MeshPacket *) {}

void XREspNowTransport::onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (!deliveryEventQueue_)
        return;

    const DeliveryEvent event{DeliveryEventType::FAILED, destination, packetId, nowMs};
    (void)xQueueSend(deliveryEventQueue_, &event, 0);
}

void XREspNowTransport::onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    if (!deliveryEventQueue_)
        return;

    const DeliveryEvent event{DeliveryEventType::ACKED, peer, packetId, nowMs};
    (void)xQueueSend(deliveryEventQueue_, &event, 0);
}

void XREspNowTransport::onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    if (!deliveryEventQueue_)
        return;

    const DeliveryEvent event{DeliveryEventType::NAKED, peer, packetId, nowMs};
    (void)xQueueSend(deliveryEventQueue_, &event, 0);
}

void XREspNowTransport::drainTxQueue(uint32_t nowMs, bool allowRadio)
{
    if (!txQueue_)
        return;

    TxPacket queued{};
    while (xQueueReceive(txQueue_, &queued, 0) == pdTRUE) {
        rememberOutbound(queued.packet, nowMs);

        if (!allowRadio || activeTx_.used)
            continue;

        const Peer *peer = findPeer(queued.packet.to);
        if (!peer || !shouldMirror(queued.packet, *peer, nowMs))
            continue;

        (void)startActiveTx(queued.packet, false, nowMs);
    }
}

void XREspNowTransport::processDeliveryEvents(uint32_t nowMs)
{
    if (!deliveryEventQueue_)
        return;

    DeliveryEvent event{};
    while (xQueueReceive(deliveryEventQueue_, &event, 0) == pdTRUE) {
        if (event.type == DeliveryEventType::FAILED) {
            CachedOutbound *cached = findCachedOutbound(event.peer, event.packetId);
            if (cached && deferred_.enqueue(cached->packet, event.timeMs)) {
                deferred_.makeDue(event.packetId, event.peer, nowMs);
                LOG_INFO("XR ESP-NOW recovery queued encrypted packet id=0x%08x to=0x%08x", event.packetId, event.peer);
                (void)deferredStore_.service(deferred_, nowMs, true);
            }
            continue;
        }

        // End-to-end ACK or an explicit remote NAK both terminate sidecar
        // recovery for the same Meshtastic packet ID.
        deferred_.markDelivered(event.packetId, event.peer);
        clearCachedOutbound(event.peer, event.packetId);

        if (activeTx_.used && activeTx_.packetId == event.packetId && activeTx_.nodeNum == event.peer)
            activeTx_ = {};

        (void)deferredStore_.service(deferred_, nowMs, true);
    }
}

void XREspNowTransport::processSendStatus(uint32_t nowMs)
{
    if (sendInFlight_ && nowMs - sendStartedMs_ > SEND_CALLBACK_TIMEOUT_MS) {
        sendInFlight_ = false;
        if (activeTx_.used)
            finishActiveTx(false, nowMs);
    }

    if (!sendStatusQueue_)
        return;

    SendStatus status{};
    while (xQueueReceive(sendStatusQueue_, &status, 0) == pdTRUE) {
        sendInFlight_ = false;

        if (!activeTx_.used || !macEqual(activeTx_.mac, status.mac))
            continue;

        if (!status.success) {
            finishActiveTx(false, nowMs);
            continue;
        }

        if (activeTx_.nextFragment < activeTx_.fragmentCount)
            ++activeTx_.nextFragment;

        if (activeTx_.nextFragment >= activeTx_.fragmentCount)
            finishActiveTx(true, nowMs);
    }
}

void XREspNowTransport::processDeferred(uint32_t nowMs)
{
    if (activeTx_.used)
        return;

    for (size_t i = 0; i < XRDeferredPacketQueue::MAX_ENTRIES; ++i) {
        auto *entry = deferred_.entry(i);
        if (!entry || !entry->used)
            continue;
        if (static_cast<int32_t>(nowMs - entry->nextAttemptMs) < 0)
            continue;

        const Peer *peer = findPeer(entry->packet.to);
        if (!peer || !shouldMirror(entry->packet, *peer, nowMs))
            continue;

        if (startActiveTx(entry->packet, true, nowMs))
            return;

        deferred_.markFailure(entry->packet.id, entry->packet.to, nowMs);
    }
}

bool XREspNowTransport::startActiveTx(const meshtastic_MeshPacket &packet, bool fromDeferred, uint32_t nowMs)
{
    if (activeTx_.used || !initialized_ || packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    Peer *peer = findPeer(packet.to);
    if (!peer || !shouldMirror(packet, *peer, nowMs))
        return false;

    std::array<uint8_t, MAX_PACKET_BYTES> encoded{};
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), encoded.size());
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &packet) || stream.bytes_written == 0)
        return false;

    const size_t fragmentCount = (stream.bytes_written + FRAGMENT_BYTES - 1u) / FRAGMENT_BYTES;
    if (fragmentCount == 0 || fragmentCount > MAX_FRAGMENTS)
        return false;

    activeTx_ = {};
    activeTx_.used = true;
    activeTx_.fromDeferred = fromDeferred;
    activeTx_.nodeNum = peer->nodeNum;
    activeTx_.packetId = packet.id;
    std::memcpy(activeTx_.mac, peer->mac, sizeof(activeTx_.mac));
    activeTx_.totalLength = static_cast<uint16_t>(stream.bytes_written);
    activeTx_.fragmentCount = static_cast<uint8_t>(fragmentCount);
    activeTx_.checksum = checksum32(encoded.data(), stream.bytes_written);
    std::memcpy(activeTx_.encoded.data(), encoded.data(), stream.bytes_written);

    if (peer->sends != UINT16_MAX)
        ++peer->sends;

    return true;
}

void XREspNowTransport::serviceActiveTx(uint32_t nowMs)
{
    if (!activeTx_.used || sendInFlight_)
        return;

    if (activeTx_.nextFragment >= activeTx_.fragmentCount) {
        finishActiveTx(true, nowMs);
        return;
    }

    const size_t offset = static_cast<size_t>(activeTx_.nextFragment) * FRAGMENT_BYTES;
    const size_t remaining = activeTx_.totalLength - offset;
    const size_t length = std::min(FRAGMENT_BYTES, remaining);

    FrameHeader header{};
    header.type = FrameType::DATA;
    header.fromNode = router ? router->getNodeNum() : 0;
    header.packetId = activeTx_.packetId;
    header.totalLength = activeTx_.totalLength;
    header.fragmentOffset = static_cast<uint16_t>(offset);
    header.fragmentIndex = activeTx_.nextFragment;
    header.fragmentCount = activeTx_.fragmentCount;
    header.fragmentLength = static_cast<uint16_t>(length);
    header.checksum = activeTx_.checksum;

    if (!header.fromNode ||
        !sendFrame(activeTx_.mac, header, activeTx_.encoded.data() + offset, length))
        finishActiveTx(false, nowMs);
}

void XREspNowTransport::finishActiveTx(bool success, uint32_t nowMs)
{
    if (!activeTx_.used)
        return;

    const uint32_t nodeNum = activeTx_.nodeNum;
    const uint32_t packetId = activeTx_.packetId;
    const bool fromDeferred = activeTx_.fromDeferred;

    if (Peer *peer = findPeer(nodeNum)) {
        if (!success && peer->sendFailures != UINT16_MAX)
            ++peer->sendFailures;
    }

    if (fromDeferred) {
        if (success)
            deferred_.markTransportAccepted(packetId, nodeNum, nowMs);
        else
            deferred_.markFailure(packetId, nodeNum, nowMs);
    }

    activeTx_ = {};

    XRAdaptiveContext context{};
    context.espNowLinkScore = linkScoreFor(nodeNum);
    context.peerSeenRecently = true;
    context.directMessage = true;
    context.privatePayload = true;

    XRAdaptiveCapabilities capabilities{};
    capabilities.loraAvailable = true;
    capabilities.espNowAvailable = initialized_;
    capabilities.espNowPrivacyApproved = true;

    const XRAdaptivePlan plan = coordinator_.plan(context, capabilities, nowMs, packetId);
    XRAdaptiveOutcome outcome{};
    outcome.transportAccepted = success;
    outcome.transportFailed = !success;
    coordinator_.report(plan, outcome, nowMs);
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
        const int failurePenalty = static_cast<int>((100u * peer->sendFailures / peer->sends) / 2u);
        score -= failurePenalty;
    }

    return static_cast<uint8_t>(std::clamp(score, 0, 100));
}

void XREspNowTransport::sendHello(uint32_t nowMs)
{
    if (!router || sendInFlight_)
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
    rememberPeer(header.fromNode, frame.mac, frame.rssi, nowMs);
    (void)addEspNowPeerIfNeeded(frame.mac);
}

void XREspNowTransport::processData(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs)
{
    if (header.fragmentCount == 0 || header.fragmentCount > MAX_FRAGMENTS ||
        header.fragmentIndex >= header.fragmentCount || header.fragmentLength > FRAGMENT_BYTES ||
        header.totalLength == 0 || header.totalLength > MAX_PACKET_BYTES ||
        static_cast<size_t>(header.fragmentOffset) + header.fragmentLength > header.totalLength ||
        frame.length != sizeof(FrameHeader) + header.fragmentLength)
        return;

    rememberPeer(header.fromNode, frame.mac, frame.rssi, nowMs);

    Reassembly &assembly = getReassembly(header, frame.mac, nowMs);
    if (!assembly.used || assembly.totalLength != header.totalLength ||
        assembly.fragmentCount != header.fragmentCount || assembly.checksum != header.checksum)
        return;

    const uint8_t *payload = frame.bytes + sizeof(FrameHeader);
    std::memcpy(assembly.data.data() + header.fragmentOffset, payload, header.fragmentLength);
    assembly.receivedMask |= static_cast<uint8_t>(1u << header.fragmentIndex);
    assembly.updatedMs = nowMs;

    const uint8_t completeMask = static_cast<uint8_t>((1u << header.fragmentCount) - 1u);
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
        packet->from != header.fromNode || packet->id != header.packetId ||
        isBroadcast(packet->from) || isBroadcast(packet->to) ||
        (router && packet->to != router->getNodeNum())) {
        packetPool.release(packet);
        return;
    }

    packet->via_mqtt = false;
    if (router)
        router->enqueueReceivedMessage(packet);
    else
        packetPool.release(packet);
}

bool XREspNowTransport::sendFrame(const uint8_t *mac, FrameHeader header, const uint8_t *payload, size_t payloadLength)
{
    if (!initialized_ || sendInFlight_ || !mac || payloadLength > FRAGMENT_BYTES ||
        sizeof(FrameHeader) + payloadLength > ESP_NOW_SAFE_FRAME)
        return false;

    if (!addEspNowPeerIfNeeded(mac))
        return false;

    std::array<uint8_t, ESP_NOW_SAFE_FRAME> frame{};
    std::memcpy(frame.data(), &header, sizeof(header));
    if (payloadLength && payload)
        std::memcpy(frame.data() + sizeof(header), payload, payloadLength);

    sendInFlight_ = true;
    sendStartedMs_ = Time::getMillis();

    const esp_err_t result = esp_now_send(mac, frame.data(), sizeof(header) + payloadLength);
    if (result != ESP_OK) {
        sendInFlight_ = false;
        return false;
    }

    return true;
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
    return *slot;
}

XREspNowTransport::Reassembly &XREspNowTransport::getReassembly(const FrameHeader &header, const uint8_t mac[6],
                                                                uint32_t nowMs)
{
    for (auto &item : reassembly_) {
        if (item.used && item.fromNode == header.fromNode && item.packetId == header.packetId &&
            macEqual(item.mac, mac))
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
    slot->checksum = header.checksum;
    slot->updatedMs = nowMs;
    return *slot;
}

void XREspNowTransport::expireState(uint32_t nowMs)
{
    for (auto &peer : peers_) {
        if (peer.used && nowMs - peer.lastSeenMs > PEER_FRESH_MS)
            peer.used = false;
    }

    for (auto &item : reassembly_) {
        if (item.used && nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS)
            item.used = false;
    }
}

bool XREspNowTransport::shouldMirror(const meshtastic_MeshPacket &packet, const Peer &peer, uint32_t nowMs)
{
    return initialized_ && peer.used && nowMs - peer.lastSeenMs <= PEER_FRESH_MS &&
           packet.to == peer.nodeNum && packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag &&
           isFromUs(&packet) && !isBroadcast(packet.to) && !packet.via_mqtt;
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

void XREspNowTransport::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    XREspNowTransport *self = instance_;
    if (!self || !self->initialized_ || !self->rxQueue_ || !info || !data || length <= 0 ||
        length > static_cast<int>(ESP_NOW_SAFE_FRAME))
        return;

    RxFrame frame{};
    std::memcpy(frame.mac, info->src_addr, sizeof(frame.mac));
    frame.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    frame.length = static_cast<uint16_t>(length);
    std::memcpy(frame.bytes, data, static_cast<size_t>(length));
    (void)xQueueSend(self->rxQueue_, &frame, 0);
}

void XREspNowTransport::onSend(const uint8_t *mac, esp_now_send_status_t status)
{
    XREspNowTransport *self = instance_;
    if (!self || !self->sendStatusQueue_)
        return;

    SendStatus report{};
    if (mac)
        std::memcpy(report.mac, mac, sizeof(report.mac));
    report.success = status == ESP_NOW_SEND_SUCCESS;
    (void)xQueueSend(self->sendStatusQueue_, &report, 0);
}

} // namespace meshoffgrid::xr

#endif // ARCH_ESP32 && T_DECK && MESHOFFGRID_ENABLE_XR
