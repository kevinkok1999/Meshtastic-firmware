#pragma once

namespace ops {
namespace v10 {

struct ExperimentalCaps {
    // These capability gates are deliberately false in the production V10.0
    // routing table until two-device hardware QA proves safe radio restoration.
    static bool gfskDirectReleaseEnabled() { return false; }
    static bool bleCodedReleaseEnabled() { return false; }
};

} // namespace v10
} // namespace ops
