#include "galaxy/site_hooks.h"

#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/layout_aspect.h"
#include "galaxy/layout_aspect_cinema.h"
#include "galaxy/layout_aspect_fits.h"
#include "galaxy/layout_aspect_home.h"

#include <bit>
#include <stdexcept>
#include <string>

namespace galaxy {
namespace {

// The star-select sky (MultiSceneActor named by the DOL literal at 0x805C4411)
// is one quad at z = -1000 in model space, sized for a 16:9 view. Wider views
// scale it about its center so it still covers the screen.
void fit_rmge01_scenario_sky(PpcContext* context, GuestMemoryV1* memory,
                             const NativeServicesV1* services, std::uint32_t pc) {
    if (guest_load_u32(memory, context->gpr[31] + 4u, services, pc) != 0x805C4411u) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || native_four_three(*policy)) return;
    const float k = experimental_nw4r_canvas_width(*policy) / 608.0f;
    if (!(k > 1.0f)) return;
    const std::uint32_t model = context->gpr[3];
    const auto f32 = [&](std::uint32_t address) {
        return std::bit_cast<float>(guest_load_u32(memory, address, services, pc));
    };
    const auto store = [&](std::uint32_t address, float value) {
        guest_store_u32(memory, address, std::bit_cast<std::uint32_t>(value), services, pc);
    };
    constexpr float kCenter[3] = {0.0f, 0.0f, -1000.0f};
    float offset[3];
    for (std::uint32_t i = 0; i < 3u; ++i) {
        const float scale = f32(model + 0x18u + i * 4u);
        offset[i] = scale * (1.0f - k) * kCenter[i];
        store(model + 0x18u + i * 4u, scale * k);
    }
    // J3DModel base matrix (+0x24) is rotation and translation only.
    for (std::uint32_t row = 0; row < 3u; ++row) {
        const std::uint32_t base = model + 0x24u + row * 0x10u;
        float shift = 0.0f;
        for (std::uint32_t column = 0; column < 3u; ++column) shift += f32(base + column * 4u) * offset[column];
        store(base + 0xCu, f32(base + 0xCu) + shift);
    }
}

bool rmge01_guest_object(std::uint32_t address) noexcept {
    return ((address >= 0x80000000u && address < 0x81800000u) ||
            (address >= 0x90000000u && address < 0x94000000u)) &&
           (address & 3u) == 0u;
}

// ScenarioSelectScene builds the perspective of its own CameraContext once, at
// boot, with the aspect in force then; a later aspect never reaches it, and the
// star select drew 16:9 into every other aspect. Each frame its stars and sky
// animate, the projection's x scale is set from its y scale and the current
// aspect. GameSystem (0x806A1228) +0x24 scene controller, +0xBC
// ScenarioSelectScene, +0x30 CameraContext, projection (row-major 4x4) at +0x6C.
void fit_rmge01_scenario_camera(GuestMemoryV1* memory, const NativeServicesV1* services,
                                std::uint32_t pc) {
    const auto policy = experimental_effective_guest_aspect(services);
    if (policy && native_four_three(*policy)) return;
    const double aspect = policy ? policy->content_aspect : kWiiContentAspect;
    if (!(aspect > 0.0)) return;
    std::uint32_t object = guest_load_u32(memory, 0x806A1228u, services, pc);
    for (const std::uint32_t offset : {0x24u, 0xBCu, 0x30u}) {
        if (!rmge01_guest_object(object)) return;
        object = guest_load_u32(memory, object + offset, services, pc);
    }
    if (!rmge01_guest_object(object)) return;
    const std::uint32_t projection = object + 0x6Cu;
    const auto f32 = [&](std::uint32_t address) {
        return std::bit_cast<float>(guest_load_u32(memory, address, services, pc));
    };
    const float y_scale = f32(projection + 0x14u);
    if (!(y_scale > 0.0f) || f32(projection + 0x38u) != -1.0f) return;
    const float x_scale = static_cast<float>(y_scale / aspect);
    if (f32(projection) != x_scale) {
        guest_store_u32(memory, projection, std::bit_cast<std::uint32_t>(x_scale), services, pc);
    }
}

}  // namespace

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
        apply_rmge01_layout_fits(context, memory, services);
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
    case SiteHookKind::MultiSceneModel:
        fit_rmge01_scenario_sky(context, memory, services, guest_pc);
        fit_rmge01_scenario_camera(memory, services, guest_pc);
        return;
    case SiteHookKind::LodDistance:
        // LodCtrl::update keeps the game's distances: which model shows is
        // also game state (MR::isHiddenModel).
        return;
    }
    throw std::runtime_error("unknown site hook kind " + std::to_string(kind));
}

}  // namespace galaxy
