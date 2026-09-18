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
    if (initialized_)
        link_.end();
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
    link_.poll();

    if (!online_ && (!lastProbeMs_ || nowMs - lastProbeMs_ >= PROBE_INTERVAL_MS))
        probeModule(nowMs);

    if (online_) {
        TxPacket queued{};
        while (txQueue_ && xQueueReceive(txQueue_, &queued, 0) == pdTRUE)
            processTx(queued, nowMs);

        if (!lastHelloMs_ || nowMs - lastHelloMs_ >= HELLO_INTERVAL_MS)
            sendHello(nowMs);
    }

    expireState(nowMs);
    return SERVICE_INTERVAL_MS;
}

void XRXBeeTransport::packetReleased(RadioInterface *, const meshtastic_MeshPacket *packet)
{
    if (!ready() || !txQueue_ || !packet || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag ||
        packet->via_mqtt || !isFromUs(packet))
        return;

    // XBee is an additional route for directed traffic. Normal mesh broadcasts remain
    // on LoRa/ESP-NOW to avoid multiplying network-wide traffic.
    if (isBroadcast(packet->to))
        return;

    const uint32_t nowMs = Time::getMillis();
    if (wasRecentIngress(packet->from, packet->id, nowMs))
        return;

    TxPacket queued{};
    queued.packet = *packet;
    (void)xQueueSend(txQueue_, &queued, 0);
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
    (void)sendPacket(queued.packet, nowMs);
}

bool XRXBeeTransport::sendPacket(const meshtastic_MeshPacket &packet, uint32_t)
{
    if (!ready() || !router || packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    std::array<uint8_t, MAX_PACKET_BYTES> encoded{};
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), encoded.size());
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &packet) || stream.bytes_written == 0)
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

    const Peer *peer = findPeer(packet.to);
    if (!peer)
        return false; // Never flood directed DATA just to discover a route; HELLO frames perform discovery.

    const uint64_t destination = peer->address64;
    const uint32_t checksum = checksum32(encoded.data(), totalLength);

    for (size_t index = 0; index < fragmentCount; ++index) {
        const size_t offset = index * fragmentBytes;
        const size_t length = std::min(fragmentBytes, totalLength - offset);

        FrameHeader header{};
        header.type = FrameType::DATA;
        header.carrierNode = router->getNodeNum();
        header.packetFrom = packet.from;
        header.packetId = packet.id;
        header.totalLength = static_cast<uint16_t>(totalLength);
        header.fragmentOffset = static_cast<uint16_t>(offset);
        header.fragmentIndex = static_cast<uint8_t>(index);
        header.fragmentCount = static_cast<uint8_t>(fragmentCount);
        header.fragmentLength = static_cast<uint8_t>(length);
        header.checksum = checksum;

        if (!sendFrame(destination, header, encoded.data() + offset, length))
            return false;
    }
    return true;
}

bool XRXBeeTransport::sendFrame(uint64_t destination64, FrameHeader header, const uint8_t *payload, size_t payloadLength)
{
    if (!initialized_ || payloadLength > MAX_FRAGMENT_BYTES ||
        sizeof(FrameHeader) + payloadLength > std::min<size_t>(npLimit_, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD))
        return false;

    std::array<uint8_t, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD> carrier{};
    std::memcpy(carrier.data(), &header, sizeof(header));
    if (payloadLength && payload)
        std::memcpy(carrier.data() + sizeof(header), payload, payloadLength);

    return link_.send(destination64, carrier.data(), sizeof(header) + payloadLength) != 0;
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

void XRXBeeTransport::expireState(uint32_t nowMs)
{
    for (auto &peer : peers_)
        if (peer.used && nowMs - peer.lastSeenMs > PEER_FRESH_MS)
            peer.used = false;

    for (auto &item : reassembly_)
        if (item.used && nowMs - item.updatedMs > REASSEMBLY_TIMEOUT_MS)
            item.used = false;

    for (auto &item : ingress_)
        if (item.used && nowMs - item.seenMs > INGRESS_SUPPRESS_MS)
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

void XRXBeeTransport::onTxStatus(uint8_t, uint8_t deliveryStatus, uint8_t, uint8_t)
{
    if (!instance_)
        return;
    instance_->online_ = true;
    if (deliveryStatus == 0x00)
        ++instance_->txSuccess_;
    else
        ++instance_->txFailures_;
}

void XRXBeeTransport::onModemStatus(uint8_t)
{
    if (!instance_)
        return;

    // A modem-status frame can indicate a reset/rejoin. Re-validate the
    // runtime payload limit before XBee is admitted as an active route again.
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
        instance_->npLimit_ = static_cast<uint8_t>(std::min<uint32_t>(np, meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD));
        instance_->online_ = true;
        LOG_INFO("XR XBee XR868 online, NP=%u", static_cast<unsigned>(instance_->npLimit_));
    }
}

} // namespace meshoffgrid::xr

#endif
