// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {

struct TexturePackSummary {
    std::uint64_t textures = 0;
    std::uint64_t levels = 0;
    bool indexed = false;
    std::vector<std::string> unreadable;
};

struct GraphicsPageState {
    std::vector<unsigned> upscalers;
    unsigned scale_cap = 1;
    unsigned render_scale = 1;
    unsigned logical_width = 640;
    unsigned logical_height = 456;
    bool dlss_supported = false;
    bool dlss_installed = false;
    bool ray_tracing_offered = false;
    bool temporal_upscaling = false;
    bool spatial_upscaling = false;
    bool curved_surfaces_offered = false;
    double measured_fps = 0;
    FrameRateCaps frame_rates;
    std::string anti_aliasing_text;
    TexturePackSummary texture_packs;
};

enum class GraphicsFolder { TextureDumps, Movies };

// The host outlives the rows and supplies device facts and desktop actions.
class GraphicsPageHost {
public:
    virtual ~GraphicsPageHost() = default;
    virtual GraphicsPageState query(const RenderConfig& render) const = 0;
    virtual void open_folder(GraphicsFolder folder) const = 0;
};

std::vector<SettingsRow> graphics_page(const RenderConfig& render, const GraphicsPageHost& host);
unsigned lighting_mode(const RenderConfig& render);

}  // namespace galaxy::ui
