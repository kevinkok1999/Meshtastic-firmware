#include "MeshtasticWireCodec.h"

#include <cstring>

namespace meshtastic::multilink {

bool MeshtasticWireCodec::encodeEncrypted(const meshtastic_MeshPacket &packet, uint8_t *out, size_t capacity, size_t &outSize)
{
    outSize = 0;
    if (out == nullptr || packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    const size_t payloadSize = packet.encrypted.size;
    const size_t totalSize = sizeof(PacketHeader) + payloadSize;
    if (payloadSize > MAX_RADIO_PAYLOAD_LEN || totalSize > MAX_LORA_PAYLOAD_LEN || totalSize > capacity)
        return false;

    PacketHeader header{};
    header.to = packet.to;
    header.from = packet.from;
    header.id = packet.id;
    header.channel = packet.channel;
    header.next_hop = packet.next_hop;
    header.relay_node = packet.relay_node;

    header.flags = static_cast<uint8_t>(packet.hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK);
    if (packet.want_ack)
        header.flags |= PACKET_FLAGS_WANT_ACK_MASK;
    if (packet.via_mqtt)
        header.flags |= PACKET_FLAGS_VIA_MQTT_MASK;
    header.flags |= static_cast<uint8_t>((packet.hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);

    std::memcpy(out, &header, sizeof(header));
    if (payloadSize != 0)
        std::memcpy(out + sizeof(header), packet.encrypted.bytes, payloadSize);

    outSize = totalSize;
    return true;
}

bool MeshtasticWireCodec::decodeEncrypted(const uint8_t *data, size_t size, meshtastic_MeshPacket &outPacket)
{
    if (data == nullptr || size < sizeof(PacketHeader) || size > MAX_LORA_PAYLOAD_LEN)
        return false;

    PacketHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.from == 0)
        return false;

    const size_t payloadSize = size - sizeof(PacketHeader);
    if (payloadSize > sizeof(outPacket.encrypted.bytes) || payloadSize > MAX_RADIO_PAYLOAD_LEN)
        return false;

    outPacket = meshtastic_MeshPacket_init_zero;
    outPacket.from = header.from;
    outPacket.to = header.to;
    outPacket.id = header.id;
    outPacket.channel = header.channel;
    outPacket.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    outPacket.hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    outPacket.want_ack = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
    outPacket.via_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    outPacket.next_hop = outPacket.hop_start == 0 ? NO_NEXT_HOP_PREFERENCE : header.next_hop;
    outPacket.relay_node = outPacket.hop_start == 0 ? NO_RELAY_NODE : header.relay_node;
    outPacket.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    if (payloadSize != 0)
        std::memcpy(outPacket.encrypted.bytes, data + sizeof(header), payloadSize);
    outPacket.encrypted.size = payloadSize;

    return true;
}

} // namespace meshtastic::multilink
