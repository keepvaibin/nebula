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
    mutable int opened = -1;
    galaxy::ui::GraphicsPageState query(const galaxy::ui::RenderConfig&) const override {
        galaxy::ui::GraphicsPageState state;
        state.upscalers = {0, 1};
        state.render_percent = 50;
        state.frame_rates.refresh_hz = 144;
        state.ray_tracing_offered = rt;
        state.anti_aliasing_text = "ANTI-ALIASING: OFF";
        state.texture_packs = {7, 2, true, {}};
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
        check(row(rows, "  REPLACEMENTS").value == "7 TEXTURES", "texture summary");
        row(rows, "UPSCALING").change(config, 1);
        check(config.upscaler == 1, "upscaler action");
        row(rows, "INTERPOLATION").change(config, 1);
        check(config.frame_generation == 0 && config.max_fps == 0, "native interpolation action");
        row(rows, "TEXTURE DUMPS FOLDER").press(config, 0);
        check(host.opened == static_cast<int>(galaxy::ui::GraphicsFolder::TextureDumps), "texture folder action");
        row(rows, "MOVIES FOLDER").press(config, 0);
        check(host.opened == static_cast<int>(galaxy::ui::GraphicsFolder::Movies), "movie folder action");
        host.rt = true; config.ray_tracing_enabled = true;
        rows = galaxy::ui::graphics_page(config, host);
        check(row(rows, "RAY TRACING").value == "ON", "ray tracing capability row");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "MSAA" || r.label == "AMBIENT OCCLUSION"; }), "exclusive graphics row visibility");
        std::cout << "graphics page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
