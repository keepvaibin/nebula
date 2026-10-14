#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/temporal_upscaler.h"
#include "galaxy/input_bindings.h"
#include "galaxy/runtime_settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
    const ScopedEnv ui_env("GALAXY_NATIVE_SETTINGS_UI");
    const ScopedEnv render_path_env("GALAXY_RENDER_CONFIG_PATH");
    const ScopedEnv runtime_path_env("GALAXY_RUNTIME_SETTINGS_PATH");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("galaxy_native_settings_overlay_tests_" +
         std::to_string(GetCurrentProcessId()));
    const std::filesystem::path initial_render = root / "initial-render.ini";
    const std::filesystem::path initial_runtime = root / "initial-runtime.ini";
    std::filesystem::remove_all(root);
    _putenv_s("GALAXY_NATIVE_SETTINGS_UI", "1");
    _putenv_s(
        "GALAXY_RENDER_CONFIG_PATH",
        initial_render.string().c_str());
    _putenv_s(
        "GALAXY_RUNTIME_SETTINGS_PATH",
        initial_runtime.string().c_str());

    galaxy::gx::native_settings_reset_overlay_state_for_tests();
    auto snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(!snapshot.visible, "overlay starts hidden");
    passed &= expect(!snapshot.dirty, "overlay starts clean");
    passed &= expect(!snapshot.save_error, "overlay starts without a save error");
    passed &= expect(snapshot.tab_count == 5u, "display/graphics/controls/audio/caps tabs exist");
    passed &= expect(snapshot.tab == 0u && snapshot.row == 0u, "display row zero is selected");
    passed &= expect(
        snapshot.row_count == 8u,
        "display tab has monitor, mode, resolution, refresh rate, aspect ratio, "
        "internal resolution, frame rate and vsync");

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

    // Display page: rows edit a draft; Enter applies, Backspace undoes.
    const auto reopen = [&] {
        return key(VK_ESCAPE) && key(VK_F1) &&
               galaxy::gx::native_settings_overlay_visible();
    };
    galaxy::gx::RenderConfig render{};
    render.efb_scale = 2u;
    render.internal_follow_window = false;
    render.display_mode = galaxy::gx::kDisplayWindowed;
    render.aspect_mode = 0u;
    render.vsync_interval = 0u;
    galaxy::gx::set_render_config(render);
    galaxy::gx::stage_next_start_efb_scale(render.efb_scale);
    passed &= expect(reopen(), "reopening takes a fresh draft");
    for (int i = 0; i < 5; ++i) passed &= expect(key(VK_DOWN), "down moves to internal resolution");
    passed &= expect(key(VK_LEFT), "left lowers the internal resolution in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().efb_scale == 1u &&
            galaxy::gx::get_render_config().efb_scale == 2u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 1u,
        "a change waits in the draft until applied");
    passed &= expect(key(VK_RETURN), "Enter applies");
    passed &= expect(
        galaxy::gx::get_render_config().efb_scale == 1u &&
            galaxy::gx::get_next_start_efb_scale() == 1u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u &&
            !galaxy::gx::native_settings_overlay_snapshot().confirming_display,
        "Enter applies the scale live, stages it and needs no display confirmation");
    passed &= expect(key(VK_LEFT), "left below 1x selects matching the output");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().internal_follow_window,
        "the lowest internal resolution entry matches the output");
    passed &= expect(key(VK_BACK), "Backspace undoes the draft");
    passed &= expect(
        !galaxy::gx::native_settings_draft_for_tests().internal_follow_window &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
        "undo returns the draft to the live settings");

    passed &= expect(key(VK_UP), "up moves to the aspect ratio");
    passed &= expect(key(VK_RIGHT), "right selects 4:3");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().window_width == 640u &&
            galaxy::gx::native_settings_draft_for_tests().window_height == 480u,
        "4:3 snaps the 854 x 480 window to 640 x 480");
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
    for (int i = 0; i < 5; ++i) passed &= expect(key(VK_DOWN), "down moves to the frame rate");
    passed &= expect(key(VK_LEFT), "left lowers the frame rate in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().max_fps == 30.0f &&
            galaxy::gx::get_render_config().max_fps == 60.0f,
        "30 is the frame rate below 60 and waits for apply");
    passed &= expect(key(VK_BACK) && galaxy::gx::native_settings_draft_for_tests().max_fps == 60.0f,
                     "undo restores the frame rate");
    passed &= expect(key(VK_DOWN), "down moves to vsync");
    passed &= expect(key(VK_RIGHT), "vsync toggles in the draft");
    passed &= expect(
        galaxy::gx::native_settings_draft_for_tests().vsync_interval == 1u &&
            galaxy::gx::get_render_config().vsync_interval == 0u,
        "vsync waits for apply");
    passed &= expect(key(VK_BACK) &&
                         galaxy::gx::native_settings_draft_for_tests().vsync_interval == 0u,
                     "undo restores vsync");

    render = galaxy::gx::get_render_config();
    render.aa_mode = 2u;
    render.smaa_quality = 2u;
    render.upscaler = 1u;
    render.sharpness = 20u;
    render.anisotropic_filtering = 8u;
    render.enhanced_mipmaps = true;
    render.per_pixel_lighting = true;
    render.hud_output_resolution = true;
    galaxy::gx::set_render_config(render);
    passed &= expect(reopen(), "reopening takes the new live settings");
    passed &= expect(key(VK_TAB), "Tab enters the graphics page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    const bool fsr3 = galaxy::gx::fsr3_available();
    passed &= expect(snapshot.tab == 1u && snapshot.row_count == (fsr3 ? 9u : 8u),
                     "graphics page has AA, AA quality, upscaler, FSR 3 quality (only with FSR 3), "
                     "sharpening, textures, mipmaps, lighting and 2D layer");
    passed &= expect(key(VK_LEFT), "left lowers the anti-aliasing mode");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "AA quality row is adjustable");
    passed &= expect(key(VK_RIGHT), "quality stops at its last value");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT), "upscaler row is adjustable");
    if (fsr3) passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "FSR 3 quality row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "sharpening row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "texture filtering row is adjustable");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "mipmap row toggles");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "lighting row toggles");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "2D layer row toggles");
    passed &= expect(
        galaxy::gx::get_render_config().aa_mode == 2u &&
            galaxy::gx::native_settings_overlay_snapshot().pending_changes == (fsr3 ? 9u : 8u),
        "every graphics change waits in the draft");
    passed &= expect(key(VK_RETURN), "Enter applies the graphics changes");
    const galaxy::gx::RenderConfig applied = galaxy::gx::get_render_config();
    passed &= expect(applied.aa_mode == 1u, "AUTO steps down to SMAA");
    passed &= expect(applied.smaa_quality == 3u, "HIGH steps up to ULTRA and stops there");
    passed &= expect(applied.upscaler == 0u, "FSR 1 steps down to bilinear");
    passed &= expect(applied.temporal_quality == (fsr3 ? 2u : 1u),
                     "FSR 3 QUALITY steps up to BALANCED, or stays without FSR 3");
    passed &= expect(applied.sharpness == 30u, "sharpening moves in steps of ten");
    passed &= expect(applied.anisotropic_filtering == 16u, "8x anisotropic steps up to 16x");
    passed &= expect(!applied.enhanced_mipmaps, "mipmaps switch to the original ones");
    passed &= expect(!applied.per_pixel_lighting, "lighting switches to per vertex");
    passed &= expect(!applied.hud_output_resolution, "2D layer switches to the internal resolution");
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
    passed &= expect(key(VK_RIGHT) &&
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
    galaxy::set_runtime_settings(runtime);
    using galaxy::input::ControlScheme;
    using galaxy::input::WiiTarget;
    galaxy::input::set_controls_config(galaxy::input::default_controls_config());
    passed &= expect(key(VK_TAB), "Tab enters the controls page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        snapshot.tab == 2u && snapshot.row_count == 30u,
        "keyboard controls: source, scheme, 25 bindings, pointer speed, invert, reset");
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
    passed &= expect(key(VK_RETURN) && key(VK_ESCAPE) && galaxy::gx::native_settings_overlay_visible() &&
                         galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "Escape cancels a capture without closing the overlay");
    passed &= expect(key(VK_BACK) && key(VK_SPACE), "Backspace clears the row, Space applies");
    passed &= expect(
        galaxy::input::get_controls_config().table(ControlScheme::Keyboard)[WiiTarget::A][0].empty(),
        "a cleared row has no inputs");
    // Up from the first binding row wraps to RESET TO DEFAULTS at the end.
    passed &= expect(key(VK_UP) && key(VK_UP) && key(VK_UP), "up wraps to the reset row");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.row == 29u, "the last row is selected");
    passed &= expect(key(VK_RETURN) && key(VK_SPACE), "Enter on reset restores defaults; Space applies");
    passed &= expect(galaxy::input::get_controls_config() == galaxy::input::default_controls_config(),
                     "defaults restored");
    // The scheme row switches the table shown.
    passed &= expect(key(VK_DOWN) && key(VK_DOWN) && key(VK_RIGHT), "scheme row selects Xbox");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.row_count == 35u,
                     "Xbox adds stick and pointer deadzones, pointer hide, vibration and its strength");
    passed &= expect(key(VK_RIGHT) && galaxy::gx::native_settings_overlay_snapshot().row_count == 39u,
                     "Switch Pro adds motion, sensitivity, gyro and gyro sensitivity rows");
    passed &= expect(galaxy::gx::native_settings_overlay_snapshot().pending_changes == 0u,
                     "choosing the scheme to edit is not a change");
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
    passed &= expect(key(VK_LEFT), "left adjusts master gain");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.master,
        0.95f,
        "audio page decreases master gain");
    passed &= expect(key(VK_DOWN) && key(VK_RIGHT), "music row is adjustable");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.music,
        1.05f,
        "audio page increases music gain");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        snapshot.row_count == 6u,
        "audio page exposes master, music, SFX, voice, ambience, and cutscene rows");
    passed &= expect(
        key(VK_DOWN) && key(VK_DOWN) && key(VK_DOWN) && key(VK_LEFT),
        "ambience row is independently adjustable");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.ambience,
        0.95f,
        "audio page decreases ambience gain");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.sfx,
        0.65f,
        "audio page leaves SFX unchanged while adjusting ambience");
    passed &= expect(key(VK_DOWN) && key(VK_LEFT), "cutscene row is adjustable");
    passed &= expect_near(
        galaxy::get_runtime_settings().audio.cutscene,
        0.95f,
        "audio page decreases cutscene gain");

    passed &= expect(key(VK_TAB), "Tab enters the capability page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 4u && snapshot.row_count == 6u, "capability page has six status rows");
    passed &= expect(key(VK_TAB), "Tab navigation wraps");
    passed &= expect(
        galaxy::gx::native_settings_overlay_snapshot().tab == 0u,
        "Tab wraps back to video");
    passed &= expect(
        key(VK_PRIOR) && galaxy::gx::native_settings_overlay_snapshot().tab == 4u,
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
        "427x240 capability rows do not overlap the compact footer");
    const auto compact =
        galaxy::gx::compute_native_settings_layout(854u, 480u, 4u);
    passed &= expect(
        layout_inside(compact, 854u, 480u) &&
            compact.rows_y + 4 * compact.row_step +
                    compact.text_scale * 7 + 8 <=
                compact.footer_y &&
            compact.footer_y + 2 * 28 + 14 <=
                compact.panel_y + compact.panel_height,
        "854x480 capability layout stays inside the backbuffer without footer overlap");
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
    passed &= expect(key(VK_ESCAPE), "Escape closes the overlay");
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
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(
        !snapshot.visible && !snapshot.dirty && !snapshot.save_error &&
            std::filesystem::is_regular_file(recovered_render) &&
            std::filesystem::is_regular_file(recovered_runtime),
        "successful retry atomically persists both files and clears error state");

    galaxy::gx::native_settings_reset_overlay_state_for_tests();
    std::filesystem::remove_all(root);
    if (!passed) {
        return 1;
    }
    std::cout << "Native settings overlay tests passed\n";
    return 0;
}
