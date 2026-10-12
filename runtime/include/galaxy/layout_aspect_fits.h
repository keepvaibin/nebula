#pragma once

#include "galaxy/layout_aspect.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace galaxy {

// How a pane follows a canvas wider or taller than the authored 608x456.
enum LayoutFit : std::uint16_t {
    kFitLeft = 1u << 0,    // move the branch with the left edge
    kFitRight = 1u << 1,
    kFitTop = 1u << 2,
    kFitBottom = 1u << 3,
    kFitWidth = 1u << 4,   // scale the picture to the canvas width
    kFitHeight = 1u << 5,  // scale the picture to the canvas height
    kFitCover = 1u << 6,   // scale artwork uniformly until it covers the canvas
    kFitFrame = 1u << 7,   // grow a window so its border keeps its edge distance
    kFitExtendUp = 1u << 8,    // grow a bar upward to the canvas top
    kFitExtendDown = 1u << 9,  // grow a bar downward to the canvas bottom
    kFitSlide = 1u << 10,      // slide proportionally further; width = 16:9 off-screen x
};

struct LayoutFitRule {
    std::string_view layout;  // LayoutManager name
    std::string_view pane;
    std::uint16_t fit;
    float width = 0.0f;       // kFitFrame: authored window size; kFitSlide: off-screen x
    float height = 0.0f;
};

// From the authored pane trees: location-adjusted panes in the outer part of
// the canvas follow their edge, unadjusted full-canvas fills span it, and
// menus keep their centered composition. Layouts the game positions every
// frame (balloons, map icons, pointers) are absent.
inline constexpr LayoutFitRule kRmge01LayoutFits[] = {
    {"AllStarList", "PicBG", kFitWidth | kFitHeight},
    {"AllStarList", "PicBelt1", kFitWidth},
    {"AllStarList", "PicBelt2", kFitWidth},
    {"AllStarList", "PicBelt3", kFitWidth},
    {"AllStarList", "PicBelt4Base", kFitWidth},
    {"AllStarList", "PicTitleBase", kFitWidth | kFitExtendUp},
    {"AllStarList", "PicButtonbase", kFitWidth | kFitExtendDown},
    {"BackButton", "Back", kFitLeft | kFitBottom},
    {"BallGuidanceReplace", "BallGuidance", kFitRight},
    {"BatteryInfo", "BatteryInfo", kFitLeft},
    {"BrosButton", "BrosButton", kFitRight},
    {"CameraInfo", "CameraInfo", kFitRight},
    {"CameraInfo", "Cross", kFitRight},
    {"CoinCounter", "CoinCounter", kFitRight | kFitBottom},
    {"CometScreenFilter", "PicRedComet", kFitWidth | kFitHeight},
    {"CometScreenFilter", "PicBlueComet", kFitWidth | kFitHeight},
    {"CometScreenFilter", "PicYellowComet", kFitWidth | kFitHeight},
    {"CometScreenFilter", "PicPurpleComet", kFitWidth | kFitHeight},
    {"CometScreenFilter", "PicWhiteComet", kFitWidth | kFitHeight},
    {"FileSelect", "StartButton", kFitRight | kFitBottom},
    {"FileSelect", "P2ManualButton", kFitRight},
    {"GalaxyDetail", "BG", kFitWidth | kFitHeight},
    {"GalaxyInfo", "InfoWindowUp", kFitRight | kFitTop},
    {"GalaxySelect", "Star", kFitLeft | kFitTop},
    {"GalaxySelect", "StartButton", kFitRight | kFitBottom},
    {"GameOver", "Color", kFitWidth | kFitHeight},
    {"HitPointMeter", "MoveMeter", kFitRight | kFitTop},
    {"LuigiLetter", "BG", kFitWidth | kFitHeight},
    {"MapButton", "MapList", kFitRight | kFitBottom},
    {"MapGalaxyBg", "PicSmoke", kFitCover},
    {"MapGalaxyBg", "PicStar", kFitCover},
    {"MapGalaxyBg", "PicGrid", kFitCover},
    {"MapGrandGalaxy", "Counter", kFitLeft | kFitTop},
    {"MapTitle", "PicTitle", kFitWidth | kFitExtendUp},
    {"MapTitle", "TextTitle", kFitLeft | kFitTop},
    {"MapTitle", "Guidance", kFitRight | kFitTop},
    {"MiiSelect", "BG", kFitWidth | kFitHeight},
    {"MiiSelect", "MiiWindow", kFitSlide, 660.0f},
    {"MiiSelect", "Text", kFitSlide, 660.0f},
    {"MiiSelect", "MiiPosition", kFitSlide, 660.0f},
    {"MiiSelect", "Arrow", kFitSlide, 660.0f},
    {"MiiSelect", "MiiGroupA", kFitSlide, 608.0f},
    {"MiiSelect", "MiiGroupB", kFitSlide, 608.0f},
    {"P2Manual", "BG", kFitWidth | kFitHeight},
    {"PauseMenu", "BG", kFitWidth | kFitHeight},
    {"PauseMenu", "PicStagebase", kFitWidth | kFitExtendUp},
    {"PauseMenu", "PicStagebase1", kFitWidth | kFitExtendDown},
    {"PeachLetter", "BG", kFitWidth | kFitHeight},
    {"PictureBook", "Base", kFitWidth | kFitHeight},
    {"PictureBook", "PicToneDown", kFitWidth | kFitHeight},
    {"PlayerLeft", "PlayerLeft", kFitLeft | kFitBottom},
    {"PointerGuidance", "WinPointFrame1", kFitFrame, 720.0f, 378.0f},
    {"PointerGuidance", "WinPointFrame2", kFitFrame, 720.0f, 365.0f},
    {"PrologueStarSteward", "FrameUp", kFitWidth | kFitExtendUp},
    {"PrologueStarSteward", "FrameDown", kFitWidth | kFitExtendDown},
    {"PrologueStarSteward", "FadeOut", kFitWidth | kFitHeight},
    {"PurpleCoinCounter", "PCoinCounter", kFitLeft | kFitBottom},
    {"RetryButton", "BG", kFitWidth | kFitHeight},
    {"ScenarioSelect", "BG", kFitWidth | kFitHeight},
    {"ScenarioSelect", "FrameU", kFitWidth | kFitExtendUp},
    {"ScenarioSelect", "FrameD", kFitWidth | kFitExtendDown},
    {"SpinGuidanceReplace", "SpinGuidance", kFitRight},
    {"StaffRollPicture", "PicBG", kFitWidth | kFitHeight},
    {"StaffRollTitle", "PicFlash", kFitWidth | kFitHeight},
    {"StarCounter", "StarCounter", kFitLeft | kFitTop},
    {"StarPieceCounter", "StarPieceCounter", kFitRight | kFitBottom},
    {"StarPieceCounter", "TSPiece", kFitBottom},
    {"StarPieceTargetCounter", "SPTargetCounter", kFitRight},
    {"SuddenDeathMeter", "MoveMeter", kFitRight | kFitTop},
    {"SysInfoWindow", "BG", kFitWidth | kFitHeight},
    {"SysInfoWindowMini", "BG", kFitWidth | kFitHeight},
    {"TiltGuidanceReplace", "TiltGuidance", kFitRight},
};

inline constexpr std::size_t kMaxLayoutFitRules = 8u;

consteval bool layout_fits_are_well_formed() {
    std::size_t run = 0u;
    for (std::size_t i = 0; i < std::size(kRmge01LayoutFits); ++i) {
        const auto& rule = kRmge01LayoutFits[i];
        if (rule.pane.size() > 16u || rule.layout.size() > 31u) return false;
        if (i > 0u && rule.layout < kRmge01LayoutFits[i - 1u].layout) return false;
        run = i > 0u && rule.layout == kRmge01LayoutFits[i - 1u].layout ? run + 1u : 1u;
        if (run > kMaxLayoutFitRules) return false;
    }
    return true;
}
static_assert(layout_fits_are_well_formed());

inline std::span<const LayoutFitRule> rmge01_layout_fits(std::string_view layout) noexcept {
    const auto* const end = std::end(kRmge01LayoutFits);
    const auto* const first = std::lower_bound(std::begin(kRmge01LayoutFits), end, layout,
        [](const LayoutFitRule& rule, std::string_view name) { return rule.layout < name; });
    const auto* last = first;
    while (last != end && last->layout == layout) ++last;
    return {first, last};
}

template <typename Access>
inline void scale_rmge01_pane_row(const Access& access, std::uint32_t pane, std::uint32_t row, float k) {
    // CalculateMtx rebuilt mGlbMtx this frame. Scaling one row scales the drawn
    // pane about the canvas center; descendants were already calculated.
    for (std::uint32_t i = 0; i < 4u; ++i) {
        const auto address = pane + 0x84u + row * 0x10u + i * 4u;
        access.f32(address, access.f32(address) * k);
    }
}

// Grows a bar about its inner edge until its outer edge has moved `delta`
// further out. Picture vertices span 0..-h, h/2..-h/2 or h..0 by base row.
template <typename Access>
inline void extend_rmge01_pane_vertical(const Access& access, std::uint32_t pane, float delta, bool up) {
    const float height = access.f32(pane + 0x50u);
    const unsigned row = access.u8(pane + 0xB6u) / 3u;
    const float top = row == 0u ? 0.0f : row == 1u ? height * 0.5f : height;
    const float m11 = access.f32(pane + 0x98u);
    const float m13 = access.f32(pane + 0xA0u);
    const float a = m11 * top + m13;
    const float b = m11 * (top - height) + m13;
    const float span = std::abs(a - b);
    if (!(span > 0.0f) || delta == 0.0f) return;
    const float inner = up ? std::min(a, b) : std::max(a, b);
    const float k = (span + delta) / span;
    for (std::uint32_t i = 0; i < 3u; ++i) {
        const auto address = pane + 0x94u + i * 4u;
        access.f32(address, access.f32(address) * k);
    }
    access.f32(pane + 0xA0u, inner + (m13 - inner) * k);
}

// Returns true when the branch moved, so copied matrix references need refreshing.
template <typename Access>
inline bool apply_rmge01_layout_fit(const Access& access, std::uint32_t pane,
                                   const LayoutFitRule& rule, const LayoutAspectExtent& extent) {
    const float wide = extent.width / 608.0f;
    const float tall = extent.height / 456.0f;
    if (rule.fit & kFitCover) {
        const float k = std::max(wide, tall);
        scale_rmge01_pane_row(access, pane, 0u, k);
        scale_rmge01_pane_row(access, pane, 1u, k);
    }
    if (rule.fit & kFitWidth) scale_rmge01_pane_row(access, pane, 0u, wide);
    if (rule.fit & kFitHeight) scale_rmge01_pane_row(access, pane, 1u, tall);
    if (rule.fit & (kFitExtendUp | kFitExtendDown))
        extend_rmge01_pane_vertical(access, pane, extent.vertical_margin_delta, (rule.fit & kFitExtendUp) != 0u);
    if (rule.fit & kFitFrame) {
        // Window::DrawSelf reads the size when drawing and CalculateMtx never
        // restores it, so it is always derived from the authored size.
        const float sx = std::abs(access.f32(pane + 0x84u));
        const float sy = std::abs(access.f32(pane + 0x98u));
        if (sx > 0.0f && sy > 0.0f) {
            access.f32(pane + 0x4Cu, rule.width + 2.0f * extent.horizontal_margin_delta / sx);
            access.f32(pane + 0x50u, rule.height + 2.0f * extent.vertical_margin_delta / sy);
        }
    }
    float x = (rule.fit & kFitLeft) ? -extent.horizontal_margin_delta
            : (rule.fit & kFitRight) ? extent.horizontal_margin_delta : 0.0f;
    // A pane the game slides off the 16:9 canvas (local x reaching rule.width)
    // must also leave the wider canvas: scale its slide by the extra width.
    if (rule.fit & kFitSlide) x += access.f32(pane + 0x2Cu) * extent.horizontal_margin_delta / rule.width;
    const float y = (rule.fit & kFitTop) ? extent.vertical_margin_delta
                  : (rule.fit & kFitBottom) ? -extent.vertical_margin_delta : 0.0f;
    if (x == 0.0f && y == 0.0f) return false;
    unsigned budget = 128u;
    translate_rmge01_hud_branch(access, pane, x, y, budget);
    return true;
}

template <typename Access>
inline std::uint32_t find_rmge01_layout_pane(const Access& access, std::uint32_t pane,
                                            std::string_view name, unsigned& budget) {
    if (budget == 0u) return 0u;
    --budget;
    if (access.named(pane, name)) return pane;
    std::uint32_t found = 0u;
    access.children(pane, [&](std::uint32_t child) {
        if (found == 0u) found = find_rmge01_layout_pane(access, child, name, budget);
    });
    return found;
}

// LayoutManager::calcAnimWithoutLocationAdjust after its copied matrix
// references (0x80367E64); r31 is the LayoutManager.
inline void apply_rmge01_layout_fits(PpcContext* context, GuestMemoryV1* memory,
                                    const NativeServicesV1* services) {
    if (!layout_aspect_repairs_enabled() || context->gpr[2] != 0x806AB280u) return;
    const Rmge01PaneAccess access{memory, services, 0x80367E64u};
    const auto manager = context->gpr[31];
    // Hidden layouts skipped CalculateMtx; never offset their stale matrices.
    if (guest_load_u8(memory, manager + 0x60u, services, access.pc) != 0u) return;

    // A manager keeps its name pointer for life; the first name word guards a
    // recycled manager address.
    struct Slot {
        std::uint32_t manager = 0u, name = 0u, word = 0u, root = 0u;
        std::span<const LayoutFitRule> rules;
        std::array<std::uint32_t, kMaxLayoutFitRules> panes{};
    };
    static std::array<Slot, 256> slots;
    const auto name = access.u32(manager + 0x78u);
    if (name == 0u) return;
    const auto word = access.u32(name);
    Slot& slot = slots[((manager >> 3u) * 0x9E3779B1u) >> 24u];
    if (slot.manager != manager || slot.name != name || slot.word != word) {
        std::array<char, 32> text{};
        std::size_t length = 0u;
        while (length + 1u < text.size()) {
            const auto byte = guest_load_u8(memory, name + static_cast<std::uint32_t>(length),
                                            services, access.pc);
            if (byte == 0u) break;
            text[length++] = static_cast<char>(byte);
        }
        slot = Slot{manager, name, word, 0u, rmge01_layout_fits({text.data(), length}), {}};
    }
    if (slot.rules.empty()) return;

    const auto policy = experimental_effective_guest_aspect(services);
    const LayoutAspectExtent extent =
        policy && !native_four_three(*policy) ? layout_aspect_extent(*policy) : LayoutAspectExtent{};
    const bool enlarged = extent.horizontal_margin_delta != 0.0f || extent.vertical_margin_delta != 0.0f;
    const auto layout = access.u32(manager + 4u);
    const auto root = layout != 0u ? access.u32(layout + 0x10u) : 0u;
    if (root == 0u) return;
    if (slot.root != root) {
        slot.root = root;
        slot.panes.fill(0u);
    }
    bool moved = false;
    for (std::size_t i = 0; i < slot.rules.size(); ++i) {
        const LayoutFitRule& rule = slot.rules[i];
        if (!enlarged && !(rule.fit & kFitFrame)) continue;
        auto& pane = slot.panes[i];
        if (pane == 1u) continue;  // not in this layout instance
        if (pane == 0u || !access.named(pane, rule.pane)) {
            unsigned budget = 512u;
            pane = find_rmge01_layout_pane(access, root, rule.pane, budget);
            if (pane == 0u) {
                pane = 1u;
                continue;
            }
        }
        if (!access.drawn(pane, root)) continue;
        moved |= apply_rmge01_layout_fit(access, pane, rule, extent);
    }
    if (moved && policy)
        refresh_rmge01_pane_references(access, manager, root,
            static_cast<float>(policy->guest_screen_width) / 608.0f);
}

} // namespace galaxy
