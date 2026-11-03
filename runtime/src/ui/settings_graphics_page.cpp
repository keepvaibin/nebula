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

// The game's settings the menu changed since it took them (its input mode is the controls draft's), each merged into
// `live` when given: a setting changed elsewhere meanwhile keeps that value unless the menu changed it too.
std::vector<SettingsRow> graphics_page(const RenderConfig& render, const GraphicsPageHost& host) {
    const GraphicsPageState state = host.query(render);
    std::vector<SettingsRow> rows;
    char text[96]{};
    rows.push_back({"RESOLUTION", {}, nullptr, {}, true});
    {
        static constexpr const char* kUpscalers[] = {"OFF", "FSR 1", "FSR 3", "DLSS"};
        const std::vector<unsigned>& choices = state.upscalers;
        const bool offered = std::find(choices.begin(), choices.end(), render.upscaler) != choices.end();
        std::string value = kUpscalers[offered ? std::min(render.upscaler, 3u) : 1u];
        if (offered && render.upscaler == 3u && !state.dlss_supported) {
            value += state.dlss_installed ? " (AFTER RESTART)" : " (DOWNLOAD)";
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
                        "DLSS AND FSR 3 RENDER FEWER PIXELS AND REBUILD A SHARP IMAGE"});
    }
    const unsigned render_percent = state.render_percent;
    // Ray tracing replaces MSAA, ambient occlusion and the lighting modes: their rows are hidden while it is on.
    const bool rt_offered = state.ray_tracing_offered;
    const bool rt_on = rt_offered && render.ray_tracing_enabled;
    {
        static constexpr const char* kTemporal[] = {"NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kDlss[] = {"DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kSpatial[] = {"NATIVE", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        const bool temporal = render.upscaler >= 2u;
        const unsigned quality = temporal ? std::min(render.temporal_quality, 4u) : std::min(render.spatial_quality, 4u);
        const unsigned upscaler = std::min(render.upscaler, 3u);
        std::snprintf(text, sizeof(text), "%s (%u%%)",
                      upscaler == 3u ? kDlss[quality] : upscaler == 2u ? kTemporal[quality] : kSpatial[quality],
                      render_percent);
        rows.push_back({upscaler == 3u ? "DLSS MODE" : upscaler == 2u ? "FSR 3 MODE" : "QUALITY MODE", text,
                        [&host](RenderConfig& draft, int delta) {
                            if (draft.upscaler >= 2u) {
                                draft.temporal_quality = settings_step(std::min(draft.temporal_quality, 4u), 5u, delta);
                            } else {
                                draft.spatial_quality = settings_step(std::min(draft.spatial_quality, 4u), 5u, delta);
                            }
                        },
                        "THE RENDER RESOLUTION BELOW THE INTERNAL ONE. QUALITY RENDERS THE MOST PIXELS"});
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
    {
        std::string value = state.anti_aliasing_text;
        if (const std::size_t colon = value.find(": "); colon != std::string::npos) value.erase(0, colon + 2u);
        if (render.aa_mode != 0u && render.msaa_samples > 1u && render.upscaler != 2u && render.upscaler != 3u &&
            !render.ray_tracing_enabled) {
            value += " (OFF WITH MSAA)";
        }
        rows.push_back({"ANTI-ALIASING", value,
                        [&host](RenderConfig& draft, int delta) { draft.aa_mode = settings_step(std::min(draft.aa_mode, 3u), 4u, delta); },
                        "SMOOTHS EDGES. AUTO SKIPS IT WHEN THE IMAGE IS ALREADY SUPERSAMPLED"});
    }
    if (render.aa_mode == 1u || render.aa_mode == 2u) {
        static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
        rows.push_back({"SMAA QUALITY", kQuality[std::min(render.smaa_quality, 3u)],
                        [&host](RenderConfig& draft, int delta) {
                            draft.smaa_quality = settings_step(std::min(draft.smaa_quality, 3u), 4u, delta);
                        },
                        "HIGHER FINDS MORE EDGES"});
    }
    if (!rt_on) {
        const unsigned samples = std::bit_floor(std::clamp(render.msaa_samples, 1u, 8u));
        std::string value = samples <= 1u ? "OFF" : std::to_string(samples) + "X";
        if (samples > 1u && (render.upscaler == 2u || render.upscaler == 3u)) value += " (OFF WITH DLSS / FSR 3)";
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
        // rt_quality: 0 medium, 1 high, 2 low; a change restarts the ray tracer (RendererD3D12::reconcile_ray_tracing).
        static constexpr unsigned kOrder[] = {2u, 0u, 1u};
        static constexpr const char* kNames[] = {"MEDIUM", "HIGH", "LOW"};
        rows.push_back({"RAY TRACING QUALITY", kNames[std::min(render.rt_quality, 2u)],
                        [&host](RenderConfig& draft, int delta) {
                            const unsigned quality = std::min(draft.rt_quality, 2u);
                            const unsigned at = quality == 2u ? 0u : quality == 0u ? 1u : 2u;
                            draft.rt_quality = kOrder[settings_step(at, 3u, delta)];
                        },
                        "HIGH: MORE LIGHTS PER PIXEL, FULL-RESOLUTION WATER"});
        // The effects players choose, each switched live.
        const auto effect_row = [&](const char* name, bool RenderConfig::*field, const char* help) {
            rows.push_back({name, render.*field ? "ON" : "OFF",
                            [field](RenderConfig& draft, int) { draft.*field = !(draft.*field); }, help});
        };
        effect_row("REFLECTIONS", &RenderConfig::rt_reflections,
                   "TRACED REFLECTIONS ON METAL, GLOSSY FLOORS, GLASS AND CRYSTAL");
        effect_row("WATER REFLECTIONS", &RenderConfig::rt_water,
                   "TRACED REFLECTIONS AND REFRACTION ON SEAS, LAKES AND POOLS");
        effect_row("SUN RAYS", &RenderConfig::rt_sun_rays, "SHAFTS OF LIGHT THE AIR SCATTERS AROUND THE SUN");
        effect_row("LENS BLOOM", &RenderConfig::rt_lens_bloom, "LIGHT THE LENS SCATTERS AROUND EVERY BRIGHT SURFACE");
    }

    rows.push_back({"FRAME RATE", {}, nullptr, {}, true});
    const Interpolation mode = interpolation_of(render);
    {
        std::string value = mode == Interpolation::kOff      ? "OFF"
                            : mode == Interpolation::kNative ? "NATIVE"
                                                             : "DLSS FRAME GENERATION";
        if (mode == Interpolation::kFrameGeneration) {
            if (!state.frame_rates.generated_supported) value += " (AFTER RESTART)";
            else if (render.upscaler != 3u) value += " (NEEDS DLSS)";
        }
        rows.push_back({"INTERPOLATION", value,
                        [&host](RenderConfig& draft, int delta) {
                            const auto modes = interpolation_modes(host.query(draft).frame_rates);
                            const Interpolation current = interpolation_of(draft);
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < modes.size(); ++i) {
                                if (modes[i] == current) index = i;
                            }
                            const Interpolation next = modes[settings_step(static_cast<unsigned>(index),
                                                                           static_cast<unsigned>(modes.size()), delta)];
                            if (next == current) return;
                            draft.frame_generation = next == Interpolation::kFrameGeneration ? 1u : 0u;
                            // Interpolation starts at the display's own rate.
                            draft.max_fps = next == Interpolation::kOff ? 60.0f : 0.0f;
                            if (next == Interpolation::kFrameGeneration && draft.upscaler != 3u) {
                                draft.upscaler = 3u;  // frame generation runs with DLSS
                            }
                            normalize_interpolation(draft, host.query(draft).frame_rates);
                        },
                        mode == Interpolation::kOff ? "THE GAME RUNS AT 60. NATIVE OR DLSS ADD FRAMES BETWEEN"
                        : mode == Interpolation::kNative
                            ? "NEBULA DRAWS EXTRA FRAMES FROM THE GAME MOTION"
                            : "NVIDIA GENERATES THE FRAMES BETWEEN THE GAME'S 60"});
    }
    const double measured = state.measured_fps;
    char now[48]{};
    std::snprintf(now, sizeof(now), "SHOWING %.1f FPS", measured);
    if (mode == Interpolation::kOff) {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        rows.push_back({"FRAME RATE LIMIT", rate <= 30u ? "30 FPS" : "60 FPS",
                        [&host](RenderConfig& draft, int delta) { draft.max_fps = delta < 0 ? 30.0f : 60.0f; },
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
        rows.push_back({"AMBIENT OCCLUSION", render.ambient_occlusion ? "ON" : "OFF",
                        [&host](RenderConfig& draft, int) { draft.ambient_occlusion = !draft.ambient_occlusion; },
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
            rows.push_back({"  INTENSITY", text,
                            [&host](RenderConfig& draft, int delta) {
                                draft.bloom = static_cast<unsigned>(std::clamp(
                                    static_cast<int>(std::min(draft.bloom, kMaxBloom)) + delta * 10, 0,
                                    static_cast<int>(kMaxBloom)));
                            },
                            "THE ENHANCED GLOW'S STRENGTH"});
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
        if (!packs.unreadable.empty()) {
            help += ". " + std::to_string(packs.unreadable.size()) + " FILE NAME" +
                    (packs.unreadable.size() == 1u ? " IS" : "S ARE") + " NOT TEXTURE NAMES: " +
                    mods_ui_text(packs.unreadable.front(), 48u);
        }
        rows.push_back({"  REPLACEMENTS", packs.indexed ? std::string(value) : std::string("READING"), nullptr, help});
    }
    rows.push_back({"DUMP TEXTURES", render.texture_dump ? "ON" : "OFF",
                    [&host](RenderConfig& draft, int) { draft.texture_dump = !draft.texture_dump; },
                    "SAVES EVERY TEXTURE THE GAME SHOWS, FOR MAKING A PACK"});
    rows.push_back(button_row("TEXTURE DUMPS FOLDER", {"OPEN"},
                              [&host](RenderConfig&, unsigned) { host.open_folder(GraphicsFolder::TextureDumps); },
                              "WHERE DUMPED TEXTURES GO"));
    rows.push_back({"MOVIES", {}, nullptr, {}, true});
    rows.push_back(button_row("MOVIES FOLDER", {"OPEN"}, [&host](RenderConfig&, unsigned) { host.open_folder(GraphicsFolder::Movies); },
                              "VIDEOS NAMED LIKE THE GAME'S (PROLOGUEA.MP4) PLAY INSTEAD"));
    return rows;
}


}  // namespace galaxy::ui
