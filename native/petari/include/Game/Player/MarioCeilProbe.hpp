#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief Mario::calcDistToCeil probes for a ceiling from Mario's position moved `offset` along the air gravity: 30
    /// under him, or at him while he hangs (0). When the hit from a start at him lies at or under his feet, it probes once
    /// more from 10 above him (cCeilProbeAbove); a hit there at or under his feet again counts as no ceiling. The console
    /// probed from 10 above again and again: when the step's gravity (the ground's, a bump's, a wall's) is 90 degrees or
    /// more from the air gravity, every probe from there lies "under his feet" and the loop never ended (N-3016).
    constexpr f32 cCeilProbeAbove = -10.0f;

    /// @brief True, with `rOffset` moved to cCeilProbeAbove, when the probe from `rOffset` is to be made again from above.
    inline bool retryCeilProbeAbove(f32& rOffset) {
        if (rOffset > 1.0f || rOffset == cCeilProbeAbove) {
            return false;
        }
        rOffset = cCeilProbeAbove;
        return true;
    }
}  // namespace MR
