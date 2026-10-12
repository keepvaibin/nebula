#include "galaxy/site_hooks.h"

#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/layout_aspect.h"
#include "galaxy/layout_aspect_cinema.h"
#include "galaxy/layout_aspect_home.h"

#include <atomic>
#include <bit>
#include <stdexcept>
#include <string>

namespace galaxy {
namespace {

std::atomic<float> g_lod_inverse_scale{1.0f};

}  // namespace

void set_site_hook_lod_distance_scale(float scale) noexcept {
    g_lod_inverse_scale.store(scale > 0.0f ? 1.0f / scale : 1.0f, std::memory_order_relaxed);
}

void dispatch_site_hook(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t kind,
    std::uint32_t argument,
    PpcContext* context,
    GuestMemoryV1* memory) {
    switch (static_cast<SiteHookKind>(kind)) {
    case SiteHookKind::CinemaAndHud:
        apply_rmge01_cinema_vertical(context, memory, services);
        apply_rmge01_hud_anchors(context, memory, services);
        return;
    case SiteHookKind::LayoutBackings:
        apply_rmge01_layout_backings(context, memory, services);
        return;
    case SiteHookKind::HomeBackings:
        apply_rmge01_home_backings(context, memory, services, guest_pc);
        return;
    case SiteHookKind::HomeVertical:
        apply_rmge01_home_vertical(context, memory, services, guest_pc);
        return;
    case SiteHookKind::NativeScreenWidthEnter:
        enter_experimental_rmge01_native_screen_width();
        return;
    case SiteHookKind::NativeScreenWidthExit:
        exit_experimental_rmge01_native_screen_width();
        return;
    case SiteHookKind::FourThreeAspectRead:
        apply_experimental_rmge01_native_four_three_aspect_read(context, memory, services);
        return;
    case SiteHookKind::LayoutConstant:
        apply_experimental_rmge01_layout_constant(
            context, services, argument & 0xFFu, ((argument >> 8u) & 0xFFu) != 0u);
        return;
    case SiteHookKind::Nw4rCanvasWidth:
        apply_experimental_rmge01_nw4r_canvas_width(context, services);
        return;
    case SiteHookKind::CameraAspect:
        apply_experimental_rmge01_camera_aspect(context, services);
        return;
    case SiteHookKind::UltrawideScreenWidth:
        apply_experimental_rmge01_ultrawide_screen_width(context, services);
        return;
    case SiteHookKind::EfbScreenWidth:
        apply_experimental_rmge01_efb_screen_width(context, services);
        return;
    case SiteHookKind::LayoutVerticalFit:
        apply_experimental_rmge01_layout_vertical_fit(
            context, services, argument & 0xFFu, ((argument >> 8u) & 0xFFu) != 0u);
        return;
    case SiteHookKind::HomeGeometry:
        apply_experimental_rmge01_home_geometry(
            context, services, argument & 0xFFu, (argument >> 8u) & 0xFFu);
        return;
    case SiteHookKind::LodDistance: {
        const float inverse = g_lod_inverse_scale.load(std::memory_order_relaxed);
        if (inverse != 1.0f) {
            // LodCtrl::update compares this single-precision distance with
            // the LodCtrl's middle and low distances.
            const double distance = std::bit_cast<double>(context->fpr_bits[1]);
            context->fpr_bits[1] = std::bit_cast<std::uint64_t>(
                static_cast<double>(static_cast<float>(distance * inverse)));
        }
        return;
    }
    }
    throw std::runtime_error("unknown site hook kind " + std::to_string(kind));
}

}  // namespace galaxy
