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
    bool temporal = false;
    bool folders = true;
    bool clock_missing = false;
    unsigned refresh_hz = 144;
    mutable int opened = -1;
    galaxy::ui::GraphicsPageState query(const galaxy::ui::RenderConfig&) const override {
        galaxy::ui::GraphicsPageState state;
        state.upscalers = {0, 1, 5};
        state.render_percent = 50;
        state.frame_rates.refresh_hz = refresh_hz;
        state.frame_rates.display_clock_missing = clock_missing;
        state.temporal_upscaling = temporal;
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

        // Native interpolation with no display clock to time its frames by (Linux before its clock runs, I-005): the
        // row says they are not synchronized; with a clock it says nothing more.
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "INTERPOLATION").value == "NATIVE", "native interpolation on a display clock");
        host.clock_missing = true;
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "INTERPOLATION").value == "NATIVE (NOT SYNCHRONIZED)",
              "native interpolation with no display clock is marked not synchronized");
        host.clock_missing = false;

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

        config.aa_mode = 1u;
        rows = galaxy::ui::graphics_page(config, host);
        check(!has(rows, "RAY TRACING") && !has(rows, "RAY TRACING QUALITY") && has(rows, "SMAA QUALITY") && has(rows, "MSAA") &&
                  has(rows, "LIGHTING") && has(rows, "AMBIENT OCCLUSION"),
              "no ray tracing rows; anti-aliasing, MSAA, lighting and occlusion are always offered");
        std::cout << "graphics page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
