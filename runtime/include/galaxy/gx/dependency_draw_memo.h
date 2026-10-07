#pragma once

#include <cstdint>

namespace galaxy::gx::detail {

// One DependencyRangeSink parse interval, over one unassigned GxState owner.
// State changes conservatively forget prior formats; a saturated revision
// disables reuse. Mark only after all required ranges were captured.
class BroadDrawDependencyMemo {
public:
    [[nodiscard]] bool contains(std::uint64_t revision, std::uint8_t format) noexcept {
        if (revision == 0u || revision != revision_) {
            revision_ = revision;
            formats_ = 0u;
        }
        return format < 8u && (formats_ & (1u << format)) != 0u;
    }

    void mark(std::uint8_t format) noexcept {
        if (format < 8u) formats_ |= static_cast<std::uint8_t>(1u << format);
    }

private:
    std::uint64_t revision_ = 0u;
    std::uint8_t formats_ = 0u;
};

}  // namespace galaxy::gx::detail
