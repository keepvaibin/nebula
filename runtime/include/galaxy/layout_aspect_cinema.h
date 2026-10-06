#pragma once

#include "galaxy/layout_aspect.h"

namespace galaxy {

template <typename Access>
inline void fit_rmge01_cinema_vertical(const Access& access, std::uint32_t root,
                                      const LayoutAspectExtent& extent) {
    if (extent.vertical_margin_delta == 0.0f) return;
    access.children(root, [&](std::uint32_t owner) {
        if (!access.named(owner, "CinemaFrame")) return;
        access.children(owner, [&](std::uint32_t bar) {
            const bool upper = access.named(bar, "FrameU");
            if (!upper && !access.named(bar, "FrameD")) return;
            // RMGE01 Wait/Appear/End author a 60-unit bar at y +/-245..260;
            // Close/Open animate height 60..250 to meet at screen center.
            // Anchor the bar to the expanded outer edge and, while closing,
            // add the matching fraction of extra half-screen coverage so
            // Blank cannot open a center gap. Size is used only by
            // Picture::DrawSelf; there is no hit region.
            const float height = access.f32(bar + 0x50u);
            if (!std::isfinite(height) || height <= 0.0f)
                throw std::runtime_error("RMGE01 cinema bar height is invalid");
            const float closing = std::clamp((height - 60.0f) / 190.0f, 0.0f, 1.0f);
            access.f32(bar + 0x50u, height + closing * extent.vertical_margin_delta);
            unsigned budget = 128u;
            translate_rmge01_hud_branch(access, bar, 0.0f,
                upper ? extent.vertical_margin_delta : -extent.vertical_margin_delta, budget);
        });
    });
}

inline void apply_rmge01_cinema_vertical(PpcContext* context, GuestMemoryV1* memory,
                                       const NativeServicesV1* services) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || policy->content_aspect == 4.0 / 3.0 ||
        policy->content_aspect >= kWiiContentAspect) return;
    const Rmge01PaneAccess access{memory, services, 0x80367E64u};
    const auto manager = context->gpr[31];
    if (guest_load_u8(memory, manager + 0x60u, services, access.pc) != 0u) return;
    const auto layout = access.u32(manager + 4u);
    const auto root = access.u32(layout + 0x10u);
    if (root != 0u && access.named(root, "RootPane"))
        fit_rmge01_cinema_vertical(access, root, layout_aspect_extent(*policy));
}

} // namespace galaxy
