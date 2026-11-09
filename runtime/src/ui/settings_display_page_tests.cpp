#include "galaxy/ui/settings_display_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared Display page must not include Windows.h
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
class Host final : public galaxy::ui::DisplayPageHost {
public:
    bool empty_sizes = false;
    bool phone = false;
    unsigned refresh_hz = 144;
    bool hdr = false;
    bool hdr_active = false;
    mutable int last = -1;
    galaxy::ui::DisplayPageState query(const galaxy::ui::RenderConfig& render) const override {
        galaxy::ui::DisplayPageState state;
        state.monitors = {{"first", "First"}, {"second", "Second"}};
        state.current_monitor = std::strcmp(render.monitor.data(), "second") == 0 ? 1u : 0u;
        state.output_size = {1280, 720};
        state.fullscreen_only = phone;
        if (!empty_sizes) state.window_sizes = {{640, 480}, {1280, 720}, {1920, 1080}};
        state.frame_rates.refresh_hz = refresh_hz;
        state.hdr_offered = hdr;
        state.hdr_active = hdr_active;
        return state;
    }
    void changed(galaxy::ui::DisplayChange change, galaxy::ui::RenderConfig&) const override {
        last = static_cast<int>(change);
    }
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
        config.display_mode = galaxy::render::kDisplayWindowed;
        config.window_width = 1280; config.window_height = 720;
        config.aspect_mode = 0;
        auto rows = galaxy::ui::display_page(config, host);
        check(row(rows, "MONITOR").value == "1 First", "monitor label");
        check(row(rows, "REFRESH RATE").value == "144 HZ", "refresh label");
        row(rows, "MONITOR").change(config, 1);
        check(std::strcmp(config.monitor.data(), "second") == 0, "monitor identity");
        check(host.last == static_cast<int>(galaxy::ui::DisplayChange::Monitor), "monitor host validation");
        row(rows, "RESOLUTION").change(config, 1);
        check(config.window_width == 1920 && config.window_height == 1080, "window resolution action");
        row(rows, "ASPECT RATIO").change(config, 1);
        check(config.aspect_mode == 1 && host.last == static_cast<int>(galaxy::ui::DisplayChange::Aspect), "aspect action");
        row(rows, "DISPLAY MODE").change(config, 1);
        check(config.display_mode == galaxy::render::kDisplayBorderless, "borderless action");
        config.display_mode = galaxy::render::kDisplayWindowed; host.empty_sizes = true;
        const unsigned width = config.window_width;
        row(rows, "RESOLUTION").change(config, -1);
        check(config.window_width == width, "empty window choices ignored");
        host.refresh_hz = 0;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "REFRESH RATE").value == "UNKNOWN", "a refresh rate the system did not report is said so");
        host.refresh_hz = 144;
        // HDR rows: none on an SDR display; the HDR switch on an HDR one, the paper white once it is on, in steps of 10 nits.
        rows = galaxy::ui::display_page(config, host);
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "HDR" || r.label == "PAPER WHITE"; }), "no HDR rows on an SDR display");
        host.hdr = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").value == "OFF", "hdr row off");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "PAPER WHITE"; }), "paper white waits for hdr");
        row(rows, "HDR").change(config, 1);
        check(config.hdr, "hdr action");
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help.find("WAITING") != std::string::npos, "hdr waits while the chain is not HDR");
        host.hdr_active = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help.find("WAITING") == std::string::npos, "hdr active");
        check(row(rows, "PAPER WHITE").value == "200 NITS", "paper white label");
        row(rows, "PAPER WHITE").change(config, 3);
        check(config.paper_white == 230u, "paper white steps by 10");
        row(rows, "PAPER WHITE").change(config, -100);
        check(config.paper_white == 80u, "paper white floor");
        row(rows, "PAPER WHITE").change(config, 100);
        check(config.paper_white == 500u, "paper white ceiling");
        host.hdr = false;
        host.phone = true;
        rows = galaxy::ui::display_page(config, host);
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "RESOLUTION" || r.label == "MONITOR" || r.label == "DISPLAY MODE"; }), "phone output follows the surface, without desktop display controls");
        std::cout << "Display page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
