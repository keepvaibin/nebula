#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>

namespace galaxy {

// One integer-pixel content rectangle shared by presentation and host input.
// Keeping the fit here prevents render and pointer paths from disagreeing by a
// rounded pixel at letterbox or pillarbox edges.
struct ContentViewport {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

inline constexpr double kWiiContentAspect = 16.0 / 9.0;

// Preserve the game's original native 4:3 movie path (including its authored
// crop). Custom widescreen gameplay uses the original 16:9 movie composition.
[[nodiscard]] inline double presentation_content_aspect(double gameplay_aspect,
    bool prerecorded_movie) noexcept {
    return prerecorded_movie && std::abs(gameplay_aspect-4.0/3.0)>1e-10
        ? kWiiContentAspect : gameplay_aspect;
}

struct PresentationContentSnapshot {
    ContentViewport viewport{};
    int surface_width{}, surface_height{};
    std::uint64_t generation{};
};

// One render-thread writer publishes the rectangle actually used for the final
// blit. Input readers have bounded retries and reject a resize mismatch. Every
// payload word is atomic, including during an interrupted publication.
class PresentationContentPublication {
    std::atomic<std::uint64_t> version_{0}, extent_{0}, rectangle_{0};
public:
    bool publish(int width,int height,ContentViewport rect) noexcept {
        if (width<=1 || height<=1 || width>65535 || height>65535 ||
            rect.left<0 || rect.top<0 || rect.width<=1 || rect.height<=1 ||
            rect.left>width-rect.width || rect.top>height-rect.height) return false;
        const auto extent=(static_cast<std::uint64_t>(width)<<32u)|static_cast<unsigned>(height);
        const auto rectangle=(static_cast<std::uint64_t>(rect.left)<<48u)|
            (static_cast<std::uint64_t>(rect.top)<<32u)|
            (static_cast<std::uint64_t>(rect.width)<<16u)|static_cast<unsigned>(rect.height);
        if (extent_.load()==extent && rectangle_.load()==rectangle && version_.load()!=0u) return true;
        const auto previous=version_.load();
        version_.store(previous+1u);
        extent_.store(extent);rectangle_.store(rectangle);
        version_.store(previous+2u);
        return true;
    }
    [[nodiscard]] std::optional<PresentationContentSnapshot> read() const noexcept {
        for (unsigned attempt=0;attempt<3u;++attempt) {
            const auto before=version_.load();
            if (!before || (before&1u)) continue;
            const auto extent=extent_.load(),rect=rectangle_.load();
            if (before!=version_.load()) continue;
            return PresentationContentSnapshot{
                {static_cast<int>(rect>>48u),static_cast<int>((rect>>32u)&65535u),
                 static_cast<int>((rect>>16u)&65535u),static_cast<int>(rect&65535u)},
                static_cast<int>(extent>>32u),static_cast<int>(extent&0xffffffffu),before/2u};
        }
        return {};
    }
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
inline PresentationContentPublication g_presentation_content;

// Center-fit an aspect ratio into a client/surface in physical pixels. The
// result uses half-open bounds [left,left+width) x [top,top+height), matching
// both D3D scissor rectangles and host pointer hit testing.
[[nodiscard]] inline ContentViewport fit_content_viewport(
    int surface_width,
    int surface_height,
    double content_aspect = kWiiContentAspect) noexcept {
    if (surface_width <= 1 || surface_height <= 1 ||
        !std::isfinite(content_aspect) || content_aspect <= 0.0) {
        return {};
    }

    ContentViewport viewport{0, 0, surface_width, surface_height};
    const double surface_aspect =
        static_cast<double>(surface_width) /
        static_cast<double>(surface_height);
    if (surface_aspect > content_aspect) {
        const double fitted_width = std::clamp(
            static_cast<double>(surface_height) * content_aspect,
            2.0,
            static_cast<double>(surface_width));
        viewport.width = static_cast<int>(std::lround(fitted_width));
        viewport.left = (surface_width - viewport.width) / 2;
    } else if (surface_aspect < content_aspect) {
        const double fitted_height = std::clamp(
            static_cast<double>(surface_width) / content_aspect,
            2.0,
            static_cast<double>(surface_height));
        viewport.height = static_cast<int>(std::lround(fitted_height));
        viewport.top = (surface_height - viewport.height) / 2;
    }
    return viewport;
}

}  // namespace galaxy
