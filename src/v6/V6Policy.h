#pragma once

#include <cstdint>

namespace meshoffgrid::v6 {

enum class ConnectionMode : uint8_t {
    OffGrid = 0,
    Internet = 1,
    Smart = 2,
};

enum class PrivacyProfile : uint8_t {
    Balanced = 0,
    Private = 1,
    Maximum = 2,
};

enum class TunnelPolicy : uint8_t {
    Off = 0,
    Preferred = 1,
    Required = 2,
};

enum class PolicyRejectReason : uint8_t {
    None = 0,
    InvalidState,
    WifiNotConfigured,
    TunnelRequiredButUnavailable,
};

struct PolicyState {
    uint32_t magic = 0x4d4f4736u; // "MOG6"
    uint16_t version = 1;
    uint8_t connectionMode = static_cast<uint8_t>(ConnectionMode::OffGrid);
    uint8_t privacyProfile = static_cast<uint8_t>(PrivacyProfile::Private);
    uint8_t tunnelPolicy = static_cast<uint8_t>(TunnelPolicy::Off);
    uint8_t reserved0 = 0;
    uint16_t reserved1 = 0;
    uint32_t generation = 1;
    uint32_t checksum = 0;
};

struct RuntimeCapabilities {
    bool wifiConfigured = false;
    bool privacyTunnelReady = false;
};

struct PolicyDecision {
    bool allowed = false;
    PolicyRejectReason reason = PolicyRejectReason::None;
};

class V6Policy
{
  public:
    static constexpr uint32_t MAGIC = 0x4d4f4736u;
    static constexpr uint16_t VERSION = 1;

    static PolicyState defaults();
    static uint32_t checksum(const PolicyState &state);
    static void seal(PolicyState &state);
    static bool valid(const PolicyState &state);

    static ConnectionMode connectionMode(const PolicyState &state);
    static PrivacyProfile privacyProfile(const PolicyState &state);
    static TunnelPolicy tunnelPolicy(const PolicyState &state);

    static bool allowsOffGrid(const PolicyState &state);
    static bool allowsInternet(const PolicyState &state);
    static bool internetOnly(const PolicyState &state);
    static bool smartMode(const PolicyState &state);
    static bool maximumPrivacy(const PolicyState &state);

    static PolicyDecision evaluateInternet(const PolicyState &state, const RuntimeCapabilities &capabilities);
};

} // namespace meshoffgrid::v6
