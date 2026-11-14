// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_graphics_page.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <utility>

namespace galaxy::ui {
using namespace galaxy::render;
unsigned lighting_mode(const RenderConfig& render) {
    return render.enhanced_lighting ? 2u : render.per_pixel_lighting ? 1u : 0u;
}

std::vector<SettingsRow> graphics_page(const RenderConfig& render, const GraphicsPageHost& host) {
    const GraphicsPageState state = host.query(render);
    std::vector<SettingsRow> rows;
    char text[96]{};
    rows.push_back(button_row("PRESET", {"PERFORMANCE"}, [](RenderConfig& draft, unsigned) { apply_performance_preset(draft); },
                              "LOWER RESOLUTION AND EVERY OPTIONAL EFFECT OFF FOR THE STEADIEST FRAME RATE"));
    rows.push_back({"RESOLUTION", {}, nullptr, {}, true});
    const std::vector<unsigned>& choices = state.upscalers;
    const bool offered = std::find(choices.begin(), choices.end(), render.upscaler) != choices.end();
    // One row sets the 3D resolution: the exact height with upscaling off, the upscaler's mode with it on
    // (render_internal_height: the renderer reads the height only with upscaling off).
    const bool upscaling = !offered || render.upscaler != 0u;
    {
        static constexpr const char* kUpscalers[] = {"OFF", "FSR 1", "FSR 3", "DLSS", "XESS", "SGSR 2", "METALFX"};
        std::string value = kUpscalers[offered ? std::min(render.upscaler, 6u) : 1u];
        if (offered && render.upscaler == 3u && !state.dlss_supported) {
            value += state.dlss_installed ? " (AFTER RESTART)" : " (DOWNLOAD)";
        }
        if (offered && render.upscaler == 4u && !state.xess_supported) {
            value += state.xess_installed ? " (AFTER RESTART)" : " (DOWNLOAD)";
        }
        rows.push_back({"UPSCALING", value,
                        [&host](RenderConfig& draft, int delta) {
                            const std::vector<unsigned> options = host.query(draft).upscalers;
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < options.size(); ++i) {
                                if (options[i] == draft.upscaler) index = i;
                            }
                            draft.upscaler = options[settings_step(static_cast<unsigned>(index),
                                                                   static_cast<unsigned>(options.size()), delta)];
                            normalize_interpolation(draft, host.query(draft).frame_rates);
                        },
                        "SGSR 2, DLSS, FSR 3 AND XESS RENDER FEWER PIXELS AND REBUILD A SHARP IMAGE"});
    }
    // Under the upscaler row, in the place its mode row takes: a change of upscaler moves no row above the one changed.
    if (!upscaling) {
        const unsigned height = normalized_internal_height(render.internal_height);
        const std::string value = height ? std::to_string(height) + "P" + (height == 2160 ? " (4K)" : "") : "MATCH DISPLAY";
        rows.push_back({"INTERNAL RESOLUTION", value,
            [](RenderConfig& draft, int delta) {
                const auto found = std::find(kInternalHeights.begin(), kInternalHeights.end(), normalized_internal_height(draft.internal_height));
                draft.internal_height = kInternalHeights[settings_step(unsigned(found - kInternalHeights.begin()), unsigned(kInternalHeights.size()), delta)];
            }, "3D PIXEL HEIGHT. WIDTH FOLLOWS ASPECT. MATCH DISPLAY FOLLOWS WINDOW SIZE AND FOLDING"});
    }
    if (state.dynamic_resolution_offered && render.upscaler == 0u) {
        rows.push_back({"DYNAMIC RESOLUTION", render.dynamic_resolution ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.dynamic_resolution = !draft.dynamic_resolution; },
                        "LOWERS THE RESOLUTION WHEN THE GRAPHICS CARD FALLS BEHIND AND RAISES IT AGAIN WHEN IT HAS ROOM"});
    }
    const unsigned render_percent = state.render_percent;
    // Ray tracing replaces MSAA, ambient occlusion and the lighting modes: their rows are hidden while it is on.
    const bool rt_offered = state.ray_tracing_offered;
    const bool rt_on = rt_offered && render.ray_tracing_enabled;
    if (upscaling) {
        // By the config's numbers (render_config.h temporal_quality: 5 ultra quality came last), shown sharpest first.
        static constexpr const char* kTemporal[] = {"NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE", "ULTRA QUALITY"};
        static constexpr const char* kSpatial[] = {"NATIVE", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE", "ULTRA QUALITY"};
        static constexpr unsigned kOrder[] = {0u, 5u, 1u, 2u, 3u, 4u};
        static constexpr unsigned kDlssOrder[] = {0u, 1u, 2u, 3u, 4u};  // DLSS has no ultra quality mode
        // MetalFX (6) is temporal only where the device has its temporal scaler; elsewhere it enlarges the picture.
        const bool temporal = render.upscaler >= 2u && (render.upscaler != 6u || state.temporal_upscaling);
        const unsigned upscaler = std::min(render.upscaler, 6u);
        unsigned quality = temporal ? std::min(render.temporal_quality, 5u) : std::min(render.spatial_quality, 5u);
        if (upscaler == 3u && quality == 5u) quality = 1u;  // as DLSS runs it
        std::snprintf(text, sizeof(text), "%s (%u%%)",
                      upscaler == 3u && quality == 0u ? "DLAA" : temporal ? kTemporal[quality] : kSpatial[quality],
                      render_percent);
        rows.push_back({upscaler == 6u ? "METALFX MODE" : upscaler == 5u ? "SGSR 2 MODE" : upscaler == 4u ? "XESS MODE" : upscaler == 3u ? "DLSS MODE" : upscaler == 2u ? "FSR 3 MODE" : "QUALITY MODE", text,
                        [&host](RenderConfig& draft, int delta) {
                            const bool dlss = draft.upscaler == 3u;
                            const unsigned* order = dlss ? kDlssOrder : kOrder;
                            const unsigned count = dlss ? 5u : 6u;
                            const bool temporal_mode = draft.upscaler >= 2u && (draft.upscaler != 6u || host.query(draft).temporal_upscaling);
                            unsigned& value = temporal_mode ? draft.temporal_quality : draft.spatial_quality;
                            const unsigned current = dlss && value == 5u ? 1u : std::min(value, 5u);
                            unsigned at = 0u;
                            for (unsigned i = 0; i < count; ++i) {
                                if (order[i] == current) at = i;
                            }
                            value = order[settings_step(at, count, delta)];
                        },
                        "HIGHER RENDERS MORE PIXELS AND LOOKS SHARPER. LOWER RUNS FASTER"});
    }
    std::snprintf(text, sizeof(text), "%u%%", std::min(render.sharpness, 100u));
    rows.push_back({"SHARPENING", text,
                    [&host](RenderConfig& draft, int delta) {
                        draft.sharpness = static_cast<unsigned>(
                            std::clamp(static_cast<int>(std::min(draft.sharpness, 100u)) + delta * 10, 0, 100));
                    },
                    "CONTRAST-ADAPTIVE SHARPENING OF THE FINAL IMAGE"});
    rows.back().slider_steps = 10u;
    rows.back().slider_at = static_cast<std::uint16_t>(std::min(render.sharpness, 100u) / 10u);
    // A temporal upscaler antialiases the scene itself: anti-aliasing and MSAA have no part in its picture (the
    // renderers skip both under it), so their rows go while it runs.
    const bool temporal = state.temporal_upscaling;
    // With ray tracing on and no temporal upscaler chosen, FSR 3 anti-aliases the scene (plan 16.3): the row says so,
    // and MSAA and SMAA have no part in the picture until ray tracing is off again.
    const bool traced_temporal = rt_on && !temporal && state.traced_temporal;
    if (traced_temporal) {
        rows.push_back({"ANTI-ALIASING", "TEMPORAL (RAY TRACING)", nullptr, "SMOOTHED OVER SEVERAL FRAMES WHILE RAY TRACING IS ON"});
    }
    if (!temporal && !traced_temporal) {
        std::string value = state.anti_aliasing_text;
        if (const std::size_t colon = value.find(": "); colon != std::string::npos) value.erase(0, colon + 2u);
        rows.push_back({"ANTI-ALIASING", value,
                        [&host](RenderConfig& draft, int delta) { draft.aa_mode = settings_step(std::min(draft.aa_mode, 3u), 4u, delta); },
                        "SMOOTHS EDGES. AUTO SKIPS IT WHEN THE IMAGE IS ALREADY SUPERSAMPLED"});
    }
    if (!temporal && !traced_temporal && (render.aa_mode == 1u || render.aa_mode == 2u)) {
        static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
        rows.push_back({"SMAA QUALITY", kQuality[std::min(render.smaa_quality, 3u)],
                        [&host](RenderConfig& draft, int delta) {
                            draft.smaa_quality = settings_step(std::min(draft.smaa_quality, 3u), 4u, delta);
                        },
                        "HIGHER FINDS MORE EDGES"});
    }
    if (!rt_on && !temporal) {
        const unsigned samples = std::bit_floor(std::clamp(render.msaa_samples, 1u, 8u));
        const std::string value = samples <= 1u ? "OFF" : std::to_string(samples) + "X";
        rows.push_back({"MSAA", value,
                        [&host](RenderConfig& draft, int delta) {
                            const unsigned index = static_cast<unsigned>(
                                std::countr_zero(std::bit_floor(std::clamp(draft.msaa_samples, 1u, 8u))));
                            draft.msaa_samples = 1u << settings_step(index, 4u, delta);
                        },
                        "SAMPLES EACH EDGE OF THE 3D SCENE SEVERAL TIMES. COSTS GPU TIME"});
    }

    if (rt_offered) {
        rows.push_back({"RAY TRACING", {}, nullptr, {}, true});
        rows.push_back({"RAY TRACING", render.ray_tracing_enabled ? "ON" : "OFF",
                        [&host](RenderConfig& draft, int) { draft.ray_tracing_enabled = !draft.ray_tracing_enabled; },
                        "TRACED SHADOWS, SKY AND BOUNCE LIGHT, GLOWS THAT LIGHT THE WORLD"});
    }
    if (rt_on) {
        // The preset sets every part (plan 19.1); a part changed by itself makes it CUSTOM. All switch live.
        static constexpr unsigned kOrder[] = {2u, 0u, 1u, kRtPresetReference};  // low, medium, high, reference
        static constexpr const char* kNames[] = {"MEDIUM", "HIGH", "LOW", "REFERENCE", "CUSTOM"};
        rows.push_back({"RAY TRACING QUALITY", kNames[std::min(render.rt_quality, kRtPresetCustom)],
                        [](RenderConfig& draft, int delta) {
                            const unsigned quality = std::min(draft.rt_quality, kRtPresetCustom);
                            const unsigned at = quality == 2u ? 0u : quality == 1u ? 2u : quality == kRtPresetReference ? 3u : 1u;
                            draft.rt_quality = kOrder[settings_step(at, 4u, delta)];
                            draft.rt_chosen = true;
                            rt_apply_preset(draft);
                        },
                        "SETS EVERY PART BELOW. REFERENCE: PATH TRACED, FOR COMPARISON"});
        static constexpr const char* kLevels[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
        const auto part_row = [&](const char* name, RtPart part, const char* help) {
            rows.push_back({name, kLevels[rt_part(render, part)],
                            [part](RenderConfig& draft, int delta) {
                                const auto at = static_cast<std::size_t>(part);
                                draft.rt_parts[at] = static_cast<std::uint8_t>(settings_step(rt_part(draft, part), 4u, delta));
                                draft.rt_quality = kRtPresetCustom;
                                draft.rt_chosen = true;
                            },
                            help});
        };
        part_row("SUN SHADOWS", RtPart::Sun, "THE SUN'S SHADOWS, TRACED. HIGH: TINTED THROUGH GLASS AND WATER");
        part_row("LOCAL LIGHTS", RtPart::Lights, "LAMPS AND GLOWING THINGS LIGHT THE WORLD AND CAST SHADOWS. HIGH: HIGHLIGHTS");
        part_row("SKY AND BOUNCE LIGHT", RtPart::Indirect, "THE SKY'S LIGHT AND LIGHT BOUNCED OFF SURFACES");
        part_row("REFLECTIONS", RtPart::Reflect, "TRACED REFLECTIONS ON METAL, GLOSSY FLOORS, GLASS AND CRYSTAL");
        part_row("GLASS AND WATER", RtPart::Transmit, "LIGHT THROUGH GLASS, CRYSTAL AND WATER. HIGH: REFRACTION");
        part_row("LIGHT IN THE AIR", RtPart::Volume, "LIGHT THE AIR SCATTERS. HIGH: SHAFTS OF LIGHT");
        rows.push_back({"LENS BLOOM", render.rt_lens_bloom ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.rt_lens_bloom = !draft.rt_lens_bloom; },
                        "LIGHT THE LENS SCATTERS AROUND EVERY BRIGHT SURFACE"});
        rows.push_back({"LANDING SHADOW", render.rt_landing_shadow ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.rt_landing_shadow = !draft.rt_landing_shadow; },
                        "A SOFT SHADOW UNDER THE PLAYER, TO JUDGE A LANDING"});
        rows.push_back({"LIGHTING STYLE", render.rt_style == 1u ? "PHYSICAL" : "PAINTED",
                        [](RenderConfig& draft, int) { draft.rt_style = draft.rt_style == 1u ? 0u : 1u; },
                        "PAINTED: THE ART'S OWN LIGHT. PHYSICAL: THE REAL SUN ON SIMPLY PAINTED SURFACES"});
    }

    rows.push_back({"FRAME RATE", {}, nullptr, {}, true});
    const Interpolation mode = interpolation_of(render);
    // A display no faster than the game's step rate offers nothing between: no row with one choice.
    if (interpolation_modes(state.frame_rates).size() > 1u || mode != Interpolation::kOff) {
        std::string value = mode == Interpolation::kOff      ? "OFF"
                            : mode == Interpolation::kNative ? "NATIVE"
                            : mode == Interpolation::kFsrGeneration ? "FSR FRAME GENERATION"
                                                             : "DLSS FRAME GENERATION";
        // AMD's frame generation reads a temporal upscaler's depth and motion: FSR 3's, XeSS's or SGSR 2's.
        const auto fsr_generation_upscaler = [](unsigned upscaler) { return upscaler == 2u || upscaler == 4u || upscaler == 5u; };
        if (mode == Interpolation::kFsrGeneration && !fsr_generation_upscaler(render.upscaler)) {
            value += " (NEEDS FSR 3, XESS OR SGSR 2)";
        }
        if (mode == Interpolation::kFrameGeneration) {
            if (!state.frame_rates.generated_supported) value += " (AFTER RESTART)";
            else if (render.upscaler != 3u) value += " (NEEDS DLSS)";
        }
        rows.push_back({"INTERPOLATION", value,
                        [&host, fsr_generation_upscaler](RenderConfig& draft, int delta) {
                            const auto modes = interpolation_modes(host.query(draft).frame_rates);
                            const Interpolation current = interpolation_of(draft);
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < modes.size(); ++i) {
                                if (modes[i] == current) index = i;
                            }
                            const Interpolation next = modes[settings_step(static_cast<unsigned>(index),
                                                                           static_cast<unsigned>(modes.size()), delta)];
                            if (next == current) return;
                            draft.frame_generation = next == Interpolation::kFrameGeneration ? 1u : next == Interpolation::kFsrGeneration ? 2u : 0u;
                            // Interpolation starts at the display's own rate.
                            draft.max_fps = next == Interpolation::kOff ? static_cast<float>(kStepRate) : 0.0f;
                            if (next == Interpolation::kFrameGeneration && draft.upscaler != 3u) {
                                draft.upscaler = 3u;  // frame generation runs with DLSS
                            }
                            if (next == Interpolation::kFsrGeneration && !fsr_generation_upscaler(draft.upscaler)) {
                                // FSR 3, AMD's own upscaler, else another this machine runs.
                                const std::vector<unsigned> offered = host.query(draft).upscalers;
                                for (const unsigned upscaler : {2u, 5u, 4u}) {
                                    if (std::find(offered.begin(), offered.end(), upscaler) != offered.end()) {
                                        draft.upscaler = upscaler;
                                        break;
                                    }
                                }
                            }
                            normalize_interpolation(draft, host.query(draft).frame_rates);
                        },
                        mode == Interpolation::kOff ? "THE GAME RUNS AT " + std::to_string(kStepRate) + ". INTERPOLATION ADDS FRAMES BETWEEN"
                        : mode == Interpolation::kNative
                            ? std::string("NEBULA DRAWS EXTRA FRAMES FROM THE GAME MOTION")
                        : mode == Interpolation::kFsrGeneration
                            ? "AMD GENERATES ONE FRAME BETWEEN EACH OF THE GAME'S " + std::to_string(kStepRate)
                            : "NVIDIA GENERATES THE FRAMES BETWEEN THE GAME'S " + std::to_string(kStepRate)});
    }
    const double measured = state.measured_fps;
    char now[48]{};
    std::snprintf(now, sizeof(now), "SHOWING %.1f FPS", measured);
    if (mode == Interpolation::kOff) {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        rows.push_back({"FRAME RATE LIMIT", std::to_string(rate <= kHalfStepRate ? kHalfStepRate : kStepRate) + " FPS",
                        [&host](RenderConfig& draft, int delta) {
                            draft.max_fps = static_cast<float>(delta < 0 ? kHalfStepRate : kStepRate);
                        },
                        now});
    } else {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        if (rate == 0u) {
            const unsigned refresh = state.frame_rates.refresh_hz;
            std::snprintf(text, sizeof(text), refresh != 0u ? "MATCH REFRESH (%u HZ)" : "MATCH REFRESH", refresh);
        } else {
            std::snprintf(text, sizeof(text), "%u FPS", rate);
        }
        rows.push_back({"TARGET FRAME RATE", text,
                        [&host](RenderConfig& draft, int delta) {
                            const auto targets = interpolation_targets(interpolation_of(draft), host.query(draft).frame_rates);
                            if (targets.empty()) return;
                            const auto current = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
                            std::size_t index = targets.size() - 1u;
                            for (std::size_t i = 0; i < targets.size(); ++i) {
                                if (targets[i] == current) index = i;
                            }
                            draft.max_fps = static_cast<float>(targets[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(targets.size()), delta)]);
                        },
                        now});
    }

    rows.push_back({"DETAIL", {}, nullptr, {}, true});
    {
        static constexpr std::array<unsigned, 5> kAnisotropy{1u, 2u, 4u, 8u, 16u};
        if (render.anisotropic_filtering <= 1u) {
            std::snprintf(text, sizeof(text), "TRILINEAR");
        } else {
            std::snprintf(text, sizeof(text), "%uX ANISOTROPIC", std::min(render.anisotropic_filtering, 16u));
        }
        rows.push_back({"TEXTURE FILTERING", text,
                        [&host](RenderConfig& draft, int delta) {
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < kAnisotropy.size(); ++i) {
                                if (kAnisotropy[i] <= draft.anisotropic_filtering) index = i;
                            }
                            draft.anisotropic_filtering = kAnisotropy[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(kAnisotropy.size()), delta)];
                        },
                        "SHARPER TEXTURES ON FLOORS AND WALLS AT AN ANGLE"});
    }
    rows.push_back({"MIPMAPS", render.enhanced_mipmaps ? "GENERATED" : "ORIGINAL",
                    [&host](RenderConfig& draft, int) { draft.enhanced_mipmaps = !draft.enhanced_mipmaps; },
                    "GENERATED ADDS SMOOTH DISTANT DETAIL WHERE THE GAME HAS NONE"});
    if (state.curved_surfaces_offered) {
        rows.push_back({"CURVED SURFACES", render.curved_surfaces ? "ON" : "OFF",
                        [&host](RenderConfig& draft, int) { draft.curved_surfaces = !draft.curved_surfaces; },
                        "ROUNDS THE OUTLINES OF CURVED MODELS"});
    }
    if (!rt_on) {
        static constexpr const char* kLighting[] = {"PER VERTEX", "PER PIXEL", "ENHANCED"};
        rows.push_back({"LIGHTING", kLighting[lighting_mode(render)],
                        [&host](RenderConfig& draft, int delta) {
                            const unsigned mode = settings_step(lighting_mode(draft), 3u, delta);
                            draft.per_pixel_lighting = mode != 0u;
                            draft.enhanced_lighting = mode == 2u;
                        },
                        lighting_mode(render) == 2u
                            ? "HIGHLIGHTS, SOFT SKY LIGHT AND SHINIER METALS FROM THE GAME'S OWN LIGHTS"
                            : "PER PIXEL LIGHTS EVERY PIXEL INSTEAD OF EVERY CORNER"});
    }
    if (!rt_on) {
        static constexpr const char* kOcclusion[] = {"OFF", "SSAO", "GTAO"};
        const auto occlusion_mode = [](const RenderConfig& config) {
            return !config.ambient_occlusion ? 0u : config.ambient_occlusion_ssao ? 1u : 2u;
        };
        rows.push_back({"AMBIENT OCCLUSION", kOcclusion[occlusion_mode(render)],
                        [&host, occlusion_mode](RenderConfig& draft, int delta) {
                            const unsigned mode = settings_step(occlusion_mode(draft), 3u, delta);
                            draft.ambient_occlusion = mode != 0u;
                            draft.ambient_occlusion_ssao = mode == 1u;
                        },
                        "SOFT SHADOWS IN CORNERS, CREASES AND WHERE THINGS MEET THE GROUND"});
    }
    {
        {
            // In steps of ten percent; 0 off.
            const unsigned value = std::min(render.bloom, kMaxBloom);
            if (value == 0u) {
                std::snprintf(text, sizeof(text), "OFF");
            } else {
                std::snprintf(text, sizeof(text), "%u%%", value);
            }
            rows.push_back({"BLOOM", text,
                            [&host](RenderConfig& draft, int delta) {
                                draft.bloom = static_cast<unsigned>(std::clamp(
                                    static_cast<int>(std::min(draft.bloom, kMaxBloom)) + delta * 10, 0,
                                    static_cast<int>(kMaxBloom)));
                            },
                            "HOW STRONGLY BRIGHT THINGS GLOW"});
            rows.back().slider_steps = static_cast<std::uint16_t>(kMaxBloom / 10u);
            rows.back().slider_at = static_cast<std::uint16_t>(value / 10u);
        }
    }
    // Texture packs (the enabled mods') apply with the settings above; the folders of dumped textures and movies.
    rows.push_back({"TEXTURE PACKS", {}, nullptr, {}, true});
    rows.push_back({"TEXTURE PACKS", !render.texture_packs ? "OFF" : render.texture_pack_preload ? "PRELOAD" : "ON",
                    [&host](RenderConfig& draft, int delta) {
                        const int mode = !draft.texture_packs ? 0 : draft.texture_pack_preload ? 2 : 1;
                        const unsigned next = settings_step(static_cast<unsigned>(mode), 3u, delta);
                        draft.texture_packs = next != 0u;
                        draft.texture_pack_preload = next == 2u;
                    },
                    "ON LOADS TEXTURES AS THEY APPEAR. PRELOAD LOADS THEM ALL AT ONCE"});
    if (render.texture_packs) {
        const TexturePackSummary& packs = state.texture_packs;
        char value[32]{};
        std::snprintf(value, sizeof(value), "%llu TEXTURES", static_cast<unsigned long long>(packs.textures));
        std::string help = "REPLACEMENT TEXTURES IN THE ENABLED MODS";
        if (packs.levels != 0u) help += ", WITH " + std::to_string(packs.levels) + " MIP LEVELS";
        // Whole phrases with the counts after a colon: the menu translates each phrase (no plural pieces).
        if (!packs.dolphin.empty()) {
            help += ". FILES WITH A DOLPHIN NAME: " + std::to_string(packs.dolphin.size()) + ". IMPORT THE PACK AS A MOD TO USE THEM";
        }
        if (!packs.unknown.empty()) {
            help += ". FILES THAT NAME NO TEXTURE OF THIS GAME: " + std::to_string(packs.unknown.size()) + ". THE FIRST: " +
                    mods_ui_text(packs.unknown.front(), 48u);
        }
        rows.push_back({"  REPLACEMENTS", packs.indexed ? std::string(value) : std::string("READING"), nullptr, help});
    }
    rows.push_back({"DUMP TEXTURES", render.texture_dump ? "ON" : "OFF",
                    [&host](RenderConfig& draft, int) { draft.texture_dump = !draft.texture_dump; },
                    "SAVES EVERY TEXTURE THE GAME SHOWS, FOR MAKING A PACK"});
    if (state.folders_open) {
        rows.push_back(button_row("TEXTURE DUMPS FOLDER", {"OPEN"},
                                  [&host](RenderConfig&, unsigned) { host.open_folder(GraphicsFolder::TextureDumps); },
                                  "WHERE DUMPED TEXTURES GO"));
    }
    if (state.folders_open) {
        rows.push_back({"MOVIES", {}, nullptr, {}, true});
        rows.push_back(button_row("MOVIES FOLDER", {"OPEN"}, [&host](RenderConfig&, unsigned) { host.open_folder(GraphicsFolder::Movies); },
                                  "VIDEOS NAMED LIKE THE GAME'S (PROLOGUEA.MP4) PLAY INSTEAD"));
    }
    return rows;
}


}  // namespace galaxy::ui
