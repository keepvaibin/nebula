#pragma once
#include "galaxy/layout_aspect.h"

namespace galaxy {

template <typename Access>
inline bool offset_rmge01_home_bar_vertical(const Access& access, std::uint32_t pane,
                                           float half_height_delta) {
    const bool upper = access.named(pane, "bar_00");
    if (!upper && !access.named(pane, "bar_10")) return false;
    // CalculateMtx has just restored the global matrix. Descendants are still
    // to be calculated and inherit this offset, including Close/hit panes.
    // Local translation, authored bar height and animation remain unchanged.
    access.f32(pane + 0xA0u, access.f32(pane + 0xA0u) +
        (upper ? half_height_delta : -half_height_delta));
    return true;
}

inline void apply_rmge01_home_vertical(PpcContext* context, GuestMemoryV1* memory,
                                      const NativeServicesV1* services, std::uint32_t pc) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || policy->content_aspect == 4.0 / 3.0 ||
        policy->content_aspect >= kWiiContentAspect) return;
    const Rmge01PaneAccess access{memory, services, pc, 0xB4u};
    const auto pane = context->gpr[29];
    if (!access.named(pane, "bar_00") && !access.named(pane, "bar_10")) return;
    const auto root = access.u32(pane + 0xCu);
    if (root == 0u || !access.named(root, "RootPane")) return;
    bool backing = false, upper = false, lower = false;
    access.children(root, [&](std::uint32_t child) {
        backing |= access.named(child, "back_00");
        upper |= access.named(child, "bar_00");
        lower |= access.named(child, "bar_10");
    });
    if (backing && upper && lower)
        offset_rmge01_home_bar_vertical(access, pane,
            layout_aspect_extent(*policy).vertical_margin_delta);
}
} // namespace galaxy
