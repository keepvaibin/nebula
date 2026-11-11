// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {

struct TexturePackSummary {
    std::uint64_t textures = 0;
    std::uint64_t levels = 0;
    bool indexed = false;
    std::vector<std::string> dolphin;  // tex1_ files outside the mods (not converted)
    std::vector<std::string> unknown;  // files naming none of the game's textures
};

struct GraphicsPageState {
    std::vector<unsigned> upscalers;
    unsigned render_percent = 100;
    bool dlss_supported = false;
    bool dlss_installed = false;
    bool xess_supported = false;
    bool xess_installed = false;
    bool ray_tracing_offered = false;
    bool traced_temporal = false;  // FSR 3 runs here: with ray tracing on it anti-aliases the scene (plan 16.3)
    bool temporal_upscaling = false;
    bool spatial_upscaling = false;
    bool curved_surfaces_offered = false;
    bool folders_open = true;  // the platform shows a folder's files (host_window::folders_open)
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
