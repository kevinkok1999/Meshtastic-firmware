#pragma once

#include "V6Policy.h"

namespace meshoffgrid::v6 {

class V6PolicyStore
{
  public:
    static constexpr const char *PATH = "/prefs/v6_policy.bin";
    static constexpr const char *TEMP_PATH = "/prefs/v6_policy.tmp";

    static bool load(PolicyState &state);
    static bool save(const PolicyState &state);
};

} // namespace meshoffgrid::v6
