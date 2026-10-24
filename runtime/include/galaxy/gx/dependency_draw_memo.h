#pragma once

#include <cstdint>

namespace galaxy::gx::detail {

// Vertex formats whose dependency ranges one DependencyRangeSink parse has already captured at
// the current dependency-shape revision of one GxState. A new revision, or the saturated
// revision 0, forgets them. mark() only after every range the draw needs is captured.
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
