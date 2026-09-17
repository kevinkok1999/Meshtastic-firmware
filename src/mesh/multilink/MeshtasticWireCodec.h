#pragma once

#include "mesh/RadioInterface.h"
#include <cstddef>
#include <cstdint>

namespace meshtastic::multilink {

/**
 * Serialize/deserialize the same encrypted Meshtastic wire frame that the LoRa
 * radio path carries: PacketHeader followed by the encrypted payload.
 *
 * This deliberately does NOT perform encryption/decryption. The bridge sits
 * after Meshtastic has encrypted/authenticated an outgoing MeshPacket and
 * before the physical bearer. Incoming frames are reconstructed as encrypted
 * MeshPackets so the existing Router/Crypto path remains authoritative.
 */
class MeshtasticWireCodec
{
  public:
    static bool encodeEncrypted(const meshtastic_MeshPacket &packet, uint8_t *out, size_t capacity, size_t &outSize);
    static bool decodeEncrypted(const uint8_t *data, size_t size, meshtastic_MeshPacket &outPacket);
};

} // namespace meshtastic::multilink
