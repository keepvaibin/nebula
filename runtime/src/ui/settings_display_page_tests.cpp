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
    bool hdr_in_use = false;
    bool display_on = true;  // the display in HDR mode (false: Windows HDR off, turned on by the game)
    bool held = false;
    bool refused = false;
    bool vsync_fixed = false;
    bool variable = false;
    mutable int last = -1;
    mutable int calibrations = 0;
    galaxy::ui::DisplayPageState query(const galaxy::ui::RenderConfig& render) const override {
        galaxy::ui::DisplayPageState state;
        state.monitors = {{"first", "First"}, {"second", "Second"}};
        state.current_monitor = std::strcmp(render.monitor.data(), "second") == 0 ? 1u : 0u;
        state.output_size = {1280, 720};
        state.fullscreen_only = phone;
        if (!empty_sizes) state.window_sizes = {{640, 480}, {1280, 720}, {1920, 1080}};
        state.frame_rates.refresh_hz = refresh_hz;
        state.frame_rates.vsync_fixed = vsync_fixed;
        state.frame_rates.variable_refresh = variable;
        state.hdr_offered = hdr;
        state.hdr_active = hdr_active;
        state.hdr_in_use = hdr_in_use;
        state.hdr_display_on = hdr && display_on;
        state.hdr_held_sdr = held;
        state.hdr_refused = refused;
        return state;
    }
    void calibrate_hdr(const galaxy::ui::RenderConfig&) const override { ++calibrations; }
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
        // A variable refresh display (G2.3.5 point 9): the row says so, and how it shows the pictures.
        host.variable = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "REFRESH RATE").value == "144 HZ (VARIABLE)", "a variable display is said so");
        check(row(rows, "REFRESH RATE").help == "THE DISPLAY REFRESHES AS EACH PICTURE COMES", "a variable display's help");
        host.variable = false;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "REFRESH RATE").value == "144 HZ" &&
                  row(rows, "REFRESH RATE").help == "SET IN THE SYSTEM DISPLAY SETTINGS",
              "a fixed display's row");
        // A display that always waits for its refresh: VSYNC is ON whatever was chosen, with no choice and the reason.
        config.vsync_interval = 0u;
        host.vsync_fixed = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "VSYNC").value == "ON" && !row(rows, "VSYNC").change, "vsync fixed on such a display");
        check(row(rows, "VSYNC").help == "THIS DISPLAY ALWAYS WAITS FOR ITS REFRESH", "vsync fixed reason");
        host.vsync_fixed = false;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "VSYNC").value == "OFF" && row(rows, "VSYNC").change, "vsync a choice elsewhere");
        // HDR rows: none on an SDR display; the HDR switch on an HDR one, the paper white once it is on, in steps of 10 nits.
        rows = galaxy::ui::display_page(config, host);
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "HDR" || r.label == "PAPER WHITE"; }), "no HDR rows on an SDR display");
        host.hdr = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").value == "OFF", "hdr row off");
        check(row(rows, "HDR").help == "WIDE BRIGHTNESS RANGE ON AN HDR DISPLAY", "hdr row help");
        host.display_on = false;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help == "TURNS ON WINDOWS HDR WHILE THE GAME RUNS", "Windows HDR off: the game turns it on");
        host.display_on = true;
        rows = galaxy::ui::display_page(config, host);
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "PAPER WHITE"; }), "paper white waits for hdr");
        row(rows, "HDR").change(config, 1);
        check(config.hdr, "hdr action");
        // On in the draft only: the calibration waits for Apply.
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help.find("WAITING") == std::string::npos, "hdr not applied yet: no wait named");
        check(row(rows, "HDR CALIBRATION").buttons.size() == 1u && row(rows, "HDR CALIBRATION").buttons[0] == "-CALIBRATE" &&
                  !row(rows, "HDR CALIBRATION").press && row(rows, "HDR CALIBRATION").help == "APPLY HDR FIRST",
              "calibration waits for hdr applied");
        host.hdr_in_use = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help.find("WAITING") != std::string::npos, "hdr waits while the chain is not HDR");
        check(!row(rows, "HDR CALIBRATION").press && row(rows, "HDR CALIBRATION").help.find("WAITING") != std::string::npos,
              "calibration waits while the chain is not HDR");
        check(row(rows, "HDR").help == "WAITING FOR THE DISPLAY TO SWITCH TO HDR", "the display's switch waited for");
        host.held = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help == "WAITING: FRAME GENERATION KEEPS THE PICTURE SDR" &&
                  row(rows, "HDR CALIBRATION").help == "WAITING: FRAME GENERATION KEEPS THE PICTURE SDR",
              "frame generation named");
        host.held = false;
        host.refused = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help == "WINDOWS HDR IS OFF FOR THIS DISPLAY" && !row(rows, "HDR CALIBRATION").press,
              "Windows keeping HDR off named");
        host.refused = false;
        host.hdr_active = true;
        rows = galaxy::ui::display_page(config, host);
        check(row(rows, "HDR").help.find("WAITING") == std::string::npos, "hdr active");
        check(row(rows, "HDR CALIBRATION").buttons[0] == "CALIBRATE" && row(rows, "HDR CALIBRATION").press,
              "calibration offered on an HDR picture");
        row(rows, "HDR CALIBRATION").press(config, 0u);
        check(host.calibrations == 1, "calibrate opens the calibration");
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
