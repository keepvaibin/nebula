#pragma once

#include <cstdint>

namespace galaxy::timing {

// Pure predicate for diagnostics whose observation window is anchored to a
// one-shot guest event. A missing/future anchor and malformed offsets all fail
// closed; callers may separately hard-fail malformed process configuration.
[[nodiscard]] constexpr bool relative_vi_window_active(
    bool anchor_seen,
    std::uint64_t anchor_vi,
    std::uint64_t current_vi,
    std::uint64_t start_offset_vi,
    std::uint64_t end_offset_vi) noexcept {
    if (!anchor_seen || current_vi < anchor_vi ||
        end_offset_vi < start_offset_vi) {
        return false;
    }
    const std::uint64_t age_vi = current_vi - anchor_vi;
    return age_vi >= start_offset_vi && age_vi <= end_offset_vi;
}

}  // namespace galaxy::timing
