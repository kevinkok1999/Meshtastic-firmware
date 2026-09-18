#include "XRXBeeTransport.h"

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include "MemoryPool.h"
#include "Router.h"
#include "UptimeClock.h"
#include "mesh/xbee/XBeeApiCodec.h"

#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid::xr {

XRXBeeTransport *XRXBeeTransport::instance_ = nullptr;
XRXBeeTransport *xrXBeeTransport = nullptr;

XRXBeeTransport::XRXBeeTransport() : concurrency::OSThread("xr-xbee", SERVICE_INTERVAL_MS)
{
    instance_ = this;
}

XRXBeeTransport::~XRXBeeTransport()
{
    shutdown();
    if (instance_ == this)
        instance_ = nullptr;
}

bool XRXBeeTransport::initialize()
{
    if (initialized_)
        return true;

    if (MESHOFFGRID_XBEE_RX_PIN < 0 || MESHOFFGRID_XBEE_TX_PIN < 0 || MESHOFFGRID_XBEE_RX_PIN == MESHOFFGRID_XBEE_TX_PIN) {
        LOG_INFO("XR XBee route compiled but inactive: configure dedicated RX/TX pins");
        return false;
    }

    if (!txQueue_)
        txQueue_ = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxPacket));
    if (!txQueue_) {
        LOG_ERROR("XR XBee TX queue allocation failed");
        return false;
    }

    link_.onReceive(&XRXBeeTransport::onReceive);
    link_.onTxStatus(&XRXBeeTransport::onTxStatus);
    link_.onModemStatus(&XRXBeeTransport::onModemStatus);
    link_.onAtResponse(&XRXBeeTransport::onAtResponse);

    if (!link_.begin(serial_, MESHOFFGRID_XBEE_BAUD, MESHOFFGRID_XBEE_RX_PIN, MESHOFFGRID_XBEE_TX_PIN)) {
        LOG_ERROR("XR XBee UART setup failed");
        return false;
    }

    initialized_ = true;
    online_ = false;
    lastProbeMs_ = 0;
    probeModule(Time::getMillis());
    LOG_INFO("XR XBee route UART ready; probing XR868 API mode");
    return true;
}

void XRXBeeTransport::shutdown()
{
    if (provisionState_ != ProvisionState::Idle)
        serial_.end();
    if (initialized_)
        link_.end();
    provisionState_ = ProvisionState::Idle;
    activeTx_ = ActiveTx{};
    initialized_ = online_ = false;
    if (txQueue_) {
        vQueueDelete(txQueue_);
        txQueue_ = nullptr;
    }
}

void XRXBeeTransport::probeModule(uint32_t nowMs)
{
    if (!initialized_)
        return;
    if (link_.sendAtCommand('N', 'P') != 0)
        lastProbeMs_ = nowMs;
}

void XRXBeeTransport::startFactoryProvisioning(uint32_t nowMs)
{
    // Current XR 868 factory defaults are transparent mode (AP=0) at 9600 baud.
    // Only attempt this fallback after a normal AP=1/115200 API probe did not
    // answer, so an already configured module is never disturbed.
    online_ = false;
    link_.end();
    serial_.begin(FACTORY_BAUD, SERIAL_8N1, MESHOFFGRID_XBEE_RX_PIN, MESHOFFGRID_XBEE_TX_PIN);
    while (serial_.available() > 0)
        (void)serial_.read();

    provisionState_ = ProvisionState::GuardBefore;
    provisionDeadlineMs_ = nowMs + FACTORY_GUARD_MS;
    lastProvisionAttemptMs_ = nowMs;
    commandOkMatch_ = 0;
    LOG_INFO("XR XBee: trying factory-default provisioning at 9600 baud");
}

bool XRXBeeTransport::consumeCommandOk()
{
    while (serial_.available() > 0) {
        const int value = serial_.read();
        if (value < 0)
            continue;

        const char ch = static_cast<char>(value);
        if (commandOkMatch_ == 0)
            commandOkMatch_ = (ch == 'O') ? 1 : 0;
        else if (commandOkMatch_ == 1)
            commandOkMatch_ = (ch == 'K') ? 2 : ((ch == 'O') ? 1 : 0);
        else {
            if (ch == '\r') {
                commandOkMatch_ = 0;
                return true;
            }
            commandOkMatch_ = (ch == 'O') ? 1 : 0;
        }
    }
    return false;
}

void XRXBeeTransport::sendFactoryCommand(const char *command, ProvisionState waitState, uint32_t nowMs)
{
    commandOkMatch_ = 0;
    serial_.print(command);
    provisionState_ = waitState;
    provisionDeadlineMs_ = nowMs + FACTORY_COMMAND_TIMEOUT_MS;
}

void XRXBeeTransport::finishFactoryProvisioning(uint32_t nowMs, bool configured)
{
    serial_.end();
    delay(5);
    link_.begin(serial_, MESHOFFGRID_XBEE_BAUD, MESHOFFGRID_XBEE_RX_PIN, MESHOFFGRID_XBEE_TX_PIN);
    provisionState_ = ProvisionState::Idle;
    online_ = false;
    lastProbeMs_ = 0;
    commandOkMatch_ = 0;

    if (configured)
        LOG_INFO("XR XBee: factory module provisioned to AP=1, AO=0, 115200 baud");
    else
        LOG_DEBUG("XR XBee: no factory-default module detected during provisioning attempt");

    probeModule(nowMs);
}

void XRXBeeTransport::serviceFactoryProvisioning(uint32_t nowMs)
{
    const auto expired = [nowMs](uint32_t deadline) {
        return static_cast<int32_t>(nowMs - deadline) >= 0;
    };

    switch (provisionState_) {
    case ProvisionState::Idle:
        return;

    case ProvisionState::GuardBefore:
        if (expired(provisionDeadlineMs_)) {
            serial_.print("+++");
            commandOkMatch_ = 0;
            provisionState_ = ProvisionState::GuardAfter;
            provisionDeadlineMs_ = nowMs + FACTORY_GUARD_MS;
        }
        return;

    case ProvisionState::GuardAfter:
        if (expired(provisionDeadlineMs_)) {
            provisionState_ = ProvisionState::WaitEnter;
            provisionDeadlineMs_ = nowMs + FACTORY_COMMAND_TIMEOUT_MS;
        }
        return;

    case ProvisionState::WaitEnter:
        if (consumeCommandOk()) {
            sendFactoryCommand("ATAP1\r", ProvisionState::WaitAp, nowMs);
            return;
        }
        break;

    case ProvisionState::WaitAp:
        if (consumeCommandOk()) {
            sendFactoryCommand("ATAO0\r", ProvisionState::WaitAo, nowMs);
            return;
        }
        break;

    case ProvisionState::WaitAo:
        if (consumeCommandOk()) {
            sendFactoryCommand("ATBD7\r", ProvisionState::WaitBd, nowMs);
            return;
        }
        break;

    case ProvisionState::WaitBd:
        if (consumeCommandOk()) {
            sendFactoryCommand("ATWR\r", ProvisionState::WaitWr, nowMs);
            return;
        }
        break;

    case ProvisionState::WaitWr:
        if (consumeCommandOk()) {
            // CN applies AP=1 and BD=7. Do not wait for its reply because the
            // module may switch UART mode/rate as it exits command mode.
            serial_.print("ATCN\r");
            provisionState_ = ProvisionState::ReopenDelay;
            provisionDeadlineMs_ = nowMs + 250u;
            return;
        }
        break;

    case ProvisionState::ReopenDelay:
        if (expired(provisionDeadlineMs_))
            finishFactoryProvisioning(nowMs, true);
        return;
    }

    if (expired(provisionDeadlineMs_))
        finishFactoryProvisioning(nowMs, false);
}

int32_t XRXBeeTransport::runOnce()
{
    if (!initAttempted_) {
        initAttempted_ = true;
        if (!initialize())
            return 10000;
    }
    if (!initialized_)
        return 10000;

    const uint32_t nowMs = Time::getMillis();

    if (provisionState_ != ProvisionState::Idle) {
        serviceFactoryProvisioning(nowMs);
        expireState(nowMs);
        return SERVICE_INTERVAL_MS;
    }

    link_.poll();
    servicePacketTx(nowMs);

    if (!online_) {
        const bool apiProbeTimedOut =
            lastProbeMs_ && (nowMs - lastProbeMs_ >= FACTORY_PROVISION_AFTER_MS);
        const bool provisionRetryDue =
            !lastProvisionAttemptMs_ || (nowMs - lastProvisionAttemptMs_ >= FACTORY_PROVISION_RETRY_MS);

        if (apiProbeTimedOut && provisionRetryDue)
            startFactoryProvisioning(nowMs);
        else if (!lastProbeMs_ || nowMs - lastProbeMs_ >= PROBE_INTERVAL_MS)
            probeModule(nowMs);
    }

    if (online_) {
        if (!activeTx_.active) {
            TxPacket queued{};
            if (txQueue_ && xQueueReceive(txQueue_, &queued, 0) == pdTRUE)
                processTx(queued, nowMs);
        }

        servicePacketTx(nowMs);

        const bool queueEmpty = !txQueue_ || uxQueueMessagesWaiting(txQueue_) == 0;
        if (!activeTx_.active && queueEmpty && (!lastHelloMs_ || nowMs - lastHelloMs_ >= HELLO_INTERVAL_MS))
            sendHello(nowMs);
    }

    expireState(nowMs);
    return SERVICE_INTERVAL_MS;
}

bool XRXBeeTransport::queueFallback(const meshtastic_MeshPacket &packet)
{
    if (!ready() || !txQueue_ || isBroadcast(packet.to) || packet.via_mqtt)
        return false;

    const uint32_t nowMs = Time::getMillis();
    uint32_t packetFrom = packet.from;
    if (!packetFrom && router)
        packetFrom = router->getNodeNum();

    if (!packetFrom || fallbackWasQueued(packetFrom, packet.id, nowMs) ||
        wasRecentIngress(packetFrom, packet.id, nowMs))
        return false;

    // Do not queue a fallback if there is no fresh XBee endpoint that can act
    // either as the final destination or as a bridge into a remote mesh area.
    if (!selectFallbackPeer(packet.to, nowMs))
        return false;

    TxPacket queued{};
    queued.packet = packet;
    queued.packet.from = packetFrom;

    if (xQueueSend(txQueue_, &queued, 0) != pdTRUE) {
        ++txQueueDrops_;
        LOG_WARN("XR XBee TX queue full, drop fallback fr=0x%08x,to=0x%08x,id=0x%08x", packetFrom, packet.to, packet.id);
        return false;
    }

    markFallbackQueued(packetFrom, packet.id, nowMs);
    LOG_INFO("XR XBee fallback queued fr=0x%08x,to=0x%08x,id=0x%08x", packetFrom, packet.to, packet.id);
    return true;
}

void XRXBeeTransport::packetReleased(RadioInterface *, const meshtastic_MeshPacket *packet)
{
    if (!ready() || !txQueue_ || !packet || isBroadcast(packet->to) || packet->via_mqtt)
        return;

    const uint32_t nowMs = Time::getMillis();
    const Peer *peer = findPeer(packet->to);
    if (!peer || !peer->lastIngressMs || nowMs - peer->lastIngressMs > RETURN_ROUTE_MS)
        return;

    uint32_t packetFrom = packet->from;
    if (!packetFrom && router)
        packetFrom = router->getNodeNum();

    // A recent DATA ingress from this destination establishes a temporary
    // reverse path. Mirror replies/ACKs once over XBee so an XBee bridge is
    // bidirectional even when the original LoRa gap still exists.
    if (!packetFrom || fallbackWasQueued(packetFrom, packet->id, nowMs) ||
        wasRecentIngress(packetFrom, packet->id, nowMs))
        return;

    TxPacket queued{};
    queued.packet = *packet;
    queued.packet.from = packetFrom;
    if (xQueueSend(txQueue_, &queued, 0) == pdTRUE) {
        markFallbackQueued(packetFrom, packet->id, nowMs);
        LOG_INFO("XR XBee return path queued fr=0x%08x,to=0x%08x,id=0x%08x", packetFrom, packet->to, packet->id);
    } else {
        ++txQueueDrops_;
        LOG_WARN("XR XBee TX queue full, drop return path fr=0x%08x,to=0x%08x,id=0x%08x", packetFrom, packet->to,
                 packet->id);
    }
}

uint8_t XRXBeeTransport::peerCount() const
{
    uint8_t count = 0;
    for (const auto &peer : peers_)
        if (peer.used)
            ++count;
    return count;
}

void XRXBeeTransport::sendHello(uint32_t nowMs)
{
    if (!router)
        return;

    FrameHeader header{};
    header.type = FrameType::HELLO;
    header.carrierNode = router->getNodeNum();
    if (!header.carrierNode)
        return;

    if (sendFrame(meshoffgrid::xbee::BROADCAST_64, header, nullptr, 0))
        lastHelloMs_ = nowMs;
}

void XRXBeeTransport::processTx(const TxPacket &queued, uint32_t nowMs)
{
    if (!sendPacket(queued.packet, nowMs))
        LOG_WARN("XR XBee could not start packet fr=0x%08x,to=0x%08x,id=0x%08x", queued.packet.from, queued.packet.to,
                 queued.packet.id);
}

bool XRXBeeTransport::sendPacket(const meshtastic_MeshPacket &packet, uint32_t nowMs)
{
    if (!ready() || !router || isBroadcast(packet.to))
        return false;

    // ReliableRouter keeps its retransmission copy before Router::send() performs
    // normal LoRa encryption. Prepare an independent carrier copy here so XBee
    // uses exactly the same Meshtastic ciphertext semantics without mutating the
    // pending LoRa record.
    meshtastic_MeshPacket working = packet;
    if (!working.from)
        working.from = router->getNodeNum();

    working.next_hop = NO_NEXT_HOP_PREFERENCE;
    working.relay_node = 0;
    if (!working.hop_start)
        working.hop_start = working.hop_limit;

    if (working.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        const auto encodeResult = perhapsEncode(&working);
        if (encodeResult != meshtastic_Routing_Error_NONE)
            return false;
    }

    if (working.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    std::array<uint8_t, MAX_PACKET_BYTES> encoded{};
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), encoded.size());
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &working) || stream.bytes_written == 0)
        return false;

    const size_t carrierLimit = std::min<size_t>(npLimit_, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD);
    if (carrierLimit <= sizeof(FrameHeader))
        return false;

    const size_t fragmentBytes = std::min<size_t>(MAX_FRAGMENT_BYTES, carrierLimit - sizeof(FrameHeader));
    if (!fragmentBytes)
        return false;

    const size_t totalLength = stream.bytes_written;
    const size_t fragmentCount = (totalLength + fragmentBytes - 1) / fragmentBytes;
    if (!fragmentCount || fragmentCount > MAX_FRAGMENTS)
        return false;

    const Peer *peer = selectFallbackPeer(working.to, nowMs);
    if (!peer)
        return false;

    if (activeTx_.active)
        return false;

    activeTx_ = ActiveTx{};
    activeTx_.active = true;
    activeTx_.destination64 = peer->address64;
    activeTx_.carrierNode = router->getNodeNum();
    activeTx_.packetFrom = working.from;
    activeTx_.packetId = working.id;
    activeTx_.checksum = checksum32(encoded.data(), totalLength);
    activeTx_.totalLength = static_cast<uint16_t>(totalLength);
    activeTx_.fragmentBytes = static_cast<uint8_t>(fragmentBytes);
    activeTx_.fragmentCount = static_cast<uint8_t>(fragmentCount);
    std::copy_n(encoded.data(), totalLength, activeTx_.encoded.data());

    servicePacketTx(nowMs);
    return activeTx_.active;
}

void XRXBeeTransport::servicePacketTx(uint32_t nowMs)
{
    if (!activeTx_.active)
        return;

    if (activeTx_.inFlightFrameId) {
        if (nowMs - activeTx_.inFlightSinceMs <= TX_STATUS_TIMEOUT_MS)
            return;

        ++txTimeouts_;
        ++txFailures_;
        LOG_WARN("XR XBee TX status timeout fr=0x%08x,id=0x%08x,fragment=%u/%u", activeTx_.packetFrom,
                 activeTx_.packetId, static_cast<unsigned>(activeTx_.fragmentIndex + 1),
                 static_cast<unsigned>(activeTx_.fragmentCount));
        activeTx_.inFlightFrameId = 0;

        if (activeTx_.retryCount < MAX_FRAGMENT_RETRIES) {
            ++activeTx_.retryCount;
            return;
        }

        LOG_WARN("XR XBee abort packet after TX timeout fr=0x%08x,id=0x%08x", activeTx_.packetFrom, activeTx_.packetId);
        activeTx_ = ActiveTx{};
        return;
    }

    if (activeTx_.fragmentIndex >= activeTx_.fragmentCount) {
        activeTx_ = ActiveTx{};
        return;
    }

    const size_t offset = static_cast<size_t>(activeTx_.fragmentIndex) * activeTx_.fragmentBytes;
    const size_t length = std::min<size_t>(activeTx_.fragmentBytes, activeTx_.totalLength - offset);

    FrameHeader header{};
    header.type = FrameType::DATA;
    header.carrierNode = activeTx_.carrierNode;
    header.packetFrom = activeTx_.packetFrom;
    header.packetId = activeTx_.packetId;
    header.totalLength = activeTx_.totalLength;
    header.fragmentOffset = static_cast<uint16_t>(offset);
    header.fragmentIndex = activeTx_.fragmentIndex;
    header.fragmentCount = activeTx_.fragmentCount;
    header.fragmentLength = static_cast<uint8_t>(length);
    header.checksum = activeTx_.checksum;

    const uint8_t frameId = sendFrame(activeTx_.destination64, header, activeTx_.encoded.data() + offset, length);
    if (!frameId) {
        ++txFailures_;
        LOG_WARN("XR XBee UART rejected fragment fr=0x%08x,id=0x%08x,fragment=%u/%u", activeTx_.packetFrom,
                 activeTx_.packetId, static_cast<unsigned>(activeTx_.fragmentIndex + 1),
                 static_cast<unsigned>(activeTx_.fragmentCount));
        if (activeTx_.retryCount < MAX_FRAGMENT_RETRIES) {
            ++activeTx_.retryCount;
            return;
        }
        activeTx_ = ActiveTx{};
        return;
    }

    activeTx_.inFlightFrameId = frameId;
    activeTx_.inFlightSinceMs = nowMs;
}

uint8_t XRXBeeTransport::sendFrame(uint64_t destination64, FrameHeader header, const uint8_t *payload, size_t payloadLength)
{
    if (!initialized_ || payloadLength > MAX_FRAGMENT_BYTES ||
        sizeof(FrameHeader) + payloadLength > std::min<size_t>(npLimit_, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD))
        return 0;

    std::array<uint8_t, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD> carrier{};
    std::memcpy(carrier.data(), &header, sizeof(header));
    if (payloadLength && payload)
        std::memcpy(carrier.data() + sizeof(header), payload, payloadLength);

    return link_.send(destination64, carrier.data(), sizeof(header) + payloadLength);
}

void XRXBeeTransport::processCarrier(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint32_t nowMs)
{
    if (!payload || payloadLength < sizeof(FrameHeader))
        return;

    FrameHeader header{};
    std::memcpy(&header, payload, sizeof(header));
    if (header.magic != WIRE_MAGIC || header.version != WIRE_VERSION || !header.carrierNode)
        return;

    if (router && header.carrierNode == router->getNodeNum())
        return;

    online_ = true;
    rememberPeer(header.carrierNode, source64, nowMs);

    if (header.type == FrameType::HELLO)
        return;
    if (header.type != FrameType::DATA)
        return;

    if (header.fragmentCount == 0 || header.fragmentCount > MAX_FRAGMENTS || header.fragmentIndex >= header.fragmentCount ||
        header.fragmentLength > MAX_FRAGMENT_BYTES || header.totalLength == 0 || header.totalLength > MAX_PACKET_BYTES ||
        static_cast<size_t>(header.fragmentOffset) + header.fragmentLength > header.totalLength ||
        payloadLength != sizeof(FrameHeader) + header.fragmentLength)
        return;

    processData(source64, header, payload + sizeof(FrameHeader), nowMs);
}

void XRXBeeTransport::processData(uint64_t source64, const FrameHeader &header, const uint8_t *payload, uint32_t nowMs)
{
    Reassembly &assembly = getReassembly(source64, header, nowMs);
    if (!assembly.used || assembly.totalLength != header.totalLength || assembly.fragmentCount != header.fragmentCount)
        return;

    std::memcpy(assembly.data.data() + header.fragmentOffset, payload, header.fragmentLength);
    assembly.receivedMask |= (1u << header.fragmentIndex);
    assembly.updatedMs = nowMs;

    const uint32_t completeMask = header.fragmentCount == 32 ? UINT32_MAX : ((1u << header.fragmentCount) - 1u);
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

    if (!decoded || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag || packet->from != header.packetFrom ||
        packet->id != header.packetId) {
        packetPool.release(packet);
        return;
    }

    // Remember the actual packet origin behind this XBee carrier. This creates
    // a short-lived reverse path for ACKs/replies without turning XBee into a
    // permanent parallel sender.
    rememberPeer(packet->from, source64, nowMs);
    if (Peer *returnPeer = findPeer(packet->from))
        returnPeer->lastIngressMs = nowMs;

    markIngress(packet->from, packet->id, nowMs);
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

const XRXBeeTransport::Peer *XRXBeeTransport::selectFallbackPeer(uint32_t destination, uint32_t nowMs) const
{
    // Prefer a direct XBee-capable destination when available.
    if (const Peer *direct = findPeer(destination)) {
        if (direct->used && nowMs - direct->lastSeenMs <= PEER_FRESH_MS)
            return direct;
    }

    // Otherwise use the freshest XBee-capable peer as a bridge. That peer
    // re-injects the original encrypted MeshPacket into its local Meshtastic
    // router, where LoRa/normal mesh forwarding can continue toward the final
    // destination. XBee fallback is only triggered for the original sender, so
    // the packet cannot bounce indefinitely between XBee bridges.
    const Peer *best = nullptr;
    for (const auto &peer : peers_) {
        if (!peer.used || !peer.address64 || nowMs - peer.lastSeenMs > PEER_FRESH_MS)
            continue;
        if (!best || peer.lastSeenMs > best->lastSeenMs)
            best = &peer;
    }
    return best;
}

void XRXBeeTransport::rememberPeer(uint32_t nodeNum, uint64_t address64, uint32_t nowMs)
{
    if (!nodeNum || !address64)
        return;

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

    if (!slot->used || slot->nodeNum != nodeNum)
        *slot = Peer{};

    slot->used = true;
    slot->nodeNum = nodeNum;
    slot->address64 = address64;
    slot->lastSeenMs = nowMs;
}

XRXBeeTransport::Reassembly &XRXBeeTransport::getReassembly(uint64_t source64, const FrameHeader &header, uint32_t nowMs)
{
    for (auto &item : reassembly_) {
        if (item.used && item.source64 == source64 && item.packetFrom == header.packetFrom && item.packetId == header.packetId)
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
    slot->source64 = source64;
    slot->packetFrom = header.packetFrom;
    slot->packetId = header.packetId;
    slot->totalLength = header.totalLength;
    slot->fragmentCount = header.fragmentCount;
    slot->updatedMs = nowMs;
    return *slot;
}

bool XRXBeeTransport::wasRecentIngress(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs) const
{
    for (const auto &item : ingress_) {
        if (item.used && item.packetFrom == packetFrom && item.packetId == packetId &&
            nowMs - item.seenMs <= INGRESS_SUPPRESS_MS)
            return true;
    }
    return false;
}

void XRXBeeTransport::markIngress(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs)
{
    RecentIngress *slot = nullptr;
    for (auto &item : ingress_) {
        if (item.used && item.packetFrom == packetFrom && item.packetId == packetId) {
            slot = &item;
            break;
        }
        if (!item.used && !slot)
            slot = &item;
    }
    if (!slot) {
        slot = &ingress_[0];
        for (auto &item : ingress_)
            if (item.seenMs < slot->seenMs)
                slot = &item;
    }
    *slot = RecentIngress{true, packetFrom, packetId, nowMs};
}

bool XRXBeeTransport::fallbackWasQueued(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs) const
{
    for (const auto &item : fallbackHistory_) {
        if (item.used && item.packetFrom == packetFrom && item.packetId == packetId &&
            nowMs - item.queuedMs <= FALLBACK_SUPPRESS_MS)
            return true;
    }
    return false;
}

void XRXBeeTransport::markFallbackQueued(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs)
{
    RecentFallback *slot = nullptr;
    for (auto &item : fallbackHistory_) {
        if (item.used && item.packetFrom == packetFrom && item.packetId == packetId) {
            slot = &item;
            break;
        }
        if (!item.used && !slot)
            slot = &item;
    }
    if (!slot) {
        slot = &fallbackHistory_[0];
        for (auto &item : fallbackHistory_)
            if (item.queuedMs < slot->queuedMs)
                slot = &item;
    }
    *slot = RecentFallback{true, packetFrom, packetId, nowMs};
}

void XRXBeeTransport::expireState(uint32_t nowMs)
{
    for (auto &peer : peers_)
        if (peer.used && nowMs - peer.lastSeenMs > PEER_FRESH_MS)
            peer = Peer{};

    for (auto &item : reassembly_)
        if (item.used && nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS)
            item.used = false;

    for (auto &item : ingress_)
        if (item.used && nowMs - item.seenMs > INGRESS_SUPPRESS_MS)
            item.used = false;

    for (auto &item : fallbackHistory_)
        if (item.used && nowMs - item.queuedMs > FALLBACK_SUPPRESS_MS)
            item.used = false;
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

void XRXBeeTransport::onReceive(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t)
{
    if (instance_)
        instance_->processCarrier(source64, payload, payloadLength, Time::getMillis());
}

void XRXBeeTransport::onTxStatus(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus)
{
    if (!instance_)
        return;

    auto &self = *instance_;
    self.online_ = true;

    const bool matchesActive =
        self.activeTx_.active && self.activeTx_.inFlightFrameId && self.activeTx_.inFlightFrameId == frameId;

    if (deliveryStatus == 0x00) {
        ++self.txSuccess_;
        if (matchesActive) {
            self.activeTx_.inFlightFrameId = 0;
            self.activeTx_.retryCount = 0;
            ++self.activeTx_.fragmentIndex;
            if (self.activeTx_.fragmentIndex >= self.activeTx_.fragmentCount) {
                LOG_INFO("XR XBee packet RF-complete fr=0x%08x,id=0x%08x,fragments=%u", self.activeTx_.packetFrom,
                         self.activeTx_.packetId, static_cast<unsigned>(self.activeTx_.fragmentCount));
                self.activeTx_ = ActiveTx{};
            }
        }
        return;
    }

    ++self.txFailures_;
    LOG_WARN("XR XBee TX failed status=0x%02x retries=%u discovery=0x%02x frame=%u", deliveryStatus,
             static_cast<unsigned>(retryCount), discoveryStatus, static_cast<unsigned>(frameId));

    if (!matchesActive)
        return;

    self.activeTx_.inFlightFrameId = 0;
    if (self.activeTx_.retryCount < MAX_FRAGMENT_RETRIES) {
        ++self.activeTx_.retryCount;
        LOG_INFO("XR XBee retry fragment fr=0x%08x,id=0x%08x,fragment=%u/%u", self.activeTx_.packetFrom,
                 self.activeTx_.packetId, static_cast<unsigned>(self.activeTx_.fragmentIndex + 1),
                 static_cast<unsigned>(self.activeTx_.fragmentCount));
        return;
    }

    LOG_WARN("XR XBee abort packet after RF failure fr=0x%08x,id=0x%08x", self.activeTx_.packetFrom,
             self.activeTx_.packetId);
    self.activeTx_ = ActiveTx{};
}

void XRXBeeTransport::onModemStatus(uint8_t)
{
    if (!instance_)
        return;

    // A modem-status frame can indicate a reset/rejoin. Re-validate the
    // runtime payload limit before XBee is admitted as an active route again.
    if (instance_->activeTx_.active) {
        ++instance_->txFailures_;
        LOG_WARN("XR XBee modem reset/rejoin aborted active packet fr=0x%08x,id=0x%08x", instance_->activeTx_.packetFrom,
                 instance_->activeTx_.packetId);
        instance_->activeTx_ = ActiveTx{};
    }
    instance_->online_ = false;
    instance_->lastProbeMs_ = 0;
}

void XRXBeeTransport::onAtResponse(uint8_t, char command0, char command1, uint8_t status, const uint8_t *value,
                                   size_t valueLength)
{
    if (!instance_ || command0 != 'N' || command1 != 'P' || status != 0 || !value || !valueLength)
        return;

    uint32_t np = 0;
    for (size_t i = 0; i < valueLength && i < 4; ++i)
        np = (np << 8) | value[i];

    if (np > sizeof(FrameHeader)) {
        instance_->npLimit_ = static_cast<uint16_t>(std::min<uint32_t>(np, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD));
        instance_->online_ = true;
        LOG_INFO("XR XBee XR868 online, NP=%u", static_cast<unsigned>(instance_->npLimit_));
    }
}

} // namespace meshoffgrid::xr

#endif
