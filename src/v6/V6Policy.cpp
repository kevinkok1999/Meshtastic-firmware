#include "V6Policy.h"

namespace meshoffgrid::v6 {

namespace {
uint32_t fnv1aByte(uint32_t hash, uint8_t value)
{
    constexpr uint32_t prime = 16777619u;
    hash ^= value;
    hash *= prime;
    return hash;
}

uint32_t feed16(uint32_t hash, uint16_t value)
{
    hash = fnv1aByte(hash, static_cast<uint8_t>(value & 0xffu));
    return fnv1aByte(hash, static_cast<uint8_t>((value >> 8) & 0xffu));
}

uint32_t feed32(uint32_t hash, uint32_t value)
{
    for (uint8_t shift = 0; shift < 32; shift += 8)
        hash = fnv1aByte(hash, static_cast<uint8_t>((value >> shift) & 0xffu));
    return hash;
}
} // namespace

PolicyState V6Policy::defaults()
{
    PolicyState state{};
    state.magic = MAGIC;
    state.version = VERSION;
    state.connectionMode = static_cast<uint8_t>(ConnectionMode::OffGrid);
    state.privacyProfile = static_cast<uint8_t>(PrivacyProfile::Private);
    state.tunnelPolicy = static_cast<uint8_t>(TunnelPolicy::Off);
    state.generation = 1;
    seal(state);
    return state;
}

uint32_t V6Policy::checksum(const PolicyState &state)
{
    constexpr uint32_t offset = 2166136261u;
    uint32_t hash = offset;
    hash = feed32(hash, state.magic);
    hash = feed16(hash, state.version);
    hash = fnv1aByte(hash, state.connectionMode);
    hash = fnv1aByte(hash, state.privacyProfile);
    hash = fnv1aByte(hash, state.tunnelPolicy);
    hash = fnv1aByte(hash, state.reserved0);
    hash = feed16(hash, state.reserved1);
    hash = feed32(hash, state.generation);
    return hash;
}

void V6Policy::seal(PolicyState &state)
{
    state.checksum = 0;
    state.checksum = checksum(state);
}

bool V6Policy::valid(const PolicyState &state)
{
    if (state.magic != MAGIC || state.version != VERSION)
        return false;
    if (state.connectionMode > static_cast<uint8_t>(ConnectionMode::Smart))
        return false;
    if (state.privacyProfile > static_cast<uint8_t>(PrivacyProfile::Maximum))
        return false;
    if (state.tunnelPolicy > static_cast<uint8_t>(TunnelPolicy::Required))
        return false;
    return state.checksum == checksum(state);
}

ConnectionMode V6Policy::connectionMode(const PolicyState &state)
{
    return static_cast<ConnectionMode>(state.connectionMode);
}

PrivacyProfile V6Policy::privacyProfile(const PolicyState &state)
{
    return static_cast<PrivacyProfile>(state.privacyProfile);
}

TunnelPolicy V6Policy::tunnelPolicy(const PolicyState &state)
{
    return static_cast<TunnelPolicy>(state.tunnelPolicy);
}

bool V6Policy::allowsOffGrid(const PolicyState &state)
{
    const auto mode = connectionMode(state);
    return mode == ConnectionMode::OffGrid || mode == ConnectionMode::Smart;
}

bool V6Policy::allowsInternet(const PolicyState &state)
{
    const auto mode = connectionMode(state);
    return mode == ConnectionMode::Internet || mode == ConnectionMode::Smart;
}

bool V6Policy::internetOnly(const PolicyState &state)
{
    return connectionMode(state) == ConnectionMode::Internet;
}

bool V6Policy::smartMode(const PolicyState &state)
{
    return connectionMode(state) == ConnectionMode::Smart;
}

bool V6Policy::maximumPrivacy(const PolicyState &state)
{
    return privacyProfile(state) == PrivacyProfile::Maximum;
}

PolicyDecision V6Policy::evaluateInternet(const PolicyState &state, const RuntimeCapabilities &capabilities)
{
    PolicyDecision out{};
    if (!valid(state)) {
        out.reason = PolicyRejectReason::InvalidState;
        return out;
    }
    if (!allowsInternet(state)) {
        out.allowed = false;
        out.reason = PolicyRejectReason::None;
        return out;
    }
    if (!capabilities.wifiConfigured) {
        out.reason = PolicyRejectReason::WifiNotConfigured;
        return out;
    }
    if (tunnelPolicy(state) == TunnelPolicy::Required && !capabilities.privacyTunnelReady) {
        out.reason = PolicyRejectReason::TunnelRequiredButUnavailable;
        return out;
    }
    out.allowed = true;
    return out;
}

} // namespace meshoffgrid::v6
