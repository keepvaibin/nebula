#pragma once

#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/native_api.h"

#include <array>
#include <cstring>

namespace galaxy {

// RMGE01 LayoutManager::calcAnimWithoutLocationAdjust, after animateRecursive
// and immediately before Layout::CalculateMtx. The resource-owned dimensions
// are absolute baselines; animation still owns translation, alpha and scale.
// This candidate remains opt-in until transition and interaction checks pass.
inline bool layout_aspect_repairs_enabled() {
    static const bool enabled = [] {
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "GALAXY_LAYOUT_ASPECT_REPAIRS") != 0)
            throw std::runtime_error("cannot read layout aspect setting");
        const std::unique_ptr<char, decltype(&std::free)> owner(value, &std::free);
        const std::string_view setting = owner ? owner.get() : "";
        if (setting.empty() || setting == "0") return false;
        if (setting != "1") throw std::invalid_argument("GALAXY_LAYOUT_ASPECT_REPAIRS requires 0 or 1");
        return true;
    }();
    return enabled;
}

struct LayoutAspectExtent {
    float width = 608.0f;
    float height = 456.0f;
    float horizontal_margin_delta = 0.0f;
    float vertical_margin_delta = 0.0f;
};

inline LayoutAspectExtent layout_aspect_extent(const ExperimentalUltrawideAspect& policy) noexcept {
    const float width = experimental_nw4r_canvas_width(policy);
    const float height = 456.0f * experimental_nw4r_vertical_fit_scale(policy);
    return {width, height, (width - 608.0f) * 0.5f, (height - 456.0f) * 0.5f};
}

// Documented NW4R ABI offsets are verified against RMGE01's Pane constructor
// and LayoutManager's root load. No resource file or authored animation changes.
class Rmge01PaneAccess {
public:
    GuestMemoryV1* memory;
    const NativeServicesV1* services;
    std::uint32_t pc;
    std::uint32_t name_offset = 0xB8u;
    std::uint32_t u32(std::uint32_t address) const {
        return guest_load_u32(memory, address, services, pc);
    }
    float f32(std::uint32_t address) const { return std::bit_cast<float>(u32(address)); }
    void f32(std::uint32_t address, float value) const {
        guest_store_u32(memory, address, std::bit_cast<std::uint32_t>(value), services, pc);
    }
    bool named(std::uint32_t pane, std::string_view wanted) const {
        if (wanted.size() > 16u) return false;
        for (std::size_t i = 0; i <= wanted.size(); ++i) {
            const auto byte = guest_load_u8(memory, pane + name_offset + static_cast<std::uint32_t>(i), services, pc);
            if (byte != (i == wanted.size() ? 0u : static_cast<unsigned char>(wanted[i]))) return false;
        }
        return true;
    }
    template <typename Visitor> void children(std::uint32_t pane, Visitor visit) const {
        const auto count = u32(pane + 0x10u);
        if (count > 128u) throw std::runtime_error("RMGE01 layout child count exceeds bounded policy");
        const auto end = pane + 0x14u;
        auto node = u32(end);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (node == end || node < 4u) throw std::runtime_error("RMGE01 layout child list is incomplete");
            const auto next = u32(node);
            visit(node - 4u);
            node = next;
        }
        if (node != end) throw std::runtime_error("RMGE01 layout child list is not conserved");
    }
};

inline float safety_surround_intensity(std::uint8_t backing_alpha, float black_fade) {
    if (!std::isfinite(black_fade) || black_fade < 0.0f || black_fade > 1.0f)
        throw std::runtime_error("RMGE01 safety fade is outside its verified range");
    // LogoFader::draw converts rate*255 to a byte before SRCALPHA blending.
    const auto fade_alpha = static_cast<unsigned>(black_fade * 255.0f);
    return (static_cast<float>(backing_alpha) / 255.0f) *
           (static_cast<float>(255u - fade_alpha) / 255.0f);
}

inline float rmge01_safety_surround(PpcContext* context, GuestMemoryV1* memory,
                                   const NativeServicesV1* services) {
    // US LogoScene omits ISBN: constructor/initLayout verified strap+14/fader+18.
    // LayoutActor's live/hidden flags and PicBG's calculated alpha own coverage.
    const Rmge01PaneAccess access{memory, services, 0x803404B0u};
    const auto strap = access.u32(context->gpr[3] + 0x14u);
    const auto fader = access.u32(context->gpr[3] + 0x18u);
    if (strap == 0u || fader == 0u ||
        guest_load_u8(memory, strap + 0x1Cu, services, access.pc) != 0u ||
        guest_load_u8(memory, strap + 0x1Eu, services, access.pc) != 0u) return 0.0f;
    const auto manager = access.u32(strap + 0xCu);
    const auto layout = access.u32(manager + 4u);
    const auto root = access.u32(layout + 0x10u);
    if (root == 0u || !access.named(root, "RootPane")) return 0.0f;
    bool identified = false;
    std::uint32_t backing = 0u;
    access.children(root, [&](std::uint32_t pane) {
        identified |= access.named(pane, "WiiRemoteStrap");
        if (access.named(pane, "PicBG")) backing = pane;
    });
    if (!identified || backing == 0u) return 0.0f;
    const auto alpha = guest_load_u8(memory, backing + 0xB5u, services, access.pc);
    const bool fade_visible = guest_load_u8(memory, fader + 0x1Cu, services, access.pc) == 0u &&
                              guest_load_u8(memory, fader + 0x1Eu, services, access.pc) == 0u;
    return safety_surround_intensity(alpha, fade_visible ? access.f32(fader + 0x28u) : 0.0f);
}

template <typename Access>
inline void extend_rmge01_layout_backings(const Access& access, std::uint32_t root,
                                        const LayoutAspectExtent& extent) {
    // Identification includes owning layout and exact pane names. In particular,
    // generic Base/PicBG panes in other effects and menus are never enlarged.
    bool safety = false;
    bool title = false;
    access.children(root, [&](std::uint32_t pane) {
        safety |= access.named(pane, "WiiRemoteStrap");
        title |= access.named(pane, "SMGTitleLogo");
    });
    access.children(root, [&](std::uint32_t pane) {
        if ((safety && access.named(pane, "PicBG")) ||
            (title && access.named(pane, "PicFlash"))) {
            access.f32(pane + 0x4Cu, extent.width);
            access.f32(pane + 0x50u, extent.height);
        } else if (access.named(pane, "CinemaFrame")) {
            access.children(pane, [&](std::uint32_t bar) {
                if (access.named(bar, "FrameU") || access.named(bar, "FrameD"))
                    access.f32(bar + 0x4Cu, extent.width);
            });
        } else if (access.named(pane, "PrologueDemo")) {
            access.children(pane, [&](std::uint32_t backing) {
                if (access.named(backing, "Base") || access.named(backing, "Fade")) {
                    access.f32(backing + 0x4Cu, extent.width);
                    access.f32(backing + 0x50u, extent.height);
                }
            });
        }
    });
}

inline void apply_rmge01_layout_backings(PpcContext* context, GuestMemoryV1* memory,
                                       const NativeServicesV1* services) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || policy->content_aspect == 4.0 / 3.0) return;
    const Rmge01PaneAccess access{memory, services, 0x80367D78u};
    const auto layout = access.u32(context->gpr[31] + 4u);
    const auto root = access.u32(layout + 0x10u);
    if (root != 0u && access.named(root, "RootPane"))
        extend_rmge01_layout_backings(access, root, layout_aspect_extent(*policy));
}

template <typename Access>
inline bool extend_rmge01_home_backings(const Access& access, std::uint32_t root,
                                       const LayoutAspectExtent& extent) {
    // Only th_HomeBtn_b's authored surround/bar set qualifies. Pointer layouts
    // P1_Def..P4_Def and the menu buttons themselves retain their transforms.
    bool backing = false, upper = false, lower = false;
    access.children(root, [&](std::uint32_t pane) {
        backing |= access.named(pane, "back_00");
        upper |= access.named(pane, "bar_00");
        lower |= access.named(pane, "bar_10");
    });
    if (!backing || !upper || !lower) return false;
    access.children(root, [&](std::uint32_t pane) {
        if (access.named(pane, "back_00") || access.named(pane, "back_01") ||
            access.named(pane, "back_02")) {
            access.f32(pane + 0x4Cu, extent.width);
            access.f32(pane + 0x50u, extent.height);
        } else if (access.named(pane, "bar_00") || access.named(pane, "bar_10")) {
            access.f32(pane + 0x4Cu, std::max(900.0f, extent.width));
            access.children(pane, [&](std::uint32_t child) {
                if (access.named(child,"bar_line_00") || access.named(child,"bar_line_10") ||
                    access.named(child,"B_btn_00") || access.named(child,"B_bar_10"))
                    access.f32(child + 0x4Cu, std::max(access.named(child,"B_bar_10") ? 612.0f : 608.0f,
                                                       extent.width));
            });
        }
    });
    return true;
}

inline void apply_rmge01_home_backings(PpcContext* context, GuestMemoryV1* memory,
                                     const NativeServicesV1* services, std::uint32_t pc) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    // NW4HBM's own Pane constructor/CalculateMtx verified: dimensions/list match
    // the game NW4R ABI, but name is B4 and alpha/flags are CD..CF, not B4..B7.
    const Rmge01PaneAccess access{memory, services, pc, 0xB4u};
    const auto root = context->gpr[3];
    if (root == 0u || !access.named(root,"RootPane")) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || policy->content_aspect == 4.0/3.0) return;
    (void)extend_rmge01_home_backings(access, root, layout_aspect_extent(*policy));
}

template <typename Access>
inline void translate_rmge01_hud_branch(const Access& access, std::uint32_t pane,
                                       float x, float y, unsigned& budget, unsigned depth = 0u) {
    if (budget == 0u || depth > 16u) throw std::runtime_error("RMGE01 HUD branch exceeds bounded policy");
    --budget;
    // mGlbMtx is rebuilt by CalculateMtx/follow-position controllers each frame.
    // Adjust the whole branch and refresh copied references. Do not change authored
    // local transforms consumed by animation/follow-position controllers.
    access.f32(pane + 0x90u, access.f32(pane + 0x90u) + x);
    access.f32(pane + 0xA0u, access.f32(pane + 0xA0u) + y);
    access.children(pane, [&](std::uint32_t child) {
        translate_rmge01_hud_branch(access, child, x, y, budget, depth + 1u);
    });
}

template <typename Access>
inline bool anchor_rmge01_hud(const Access& access, std::uint32_t root,
                             const LayoutAspectExtent& extent) {
    if (extent.horizontal_margin_delta == 0.0f && extent.vertical_margin_delta == 0.0f) return false;
    bool changed = false;
    access.children(root, [&](std::uint32_t pane) {
        float x = 0.0f, y = 0.0f;
        if (access.named(pane, "StarCounter")) {
            x = -extent.horizontal_margin_delta; y = extent.vertical_margin_delta;
        } else if (access.named(pane, "PlayerLeft")) {
            x = -extent.horizontal_margin_delta; y = -extent.vertical_margin_delta;
        } else if (access.named(pane, "CoinCounter") || access.named(pane, "StarPieceCounter")) {
            x = extent.horizontal_margin_delta; y = -extent.vertical_margin_delta;
        } else if (access.named(pane, "MoveMeter")) {
            bool life = false;
            access.children(pane, [&](std::uint32_t child) { life |= access.named(child, "HPMeter"); });
            if (life) { x = extent.horizontal_margin_delta; y = extent.vertical_margin_delta; }
        }
        if (x != 0.0f || y != 0.0f) {
            unsigned budget = 128u;
            translate_rmge01_hud_branch(access, pane, x, y, budget);
            changed = true;
        }
    });
    return changed;
}

template <typename Access>
inline void copy_rmge01_pane_reference(const Access& access, std::uint32_t pane,
                                      std::uint32_t reference, float wide_factor) {
    for (unsigned i = 0; i < 12u; ++i)
        access.f32(reference + i * 4u, access.f32(pane + 0x84u + i * 4u));
    access.f32(reference + 0xCu, access.f32(reference + 0xCu) * wide_factor);
}

inline std::uint32_t find_rmge01_reference_pane(const Rmge01PaneAccess& access,
        std::uint32_t root, std::string_view name, unsigned& budget) {
    if (budget == 0u) throw std::runtime_error("RMGE01 HUD reference search exceeds bounded policy");
    --budget;
    if (access.named(root, name)) return root;
    std::uint32_t result = 0u;
    access.children(root, [&](std::uint32_t child) {
        if (result == 0u) result = find_rmge01_reference_pane(access, child, name, budget);
    });
    return result;
}

inline void apply_rmge01_hud_anchors(PpcContext* context, GuestMemoryV1* memory,
                                   const NativeServicesV1* services) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy || policy->content_aspect == 4.0 / 3.0) return;
    const Rmge01PaneAccess access{memory, services, 0x80367E64u};
    const auto manager = context->gpr[31];
    // Hidden layouts skipped CalculateMtx; never repeatedly offset stale data.
    if (guest_load_u8(memory, manager + 0x60u, services, access.pc) != 0u) return;
    const auto layout = access.u32(context->gpr[31] + 4u);
    const auto root = access.u32(layout + 0x10u);
    if (root == 0u || !access.named(root, "RootPane") ||
        !anchor_rmge01_hud(access, root, layout_aspect_extent(*policy))) return;
    // Follow-position controllers can recalculate branch matrices after the
    // initial CalculateMtx. Apply offsets after all of them, then refresh the
    // same registered references with their original screen-space conversion.
    const auto count = access.u32(manager + 0x68u);
    const auto table = access.u32(manager + 0x6Cu);
    if (count > 128u) throw std::runtime_error("RMGE01 HUD reference count exceeds bounded policy");
    const float wide_factor = static_cast<float>(policy->guest_screen_width) / 608.0f;
    for (unsigned i = 0; i < count; ++i) {
        const auto slot = table + i * 0x18u;
        const auto reference = access.u32(slot + 0xCu);
        if (reference == 0u) continue;
        const auto controller = access.u32(slot + 4u);
        auto pane = controller != 0u ? access.u32(controller + 4u) : 0u;
        if (pane == 0u) {
            const auto name_pointer = access.u32(slot);
            if (name_pointer == 0u) pane = root;
            else {
                std::array<char,17> name{};
                for (unsigned c = 0; c < name.size(); ++c) {
                    name[c] = static_cast<char>(guest_load_u8(memory, name_pointer + c, services, access.pc));
                    if (name[c] == '\0') break;
                }
                if (name.back() != '\0') throw std::runtime_error("RMGE01 HUD reference pane name is invalid");
                unsigned budget = 256u;
                pane = find_rmge01_reference_pane(access, root, name.data(), budget);
            }
        }
        if (pane == 0u) throw std::runtime_error("RMGE01 HUD matrix reference has no source pane");
        copy_rmge01_pane_reference(access, pane, reference, wide_factor);
    }
}

} // namespace galaxy
