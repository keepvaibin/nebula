#include "galaxy/ui/settings_graphics_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared graphics page must not include Windows.h
#endif

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
class Host final : public galaxy::ui::GraphicsPageHost {
public:
    bool rt = false;
    bool temporal = false;
    bool traced = false;  // FSR 3 runs: with ray tracing on it anti-aliases the scene
    bool folders = true;
    unsigned refresh_hz = 144;
    mutable int opened = -1;
    galaxy::ui::GraphicsPageState query(const galaxy::ui::RenderConfig&) const override {
        galaxy::ui::GraphicsPageState state;
        state.upscalers = {0, 1, 5};
        state.render_percent = 50;
        state.frame_rates.refresh_hz = refresh_hz;
        state.ray_tracing_offered = rt;
        state.temporal_upscaling = temporal;
        state.traced_temporal = traced;
        state.folders_open = folders;
        state.anti_aliasing_text = "ANTI-ALIASING: OFF";
        state.texture_packs = {7, 2, true, {}, {}};
        return state;
    }
    void open_folder(galaxy::ui::GraphicsFolder folder) const override { opened = static_cast<int>(folder); }
};
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
bool has(const std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    return std::any_of(rows.begin(), rows.end(), [label](const auto& r) { return r.label == label; });
}
std::size_t at(const std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    return static_cast<std::size_t>(std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return r.label == label; }) -
                                    rows.begin());
}
}

int main() {
    try {
        Host host;
        galaxy::ui::RenderConfig config;
        config.upscaler = 0;
        config.texture_packs = true;
        config.max_fps = 60;
        auto rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "ANTI-ALIASING").value == "OFF", "anti-aliasing label");
        check(row(rows, "INTERNAL RESOLUTION").value == "MATCH DISPLAY", "default internal resolution follows output");
        check(!has(rows, "QUALITY MODE"), "upscaling off has no mode row");
        const std::size_t resolution_at = at(rows, "INTERNAL RESOLUTION");
        check(at(rows, "UPSCALING") + 1u == resolution_at, "the resolution row is under the upscaler row");
        row(rows, "INTERNAL RESOLUTION").change(config, 1);
        check(config.internal_height == 144u, "pixel height selection");
        check(row(rows, "  REPLACEMENTS").value == "7 TEXTURES", "texture summary");
        row(rows, "UPSCALING").change(config, 1);
        check(config.upscaler == 1, "upscaler action");
        row(rows, "INTERPOLATION").change(config, 1);
        check(config.frame_generation == 0 && config.max_fps == 0, "native interpolation action");
        row(rows, "TEXTURE DUMPS FOLDER").press(config, 0);
        check(host.opened == static_cast<int>(galaxy::ui::GraphicsFolder::TextureDumps), "texture folder action");
        row(rows, "MOVIES FOLDER").press(config, 0);
        check(host.opened == static_cast<int>(galaxy::ui::GraphicsFolder::Movies), "movie folder action");

        // An upscaler on: its mode sets the resolution, the exact height is not offered beside it.
        rows = galaxy::ui::graphics_page(config, host);
        check(!has(rows, "INTERNAL RESOLUTION") && has(rows, "QUALITY MODE"), "one resolution row with an upscaler");
        check(at(rows, "QUALITY MODE") == resolution_at, "the mode row takes the resolution row's place");
        // A temporal upscaler antialiases itself: no anti-aliasing or MSAA rows.
        config.upscaler = 5;
        host.temporal = true;
        rows = galaxy::ui::graphics_page(config, host);
        check(has(rows, "SGSR 2 MODE") && !has(rows, "ANTI-ALIASING") && !has(rows, "MSAA"), "temporal upscaling rows");
        host.temporal = false;
        config.upscaler = 0;

        // A 60 Hz display offers no interpolation: no one-choice row. A platform that opens no folders: no folder rows.
        host.refresh_hz = 60;
        host.folders = false;
        config.max_fps = 60;
        rows = galaxy::ui::graphics_page(config, host);
        check(!has(rows, "INTERPOLATION") && has(rows, "FRAME RATE LIMIT"), "no one-choice interpolation row");
        check(!has(rows, "MOVIES FOLDER") && !has(rows, "MOVIES") && !has(rows, "TEXTURE DUMPS FOLDER"), "no folder rows");
        host.refresh_hz = 144;
        host.folders = true;

        host.rt = true; config.ray_tracing_enabled = true;
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "RAY TRACING").value == "ON", "ray tracing capability row");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "MSAA" || r.label == "AMBIENT OCCLUSION"; }), "exclusive graphics row visibility");
        // The preset and every part (plan 19.1): a part changed alone makes the preset custom; a preset sets them all.
        {
            auto quality = row(rows, "RAY TRACING QUALITY");
            check(quality.value == "MEDIUM" && row(rows, "SUN SHADOWS").value == "MEDIUM" && has(rows, "LIGHT IN THE AIR") &&
                      has(rows, "GLASS AND WATER") && has(rows, "SKY AND BOUNCE LIGHT") && has(rows, "LOCAL LIGHTS") &&
                      has(rows, "REFLECTIONS") && has(rows, "LENS BLOOM"),
                  "ray tracing's preset and parts rows");
            galaxy::ui::RenderConfig draft = config;
            row(rows, "LOCAL LIGHTS").change(draft, -1);
            check(draft.rt_quality == galaxy::render::kRtPresetCustom && draft.rt_parts[1] == 1u, "a part alone makes it custom");
            quality.change(draft, 1);
            check(draft.rt_quality != galaxy::render::kRtPresetCustom && draft.rt_parts[1] == draft.rt_parts[0], "a preset sets every part");
        }
        host.traced = true;
        config.aa_mode = 1u;
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "ANTI-ALIASING").value == "TEMPORAL (RAY TRACING)" && !row(rows, "ANTI-ALIASING").change &&
                  !has(rows, "SMAA QUALITY") && !has(rows, "MSAA"),
              "with ray tracing on FSR 3 anti-aliases the scene and the row says so");
        config.ray_tracing_enabled = false;
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "ANTI-ALIASING").value != "TEMPORAL (RAY TRACING)" && has(rows, "SMAA QUALITY") && has(rows, "MSAA"),
              "ray tracing off gives the anti-aliasing rows back");
        std::cout << "graphics page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
