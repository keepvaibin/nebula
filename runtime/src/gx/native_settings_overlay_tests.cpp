#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/host_window_win32.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/temporal_upscaler.h"
#include "galaxy/input_bindings.h"
#include "galaxy/runtime_settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

class ScopedEnv {
public:
    explicit ScopedEnv(const char* name) : name_(name) {
        char* old = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&old, &length, name_) == 0 && old != nullptr) {
            had_old_ = true;
            old_value_ = old;
            std::free(old);
        }
    }

    ~ScopedEnv() {
        _putenv_s(name_, had_old_ ? old_value_.c_str() : "");
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    bool had_old_ = false;
    std::string old_value_;
};

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool expect_near(float actual, float expected, const char* message) {
    return expect(std::fabs(actual - expected) <= 0.0001f, message);
}

bool key(std::uintptr_t virtual_key, bool was_down = false) {
    return galaxy::gx::native_settings_handle_window_key(
        virtual_key,
        was_down);
}

// NebulaSettingsMenu's NebulaText id for a host text: "S." and the text, its spaces as _ and each number (digits, a
// decimal point between digits) as #.
std::string settings_text_id(std::string_view text) {
    std::string id = "S.";
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] < '0' || text[i] > '9') {
            id += text[i] == ' ' ? '_' : text[i];
            ++i;
            continue;
        }
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') ||
                                   (text[i] == '.' && i + 1u < text.size() && text[i + 1u] >= '0' && text[i + 1u] <= '9'))) {
            ++i;
        }
        id += '#';
    }
    return id;
}

bool layout_inside(
    const galaxy::gx::NativeSettingsLayout& layout,
    unsigned width,
    unsigned height) {
    return layout.supported && layout.panel_x >= 0 && layout.panel_y >= 0 &&
           layout.panel_width > 0 && layout.panel_height > 0 &&
           layout.panel_x + layout.panel_width <= static_cast<int>(width) &&
           layout.panel_y + layout.panel_height <= static_cast<int>(height) &&
           layout.rows_y > layout.panel_y &&
           layout.footer_y < layout.panel_y + layout.panel_height;
}

}  // namespace

int main() {
    bool passed = true;
    // The frame rate targets follow the display: a 240 Hz one here, whatever this machine has.
    galaxy::gx::host_window::native_settings_set_refresh_override_for_tests(240u);
    const ScopedEnv render_path_env("GALAXY_RENDER_CONFIG_PATH");
    const ScopedEnv runtime_path_env("GALAXY_RUNTIME_SETTINGS_PATH");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("galaxy_native_settings_overlay_tests_" +
         std::to_string(GetCurrentProcessId()));
    const std::filesystem::path initial_render = root / "initial-render.ini";
    const std::filesystem::path initial_runtime = root / "initial-runtime.ini";
    std::filesystem::remove_all(root);
    _putenv_s(
        "GALAXY_RENDER_CONFIG_PATH",
        initial_render.string().c_str());
    _putenv_s(
        "GALAXY_RUNTIME_SETTINGS_PATH",
        initial_runtime.string().c_str());

    galaxy::gx::native_settings_reset_overlay_state_for_tests();
    // North America's disc: English, French and Spanish.
    const std::string usa_languages[] = {"UsEnglish", "UsFrench", "UsSpanish"};
    galaxy::set_disc_languages(usa_languages);
    auto snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(!snapshot.visible, "overlay starts hidden");
    passed &= expect(!snapshot.dirty, "overlay starts clean");
    passed &= expect(!snapshot.save_error, "overlay starts without a save error");
    passed &= expect(snapshot.tab_count == 8u, "display/graphics/controls/audio/game/mods/system/advanced tabs exist");
    passed &= expect(snapshot.tab == 0u && snapshot.row == 1u, "the first setting row (below its heading) is selected");
    passed &= expect(
        snapshot.row_count == 9u,
        "display tab has a screen heading, monitor, mode, resolution, refresh rate, aspect ratio, vsync, "
        "an interface heading and HUD resolution");

    passed &= expect(key(VK_F1, true), "repeated F1 is consumed");
    passed &= expect(
        !galaxy::gx::native_settings_overlay_visible(),
        "repeated F1 cannot open the overlay");
    passed &= expect(key(VK_F1), "initial F1 opens the global native overlay");
    passed &= expect(
        galaxy::gx::native_settings_overlay_visible(),
        "native overlay is globally reachable while the window owns focus");
    passed &= expect(key(VK_F1, true), "held F1 remains consumed");
    passed &= expect(
        galaxy::gx::native_settings_overlay_visible(),
        "held F1 cannot flap a visible overlay closed");

    // Display page: rows edit a draft; Enter applies, Backspace undoes. Rows: 0 heading, 1 monitor,
    // 2 mode, 3 resolution, 4 refresh rate, 5 aspect ratio, 6 vsync, 7 heading, 8 HUD resolution.
    // Leaving with changes not applied asks first; Backspace discards them and closes.
    const auto close_discarding = [&] {
        bool ok = key(VK_ESCAPE);
        if (galaxy::gx::native_settings_overlay_visible()) ok = ok && key(VK_BACK);
        return ok && !galaxy::gx::native_settings_overlay_visible();
    };
    const auto reopen = [&] {
        return close_discarding() && key(VK_F1) &&
               galaxy::gx::native_settings_overlay_visible();
    };
    // A page from its first setting row: back one page and forward again.
    const auto page_start = [&] { return key(VK_PRIOR) && key(VK_NEXT); };
    galaxy::gx::RenderConfig render{};
    render.efb_scale = 2u;
    render.internal_follow_window = false;
    render.display_mode = galaxy::gx::kDisplayWindowed;
    render.aspect_mode = 0u;
    render.vsync_interval = 0u;
    render.hud_output_resolution = true;
    galaxy::gx::set_render_config(render);
    galaxy::gx::stage_next_start_efb_scale(render.efb_scale);
    passed &= expect(reopen() && page_start(), "reopening takes a fresh draft");
    for (int i = 0; i < 4; ++i) passed &= expect(key(VK_DOWN), "down moves to the aspect ratio");
    passed &= expect(key(VK_RIGHT), "right selects 4:3");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().window_width == 640u &&
            galaxy::gx::native_settings_draft_for_tests().window_height == 480u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 2u,
        "4:3 snaps the 854 x 480 window to 640 x 480 in the draft");
    passed &= expect(key(VK_RETURN), "aspect ratio applies");
    passed &= expect(
        galaxy::gx::get_render_config().aspect_mode == 1u &&
            galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "the new window size waits for confirmation");
    passed &= expect(key(VK_RETURN), "Enter keeps the window size");
    passed &= expect(
        galaxy::gx::get_render_config().window_width == 640u &&
            galaxy::gx::get_render_config().window_height == 480u &&
            !galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "the 4:3 window stays");

    for (int i = 0; i < 3; ++i) passed &= expect(key(VK_UP), "up moves to the display mode");
    passed &= expect(key(VK_RIGHT) && key(VK_RETURN), "borderless applies");
    passed &= expect(
        galaxy::gx::get_render_config().display_mode == galaxy::gx::kDisplayBorderless &&
            galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "a display change waits for confirmation");
    passed &= expect(key(VK_RIGHT), "rows are locked while confirming");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().display_mode ==
            galaxy::gx::kDisplayBorderless,
        "confirmation blocks further edits");
    passed &= expect(key(VK_ESCAPE), "Escape undoes the display change");
    passed &= expect(
        galaxy::gx::get_render_config().display_mode == galaxy::gx::kDisplayWindowed &&
            !galaxy::gx::native_settings_overlay_snapshot().confirming_display &&
            galaxy::gx::native_settings_overlay_visible(),
        "the previous display returns and the menu stays open");
    passed &= expect(key(VK_RIGHT) && key(VK_RETURN) && key(VK_RETURN),
                     "borderless applies and Enter keeps it");
    passed &= expect(
        galaxy::gx::get_render_config().display_mode == galaxy::gx::kDisplayBorderless &&
            !galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "a kept display change stays");
    passed &= expect(key(VK_LEFT) && key(VK_RETURN) && key(VK_RETURN),
                     "windowed applies and is kept");
    passed &= expect(
        galaxy::gx::get_render_config().display_mode == galaxy::gx::kDisplayWindowed,
        "windowed is restored");
    for (int i = 0; i < 4; ++i) passed &= expect(key(VK_DOWN), "down moves to vsync");
    passed &= expect(key(VK_RIGHT), "vsync toggles in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().vsync_interval == 1u &&
            galaxy::gx::get_render_config().vsync_interval == 0u,
        "vsync waits for apply");
    passed &= expect(key(VK_BACK) &&
                         galaxy::gx::native_settings_draft_for_tests().vsync_interval == 0u,
                     "undo restores vsync");
    passed &= expect(key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 8u,
                     "down skips the interface heading to HUD resolution");
    passed &= expect(key(VK_RIGHT) && !galaxy::gx::native_settings_draft_for_tests().hud_output_resolution,
                     "HUD resolution switches to internal in the draft");
    passed &= expect(key(VK_BACK) && galaxy::gx::native_settings_draft_for_tests().hud_output_resolution,
                     "undo restores the HUD resolution");

    // Graphics page with no upscaler and the native quality mode: 0 heading, 1 upscaling, 2 quality
    // mode, 3 internal resolution, 4 sharpening, 5 anti-aliasing, 6 MSAA, 7 heading, 8 interpolation,
    // 9 frame rate limit, 10 heading, 11 texture filtering, 12 mipmaps, 13 lighting, 14 ambient occlusion, 15 bloom,
    // 16 its intensity (enhanced), 17 heading, 18 texture packs, (19 their replacements while on), dump textures, their
    // folder, heading, movies' folder. No ray tracing section: the test machine's GPU is not offered it.
    passed &= expect(key(VK_TAB), "Tab enters the graphics page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    const unsigned texture_rows = galaxy::gx::get_render_config().texture_packs ? 7u : 6u;
    passed &= expect(snapshot.tab == 1u && snapshot.row == 1u && snapshot.row_count == 17u + texture_rows,
                     "graphics page: resolution, frame rate, detail, texture pack and movie sections");
    passed &= expect(key(VK_DOWN) && key(VK_DOWN), "down moves to the internal resolution");
    passed &= expect(key(VK_LEFT), "left lowers the internal resolution in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().efb_scale == 1u &&
            galaxy::gx::get_render_config().efb_scale == 2u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
        "a change waits in the draft until applied");
    passed &= expect(key(VK_RETURN), "Enter applies");
    passed &= expect(
        galaxy::gx::get_render_config().efb_scale == 1u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u &&
            !galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "Enter applies the scale live and needs no display confirmation");
    passed &= expect(key(VK_LEFT), "left below 1x selects matching the output");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().internal_follow_window,
        "the lowest internal resolution entry matches the output");
    passed &= expect(key(VK_BACK), "Backspace undoes the draft");
    passed &= expect(
        !galaxy::gx::native_settings_draft_for_tests().internal_follow_window &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
        "undo returns the draft to the live settings");
    for (int i = 0; i < 5; ++i) passed &= expect(key(VK_DOWN), "down moves to the frame rate limit");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row == 9u, "the heading is skipped");
    passed &= expect(key(VK_LEFT), "left lowers the frame rate limit in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().max_fps == 30.0f &&
            galaxy::gx::get_render_config().max_fps == 60.0f,
        "30 is the limit below 60 and waits for apply");
    passed &= expect(key(VK_BACK) && galaxy::gx::native_settings_draft_for_tests().max_fps == 60.0f,
                     "undo restores the frame rate");
    passed &= expect(key(VK_UP) && key(VK_RIGHT), "interpolation switches to native");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().max_fps == 0.0f &&
            galaxy::gx::native_settings_draft_for_tests().frame_generation == 0u,
        "native interpolation starts at the display's own rate");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT) &&
                         galaxy::gx::native_settings_draft_for_tests().max_fps == 165.0f,
                     "the target steps down from the 240 Hz refresh rate to 165");
    passed &= expect(key(VK_UP) && key(VK_LEFT) &&
                         galaxy::gx::native_settings_draft_for_tests().max_fps == 60.0f,
                     "interpolation off returns to 60");
    passed &= expect(key(VK_BACK), "undo the frame rate draft");

    render = galaxy::gx::get_render_config();
    render.aa_mode = 2u;
    render.smaa_quality = 2u;
    render.upscaler = 1u;
    render.sharpness = 20u;
    render.anisotropic_filtering = 8u;
    render.enhanced_mipmaps = true;
    render.per_pixel_lighting = true;
    galaxy::gx::set_render_config(render);
    passed &= expect(reopen() && page_start(), "reopening takes the new live settings");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 1u && snapshot.row_count == 18u + texture_rows,
                     "with FSR 1 and SMAA the page also has the SMAA quality row");
    passed &= expect(key(VK_LEFT), "left lowers the upscaler");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "quality mode row is adjustable");
    passed &= expect(key(VK_DOWN), "down passes the internal resolution");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "sharpening row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT), "anti-aliasing row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "SMAA quality row is adjustable");
    passed &= expect(key(VK_RIGHT), "quality stops at its last value");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "MSAA row is adjustable");
    for (int i = 0; i < 3; ++i) passed &= expect(key(VK_DOWN), "down moves to texture filtering");
    passed &= expect(key(VK_RIGHT), "texture filtering row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "mipmap row toggles");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "lighting row toggles");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "ambient occlusion row toggles");
    passed &= expect(
        galaxy::gx::get_render_config().aa_mode == 2u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 10u,
        "every graphics change waits in the draft");
    passed &= expect(key(VK_RETURN), "Enter applies the graphics changes");
    const galaxy::gx::RenderConfig applied = galaxy::gx::get_render_config();
    passed &= expect(applied.aa_mode == 1u, "AUTO steps down to SMAA");
    passed &= expect(applied.smaa_quality == 3u, "HIGH steps up to ULTRA and stops there");
    passed &= expect(applied.msaa_samples == 2u, "MSAA steps from off to 2x");
    passed &= expect(applied.upscaler == 0u, "FSR 1 steps down to off");
    passed &= expect(applied.spatial_quality == 1u && applied.temporal_quality == 1u,
                     "with no upscaler the quality mode steps NATIVE up to QUALITY; FSR 3/DLSS keep theirs");
    passed &= expect(applied.sharpness == 30u, "sharpening moves in steps of ten");
    passed &= expect(applied.anisotropic_filtering == 16u, "8x anisotropic steps up to 16x");
    passed &= expect(!applied.enhanced_mipmaps, "mipmaps switch to the original ones");
    passed &= expect(applied.per_pixel_lighting && applied.enhanced_lighting,
                     "lighting steps from per pixel to enhanced");
    passed &= expect(applied.ambient_occlusion, "ambient occlusion switches on");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "nothing is pending after apply");

    // Match output: Apply keeps the scale the window follow chose after the
    // menu opened.
    render = galaxy::gx::get_render_config();
    render.internal_follow_window = true;
    render.efb_scale = 6u;
    galaxy::gx::set_render_config(render);
    passed &= expect(reopen(), "reopen on the graphics page");
    render.efb_scale = 4u;
    galaxy::gx::set_render_config(render);
    // The cursor is on LIGHTING, at its last value (ENHANCED).
    passed &= expect(key(VK_LEFT) &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u &&
                         key(VK_RETURN),
                     "an unrelated change applies");
    passed &= expect(
        galaxy::gx::get_render_config().internal_follow_window &&
            galaxy::gx::get_render_config().efb_scale == 4u,
        "apply keeps the followed internal resolution");
    render = galaxy::gx::get_render_config();
    render.internal_follow_window = false;
    render.efb_scale = 2u;
    galaxy::gx::set_render_config(render);
    passed &= expect(reopen(), "back on the graphics page");

    galaxy::RuntimeSettings runtime{};
    runtime.input_mode = galaxy::RuntimeInputMode::Auto;
    runtime.language = galaxy::GameLanguage::English;  // chosen, not Auto: the steps below do not follow the system's
    galaxy::set_runtime_settings(runtime);
    using galaxy::input::ControlScheme;
    using galaxy::input::ControllerInfo;
    using galaxy::input::ControllerLink;
    using galaxy::input::DeviceKey;
    using galaxy::input::WiiTarget;
    galaxy::input::set_controls_config(galaxy::input::default_controls_config());
    const auto attached = [](ControlScheme scheme, std::uint8_t slot, ControllerLink link, const char* model) {
        ControllerInfo info;
        info.scheme = scheme;
        info.slot = slot;
        info.link = link;
        info.model = model;
        return info;
    };
    const std::array<ControllerInfo, 4> controllers{
        attached(ControlScheme::Xbox, 0, ControllerLink::Unknown, "Xbox controller"),
        attached(ControlScheme::SwitchPro, 0, ControllerLink::Bluetooth, "Switch Pro Controller"),
        attached(ControlScheme::PlayStation, 0, ControllerLink::Usb, "DualSense"),
        attached(ControlScheme::Steam, 1, ControllerLink::Wireless, "Steam Controller"),
    };
    galaxy::gx::native_settings_set_controllers_for_tests(controllers, DeviceKey{ControlScheme::PlayStation, 0});
    passed &= expect(key(VK_TAB), "Tab enters the controls page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 2u && snapshot.row_count == 30u,
                     "keyboard controls: source, device, 25 bindings, pointer speed, invert, reset");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: AUTO" &&
                         galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: KEYBOARD AND MOUSE",
                     "Auto first, the device row on the keyboard and mouse");
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
                     "the input source changes in the draft");
    passed &= expect(key(VK_BACK) && galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "Backspace on a setting row undoes the draft");
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::Auto,
        "the source only changes on apply");
    // Bind F to Wii A: select the row, Enter waits, the next key binds.
    passed &= expect(key(VK_DOWN) && key(VK_DOWN), "down reaches the A binding row");
    passed &= expect(key(VK_RETURN) && key('F'), "Enter waits for a key, F binds");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
                     "a captured key is a draft change");
    passed &= expect(key(VK_SPACE), "Space applies on the controls page");
    {
        const auto& a = galaxy::input::get_controls_config().table(ControlScheme::Keyboard)[WiiTarget::A];
        passed &= expect(galaxy::input::format_target_bindings(ControlScheme::Keyboard, a) ==
                             "Space, Enter, MouseLeft, F",
                         "applied binding appends to the existing inputs");
    }
    // An input two controls share shows red on both rows (both act together); it is not refused.
    passed &= expect(key(VK_DOWN) && key(VK_RETURN) && key('F') && key(VK_SPACE), "F binds to B as well");
    {
        const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
        passed &= expect(view.rows.size() > 4u && view.rows[2].shared && view.rows[3].shared && !view.rows[4].shared,
                         "the two rows sharing F are red, the others not");
    }
    passed &= expect(key(VK_UP), "back on the A row");
    passed &= expect(key(VK_RETURN) && key(VK_ESCAPE) && galaxy::gx::native_settings_overlay_visible() &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "Escape cancels a capture without closing the overlay");
    passed &= expect(key(VK_BACK) && key(VK_SPACE), "Backspace clears the row, Space applies");
    passed &= expect(
        galaxy::input::get_controls_config().table(ControlScheme::Keyboard)[WiiTarget::A][0].empty(),
        "a cleared row has no inputs");
    {
        const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
        passed &= expect(!view.rows[2].shared && !view.rows[3].shared, "no red once nothing is shared");
    }
    // Up from the first binding row wraps to RESET TO DEFAULTS at the end.
    passed &= expect(key(VK_UP) && key(VK_UP) && key(VK_UP), "up wraps to the reset row");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.row == 29u, "the last row is selected");
    passed &= expect(key(VK_RETURN) && key(VK_SPACE), "Enter on reset restores defaults; Space applies");
    passed &= expect(galaxy::input::get_controls_config() == galaxy::input::default_controls_config(),
                     "defaults restored");
    // A Joy-Con half connected: PAIR JOY-CON at the top, under the input source, the selection kept on its row.
    galaxy::gx::native_settings_set_joycons_for_tests(1u, 0u);
    (void)galaxy::gx::native_settings_view();
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "PAIR JOY-CON" &&
                         galaxy::gx::native_settings_overlay_snapshot().row_count == 31u &&
                         galaxy::gx::native_settings_overlay_snapshot().row == 30u,
                     "a Joy-Con half puts PAIR JOY-CON at the top; the selection stays on reset");
    passed &= expect(key(VK_DOWN) && key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 1u &&
                         key(VK_RETURN) && key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 1u,
                     "the Pair Joy-Con screen holds the keys");
    passed &= expect(key(VK_ESCAPE) && galaxy::gx::native_settings_overlay_visible(),
                     "Escape leaves the Pair Joy-Con screen and keeps the menu");
    galaxy::gx::native_settings_set_joycons_for_tests(0u, 0u);
    (void)galaxy::gx::native_settings_view();
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row_count == 30u &&
                         galaxy::gx::native_settings_overlay_snapshot().row == 1u,
                     "the halves gone, the row goes and the selection is on the row in its place (the device)");
    // The device row lists the attached controllers and switches the table shown.
    passed &= expect(key(VK_RIGHT), "device row selects the Xbox controller");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.row_count == 35u,
                     "Xbox adds stick and pointer deadzones, pointer hide, vibration and its strength");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: XBOX CONTROLLER",
                     "the Xbox controller is named");
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_overlay_snapshot().row_count == 39u,
                     "Switch Pro adds the pointer, gyro sensitivity, motion shake and its sensitivity");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: SWITCH PRO CONTROLLER (BT)",
                     "the Switch Pro Controller is named with its link");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: MOTION",
                     "the Switch Pro Controller points with motion");
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_overlay_snapshot().row_count == 40u,
                     "the DualSense adds the touchpad's sensitivity");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: DUALSENSE (USB)  ACTIVE",
                     "the active controller is marked");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(2u) == "A: CROSS",
                     "PlayStation bindings use its button names");
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: MOTION + TOUCHPAD" &&
                         galaxy::gx::native_settings_row_text_for_tests(35u) == "GYRO SENSITIVITY: 1.0" &&
                         galaxy::gx::native_settings_row_text_for_tests(36u) == "TOUCHPAD SENSITIVITY: 1.0",
                     "the DualSense points with motion and its touchpad, each with its sensitivity");
    {
        // The pointer row (after the 25 bindings, pointer speed, invert, the deadzones, hide and vibration): left steps
        // back through its sources, each sensitivity row with its source.
        for (unsigned i = 0; i < 33u; ++i) key(VK_DOWN);
        passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row == 34u && key(VK_LEFT) &&
                             galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: TOUCHPAD" &&
                             galaxy::gx::native_settings_row_text_for_tests(35u) == "TOUCHPAD SENSITIVITY: 1.0" &&
                             galaxy::gx::native_settings_overlay_snapshot().row_count == 39u,
                         "touchpad alone: no gyro sensitivity row");
        passed &= expect(key(VK_LEFT) && galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: MOTION" &&
                             key(VK_LEFT) && galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: STICK" &&
                             galaxy::gx::native_settings_overlay_snapshot().row_count == 38u,
                         "motion, then the stick alone: no sensitivity rows");
        passed &= expect(key(VK_BACK) && galaxy::gx::native_settings_row_text_for_tests(34u) == "POINTER: MOTION + TOUCHPAD",
                         "Backspace restores the pointer's sources");
        for (unsigned i = 0; i < 33u; ++i) key(VK_UP);
    }
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row == 1u && key(VK_RIGHT) &&
                         galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: STEAM CONTROLLER (WIRELESS)",
                     "the Steam Controller is last");
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_row_text_for_tests(1u) ==
                                          "EDIT: STEAM CONTROLLER (WIRELESS)",
                     "the list ends there");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "choosing the device to edit is not a change");
    // Hot plug: the controller being edited goes away and the keyboard takes over.
    galaxy::gx::native_settings_set_controllers_for_tests(
        std::span<const ControllerInfo>(controllers.data(), 2u), std::nullopt);
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: KEYBOARD AND MOUSE" &&
                         galaxy::gx::native_settings_overlay_snapshot().row_count == 30u,
                     "an unplugged controller is no longer offered");
    galaxy::gx::native_settings_set_controllers_for_tests(controllers, std::nullopt);
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: XBOX CONTROLLER",
                     "a plugged controller appears in the list");
    // The input source decides what the device row offers: the controllers alone, the keyboard and mouse alone.
    passed &= expect(key(VK_UP) && key(VK_RIGHT) && key(VK_RIGHT) &&
                         galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: CONTROLLER ONLY",
                     "right steps to controller only");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT) && galaxy::gx::native_settings_row_text_for_tests(1u) ==
                                                        "EDIT: XBOX CONTROLLER",
                     "controller only: the keyboard is not offered");
    galaxy::gx::native_settings_set_controllers_for_tests(std::span<const ControllerInfo>(), std::nullopt);
    passed &= expect(galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: NONE" &&
                         !galaxy::gx::native_settings_view().rows[1].changes &&
                         galaxy::gx::native_settings_overlay_snapshot().row_count == 2u,
                     "controller only without a controller: nothing to set up");
    galaxy::gx::native_settings_set_controllers_for_tests(controllers, std::nullopt);
    passed &= expect(key(VK_UP) && key(VK_LEFT) && galaxy::gx::native_settings_row_text_for_tests(0u) ==
                                                      "INPUT: KEYBOARD+MOUSE ONLY",
                     "left steps to keyboard and mouse only");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT) && galaxy::gx::native_settings_row_text_for_tests(1u) ==
                                                         "EDIT: KEYBOARD AND MOUSE",
                     "keyboard and mouse only: no controller is offered");
    passed &= expect(key(VK_UP) && key(VK_BACK) && galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: AUTO" &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "Backspace returns to Auto");
    // A Wii Remote connected: its own input mode, after the others; nothing to set up in it (its buttons are its own).
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) && key(VK_RIGHT) &&
                         galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: CONTROLLER ONLY" && key(VK_BACK),
                     "without a Wii Remote the modes end at controller only");
    galaxy::gx::native_settings_set_wii_remote_for_tests(true);
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) && key(VK_RIGHT) &&
                         galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: WII REMOTE" &&
                         galaxy::gx::native_settings_overlay_snapshot().row_count == 1u,
                     "a Wii Remote's mode: the input row alone");
    galaxy::gx::native_settings_set_joycons_for_tests(0u, 1u);
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row_count == 1u,
                     "the Wii Remote mode does not offer to pair Joy-Con");
    galaxy::gx::native_settings_set_joycons_for_tests(0u, 0u);
    // The remote goes before Apply: the mode stays as it was, and the menu says why.
    galaxy::gx::native_settings_set_wii_remote_for_tests(false);
    passed &= expect(key(VK_SPACE) && galaxy::get_runtime_settings().input_mode == galaxy::RuntimeInputMode::Auto &&
                         galaxy::gx::native_settings_view().status_text == "NO WII REMOTE CONNECTED" &&
                         galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: AUTO" &&
                         galaxy::gx::native_settings_row_text_for_tests(1u) == "EDIT: XBOX CONTROLLER",
                     "a Wii Remote gone before Apply keeps the input mode and the device being set up");
    // Controller only applied with no controller attached: refused the same way.
    galaxy::gx::native_settings_set_controllers_for_tests(std::span<const ControllerInfo>(), std::nullopt);
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) &&
                         galaxy::gx::native_settings_row_text_for_tests(0u) == "INPUT: CONTROLLER ONLY" &&
                         key(VK_SPACE) && galaxy::get_runtime_settings().input_mode == galaxy::RuntimeInputMode::Auto &&
                         galaxy::gx::native_settings_view().status_text == "NO CONTROLLER CONNECTED",
                     "controller only needs a controller attached");
    galaxy::gx::native_settings_set_controllers_for_tests(controllers, std::nullopt);
    const auto controls_layout = galaxy::gx::compute_native_settings_layout(1280u, 720u, 2u);
    passed &= expect(layout_inside(controls_layout, 1280u, 720u), "the long controls page scrolls inside the panel");
    const auto tiny_controls = galaxy::gx::compute_native_settings_layout(427u, 240u, 2u);
    passed &= expect(layout_inside(tiny_controls, 427u, 240u), "the controls page fits the compact layout");

    passed &= expect(key(VK_TAB), "Tab enters the audio page");
    runtime = galaxy::get_runtime_settings();
    runtime.audio.master = 1.0f;
    runtime.audio.music = 1.0f;
    runtime.audio.sfx = 0.65f;
    galaxy::set_runtime_settings(runtime);
    passed &= expect(key(VK_BACK), "Backspace takes the mix as it is now");
    passed &= expect(key(VK_LEFT), "left adjusts master gain");
    passed &= expect_near(galaxy::get_runtime_settings().audio.master, 1.0f, "the mix waits for apply");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u, "the gain is pending");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "music row is adjustable");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        snapshot.row_count == 7u,
        "audio page exposes master, music, SFX, voice, ambience, cutscene and Wiimote rows");
    passed &= expect(
        key(VK_DOWN) && key(VK_DOWN) && key(VK_DOWN) && key(VK_LEFT),
        "ambience row is independently adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT), "cutscene row is adjustable");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.wiimote_speaker,
        0.0f,
        "Wiimote sounds start off");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "Wiimote sounds row is adjustable");
    passed &= expect(key(VK_RETURN), "Enter applies the mix");
    passed &= expect_near(galaxy::get_runtime_settings().audio.master, 0.95f, "audio page decreases master gain");
    passed &= expect_near(galaxy::get_runtime_settings().audio.music, 1.05f, "audio page increases music gain");
    passed &= expect_near(galaxy::get_runtime_settings().audio.ambience, 0.95f, "audio page decreases ambience gain");
    passed &= expect_near(galaxy::get_runtime_settings().audio.sfx, 0.65f,
                          "audio page leaves SFX unchanged while adjusting ambience");
    passed &= expect_near(galaxy::get_runtime_settings().audio.cutscene, 0.95f, "audio page decreases cutscene gain");
    passed &= expect_near(galaxy::get_runtime_settings().audio.wiimote_speaker, 0.05f,
                          "audio page raises Wiimote sounds");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u, "nothing pending after apply");

    passed &= expect(key(VK_TAB), "Tab enters the general page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 4u && snapshot.row_count == 8u,
                     "general page has text speed, button icons, language, Discord status and Mii import (a heading, "
                     "import, count, folder)");
    // Each row's text (the draft) and the setting in force (applied).
    const auto shown = [](unsigned row) { return galaxy::gx::native_settings_row_text_for_tests(row); };
    passed &= expect(galaxy::get_runtime_settings().text_speed == 1u, "text speed starts at the game's pace");
    passed &= expect(key(VK_RIGHT) && shown(0u) == "TEXT SPEED: 2X" && galaxy::get_runtime_settings().text_speed == 1u,
                     "right raises text speed, waiting for apply");
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) && shown(0u) == "TEXT SPEED: INSTANT", "text speed reaches instant");
    passed &= expect(key(VK_RIGHT) && shown(0u) == "TEXT SPEED: INSTANT", "text speed stops at instant");
    passed &= expect(key(VK_LEFT) && key(VK_LEFT) && shown(0u) == "TEXT SPEED: 2X", "left lowers text speed");
    // A setting changed elsewhere while the menu is open keeps that value when the menu applies its own.
    runtime = galaxy::get_runtime_settings();
    const float voice = runtime.audio.voice;
    runtime.audio.voice = 0.5f;
    galaxy::set_runtime_settings(runtime);
    passed &= expect(key(VK_RETURN) && galaxy::get_runtime_settings().text_speed == 2u, "Enter applies the text speed");
    passed &= expect_near(galaxy::get_runtime_settings().audio.voice, 0.5f, "apply keeps a setting changed elsewhere");
    runtime = galaxy::get_runtime_settings();
    runtime.audio.voice = voice;
    galaxy::set_runtime_settings(runtime);
    passed &= expect(galaxy::get_runtime_settings().button_icons == galaxy::ButtonIcons::Auto, "button icons start Auto");
    passed &= expect(key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 1u, "down selects button icons");
    passed &= expect(key(VK_RIGHT) && shown(1u) == "BUTTON ICONS: WII" &&
                         galaxy::get_runtime_settings().button_icons == galaxy::ButtonIcons::Auto,
                     "right chooses the Wii's icons, waiting for apply");
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) && key(VK_RIGHT) && shown(1u) == "BUTTON ICONS: PLAYSTATION" &&
                         key(VK_RIGHT) && shown(1u) == "BUTTON ICONS: KEYBOARD AND MOUSE" && key(VK_RIGHT) &&
                         shown(1u) == "BUTTON ICONS: KEYBOARD AND MOUSE",
                     "button icons go on to the keyboard and mouse's and stop there");
    passed &= expect(key(VK_BACK) && shown(1u) == "BUTTON ICONS: AUTO (WII)" &&
                         galaxy::get_runtime_settings().button_icons == galaxy::ButtonIcons::Auto,
                     "Backspace undoes the choice; Auto names what it shows (no controller here: the Wii's)");
    passed &= expect(key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 2u, "down selects language");
    passed &= expect(galaxy::get_runtime_settings().language == galaxy::GameLanguage::English, "language starts as English");
    passed &= expect(key(VK_RIGHT) && galaxy::get_runtime_settings().language == galaxy::GameLanguage::English &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
                     "right chooses French, waiting for apply");
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT) && key(VK_LEFT) &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
                     "language steps stop at Spanish and come back to French");
    passed &= expect(key(VK_LEFT) && galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "left returns to English: nothing pending");
    passed &= expect(galaxy::get_runtime_settings().discord_status, "Discord status starts on");
    passed &= expect(key(VK_DOWN) && galaxy::gx::native_settings_overlay_snapshot().row == 3u, "down selects Discord status");
    passed &= expect(key(VK_RIGHT) && galaxy::get_runtime_settings().discord_status && key(VK_RETURN) &&
                         !galaxy::get_runtime_settings().discord_status,
                     "right turns Discord status off when applied");
    passed &= expect(key(VK_RIGHT) && key(VK_RETURN) && galaxy::get_runtime_settings().discord_status,
                     "right turns it back on when applied");
    passed &= expect(key(VK_UP) && key(VK_UP) && key(VK_UP) && galaxy::gx::native_settings_overlay_snapshot().row == 0u,
                     "up returns to text speed");
    passed &= expect(key(VK_TAB), "Tab enters the mods page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 5u && snapshot.row_count >= 3u, "mods page has its two lists and its buttons");
    passed &= expect(key(VK_TAB), "Tab enters the system page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 6u && snapshot.row_count == 8u,
                     "system page has the graphics card, six status rows and diagnostics");
    passed &= expect(snapshot.row == 0u && key(VK_RIGHT) && key(VK_LEFT) &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "the graphics card row cannot be changed in game");
    for (int i = 0; i < 7; ++i) key(VK_DOWN);
    using galaxy::gx::NativeSettingsStatus;
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().row == 7u && key(VK_SPACE) &&
                         galaxy::gx::native_settings_view().status == NativeSettingsStatus::DiagnosticsAsk,
                     "diagnostics' Record asks to restart");
    passed &= expect(key(VK_ESCAPE) && galaxy::gx::native_settings_view().status != NativeSettingsStatus::DiagnosticsAsk &&
                         galaxy::gx::native_settings_overlay_snapshot().visible,
                     "Escape answers not now and keeps the menu");
    for (int i = 0; i < 7; ++i) key(VK_UP);
    passed &= expect(key(VK_TAB), "Tab enters the advanced page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 7u && snapshot.row_count == 5u,
                     "advanced page has the galaxy select heading, galaxy, star and its two buttons");
    passed &= expect(key(VK_TAB), "Tab navigation wraps");
    passed &= expect(
        galaxy::gx::native_settings_overlay_snapshot().tab == 0u,
        "Tab wraps back to video");
    passed &= expect(
        key(VK_PRIOR) && galaxy::gx::native_settings_overlay_snapshot().tab == 7u,
        "Page Up (controller LB) selects the previous page, wrapping");
    passed &= expect(
        key(VK_NEXT) && galaxy::gx::native_settings_overlay_snapshot().tab == 0u,
        "Page Down (controller RB) selects the next page");

    const auto tiny = galaxy::gx::compute_native_settings_layout(427u, 240u, 0u);
    passed &= expect(
        layout_inside(tiny, 427u, 240u) && tiny.text_scale == 1,
        "427x240 uses a compact one-pixel-scale layout inside the backbuffer");
    passed &= expect(
        !galaxy::gx::compute_native_settings_layout(426u, 240u, 0u)
             .supported,
        "layout fails closed below the advertised compact width");
    const auto tiny_graphics =
        galaxy::gx::compute_native_settings_layout(427u, 240u, 1u);
    passed &= expect(
        layout_inside(tiny_graphics, 427u, 240u) &&
            tiny_graphics.rows_y + 9 * tiny_graphics.row_step +
                    tiny_graphics.text_scale * 7 + 8 <=
                tiny_graphics.footer_y,
        "427x240 fits the ten graphics rows above the compact footer");
    for (const auto [w, h] : {std::pair{854u, 480u}, std::pair{1280u, 720u},
                              std::pair{1280u, 480u}, std::pair{1920u, 1080u},
                              std::pair{2560u, 1440u}, std::pair{3840u, 2160u}}) {
        const auto graphics = galaxy::gx::compute_native_settings_layout(w, h, 1u);
        passed &= expect(
            graphics.supported && layout_inside(graphics, w, h) &&
                graphics.rows_y + 9 * graphics.row_step +
                        graphics.text_scale * 7 + 8 <= graphics.footer_y,
            "every window size fits the ten graphics rows above the footer");
    }
    const auto tiny_caps =
        galaxy::gx::compute_native_settings_layout(427u, 240u, 4u);
    passed &= expect(
        layout_inside(tiny_caps, 427u, 240u) &&
            tiny_caps.rows_y + 4 * tiny_caps.row_step +
                    tiny_caps.text_scale * 7 + 8 <=
                tiny_caps.footer_y &&
            tiny_caps.footer_y + 2 * 18 + 7 <=
                tiny_caps.panel_y + tiny_caps.panel_height,
        "427x240 general rows do not overlap the compact footer");
    const auto compact =
        galaxy::gx::compute_native_settings_layout(854u, 480u, 4u);
    passed &= expect(
        layout_inside(compact, 854u, 480u) &&
            compact.rows_y + 4 * compact.row_step +
                    compact.text_scale * 7 + 8 <=
                compact.footer_y &&
            compact.footer_y + 2 * 28 + 14 <=
                compact.panel_y + compact.panel_height,
        "854x480 general layout stays inside the backbuffer without footer overlap");
    const auto ultra_hd =
        galaxy::gx::compute_native_settings_layout(3840u, 2160u, 4u);
    passed &= expect(
        layout_inside(ultra_hd, 3840u, 2160u) && ultra_hd.text_scale == 4 &&
            ultra_hd.footer_y + 2 * 28 + 14 <=
                ultra_hd.panel_y + ultra_hd.panel_height,
        "4K layout scales text and remains inside the backbuffer");

    const std::filesystem::path blocking_parent = root / "not-a-directory";
    std::filesystem::create_directories(root);
    {
        std::ofstream file(blocking_parent, std::ios::binary);
        file << "block";
    }
    _putenv_s(
        "GALAXY_RENDER_CONFIG_PATH",
        (blocking_parent / "render.ini").string().c_str());
    _putenv_s(
        "GALAXY_RUNTIME_SETTINGS_PATH",
        (blocking_parent / "runtime.ini").string().c_str());
    passed &= expect(close_discarding(), "Escape closes the overlay");
    galaxy::gx::native_settings_wait_saved_for_tests();
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        !snapshot.visible && snapshot.dirty && snapshot.save_error,
        "failed atomic persistence keeps dirty state and exposes a save error");

    const std::filesystem::path recovered_render = root / "recovered-render.ini";
    const std::filesystem::path recovered_runtime = root / "recovered-runtime.ini";
    _putenv_s(
        "GALAXY_RENDER_CONFIG_PATH",
        recovered_render.string().c_str());
    _putenv_s(
        "GALAXY_RUNTIME_SETTINGS_PATH",
        recovered_runtime.string().c_str());
    passed &= expect(key(VK_F1) && key(VK_F1), "a later close retries dirty settings");
    galaxy::gx::native_settings_wait_saved_for_tests();
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        !snapshot.visible && !snapshot.dirty && !snapshot.save_error &&
            std::filesystem::is_regular_file(recovered_render) &&
            std::filesystem::is_regular_file(recovered_runtime),
        "successful retry atomically persists both files and clears error state");

    // Leaving with changes not applied: asked, Escape stays, Enter applies and closes, Backspace discards and closes.
    {
        using galaxy::gx::NativeSettingsInput;
        using galaxy::gx::NativeSettingsStatus;
        galaxy::gx::native_settings_reset_overlay_state_for_tests();
        galaxy::gx::RenderConfig base = galaxy::gx::get_render_config();
        base.vsync_interval = 0u;
        galaxy::gx::set_render_config(base);
        const auto vsync_row = [&] {
            const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
            for (unsigned i = 0; i < view.rows.size(); ++i) {
                if (view.rows[i].label == "VSYNC") return static_cast<int>(i);
            }
            return -1;
        };
        passed &= expect(key(VK_F1) && galaxy::gx::native_settings_overlay_visible(), "the menu opens for the leave checks");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, vsync_row());
        galaxy::gx::native_settings_input(NativeSettingsInput::Change, 1);
        passed &= expect(galaxy::gx::native_settings_view().status == NativeSettingsStatus::Pending &&
                             galaxy::gx::native_settings_view().status_number == 1,
                         "the pointer picks the vsync row and changes it: one change waits");
        passed &= expect(key(VK_ESCAPE) && galaxy::gx::native_settings_overlay_visible() &&
                             galaxy::gx::native_settings_view().status == NativeSettingsStatus::Leave,
                         "Escape with a change not applied asks instead of closing");
        passed &= expect(key(VK_DOWN) && key(VK_ESCAPE) && galaxy::gx::native_settings_overlay_visible() &&
                             galaxy::gx::native_settings_view().status == NativeSettingsStatus::Pending,
                         "the question waits for its answer; Escape again stays on the page");
        passed &= expect(key(VK_ESCAPE) && key(VK_RETURN) && !galaxy::gx::native_settings_overlay_visible() &&
                             galaxy::gx::get_render_config().vsync_interval != 0u,
                         "Enter applies the change and closes");
        passed &= expect(key(VK_F1), "the menu opens again");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, vsync_row());
        galaxy::gx::native_settings_input(NativeSettingsInput::Change, 1);
        galaxy::gx::native_settings_input(NativeSettingsInput::Close, 0);
        passed &= expect(galaxy::gx::native_settings_view().status == NativeSettingsStatus::Leave,
                         "the pointer's close asks too");
        passed &= expect(key(VK_BACK) && !galaxy::gx::native_settings_overlay_visible() &&
                             galaxy::gx::get_render_config().vsync_interval != 0u,
                         "Backspace discards the change and closes");

        // The view: every page, rows split into label and value, headings, and the pointer's choices.
        passed &= expect(key(VK_F1), "the menu opens for the view checks");
        galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
        passed &= expect(view.open && view.pages.size() == 8u && view.pages[0] == "DISPLAY" && view.controls_page == 2u,
                         "the view lists the seven pages and the controls page");
        passed &= expect(!view.rows.empty() && view.rows[0].heading && !view.rows[view.selected].heading,
                         "a page starts with a heading; the selection is never one");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, 0);
        passed &= expect(galaxy::gx::native_settings_view().selected == view.selected, "the pointer cannot select a heading");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 3);
        view = galaxy::gx::native_settings_view();
        bool split = view.page == 3u && view.rows.size() == 7u;
        for (const galaxy::gx::NativeSettingsViewRow& row : view.rows) {
            split = split && row.label.find(": ") == std::string::npos && !row.value.empty() && row.value.back() == '%';
        }
        passed &= expect(split, "the audio page's rows come as label and percentage");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 2);
        view = galaxy::gx::native_settings_view();
        bool controls_split = view.page == 2u && view.rows.size() > 3u;
        for (const galaxy::gx::NativeSettingsViewRow& row : view.rows) {
            controls_split = controls_split && row.label.find(": ") == std::string::npos;
        }
        passed &= expect(controls_split, "the controls page's rows come as label and value");

        // A row of buttons (the MODS page's, under its two lists): Left and Right move between them (none past either
        // end), Space and the pointer press one, the row starts at its first when selected again. RESCAN only: the
        // others open a file picker or a folder, or act on a mod.
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 5);
        int buttons = -1;
        view = galaxy::gx::native_settings_view();
        for (unsigned i = 0; i < view.rows.size(); ++i) {
            if (view.rows[i].buttons) buttons = static_cast<int>(i);
        }
        passed &= expect(view.rows.size() >= 3u && view.rows[0].heading && view.rows[0].mod_list == 1u &&
                             view.rows[0].label == "DISABLED",
                         "the MODS page starts with the list of mods off");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, buttons);
        view = galaxy::gx::native_settings_view();
        passed &= expect(buttons >= 0 &&
                             view.rows[static_cast<unsigned>(buttons)].value ==
                                 "IMPORT FILE|IMPORT FOLDER|-OPTIONS|OPEN FOLDER|RESCAN" &&
                             view.selected_button == 0u,
                         "the MODS page's buttons are the launcher's, the first selected");
        for (int i = 0; i < 5; ++i) (void)key(VK_RIGHT);
        passed &= expect(galaxy::gx::native_settings_view().selected_button == 4u,
                         "Right moves to the last button and stays there");
        passed &= expect(key(VK_SPACE) && galaxy::gx::native_settings_view().selected_button == 4u,
                         "Space presses the selected button (RESCAN)");
        passed &= expect(key(VK_LEFT) && galaxy::gx::native_settings_view().selected_button == 3u, "Left moves back");
        galaxy::gx::native_settings_input(NativeSettingsInput::Press, 4);
        passed &= expect(galaxy::gx::native_settings_view().selected_button == 4u, "the pointer's press selects its button");
        galaxy::gx::native_settings_input(NativeSettingsInput::Press, 5);
        passed &= expect(galaxy::gx::native_settings_view().selected_button == 4u, "a press past the row's buttons does nothing");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 4);
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 5);
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, buttons);
        passed &= expect(galaxy::gx::native_settings_view().selected_button == 0u, "selected again, the row starts at its first");

        // Sliders: the mix's volumes (0 to 200% in fives), the pointer sets one by its step; Left and Right still step.
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 3);
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, 0);
        passed &= expect(galaxy::gx::native_settings_view().rows[0].slider_steps == 40u, "the master volume is a slider of 40 steps");
        galaxy::gx::native_settings_input(NativeSettingsInput::SetValue, 10);
        view = galaxy::gx::native_settings_view();
        passed &= expect(view.rows[0].slider_at == 10u && view.rows[0].value == "50%", "the pointer sets it to 50%");
        passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_view().rows[0].value == "55%", "Right steps it on");
        galaxy::gx::native_settings_input(NativeSettingsInput::SetValue, 20);
        passed &= expect(galaxy::gx::native_settings_view().rows[0].value == "100%", "and back to 100%");
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 1);
        bool sharpening = false;
        for (const galaxy::gx::NativeSettingsViewRow& row : galaxy::gx::native_settings_view().rows) {
            sharpening = sharpening || (row.label == "SHARPENING" && row.slider_steps == 10u);
        }
        passed &= expect(sharpening, "sharpening is a slider of 10 steps");

        // The pointer's arrows go round at a list's ends: two steps of a two-value setting come back to where it was.
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 0);
        view = galaxy::gx::native_settings_view();
        unsigned mode_row = 0u;
        for (unsigned i = 0; i < view.rows.size(); ++i) {
            if (view.rows[i].label == "DISPLAY MODE") mode_row = i;
        }
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, static_cast<int>(mode_row));
        const std::string mode = galaxy::gx::native_settings_view().rows[mode_row].value;
        galaxy::gx::native_settings_input(NativeSettingsInput::Change, 2);
        const std::string next_mode = galaxy::gx::native_settings_view().rows[mode_row].value;
        galaxy::gx::native_settings_input(NativeSettingsInput::Change, 2);
        passed &= expect(mode_row != 0u && next_mode != mode && galaxy::gx::native_settings_view().rows[mode_row].value == mode,
                         "the display mode goes round both ways from either end");
        passed &= expect(close_discarding(), "the menu closes after the view checks");
    }

    // Every text the menu shows has its entry in the game's text table, so the game's menu shows it in every language:
    // each page's rows, every value each row steps through (with every value of each other row on its page), each line
    // under the page. Names are shown as they are; inputs ("SPACE, MOUSELEFT") are named one by one.
    {
        using galaxy::gx::NativeSettingsInput;
        std::set<std::string> ids;
        std::ifstream table(NEBULA_TEXT_TABLE);
        for (std::string line; std::getline(table, line);) {
            ids.insert(line.substr(0, line.find('\t')));
        }
        // A text without an entry of its own is named piece by piece, as the game's menu does: the words before a
        // closing "(...)" and those inside it, then its sentences, the words before ": " and after it, a list's items.
        const std::function<bool(std::string_view)> covered = [&](std::string_view text) {
            if (text.empty() || ids.count(settings_text_id(text)) != 0u) return true;
            std::size_t at = std::string_view::npos;
            std::size_t second_end = text.size();
            if (text.back() == ')' && (at = text.rfind(" (")) != std::string_view::npos) {
                second_end = text.size() - 1u;
            } else {
                for (const std::string_view separator : {". ", ": ", ", "}) {
                    if ((at = text.find(separator)) != std::string_view::npos) break;
                }
            }
            if (at == std::string_view::npos) return false;
            std::string_view first = text.substr(0, at);
            while (!first.empty() && first.back() == ' ') first.remove_suffix(1u);
            return covered(first) && covered(text.substr(at + 2u, second_end - at - 2u));
        };
        std::set<std::string> missing;
        const auto check = [&](std::string_view text) {
            if (!text.empty() && !covered(text)) missing.insert(std::string(text));
        };
        const auto check_view = [&] {
            const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
            for (const std::string& page : view.pages) check(page);
            for (const galaxy::gx::NativeSettingsViewRow& row : view.rows) {
                if (!row.label_name) {
                    check(row.label.rfind("  ", 0) == 0u ? std::string_view(row.label).substr(2u) : std::string_view(row.label));
                }
                if (row.inputs) {
                    for (std::size_t at = 0u; at < row.value.size();) {
                        const std::size_t comma = row.value.find(", ", at);
                        const std::string input =
                            row.value.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                        // Words ("Space", "MouseLeft") have entries; a key or button's own letters ("F", "LB") are
                        // shown as they are.
                        if (std::any_of(input.begin(), input.end(), [](char c) { return c >= 'a' && c <= 'z'; })) {
                            check(input);
                        }
                        at = comma == std::string::npos ? row.value.size() : comma + 2u;
                    }
                } else if (row.buttons) {
                    // Each button's word (after the '-' of one unavailable).
                    for (std::size_t at = 0u; at <= row.value.size();) {
                        const std::size_t bar = std::min(row.value.find('|', at), row.value.size());
                        const std::size_t start = at < bar && row.value[at] == '-' ? at + 1u : at;
                        const std::string word = row.value.substr(start, bar - start);
                        if (!word.empty()) check(word);
                        at = bar + 1u;
                    }
                } else if (!row.name) {
                    check(row.value);
                }
            }
            check(view.help);
            check(view.status_text);
        };
        // The rows that change a setting (not one that opens a folder, imports, travels or waits for an input), found by
        // their labels as rows come and go.
        const auto find_row = [](const std::string& label) {
            const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
            for (unsigned i = 0; i < view.rows.size(); ++i) {
                if (view.rows[i].label == label) return static_cast<int>(i);
            }
            return -1;
        };
        const auto steps = [](const galaxy::gx::NativeSettingsViewRow& row) {
            return row.changes && !row.heading && !row.inputs && !row.buttons && row.label.rfind("RESET ", 0) != 0u;
        };
        const auto step_through = [&](const std::string& label, const std::function<void()>& each) {
            int row = find_row(label);
            if (row < 0) return;
            galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, row);
            const std::string first = galaxy::gx::native_settings_view().rows[static_cast<unsigned>(row)].value;
            for (int n = 0; n < 40; ++n) {
                galaxy::gx::native_settings_input(NativeSettingsInput::Change, 1);
                each();
                row = find_row(label);
                if (row < 0 || galaxy::gx::native_settings_view().rows[static_cast<unsigned>(row)].value == first) break;
                galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, row);
            }
        };
        galaxy::gx::native_settings_reset_overlay_state_for_tests();
        // Every kind of controller, for the controls page's rows and button names.
        const std::array<ControllerInfo, 7> every_controller{
            attached(ControlScheme::Xbox, 0, ControllerLink::Usb, "Xbox controller"),
            attached(ControlScheme::SwitchPro, 0, ControllerLink::Bluetooth, "Switch Pro Controller"),
            attached(ControlScheme::SwitchPro2, 0, ControllerLink::Usb, "Switch 2 Pro Controller"),
            attached(ControlScheme::PlayStation, 0, ControllerLink::Usb, "DualSense"),
            attached(ControlScheme::PlayStation, 1, ControllerLink::Bluetooth, "DualShock 4"),
            attached(ControlScheme::Steam, 0, ControllerLink::Wireless, "Steam Controller"),
            attached(ControlScheme::Generic, 0, ControllerLink::Usb, "Gamepad"),
        };
        galaxy::gx::native_settings_set_controllers_for_tests(every_controller, DeviceKey{ControlScheme::Xbox, 0});
        // A Joy-Con half and a Wii Remote: PAIR JOY-CON and every input mode.
        galaxy::gx::native_settings_set_joycons_for_tests(1u, 0u);
        galaxy::gx::native_settings_set_wii_remote_for_tests(true);
        passed &= expect(key(VK_F1), "the menu opens for the text checks");
        for (unsigned page = 0; page < 7u; ++page) {
            galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, static_cast<int>(page));
            check_view();
            std::vector<std::string> labels;
            for (const galaxy::gx::NativeSettingsViewRow& row : galaxy::gx::native_settings_view().rows) {
                if (steps(row)) labels.push_back(row.label);
            }
            for (const std::string& outer : labels) {
                step_through(outer, [&] {
                    check_view();
                    for (const std::string& inner : labels) {
                        if (inner != outer) step_through(inner, check_view);
                    }
                });
            }
            galaxy::gx::native_settings_input(NativeSettingsInput::Revert, 0);
        }
        // The controls page with no controller attached (the device row's none in controller only), and the
        // messages its Apply gives.
        galaxy::gx::native_settings_set_controllers_for_tests(std::span<const ControllerInfo>(), std::nullopt);
        galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, 2);
        step_through(galaxy::gx::native_settings_view().rows[0].label, check_view);
        for (const char* message : {"NO CONTROLLER CONNECTED", "NO WII REMOTE CONNECTED", "THAT INPUT CANNOT DRIVE THIS ROW",
                                    "COULD NOT RESTART"}) {
            check(message);
        }
        passed &= expect(close_discarding(), "the menu closes after the text checks");
        for (const std::string& text : missing) {
            std::cerr << "no text table entry: " << settings_text_id(text) << "\t" << text << '\n';
        }
        passed &= expect(missing.empty(), "every text the settings menu shows has a text table entry");
    }

    // Every row the pointer changes answers it on every page: a list's arrows step it, going round at its ends (on and
    // back again comes back to its value), and a slider takes its first and last steps and returns to its own.
    {
        using galaxy::gx::NativeSettingsInput;
        galaxy::gx::native_settings_reset_overlay_state_for_tests();
        passed &= expect(key(VK_F1), "the menu opens for the pointer checks");
        unsigned lists = 0u;
        unsigned sliders = 0u;
        const std::size_t pages = galaxy::gx::native_settings_view().pages.size();
        for (unsigned page = 0; page < pages; ++page) {
            galaxy::gx::native_settings_input(NativeSettingsInput::SelectPage, static_cast<int>(page));
            for (unsigned i = 0; i < galaxy::gx::native_settings_view().rows.size(); ++i) {
                const galaxy::gx::NativeSettingsViewRow row = galaxy::gx::native_settings_view().rows[i];
                if (!row.changes || row.heading || row.inputs || row.buttons || row.mod_list != 0u) continue;
                galaxy::gx::native_settings_input(NativeSettingsInput::SelectRow, static_cast<int>(i));
                // The row again (a step may add or remove rows after it: then an empty one fails the check).
                const auto now = [i] {
                    const galaxy::gx::NativeSettingsView view = galaxy::gx::native_settings_view();
                    return i < view.rows.size() ? view.rows[i] : galaxy::gx::NativeSettingsViewRow{};
                };
                if (row.slider_steps != 0u) {
                    galaxy::gx::native_settings_input(NativeSettingsInput::SetValue, 0);
                    const unsigned first = now().slider_at;
                    galaxy::gx::native_settings_input(NativeSettingsInput::SetValue, static_cast<int>(row.slider_steps));
                    const unsigned last = now().slider_at;
                    galaxy::gx::native_settings_input(NativeSettingsInput::SetValue, static_cast<int>(row.slider_at));
                    passed &= expect(first == 0u && last == row.slider_steps && now().value == row.value,
                                     (row.label + ": the slider reaches both ends and returns").c_str());
                    ++sliders;
                } else {
                    galaxy::gx::native_settings_input(NativeSettingsInput::Change, 2);
                    galaxy::gx::native_settings_input(NativeSettingsInput::Change, -2);
                    passed &= expect(now().label == row.label && now().value == row.value,
                                     (row.label + ": its arrows go on and back to " + row.value).c_str());
                    ++lists;
                }
            }
            galaxy::gx::native_settings_input(NativeSettingsInput::Revert, 0);
        }
        std::cerr << "pointer checks: " << lists << " lists, " << sliders << " sliders\n";
        passed &= expect(lists >= 20u && sliders >= 8u, "every page's lists and sliders were checked");
        passed &= expect(close_discarding(), "the menu closes after the pointer checks");
    }

    // The checks above applied a change: its settings files are written in the background.
    galaxy::gx::native_settings_wait_saved_for_tests();
    galaxy::gx::native_settings_reset_overlay_state_for_tests();
    std::error_code removed;
    std::filesystem::remove_all(root, removed);
    if (!passed) {
        return 1;
    }
    std::cout << "Native settings overlay tests passed\n";
    return 0;
}
