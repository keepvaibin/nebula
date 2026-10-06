#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/render_config.h"
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
    passed &= expect(snapshot.tab_count == 4u, "video/input/audio/caps tabs exist");
    passed &= expect(snapshot.tab == 0u && snapshot.row == 0u, "video row zero is selected");
    passed &= expect(
        snapshot.row_count == 2u,
        "video tab exposes restart scale and the read-only fixed present profile");

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

    galaxy::gx::RenderConfig render{};
    render.efb_scale = 3u;
    render.vsync_interval = 0u;
    render.max_fps = 60.0f;
    galaxy::gx::set_render_config(render);
    galaxy::gx::stage_next_start_efb_scale(render.efb_scale);
    passed &= expect(key(VK_DOWN), "down selects the present profile row");
    passed &= expect(key(VK_RIGHT), "read-only present row still consumes navigation");
    passed &= expect(
        galaxy::gx::get_render_config().vsync_interval == 0u &&
            galaxy::gx::get_render_config().max_fps == 60.0f &&
            !galaxy::gx::native_settings_overlay_snapshot().dirty,
        "fixed Release present profile cannot be mutated in-game");
    passed &= expect(key(VK_UP), "up returns to the scale row");
    passed &= expect(key(VK_RIGHT), "right adjusts the selected scale row");
    passed &= expect(
        galaxy::gx::get_render_config().efb_scale == 3u &&
            galaxy::gx::get_next_start_efb_scale() == 4u &&
            galaxy::gx::native_settings_overlay_snapshot().dirty,
        "scale stages 4x for restart without changing live 3x GPU resources");
    passed &= expect(key(VK_RIGHT) && key(VK_RIGHT),
                     "scale selector reaches the opt-in 6x setting");
    passed &= expect(
        galaxy::gx::get_render_config().efb_scale == 3u &&
            galaxy::gx::get_next_start_efb_scale() == 6u,
        "opt-in 6x remains staged until the renderer restarts");

    galaxy::RuntimeSettings runtime{};
    runtime.input_mode = galaxy::RuntimeInputMode::KeyboardMouse;
    galaxy::set_runtime_settings(runtime);
    passed &= expect(key(VK_TAB), "Tab enters the input page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 1u && snapshot.row_count == 1u, "input page is read-only");
    passed &= expect(key(VK_RIGHT), "input-page adjustment is consumed");
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::KeyboardMouse,
        "in-game UI cannot select an absent controller or physical Wii Remote");

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
        snapshot.row_count == 5u,
        "audio page exposes master, music, SFX, voice, and ambience rows");
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

    passed &= expect(key(VK_TAB), "Tab enters the capability page");
    snapshot = galaxy::gx::native_settings_overlay_snapshot();
    passed &= expect(snapshot.tab == 3u && snapshot.row_count == 5u, "capability page has five status rows");
    passed &= expect(key(VK_TAB), "Tab navigation wraps");
    passed &= expect(
        galaxy::gx::native_settings_overlay_snapshot().tab == 0u,
        "Tab wraps back to video");

    const auto tiny = galaxy::gx::compute_native_settings_layout(427u, 240u, 0u);
    passed &= expect(
        layout_inside(tiny, 427u, 240u) && tiny.text_scale == 1,
        "427x240 uses a compact one-pixel-scale layout inside the backbuffer");
    passed &= expect(
        !galaxy::gx::compute_native_settings_layout(426u, 240u, 0u)
             .supported,
        "layout fails closed below the advertised compact width");
    const auto tiny_caps =
        galaxy::gx::compute_native_settings_layout(427u, 240u, 3u);
    passed &= expect(
        layout_inside(tiny_caps, 427u, 240u) &&
            tiny_caps.rows_y + 4 * tiny_caps.row_step +
                    tiny_caps.text_scale * 7 + 8 <=
                tiny_caps.footer_y &&
            tiny_caps.footer_y + 2 * 18 + 7 <=
                tiny_caps.panel_y + tiny_caps.panel_height,
        "427x240 capability rows do not overlap the compact footer");
    const auto compact =
        galaxy::gx::compute_native_settings_layout(854u, 480u, 3u);
    passed &= expect(
        layout_inside(compact, 854u, 480u) &&
            compact.rows_y + 4 * compact.row_step +
                    compact.text_scale * 7 + 8 <=
                compact.footer_y &&
            compact.footer_y + 2 * 28 + 14 <=
                compact.panel_y + compact.panel_height,
        "854x480 capability layout stays inside the backbuffer without footer overlap");
    const auto ultra_hd =
        galaxy::gx::compute_native_settings_layout(3840u, 2160u, 3u);
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
