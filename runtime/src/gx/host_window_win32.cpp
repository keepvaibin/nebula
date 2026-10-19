// SPDX-License-Identifier: GPL-3.0-only
// The game's Win32 window on both renderers (host_window_win32.h): the window procedure, the pointer, the settings
// overlay and HOME Menu (state, keys, drawing through a renderer's UiCanvas) and the host API that reads them.

#include "galaxy/gx/host_window_win32.h"

#include "galaxy/gx/display_modes.h"
#include "galaxy/gx/post_process_settings.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_host.h"
#include "galaxy/runtime_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::gx {

namespace host_window {

UINT backbuffer_width = 1280;
UINT backbuffer_height = 720;
std::atomic<double> measured_present_hz{0.0};
std::string native_settings_content_root;
std::atomic<std::uintptr_t> settings_window{0};
std::atomic<bool> borderless_active{false};
std::atomic<bool> exclusive_fullscreen_active{false};
std::atomic<unsigned> efb_scale_rejected{0u};
std::atomic<std::uintptr_t> pointer_hwnd{0};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0};
std::atomic<bool> follow_window_rearm{false};

}  // namespace host_window

using host_window::display_efb_scale_cap;
using host_window::flush_native_settings_if_dirty;
using host_window::poll_render_config_file;
using host_window::hidden_window_mode_enabled;
using host_window::windowed_rect_on_monitor;
using host_window::wnd_proc;
using host_window::kSettingsKeyMessage;
using host_window::kWindowStyle;
using host_window::kPlaceWindowMessage;
using host_window::WindowPlacement;

namespace {

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime);


std::atomic<bool> s_pointer_focused{false};
galaxy::host::HostPointerSnapshotCache s_pointer_snapshot;
std::atomic<int> s_pointer_last_cursor_poll_x{std::numeric_limits<int>::min()};
std::atomic<int> s_pointer_last_cursor_poll_y{std::numeric_limits<int>::min()};
std::atomic<bool> s_pointer_left_button{false};
std::atomic<bool> s_pointer_right_button{false};
std::atomic<bool> s_pointer_focus_valid{false};
galaxy::host::HostPointerEventRing s_pointer_transitions;
bool s_tracking_mouse_leave = false;
std::atomic<bool> s_system_menu_active{false};
std::recursive_mutex s_native_settings_mutex;

std::atomic<bool> s_native_settings_visible{false};
// The HOME Menu (home_menu_open): its row and the choice the game polls (-1 none yet).
std::atomic<bool> s_home_menu_visible{false};
std::atomic<unsigned> s_home_menu_row{0};
std::atomic<int> s_home_menu_choice{-1};
constexpr unsigned kHomeMenuRows = 3;
// Rows top to bottom and the choice each makes (the console menu's order: close, Wii Menu, reset).
constexpr const char* kHomeMenuRowText[kHomeMenuRows] = {"RETURN TO GAME", "WII MENU", "RESET"};
constexpr int kHomeMenuRowChoice[kHomeMenuRows] = {0, 1, 2};

void choose_home_menu_row(unsigned row) {
    s_home_menu_choice.store(kHomeMenuRowChoice[row % kHomeMenuRows], std::memory_order_release);
    s_home_menu_visible.store(false, std::memory_order_release);
}

// Keys while the HOME Menu is open (controllers arrive as these keys too: D-pad, A as Right, Y as Return, B as
// Escape). Every key is the menu's: the game and the settings overlay wait.
bool handle_home_menu_key(WPARAM key, bool was_down) {
    if (was_down && key != VK_UP && key != VK_DOWN) {
        return true;
    }
    const unsigned row = s_home_menu_row.load(std::memory_order_relaxed);
    switch (key) {
    case VK_UP:
        s_home_menu_row.store((row + kHomeMenuRows - 1u) % kHomeMenuRows, std::memory_order_relaxed);
        break;
    case VK_DOWN:
        s_home_menu_row.store((row + 1u) % kHomeMenuRows, std::memory_order_relaxed);
        break;
    case VK_RETURN:
    case VK_RIGHT:
    case VK_SPACE:
        choose_home_menu_row(row);
        break;
    case VK_ESCAPE:
    case VK_BACK:
    case VK_HOME:
        choose_home_menu_row(0u);
        break;
    default:
        break;
    }
    return true;
}
std::atomic<bool> s_native_settings_dirty{false};
std::atomic<bool> s_native_settings_save_error{false};
std::atomic<unsigned> s_native_settings_tab{0};
std::atomic<unsigned> s_native_settings_row{0};

// Settings menu draft. Display and graphics rows edit s_settings_draft; Enter
// (Y on a controller) applies it, Backspace (X) reverts it and closing the
// menu discards it. Audio rows change the live mix. A display change
// (monitor, mode, resolution, refresh rate) stays only when it is kept within
// kDisplayConfirmMs. All guarded by s_native_settings_mutex.
RenderConfig s_settings_draft{};
std::vector<MonitorInfo> s_settings_monitors;
std::uint64_t s_display_confirm_deadline_ms = 0u;
RenderConfig s_display_confirm_previous{};
std::uint64_t s_settings_applied_ms = 0u;
constexpr std::uint64_t kDisplayConfirmMs = 15000u;

// CONTROLS page (tab 2): a draft of the bindings/tuning and of the input
// source, applied together with the other pages. The edited scheme only
// selects which table the rows show. A binding row waiting for an input is
// s_capture_target (kWiiTargetCount: none). Guarded by s_native_settings_mutex.
constexpr unsigned kControlsTab = 2u;
galaxy::input::ControlsConfig s_controls_draft = galaxy::input::default_controls_config();
RuntimeInputMode s_input_mode_draft = RuntimeInputMode::Auto;
galaxy::input::ControlScheme s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
std::size_t s_capture_target = galaxy::input::kWiiTargetCount;
std::uint64_t s_capture_deadline_ms = 0u;
constexpr std::uint64_t kCaptureTimeoutMs = 6000u;
std::string s_controls_message;
std::uint64_t s_controls_message_until_ms = 0u;
unsigned s_settings_first_row = 0u;
// The window thread's left/right-resolved code for the key being handled.
std::uint32_t s_handled_key_normalized = 0u;


constexpr unsigned kNativeSettingsTabCount = 5;

// The GENERAL page's languages, in the order LEFT/RIGHT steps through them.
constexpr GameLanguage kGameLanguages[] = {GameLanguage::English, GameLanguage::French, GameLanguage::Spanish};

// Each language in its own name; the UTF-8 Ç and Ñ are drawn by ui_text.
const char* game_language_name(GameLanguage language) {
    switch (language) {
    case GameLanguage::French: return "FRAN\xC3\x87" "AIS";
    case GameLanguage::Spanish: return "ESPA\xC3\x91" "OL";
    case GameLanguage::English: break;
    }
    return "ENGLISH";
}
// Rows the panel shows at once; longer pages scroll.
constexpr unsigned kNativeSettingsVisibleRows = 9u;

unsigned native_settings_first_row(unsigned first, unsigned selected, unsigned count) {
    if (count <= kNativeSettingsVisibleRows) return 0u;
    if (selected < first) first = selected;
    if (selected >= first + kNativeSettingsVisibleRows) first = selected + 1u - kNativeSettingsVisibleRows;
    return std::min(first, count - kNativeSettingsVisibleRows);
}

bool native_settings_ui_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_NATIVE_SETTINGS_UI") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}


float step_gain(float current, int delta) {
    const float snapped = std::round(current * 20.0f) / 20.0f;
    return std::clamp(snapped + static_cast<float>(delta) * 0.05f, 0.0f, 2.0f);
}

bool display_settings_differ(const RenderConfig& a, const RenderConfig& b) {
    return a.display_mode != b.display_mode || a.monitor != b.monitor ||
        a.fullscreen_width != b.fullscreen_width ||
        a.fullscreen_height != b.fullscreen_height ||
        a.fullscreen_refresh_hz != b.fullscreen_refresh_hz ||
        (a.display_mode == kDisplayWindowed &&
         (a.window_width != b.window_width || a.window_height != b.window_height));
}

const MonitorInfo* settings_monitor(const RenderConfig& render);

// The refresh rate the picture will be shown at with these settings.
unsigned settings_refresh_hz(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayExclusive && render.fullscreen_refresh_hz != 0u) {
        return render.fullscreen_refresh_hz;
    }
    return monitor != nullptr ? monitor->current.refresh_hz : 0u;
}

const MonitorInfo* settings_monitor(const RenderConfig& render) {
    return find_monitor(s_settings_monitors, widen_utf8(render.monitor.data()));
}

// The picture size the settings target: the fullscreen mode, the monitor
// (borderless) or the window.
std::pair<unsigned, unsigned> settings_output_size(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    const unsigned desktop_w = monitor ? monitor->current.width : host_window::backbuffer_width;
    const unsigned desktop_h = monitor ? monitor->current.height : host_window::backbuffer_height;
    switch (render.display_mode) {
    case kDisplayExclusive:
        return render.fullscreen_width != 0u && render.fullscreen_height != 0u
            ? std::pair{render.fullscreen_width, render.fullscreen_height}
            : std::pair{desktop_w, desktop_h};
    case kDisplayBorderless:
        return {desktop_w, desktop_h};
    default:
        return {render.window_width, render.window_height};
    }
}

// The largest internal resolution the selected display can show.
unsigned settings_scale_cap(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    unsigned width = monitor ? monitor->current.width : host_window::backbuffer_width;
    unsigned height = monitor ? monitor->current.height : host_window::backbuffer_height;
    if (render.display_mode == kDisplayExclusive && render.fullscreen_width != 0u) {
        width = render.fullscreen_width;
        height = render.fullscreen_height;
    }
    return efb_scale_covering(width, height, kMaxEfbScale);
}

// Window client sizes that fit the monitor's work area, smallest first: those
// of a fixed aspect (aspect_window_sizes), or common sizes and the monitor's
// own resolutions.
std::vector<std::pair<unsigned, unsigned>> settings_window_sizes(
    const MonitorInfo* monitor, unsigned aspect_mode) {
    static constexpr std::pair<unsigned, unsigned> kCommon[] = {
        {854u, 480u}, {1024u, 768u}, {1280u, 720u}, {1280u, 800u}, {1366u, 768u},
        {1440u, 900u}, {1600u, 900u}, {1680u, 1050u}, {1920u, 1080u}, {1920u, 1200u},
        {2560u, 1080u}, {2560u, 1440u}, {2560u, 1600u}, {3440u, 1440u}, {3840u, 1080u},
        {3840u, 2160u}, {5120u, 1440u}, {5120u, 2160u}, {5120u, 2880u}, {7680u, 4320u}};
    long max_w = 7680;
    long max_h = 4320;
    if (monitor != nullptr) {
        RECT frame{0, 0, 0, 0};
        const UINT dpi = std::max<UINT>(96u, GetDpiForSystem());
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, dpi);
        max_w = (monitor->work_area.right - monitor->work_area.left) - (frame.right - frame.left);
        max_h = (monitor->work_area.bottom - monitor->work_area.top) - (frame.bottom - frame.top);
    }
    const unsigned aspect = std::min(aspect_mode, kAspectModeCount - 1u);
    if (aspect != 0u) {
        auto sizes = aspect_window_sizes(aspect, static_cast<unsigned>(std::max(max_w, 0L)),
                                         static_cast<unsigned>(std::max(max_h, 0L)));
        if (sizes.empty()) {
            // Nothing fits: offer the smallest.
            sizes = aspect_window_sizes(aspect, std::numeric_limits<unsigned>::max(), 360u);
        }
        return sizes;
    }
    std::vector<std::pair<unsigned, unsigned>> sizes;
    const auto add = [&](unsigned w, unsigned h) {
        if (static_cast<long>(w) <= max_w && static_cast<long>(h) <= max_h &&
            std::find(sizes.begin(), sizes.end(), std::pair{w, h}) == sizes.end()) {
            sizes.emplace_back(w, h);
        }
    };
    for (const auto& [w, h] : kCommon) add(w, h);
    if (monitor != nullptr) {
        for (const auto& [w, h] : monitor_resolutions(*monitor)) add(w, h);
    }
    if (sizes.empty()) sizes.emplace_back(854u, 480u);
    std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
        return std::uint64_t{a.first} * a.second < std::uint64_t{b.first} * b.second;
    });
    return sizes;
}

// Fixed aspect, windowed: the smallest listed size at least as tall as the
// current window, or the largest one.
void snap_window_size(RenderConfig& render) {
    const auto sizes = settings_window_sizes(settings_monitor(render), render.aspect_mode);
    auto chosen = sizes.back();
    for (const auto& size : sizes) {
        if (size.second >= render.window_height) {
            chosen = size;
            break;
        }
    }
    render.window_width = chosen.first;
    render.window_height = chosen.second;
}

// Keeps the draft inside what its display allows.
void sanitize_settings_draft(RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayWindowed) {
        const auto sizes = settings_window_sizes(monitor, render.aspect_mode);
        const auto& largest = sizes.back();
        if (render.aspect_mode != 0u &&
            (render.window_width > largest.first || render.window_height > largest.second)) {
            snap_window_size(render);
        } else {
            render.window_width = std::min(render.window_width, largest.first);
            render.window_height = std::min(render.window_height, largest.second);
        }
    }
    render.efb_scale = std::clamp(render.efb_scale, 1u, settings_scale_cap(render));
}

unsigned controls_changes();

unsigned pending_settings_changes() {
    const RenderConfig live = get_render_config();
    const RenderConfig& d = s_settings_draft;
    unsigned changes = 0u;
    changes += d.display_mode != live.display_mode;
    changes += d.monitor != live.monitor;
    changes += d.display_mode == kDisplayExclusive &&
        (d.fullscreen_width != live.fullscreen_width ||
         d.fullscreen_height != live.fullscreen_height);
    changes += d.display_mode == kDisplayExclusive &&
        d.fullscreen_refresh_hz != live.fullscreen_refresh_hz;
    changes += d.display_mode == kDisplayWindowed &&
        (d.window_width != live.window_width || d.window_height != live.window_height);
    changes += d.aspect_mode != live.aspect_mode;
    changes += d.internal_follow_window != live.internal_follow_window ||
        (!d.internal_follow_window && d.efb_scale != live.efb_scale);
    changes += d.vsync_interval != live.vsync_interval;
    changes += d.max_fps != live.max_fps;
    changes += d.aa_mode != live.aa_mode;
    changes += d.smaa_quality != live.smaa_quality;
    changes += d.upscaler != live.upscaler;
    changes += d.temporal_quality != live.temporal_quality;
    changes += d.sharpness != live.sharpness;
    changes += d.anisotropic_filtering != live.anisotropic_filtering;
    changes += d.enhanced_mipmaps != live.enhanced_mipmaps;
    changes += d.per_pixel_lighting != live.per_pixel_lighting;
    changes += d.hud_output_resolution != live.hud_output_resolution;
    changes += controls_changes();
    return changes;
}

enum class ControlsRow {
    InputMode,
    Scheme,
    Binding,
    PointerSpeed,
    InvertPointerY,
    StickDeadzone,
    PointerDeadzone,
    PointerHide,
    Vibration,
    VibrationStrength,
    MotionShake,
    ShakeSensitivity,
    GyroPointer,
    GyroSensitivity,
    Reset,
};

struct ControlsRowInfo {
    ControlsRow kind = ControlsRow::Reset;
    galaxy::input::WiiTarget target = galaxy::input::WiiTarget::A;
};

constexpr unsigned kControlsBindingRow0 = 2u;

std::vector<ControlsRow> controls_tail_rows() {
    using galaxy::input::scheme_has_motion;
    using galaxy::input::scheme_is_gamepad;
    std::vector<ControlsRow> rows{ControlsRow::PointerSpeed, ControlsRow::InvertPointerY};
    if (scheme_is_gamepad(s_controls_scheme)) {
        rows.push_back(ControlsRow::StickDeadzone);
        rows.push_back(ControlsRow::PointerDeadzone);
        rows.push_back(ControlsRow::PointerHide);
        rows.push_back(ControlsRow::Vibration);
        rows.push_back(ControlsRow::VibrationStrength);
    }
    if (scheme_has_motion(s_controls_scheme)) {
        rows.push_back(ControlsRow::MotionShake);
        rows.push_back(ControlsRow::ShakeSensitivity);
        rows.push_back(ControlsRow::GyroPointer);
        rows.push_back(ControlsRow::GyroSensitivity);
    }
    rows.push_back(ControlsRow::Reset);
    return rows;
}

unsigned controls_row_count() {
    return kControlsBindingRow0 + static_cast<unsigned>(galaxy::input::kWiiTargetCount) +
           static_cast<unsigned>(controls_tail_rows().size());
}

ControlsRowInfo controls_row(unsigned row) {
    ControlsRowInfo info{};
    if (row == 0u) {
        info.kind = ControlsRow::InputMode;
    } else if (row == 1u) {
        info.kind = ControlsRow::Scheme;
    } else if (row - kControlsBindingRow0 < galaxy::input::kWiiTargetCount) {
        // The jump/back row follows A and B.
        using galaxy::input::WiiTarget;
        const unsigned index = row - kControlsBindingRow0;
        info.kind = ControlsRow::Binding;
        info.target = index == 2u ? WiiTarget::JumpBack
            : index > 2u ? static_cast<WiiTarget>(index - 1u)
            : static_cast<WiiTarget>(index);
    } else {
        const auto tail = controls_tail_rows();
        const std::size_t index = row - kControlsBindingRow0 - galaxy::input::kWiiTargetCount;
        info.kind = index < tail.size() ? tail[index] : ControlsRow::Reset;
    }
    return info;
}

unsigned controls_changes() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    unsigned changes = 0u;
    for (std::size_t i = 0; i < galaxy::input::kControlSchemeCount; ++i) {
        for (std::size_t t = 0; t < galaxy::input::kWiiTargetCount; ++t) {
            changes += s_controls_draft.tables[i].targets[t] != live.tables[i].targets[t];
        }
        changes += !(s_controls_draft.tuning[i] == live.tuning[i]);
    }
    changes += s_input_mode_draft != get_runtime_settings().input_mode;
    return changes;
}

void show_controls_message(const char* message) {
    s_controls_message = message;
    s_controls_message_until_ms = GetTickCount64() + 2500u;
}

bool controls_capturing() {
    return s_capture_target < galaxy::input::kWiiTargetCount;
}

void end_controls_capture() {
    if (controls_capturing()) {
        s_capture_target = galaxy::input::kWiiTargetCount;
        galaxy::host::cancel_controller_binding_capture();
    }
}

void start_controls_capture(galaxy::input::WiiTarget target) {
    s_capture_target = static_cast<std::size_t>(target);
    s_capture_deadline_ms = GetTickCount64() + kCaptureTimeoutMs;
    if (galaxy::input::scheme_is_gamepad(s_controls_scheme)) {
        galaxy::host::arm_controller_binding_capture(s_controls_scheme);
    }
}

void finish_controls_capture(galaxy::input::InputSource source) {
    if (!controls_capturing()) {
        return;
    }
    const auto target = static_cast<galaxy::input::WiiTarget>(s_capture_target);
    end_controls_capture();
    if (!galaxy::input::binding_source_allowed(s_controls_scheme, target, source)) {
        show_controls_message("THAT INPUT CANNOT DRIVE THIS ROW");
        return;
    }
    galaxy::input::add_binding(s_controls_draft.table(s_controls_scheme)[target], source);
    std::cerr << "[settings] bind " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
              << galaxy::input::wii_target_id(target) << " = "
              << galaxy::input::format_target_bindings(
                     s_controls_scheme, s_controls_draft.table(s_controls_scheme)[target])
              << '\n';
}

void expire_controls_capture() {
    if (controls_capturing() && GetTickCount64() >= s_capture_deadline_ms) {
        end_controls_capture();
    }
}

void reset_controls_draft() {
    end_controls_capture();
    s_controls_draft = galaxy::input::get_controls_config();
    s_input_mode_draft = get_runtime_settings().input_mode;
}

// The graphics page has its FSR 3 QUALITY row (row 3) only in runtimes built
// with FSR 3; maps a shown row to its graphics row.
unsigned graphics_row(unsigned row) {
    return !fsr3_available() && row >= 3u ? row + 1u : row;
}

unsigned native_settings_row_count(unsigned tab) {
    switch (tab % kNativeSettingsTabCount) {
    case 0: return 8u;
    case 1: return fsr3_available() ? 9u : 8u;
    case 2: return controls_row_count();
    case 3: return 6u;
    case 4: return 2u;
    default: return 0u;
    }
}

bool settings_display_confirming() {
    return s_display_confirm_deadline_ms != 0u;
}

void keep_display_settings() {
    s_display_confirm_deadline_ms = 0u;
    std::cerr << "[settings] display kept\n";
}

// Returns the display to the settings it had before the last apply; the
// other settings of that apply stay.
void revert_display_settings() {
    if (!settings_display_confirming()) {
        return;
    }
    s_display_confirm_deadline_ms = 0u;
    RenderConfig render = get_render_config();
    const RenderConfig& previous = s_display_confirm_previous;
    render.display_mode = previous.display_mode;
    render.monitor = previous.monitor;
    render.fullscreen_width = previous.fullscreen_width;
    render.fullscreen_height = previous.fullscreen_height;
    render.fullscreen_refresh_hz = previous.fullscreen_refresh_hz;
    render.window_width = previous.window_width;
    render.window_height = previous.window_height;
    set_render_config(render);
    s_settings_draft.display_mode = render.display_mode;
    s_settings_draft.monitor = render.monitor;
    s_settings_draft.fullscreen_width = render.fullscreen_width;
    s_settings_draft.fullscreen_height = render.fullscreen_height;
    s_settings_draft.fullscreen_refresh_hz = render.fullscreen_refresh_hz;
    s_settings_draft.window_width = render.window_width;
    s_settings_draft.window_height = render.window_height;
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    std::cerr << "[settings] display reverted\n";
}

void expire_display_confirmation() {
    if (settings_display_confirming() && GetTickCount64() >= s_display_confirm_deadline_ms) {
        revert_display_settings();
    }
}

// Applies the CONTROLS draft. Controller input is selected only while a
// controller is attached.
void apply_controls_draft() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    if (!(s_controls_draft == live)) {
        galaxy::input::set_controls_config(s_controls_draft);
        s_controls_draft = galaxy::input::get_controls_config();
    }
    RuntimeSettings runtime = get_runtime_settings();
    if (s_input_mode_draft != runtime.input_mode) {
        if (s_input_mode_draft == RuntimeInputMode::Controller && !galaxy::host::controller_present_now()) {
            show_controls_message("NO CONTROLLER CONNECTED");
            s_input_mode_draft = runtime.input_mode;
        } else {
            runtime.input_mode = s_input_mode_draft;
            set_runtime_settings(runtime);
        }
    }
}

void apply_native_settings_draft() {
    if (pending_settings_changes() == 0u) {
        return;
    }
    apply_controls_draft();
    RenderConfig next = s_settings_draft;
    sanitize_settings_draft(next);
    const RenderConfig live = get_render_config();
    // Settings the menu does not edit keep their live values.
    RenderConfig applied = live;
    applied.display_mode = next.display_mode;
    applied.monitor = next.monitor;
    applied.fullscreen_width = next.fullscreen_width;
    applied.fullscreen_height = next.fullscreen_height;
    applied.fullscreen_refresh_hz = next.fullscreen_refresh_hz;
    applied.window_width = next.window_width;
    applied.window_height = next.window_height;
    applied.aspect_mode = next.aspect_mode;
    applied.internal_follow_window = next.internal_follow_window;
    applied.efb_scale = next.efb_scale;
    applied.vsync_interval = next.vsync_interval;
    applied.max_fps = next.max_fps;
    applied.aa_mode = next.aa_mode;
    applied.smaa_quality = next.smaa_quality;
    applied.upscaler = next.upscaler;
    applied.temporal_quality = next.temporal_quality;
    applied.sharpness = next.sharpness;
    applied.anisotropic_filtering = next.anisotropic_filtering;
    applied.enhanced_mipmaps = next.enhanced_mipmaps;
    applied.per_pixel_lighting = next.per_pixel_lighting;
    applied.hud_output_resolution = next.hud_output_resolution;
    const bool display_changed = display_settings_differ(live, applied);
    const unsigned changes = pending_settings_changes();
    if (applied.internal_follow_window) {
        // The draft's scale is the one the menu opened with; the live scale
        // may have followed the window since.
        applied.efb_scale = live.efb_scale;
    }
    set_render_config(applied);
    if (!applied.internal_follow_window) {
        stage_next_start_efb_scale(applied.efb_scale);
    } else {
        host_window::follow_window_rearm.store(true, std::memory_order_release);
    }
    s_settings_draft = get_render_config();
    if (display_changed) {
        if (!settings_display_confirming()) {
            s_display_confirm_previous = live;
        }
        s_display_confirm_deadline_ms = GetTickCount64() + kDisplayConfirmMs;
    }
    s_settings_applied_ms = GetTickCount64();
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    std::cerr << "[settings] applied " << changes << " change"
              << (changes == 1u ? "" : "s") << '\n';
}

void revert_native_settings_draft() {
    s_settings_draft = get_render_config();
    reset_controls_draft();
    std::cerr << "[settings] reverted\n";
}


// Every overlay change is logged with its new row text.
void adjust_native_setting_unlogged(int delta);

void adjust_native_setting(int delta) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    const auto text = [tab, row] {
        return native_settings_row_text(tab, row, s_settings_draft,
                                        get_runtime_settings());
    };
    const std::string before = text();
    adjust_native_setting_unlogged(delta);
    const std::string after = text();
    if (after != before) {
        std::cerr << "[settings] " << after << '\n';
    }
}

void adjust_native_setting_unlogged(int delta) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (delta == 0 || settings_display_confirming()) {
        return;
    }
    RenderConfig& draft = s_settings_draft;
    const auto step = [delta](unsigned value, unsigned count) {
        return static_cast<unsigned>(std::clamp(
            static_cast<int>(value) + delta, 0, static_cast<int>(count) - 1));
    };

    if (tab == 0u) {
        const MonitorInfo* monitor = settings_monitor(draft);
        switch (row) {
        case 0: {  // monitor
            if (s_settings_monitors.size() < 2u) return;
            std::size_t index = 0u;
            for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                if (&s_settings_monitors[i] == monitor) index = i;
            }
            const std::size_t next = step(static_cast<unsigned>(index),
                                          static_cast<unsigned>(s_settings_monitors.size()));
            if (next == index) return;
            const std::string device = narrow_utf8(s_settings_monitors[next].device);
            draft.monitor.fill('\0');
            std::memcpy(draft.monitor.data(), device.data(),
                        std::min(device.size(), draft.monitor.size() - 1u));
            // A fullscreen mode belongs to its monitor; start from the new
            // one's desktop mode.
            draft.fullscreen_width = 0u;
            draft.fullscreen_height = 0u;
            draft.fullscreen_refresh_hz = 0u;
            sanitize_settings_draft(draft);
            return;
        }
        case 1:  // display mode
            draft.display_mode = step(std::min(draft.display_mode, kDisplayBorderless), 2u);
            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                snap_window_size(draft);
            }
            sanitize_settings_draft(draft);
            return;
        case 2: {  // resolution
            if (draft.display_mode == kDisplayBorderless) return;
            if (draft.display_mode == kDisplayExclusive) {
                if (monitor == nullptr) return;
                const auto sizes = monitor_resolutions(*monitor);  // largest first
                const unsigned w = draft.fullscreen_width != 0u ? draft.fullscreen_width
                                                                : monitor->current.width;
                const unsigned h = draft.fullscreen_height != 0u ? draft.fullscreen_height
                                                                 : monitor->current.height;
                std::size_t index = 0u;
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (sizes[i].first == w && sizes[i].second == h) index = i;
                }
                // Right moves to a larger mode.
                const int moved = static_cast<int>(index) - delta;
                if (moved < 0 || moved >= static_cast<int>(sizes.size())) return;
                draft.fullscreen_width = sizes[static_cast<std::size_t>(moved)].first;
                draft.fullscreen_height = sizes[static_cast<std::size_t>(moved)].second;
                const auto rates = monitor_refresh_rates(
                    *monitor, draft.fullscreen_width, draft.fullscreen_height);
                draft.fullscreen_refresh_hz = rates.empty() ? 0u : rates.front();
            } else {
                const auto sizes = settings_window_sizes(monitor, draft.aspect_mode);  // smallest first
                std::size_t index = 0u;
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (std::uint64_t{sizes[i].first} * sizes[i].second <=
                        std::uint64_t{draft.window_width} * draft.window_height) {
                        index = i;
                    }
                }
                const int moved = static_cast<int>(index) + delta;
                if (moved < 0 || moved >= static_cast<int>(sizes.size())) return;
                draft.window_width = sizes[static_cast<std::size_t>(moved)].first;
                draft.window_height = sizes[static_cast<std::size_t>(moved)].second;
            }
            sanitize_settings_draft(draft);
            return;
        }
        case 3: {  // refresh rate (exclusive fullscreen)
            if (draft.display_mode != kDisplayExclusive || monitor == nullptr) return;
            const unsigned w = draft.fullscreen_width != 0u ? draft.fullscreen_width
                                                            : monitor->current.width;
            const unsigned h = draft.fullscreen_height != 0u ? draft.fullscreen_height
                                                             : monitor->current.height;
            const auto rates = monitor_refresh_rates(*monitor, w, h);  // highest first
            if (rates.empty()) return;
            const unsigned current = draft.fullscreen_refresh_hz != 0u
                ? draft.fullscreen_refresh_hz : monitor->current.refresh_hz;
            std::size_t index = 0u;
            for (std::size_t i = 0; i < rates.size(); ++i) {
                if (rates[i] == current) index = i;
            }
            const int moved = static_cast<int>(index) - delta;
            if (moved < 0 || moved >= static_cast<int>(rates.size())) return;
            draft.fullscreen_width = w;
            draft.fullscreen_height = h;
            draft.fullscreen_refresh_hz = rates[static_cast<std::size_t>(moved)];
            return;
        }
        case 4:  // aspect ratio
            draft.aspect_mode = step(draft.aspect_mode, kAspectModeCount);
            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                snap_window_size(draft);
            }
            return;
        case 5: {  // internal resolution: 0 matches the output, then 1x..cap
            const unsigned cap = settings_scale_cap(draft);
            const unsigned current = draft.internal_follow_window ? 0u
                : std::clamp(draft.efb_scale, 1u, cap);
            const unsigned next = step(current, cap + 1u);
            if (next == 0u) {
                draft.internal_follow_window = true;
            } else {
                draft.internal_follow_window = false;
                draft.efb_scale = next;
            }
            return;
        }
        case 6: {  // frame rate
            const auto rates = frame_rate_options();
            const auto current = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
            std::size_t index = 0u;
            for (std::size_t i = 0; i < rates.size(); ++i) {
                if (rates[i] <= current) index = i;
            }
            const std::size_t next = step(static_cast<unsigned>(index), static_cast<unsigned>(rates.size()));
            draft.max_fps = static_cast<float>(rates[next]);
            return;
        }
        case 7:
            draft.vsync_interval = draft.vsync_interval == 0u ? 1u : 0u;
            return;
        default:
            return;
        }
    }

    if (tab == 1u) {
        static constexpr std::array<unsigned, 5> kAnisotropy{1u, 2u, 4u, 8u, 16u};
        switch (graphics_row(row)) {
        case 0: draft.aa_mode = step(std::min(draft.aa_mode, 3u), 4u); break;
        case 1: draft.smaa_quality = step(std::min(draft.smaa_quality, 3u), 4u); break;
        case 2: {
            const unsigned count = fsr3_available() ? 3u : 2u;
            draft.upscaler = step(std::min(draft.upscaler, count - 1u), count);
            break;
        }
        case 3: draft.temporal_quality = step(std::min(draft.temporal_quality, 4u), 5u); break;
        case 4:
            draft.sharpness = static_cast<unsigned>(std::clamp(
                static_cast<int>(std::min(draft.sharpness, 100u)) + delta * 10, 0, 100));
            break;
        case 5: {
            std::size_t index = 0u;
            for (std::size_t i = 0; i < kAnisotropy.size(); ++i) {
                if (kAnisotropy[i] <= draft.anisotropic_filtering) index = i;
            }
            draft.anisotropic_filtering = kAnisotropy[step(
                static_cast<unsigned>(index), static_cast<unsigned>(kAnisotropy.size()))];
            break;
        }
        case 6: draft.enhanced_mipmaps = !draft.enhanced_mipmaps; break;
        case 7: draft.per_pixel_lighting = !draft.per_pixel_lighting; break;
        case 8: draft.hud_output_resolution = !draft.hud_output_resolution; break;
        default: return;
        }
        return;
    }

    RuntimeSettings settings = get_runtime_settings();
    if (tab == kControlsTab) {
        using galaxy::input::ControlScheme;
        galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
        const ControlsRowInfo info = controls_row(row);
        const auto step_float = [delta](float value, float increment, float lo, float hi) {
            const float snapped = std::round(value / increment) * increment;
            return std::clamp(snapped + static_cast<float>(delta) * increment, lo, hi);
        };
        switch (info.kind) {
        case ControlsRow::InputMode:
            // A physical Wii Remote is chosen in the launcher only.
            if (s_input_mode_draft == RuntimeInputMode::RealWiimote) return;
            {
                static constexpr RuntimeInputMode kModes[] = {
                    RuntimeInputMode::Auto, RuntimeInputMode::KeyboardMouse, RuntimeInputMode::Controller};
                unsigned index = 0u;
                for (unsigned i = 0; i < 3u; ++i) {
                    if (kModes[i] == s_input_mode_draft) index = i;
                }
                s_input_mode_draft = kModes[step(index, 3u)];
            }
            return;
        case ControlsRow::Scheme:
            s_controls_scheme = static_cast<ControlScheme>(step(
                static_cast<unsigned>(s_controls_scheme),
                static_cast<unsigned>(galaxy::input::kControlSchemeCount)));
            return;
        case ControlsRow::Binding:
            if (delta > 0) start_controls_capture(info.target);
            return;
        case ControlsRow::PointerSpeed:
            tuning.pointer_speed = step_float(tuning.pointer_speed, 0.2f, 0.2f, 6.0f);
            return;
        case ControlsRow::InvertPointerY:
            tuning.invert_pointer_y = !tuning.invert_pointer_y;
            return;
        case ControlsRow::StickDeadzone:
            tuning.stick_deadzone = step_float(tuning.stick_deadzone, 0.02f, 0.0f, 0.5f);
            return;
        case ControlsRow::PointerDeadzone:
            tuning.pointer_deadzone = step_float(tuning.pointer_deadzone, 0.02f, 0.0f, 0.5f);
            return;
        case ControlsRow::PointerHide:
            tuning.pointer_hide_seconds = step_float(tuning.pointer_hide_seconds, 1.0f, 0.0f, 30.0f);
            return;
        case ControlsRow::Vibration:
            tuning.vibration = !tuning.vibration;
            return;
        case ControlsRow::VibrationStrength: {
            // 10..100 percent in steps of 10 (rumble.h).
            const unsigned index = std::min(9u, (std::max<unsigned>(tuning.rumble, 10u) - 5u) / 10u);
            tuning.rumble = static_cast<std::uint8_t>(10u + step(index, 10u) * 10u);
            return;
        }
        case ControlsRow::MotionShake:
            tuning.motion_shake = static_cast<galaxy::input::MotionShake>(
                step(static_cast<unsigned>(tuning.motion_shake), 3u));
            return;
        case ControlsRow::ShakeSensitivity:
            tuning.shake_sensitivity = static_cast<std::uint8_t>(
                step(tuning.shake_sensitivity - 1u, galaxy::input::kShakeSensitivitySteps) + 1u);
            return;
        case ControlsRow::GyroPointer:
            tuning.gyro_pointer = !tuning.gyro_pointer;
            return;
        case ControlsRow::GyroSensitivity:
            tuning.gyro_sensitivity = step_float(tuning.gyro_sensitivity, 0.1f, 0.2f, 5.0f);
            return;
        case ControlsRow::Reset:
            if (delta > 0) {
                s_controls_draft.table(s_controls_scheme) =
                    galaxy::input::default_binding_table(s_controls_scheme);
                s_controls_draft.tune(s_controls_scheme) =
                    galaxy::input::default_scheme_tuning(s_controls_scheme);
            }
            return;
        }
        return;
    }
    if (tab == 3u) {
        // The mix changes live (no apply).
        float* const gains[] = {&settings.audio.master, &settings.audio.music,
                                &settings.audio.sfx, &settings.audio.voice,
                                &settings.audio.ambience, &settings.audio.cutscene};
        if (row >= std::size(gains)) return;
        const float next = step_gain(*gains[row], delta);
        if (next != *gains[row]) {
            *gains[row] = next;
            set_runtime_settings(settings);
            s_native_settings_dirty.store(true, std::memory_order_release);
        }
    }
    if (tab == 4u && row == 0u) {
        // Text speed changes live (no apply).
        const unsigned next = step(std::clamp(settings.text_speed, 1u, 3u) - 1u, 3u) + 1u;
        if (next != settings.text_speed) {
            settings.text_speed = next;
            set_runtime_settings(settings);
            s_native_settings_dirty.store(true, std::memory_order_release);
        }
    }
    if (tab == 4u && row == 1u) {
        // Saved at once; the game takes it at its next start (footer note).
        const unsigned index = static_cast<unsigned>(
            std::find(std::begin(kGameLanguages), std::end(kGameLanguages), settings.language) -
            std::begin(kGameLanguages));
        const GameLanguage next = kGameLanguages[step(std::min(index, 2u), 3u)];
        if (next != settings.language) {
            settings.language = next;
            set_runtime_settings(settings);
            s_native_settings_dirty.store(true, std::memory_order_release);
        }
    }
}

void move_native_setting_row(int delta) {
    end_controls_capture();
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    if (count == 0u) {
        s_native_settings_row.store(0u, std::memory_order_relaxed);
        return;
    }
    int row = static_cast<int>(s_native_settings_row.load(std::memory_order_relaxed));
    row = (row + delta) % static_cast<int>(count);
    if (row < 0) {
        row += static_cast<int>(count);
    }
    s_native_settings_row.store(static_cast<unsigned>(row), std::memory_order_relaxed);
}

void cycle_native_settings_tab(int delta) {
    end_controls_capture();
    s_settings_first_row = 0u;
    int tab = static_cast<int>(
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount);
    tab = (tab + delta) % static_cast<int>(kNativeSettingsTabCount);
    if (tab < 0) {
        tab += static_cast<int>(kNativeSettingsTabCount);
    }
    s_native_settings_tab.store(
        static_cast<unsigned>(tab), std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
}

void set_native_settings_visible(bool visible) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (visible && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        // A fresh draft and monitor list each time the menu opens.
        s_settings_monitors = enumerate_monitors();
        s_settings_draft = get_render_config();
        reset_controls_draft();
    }
    if (!visible) {
        // Closing discards an unapplied draft and an unconfirmed display.
        revert_display_settings();
        s_settings_draft = get_render_config();
        reset_controls_draft();
        flush_native_settings_if_dirty();
    }
    if (s_native_settings_visible.exchange(visible, std::memory_order_relaxed) != visible) {
        std::cerr << (visible ? "[settings] overlay open" : "[settings] overlay closed") << std::endl;
    }
}

// Binds a mouse button while a keyboard-scheme row waits for an input.
bool native_settings_capture_mouse_button(galaxy::input::MouseButton button) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!s_native_settings_visible.load(std::memory_order_relaxed) || !controls_capturing() ||
        s_controls_scheme != galaxy::input::ControlScheme::Keyboard) {
        return false;
    }
    finish_controls_capture({galaxy::input::SourceKind::Mouse, static_cast<std::uint8_t>(button)});
    return true;
}

// Keys on the CONTROLS page that differ from the other pages: Enter/A on a
// binding row waits for an input, Backspace/X clears it, Space/Y applies.
// Returns true when the key was handled here.
bool handle_controls_key(WPARAM key, bool was_down, bool from_pad) {
    if (key == galaxy::host::kBindingCaptureKey) {
        if (const auto source = galaxy::host::take_controller_binding_capture()) {
            finish_controls_capture(*source);
        }
        return true;
    }
    expire_controls_capture();
    if (controls_capturing()) {
        if (key == VK_ESCAPE && !from_pad) {
            end_controls_capture();
        } else if (s_controls_scheme == galaxy::input::ControlScheme::Keyboard && !from_pad && !was_down) {
            const std::uint32_t vk = s_handled_key_normalized != 0u ? s_handled_key_normalized
                                                                    : static_cast<std::uint32_t>(key);
            finish_controls_capture({galaxy::input::SourceKind::Key, static_cast<std::uint8_t>(vk & 0xffu)});
        }
        return true;
    }
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    if (tab != kControlsTab) {
        return false;
    }
    const ControlsRowInfo info = controls_row(s_native_settings_row.load(std::memory_order_relaxed));
    switch (key) {
    case VK_SPACE:
        if (!was_down) apply_native_settings_draft();
        return true;
    case VK_RETURN:
        if (was_down) return true;
        if (from_pad || settings_display_confirming()) return false;  // Y applies
        if (info.kind == ControlsRow::Binding) {
            start_controls_capture(info.target);
            return true;
        }
        if (info.kind == ControlsRow::Reset) {
            adjust_native_setting(1);
            return true;
        }
        return false;
    case VK_BACK:
        if (info.kind == ControlsRow::Binding) {
            if (!was_down) {
                s_controls_draft.table(s_controls_scheme)[info.target] = {};
                std::cerr << "[settings] clear " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
                          << galaxy::input::wii_target_id(info.target) << '\n';
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool handle_native_settings_key(WPARAM key, bool was_down, bool from_pad = false) {
    if (s_home_menu_visible.load(std::memory_order_acquire)) {
        return handle_home_menu_key(key, was_down);
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!native_settings_ui_enabled()) {
        return false;
    }
    if (key == galaxy::host::kBindingCaptureKey && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        (void)galaxy::host::take_controller_binding_capture();
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        handle_controls_key(key, was_down, from_pad)) {
        return true;
    }
    if (key == VK_F1) {
        if (was_down) {
            return true;
        }
        const bool visible = s_native_settings_visible.load(
            std::memory_order_relaxed);
        if (visible) {
            set_native_settings_visible(false);
            return true;
        }
        set_native_settings_visible(true);
        return true;
    }
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return false;
    }
    expire_display_confirmation();
    switch (key) {
    case VK_ESCAPE:
        if (settings_display_confirming()) {
            revert_display_settings();
            return true;
        }
        set_native_settings_visible(false);
        return true;
    case VK_RETURN:
        if (was_down) {
            return true;
        }
        if (settings_display_confirming()) {
            keep_display_settings();
        } else {
            apply_native_settings_draft();
        }
        return true;
    case VK_BACK:
        if (!was_down && !settings_display_confirming()) {
            revert_native_settings_draft();
        }
        return true;
    case VK_TAB:
    case VK_NEXT:
        cycle_native_settings_tab(1);
        return true;
    case VK_PRIOR:
        cycle_native_settings_tab(-1);
        return true;
    case VK_UP:
        move_native_setting_row(-1);
        return true;
    case VK_DOWN:
        move_native_setting_row(1);
        return true;
    case VK_LEFT:
        adjust_native_setting(-1);
        return true;
    case VK_RIGHT:
        adjust_native_setting(1);
        return true;
    default:
        return false;
    }
}

int signed_lparam_x(LPARAM lp) {
    return static_cast<int>(static_cast<short>(lp & 0xFFFF));
}

int signed_lparam_y(LPARAM lp) {
    return static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
}


bool hide_os_cursor_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_HIDE_OS_CURSOR") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool capture_mouse_buttons_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_CAPTURE_MOUSE_BUTTONS") != 0 ||
            length <= 1) {
            // Win32 only guarantees that a release outside the client is
            // delivered after the press owner captures the mouse.  Losing
            // that edge leaves the native Wiimote A/B state held indefinitely.
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

galaxy::host::HostPointerCursorPollMode mouse_ir_cursor_poll_mode() {
    static const galaxy::host::HostPointerCursorPollMode mode = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_MOUSE_IR_CURSOR_POLL") != 0 ||
            length <= 1) {
            return galaxy::host::HostPointerCursorPollMode::Adaptive;
        }
        if (value[0] == 'a' || value[0] == 'A') {
            return galaxy::host::HostPointerCursorPollMode::Adaptive;
        }
        if (value[0] == '0' || value[0] == 'n' || value[0] == 'N') {
            return galaxy::host::HostPointerCursorPollMode::Disabled;
        }
        return galaxy::host::HostPointerCursorPollMode::Forced;
    }();
    return mode;
}

std::uint64_t mouse_ir_message_recent_ms() {
    static const std::uint64_t value = [] {
        char text[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, text, sizeof(text),
                "GALAXY_MOUSE_IR_MESSAGE_RECENT_MS") != 0 ||
            length <= 1) {
            return 120ull;
        }
        return std::min<std::uint64_t>(
            std::strtoull(text, nullptr, 10), 1000ull);
    }();
    return value;
}

std::uint64_t mouse_ir_stale_center_reject_ms() {
    static const std::uint64_t value = [] {
        char text[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, text, sizeof(text),
                "GALAXY_MOUSE_IR_STALE_CENTER_REJECT_MS") != 0 ||
            length <= 1) {
            return 5000ull;
        }
        return std::min<std::uint64_t>(
            std::strtoull(text, nullptr, 10), 30000ull);
    }();
    return value;
}

void maybe_hide_os_cursor() {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const bool focused =
        s_pointer_focused.load(std::memory_order_relaxed) ||
        (hwnd != nullptr && GetForegroundWindow() == hwnd);
    if (hide_os_cursor_enabled() && focused) {
        SetCursor(nullptr);
    }
}

galaxy::host::HostPointerCoordinates cached_pointer_coordinates(
    bool inside_override) noexcept {
    const galaxy::host::HostPointerSnapshot snapshot =
        s_pointer_snapshot.snapshot();
    galaxy::host::HostPointerCoordinates coordinates = snapshot.coordinates;
    coordinates.inside_client = inside_override && coordinates.absolute_valid;
    return coordinates;
}

struct AcquiredHostPointerCoordinates {
    galaxy::host::HostPointerCoordinates coordinates{};
    std::uint64_t absolute_acquired_ms{};
};

void mark_pointer_outside() noexcept {
    (void)s_pointer_snapshot.update(
        [](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
        });
}

void clear_pointer_buttons() noexcept {
    s_pointer_left_button.store(false, std::memory_order_relaxed);
    s_pointer_right_button.store(false, std::memory_order_relaxed);
}

void invalidate_pointer_focus_once() noexcept {
    if (s_pointer_focus_valid.exchange(false, std::memory_order_acq_rel)) {
        s_pointer_transitions.invalidate_focus(
            cached_pointer_coordinates(true),
            static_cast<std::uint64_t>(GetTickCount64()));
    }
}

void clear_pointer_window_state(HWND hwnd) noexcept {
    // Focus/capture ownership controls buttons, not pointer geometry. The
    // global cursor poll remains authoritative while this HWND is unfocused;
    // only WM_MOUSELEAVE or an actual outside sample may publish sensor loss.
    clear_pointer_buttons();
    if (hwnd != nullptr && GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

bool seed_pointer_from_client(
    HWND hwnd,
    bool force_center) {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return false;
    }

    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerSnapshot seeded_snapshot =
        s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_seed(
                snapshot,
                {
                    width,
                    height,
                    force_center,
                    pointer_now_ms,
                });
        });

    // A stationary cursor already inside the game window may not generate WM_MOUSEMOVE after
    // launch, resize, or refocus. The geometry seed above is inactive, so acquire the real local
    // cursor here and publish it with the same compare-before-publish rule used by the normal
    // first-poll fallback. Without this, the synthetic valid/outside seed can mask
    // native_runtime's independent cursor recovery indefinitely.
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const auto poll_authority =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            mouse_ir_cursor_poll_mode(), remote_session, false);
    POINT cursor{};
    if (poll_authority.poll_enabled &&
        poll_authority.authoritative && GetCursorPos(&cursor) &&
        ScreenToClient(hwnd, &cursor) && cursor.x >= 0 && cursor.y >= 0 &&
        cursor.x < width && cursor.y < height) {
        const int cursor_x =
            std::clamp(static_cast<int>(cursor.x), 0, width - 1);
        const int cursor_y =
            std::clamp(static_cast<int>(cursor.y), 0, height - 1);
        bool publication_accepted = false;
        (void)s_pointer_snapshot.update(
            [&](galaxy::host::HostPointerSnapshot& current) noexcept {
                publication_accepted =
                    galaxy::host::apply_host_pointer_fallback_inside_publication(
                        current,
                        {
                            cursor_x,
                            cursor_y,
                            width,
                            height,
                            pointer_now_ms,
                            seeded_snapshot.event_sequence,
                            seeded_snapshot.absolute_sequence,
                        }) == galaxy::host::
                            HostPointerFallbackInsidePublicationResult::
                                PublishedInside;
            });
        if (publication_accepted) {
            s_pointer_last_cursor_poll_x.store(
                cursor_x, std::memory_order_relaxed);
            s_pointer_last_cursor_poll_y.store(
                cursor_y, std::memory_order_relaxed);
        }
    }
    return true;
}

AcquiredHostPointerCoordinates update_pointer_absolute(
    HWND hwnd,
    LPARAM lp) {
    galaxy::host::HostPointerCoordinates coordinates{};
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return {};
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return {};
    }

    const int raw_x = signed_lparam_x(lp);
    const int raw_y = signed_lparam_y(lp);
    const bool inside =
        raw_x >= 0 && raw_y >= 0 && raw_x < width && raw_y < height;
    coordinates = {
        true,
        inside,
        raw_x,
        raw_y,
        width,
        height,
    };
    const std::uint64_t now_ms = static_cast<std::uint64_t>(GetTickCount64());
    // Preserve queued acquisition time instead of refreshing it at dispatch.
    const DWORD message_age = static_cast<DWORD>(now_ms) -
        static_cast<DWORD>(GetMessageTime());
    const std::uint64_t pointer_now_ms = message_age <= 0x7fffffffu && message_age <= now_ms
        ? now_ms - message_age : 0u;
    (void)s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_client_coordinate(
                snapshot,
                {
                    raw_x,
                    raw_y,
                    width,
                    height,
                    pointer_now_ms,
                });
        });

    if (inside && !s_tracking_mouse_leave) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        s_tracking_mouse_leave = TrackMouseEvent(&tme) != FALSE;
    }
    return {coordinates, pointer_now_ms};
}

void update_pointer_button(HWND hwnd, LPARAM lp, bool left, bool down) {
    const AcquiredHostPointerCoordinates publication =
        update_pointer_absolute(hwnd, lp);
    if (left) {
        s_pointer_left_button.store(down, std::memory_order_relaxed);
    } else {
        s_pointer_right_button.store(down, std::memory_order_relaxed);
    }
    s_pointer_transitions.accept_button_transition(
        left ? galaxy::host::HostPointerButton::Left
             : galaxy::host::HostPointerButton::Right,
        down,
        publication.coordinates,
        publication.absolute_acquired_ms);
}

// The input message being handled came from SendInput, not a device.
bool input_message_injected() noexcept {
    INPUT_MESSAGE_SOURCE source{};
    return GetCurrentInputMessageSource(&source) && source.originId == IMO_INJECTED;
}

// While the game window is in front the display stays on and the PC stays
// awake: Windows does not count controller input as activity. Window thread
// only.
void hold_display_on(bool hold) noexcept {
    static const HANDLE request = [] {
        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString = const_cast<LPWSTR>(L"Nebula is running");
        return PowerCreateRequest(&reason);
    }();
    static bool held = false;
    if (request == INVALID_HANDLE_VALUE || hold == held || (hold && hidden_window_mode_enabled())) return;
    if (hold) {
        (void)PowerSetRequest(request, PowerRequestDisplayRequired);
        (void)PowerSetRequest(request, PowerRequestSystemRequired);
    } else {
        (void)PowerClearRequest(request, PowerRequestDisplayRequired);
        (void)PowerClearRequest(request, PowerRequestSystemRequired);
    }
    held = hold;
}

// The two accented capitals the language names need, as ui_glyph codes.
constexpr char kGlyphCCedilla = '\x01';
constexpr char kGlyphNTilde = '\x02';

std::array<std::uint8_t, 7> ui_glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    switch (ch) {
    case 'A': return {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
    case 'B': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu};
    case 'C': return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu};
    case 'D': return {0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu};
    case 'E': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu};
    case 'F': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u};
    case 'G': return {0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Fu};
    case 'H': return {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u};
    case 'I': return {0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
    case 'J': return {0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0Cu};
    case 'K': return {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u};
    case 'L': return {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu};
    case 'M': return {0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u};
    case 'N': return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    case 'O': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
    case 'P': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u};
    case 'Q': return {0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du};
    case 'R': return {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u};
    case 'S': return {0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
    case 'T': return {0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
    case 'U': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu};
    case 'V': return {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u};
    case 'W': return {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0Au};
    case 'X': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u};
    case 'Y': return {0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u};
    case 'Z': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu};
    case '0': return {0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu};
    case '1': return {0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu};
    case '2': return {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu};
    case '3': return {0x1Eu, 0x01u, 0x01u, 0x0Eu, 0x01u, 0x01u, 0x1Eu};
    case '4': return {0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u};
    case '5': return {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x1Eu};
    case '6': return {0x06u, 0x08u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu};
    case '7': return {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u};
    case '8': return {0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu};
    case '9': return {0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x02u, 0x0Cu};
    case ':': return {0x00u, 0x04u, 0x04u, 0x00u, 0x04u, 0x04u, 0x00u};
    case '.': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu};
    case '-': return {0x00u, 0x00u, 0x00u, 0x1Eu, 0x00u, 0x00u, 0x00u};
    case '+': return {0x00u, 0x04u, 0x04u, 0x1Fu, 0x04u, 0x04u, 0x00u};
    case '/': return {0x01u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x10u};
    case ',': return {0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x04u, 0x08u};
    case '=': return {0x00u, 0x00u, 0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x00u};
    case '%': return {0x19u, 0x19u, 0x02u, 0x04u, 0x08u, 0x13u, 0x13u};
    case '<': return {0x02u, 0x04u, 0x08u, 0x10u, 0x08u, 0x04u, 0x02u};
    case '(': return {0x02u, 0x04u, 0x08u, 0x08u, 0x08u, 0x04u, 0x02u};
    case ')': return {0x08u, 0x04u, 0x02u, 0x02u, 0x02u, 0x04u, 0x08u};
    case '>': return {0x08u, 0x04u, 0x02u, 0x01u, 0x02u, 0x04u, 0x08u};
    case kGlyphCCedilla: return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x11u, 0x0Eu, 0x04u};
    // A full N; ui_text draws the tilde above the cell.
    case kGlyphNTilde: return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    default: return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    }
}

// Draw-call based (see ensure_native_settings_solid_pipeline_loaded). ClearRenderTargetView with a
// rect list (NumRects > 0) faulted inside nvwgf2umx.dll with STATUS_STACK_BUFFER_OVERRUN on an
// RTX 5090 (driver 32.0.15.9597). That fail-fast bypasses this process's SEH and terminate handlers.
// The fault reproduced at the same offset with a batch size of 48 and with the corner texture path
// off, so the trigger is the rect-limited clear itself. Full-target clears (NumRects == 0,
// pRects == nullptr) are unaffected and still used elsewhere in this file.
void ui_rect(
    UiCanvas& renderer,
    int x,
    int y,
    int w,
    int h,
    const std::array<float, 4>& color) {
    renderer.draw_ui_solid_quad(x, y, w, h, color);
}

void ui_text(
    UiCanvas& renderer,
    int x,
    int y,
    int scale,
    const std::string& text,
    const std::array<float, 4>& color) {
    int cursor_x = x;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == ' ') {
            cursor_x += 4 * scale;
            continue;
        }
        // UTF-8 Ç/ç and Ñ/ñ (C3 87/A7, C3 91/B1); any other two-byte letter draws as one blank cell.
        if (static_cast<unsigned char>(ch) == 0xC3u && i + 1u < text.size()) {
            const auto next = static_cast<unsigned char>(text[++i]);
            ch = next == 0x87u || next == 0xA7u ? kGlyphCCedilla
               : next == 0x91u || next == 0xB1u ? kGlyphNTilde : '\0';
        }
        const auto bitmap_of = [](const std::array<std::uint8_t, 7>& rows) {
            std::uint64_t bitmap = 0u;
            for (unsigned row = 0; row < 7u; ++row) {
                for (unsigned col = 0; col < 5u; ++col) {
                    if ((rows[row] & (1u << (4u - col))) != 0u) {
                        bitmap |= UINT64_C(1) << (row * 5u + col);
                    }
                }
            }
            return bitmap;
        };
        if (ch == kGlyphNTilde) {
            // Two rows of tilde, one row clear of the N: squeezed into the cell, it read as a plain N.
            static const std::uint64_t tilde = bitmap_of({0x0Du, 0x16u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u});
            renderer.draw_ui_glyph(cursor_x, y - 3 * scale, scale, tilde, color);
        }
        renderer.draw_ui_glyph(cursor_x, y, scale, bitmap_of(ui_glyph(ch)), color);
        cursor_x += 6 * scale;
    }
}

std::string percent_text(float value) {
    char text[32]{};
    std::snprintf(
        text,
        sizeof(text),
        "%u%%",
        static_cast<unsigned>(std::lround(value * 100.0f)));
    return text;
}

std::string upper_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime) {
    char text[128]{};
    switch (tab % kNativeSettingsTabCount) {
    case 0: {
        const MonitorInfo* monitor = settings_monitor(render);
        switch (row) {
        case 0: {
            if (monitor == nullptr) return "MONITOR: PRIMARY";
            std::size_t index = 0u;
            for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                if (&s_settings_monitors[i] == monitor) index = i;
            }
            std::string name = monitor->name.empty() ? std::string("DISPLAY") : monitor->name;
            if (name.size() > 22u) name.resize(22u);
            std::snprintf(text, sizeof(text), "MONITOR: %u %s %uX%u",
                          static_cast<unsigned>(index + 1u), upper_ascii(name).c_str(),
                          monitor->current.width, monitor->current.height);
            return text;
        }
        case 1: {
            static constexpr const char* kModes[] = {"WINDOWED", "BORDERLESS"};
            return std::string("MODE: ") + kModes[std::min(render.display_mode, kDisplayBorderless)];
        }
        case 2: {
            const auto [w, h] = settings_output_size(render);
            std::snprintf(text, sizeof(text), "RESOLUTION: %uX%u%s", w, h,
                          render.display_mode == kDisplayBorderless ? " DESKTOP" : "");
            return text;
        }
        case 3:
            std::snprintf(text, sizeof(text), "REFRESH RATE: %u HZ", monitor ? monitor->current.refresh_hz : 60u);
            return text;
        case 4: {
            static constexpr const char* kAspects[] = {
                "AUTO", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9"};
            static_assert(std::size(kAspects) == kAspectModeCount);
            return std::string("ASPECT RATIO: ") +
                   kAspects[std::min(render.aspect_mode, kAspectModeCount - 1u)];
        }
        case 5: {
            if (render.internal_follow_window) return "INTERNAL RES: MATCH OUTPUT";
            const unsigned scale = std::clamp(render.efb_scale, 1u, kMaxEfbScale);
            std::snprintf(text, sizeof(text), "INTERNAL RES: %uX %uX%u%s", scale,
                          kEfbWidth * scale, kEfbHeight * scale,
                          scale >= settings_scale_cap(render) ? " MAX" : "");
            return text;
        }
        case 6: {
            const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
            const double measured = host_window::measured_present_hz.load(std::memory_order_relaxed);
            std::snprintf(text, sizeof(text), "FRAME RATE: %u  %.1f", rate, measured);
            return text;
        }
        case 7:
            return std::string("VSYNC: ") + (render.vsync_interval == 0u ? "OFF" : "ON");
        default:
            return {};
        }
    }
    case 1: {
        static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
        switch (graphics_row(row)) {
        case 0: return anti_aliasing_row_text(render.aa_mode, render.smaa_quality, get_post_process_status());
        case 1: return std::string("AA QUALITY: ") + kQuality[std::min(render.smaa_quality, 3u)];
        case 2: {
            static constexpr const char* kUpscalers[] = {"BILINEAR", "FSR 1", "FSR 3"};
            return std::string("UPSCALER: ") +
                   kUpscalers[std::min(render.upscaler, fsr3_available() ? 2u : 1u)];
        }
        case 3: {
            static constexpr const char* kTemporal[] = {
                "NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
            return std::string("FSR 3 QUALITY: ") + kTemporal[std::min(render.temporal_quality, 4u)];
        }
        case 4:
            std::snprintf(text, sizeof(text), "SHARPENING: %u%%", std::min(render.sharpness, 100u));
            return text;
        case 5:
            if (render.anisotropic_filtering <= 1u) return "TEXTURES: TRILINEAR";
            std::snprintf(text, sizeof(text), "TEXTURES: %uX ANISOTROPIC",
                          std::min(render.anisotropic_filtering, 16u));
            return text;
        case 6:
            return std::string("MIPMAPS: ") + (render.enhanced_mipmaps ? "GENERATED" : "ORIGINAL");
        case 7:
            return std::string("LIGHTING: ") +
                   (render.per_pixel_lighting ? "PER PIXEL" : "PER VERTEX");
        case 8:
            return std::string("2D LAYER: ") +
                   (render.hud_output_resolution ? "DISPLAY RES" : "INTERNAL RES");
        default:
            return {};
        }
    }
    case 2: {
        (void)runtime;
        using galaxy::input::ControlScheme;
        const ControlsRowInfo info = controls_row(row);
        const galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
        const auto on_off = [](bool value) { return value ? "ON" : "OFF"; };
        switch (info.kind) {
        case ControlsRow::InputMode:
            switch (s_input_mode_draft) {
            case RuntimeInputMode::Controller: return "INPUT: CONTROLLER ONLY";
            case RuntimeInputMode::RealWiimote: return "INPUT: WII REMOTE";
            case RuntimeInputMode::KeyboardMouse: return "INPUT: KEYBOARD+MOUSE ONLY";
            default: return "INPUT: AUTO";
            }
        case ControlsRow::Scheme: {
            std::string line = std::string("EDIT: ") + galaxy::input::control_scheme_label(s_controls_scheme);
            const galaxy::host::ConnectedControllers connected = galaxy::host::connected_controllers();
            if (s_controls_scheme == ControlScheme::Xbox && connected.xbox) line += "  CONNECTED";
            if (s_controls_scheme == ControlScheme::SwitchPro && connected.switch_pro != 0u) line += "  CONNECTED";
            if (s_controls_scheme == ControlScheme::SwitchPro2) {
                if (connected.pro2_needs_winusb) line += "  NEEDS WINUSB";
                else if (connected.switch_pro2 != 0u) line += "  CONNECTED";
            }
            return line;
        }
        case ControlsRow::Binding: {
            std::string line = std::string(galaxy::input::wii_target_label(info.target)) + ": ";
            if (controls_capturing() && s_capture_target == static_cast<std::size_t>(info.target)) {
                return line + "PRESS AN INPUT";
            }
            std::string bound = upper_ascii(galaxy::input::format_target_bindings(
                s_controls_scheme, s_controls_draft.table(s_controls_scheme)[info.target]));
            if (bound.size() > 34u) bound = bound.substr(0, 32u) + "..";
            return line + bound;
        }
        case ControlsRow::PointerSpeed:
            std::snprintf(text, sizeof(text), "POINTER SPEED: %.1f", static_cast<double>(tuning.pointer_speed));
            return text;
        case ControlsRow::InvertPointerY:
            return std::string("INVERT POINTER Y: ") + on_off(tuning.invert_pointer_y);
        case ControlsRow::StickDeadzone:
            std::snprintf(text, sizeof(text), "STICK DEADZONE: %u%%",
                          static_cast<unsigned>(std::lround(tuning.stick_deadzone * 100.0f)));
            return text;
        case ControlsRow::PointerDeadzone:
            std::snprintf(text, sizeof(text), "POINTER STICK DEADZONE: %u%%",
                          static_cast<unsigned>(std::lround(tuning.pointer_deadzone * 100.0f)));
            return text;
        case ControlsRow::PointerHide:
            if (tuning.pointer_hide_seconds <= 0.0f) return "HIDE STILL POINTER: NEVER";
            std::snprintf(text, sizeof(text), "HIDE STILL POINTER: %.0f S",
                          static_cast<double>(tuning.pointer_hide_seconds));
            return text;
        case ControlsRow::Vibration:
            return std::string("VIBRATION: ") + on_off(tuning.vibration);
        case ControlsRow::VibrationStrength:
            std::snprintf(text, sizeof(text), "VIBRATION STRENGTH: %u%%", static_cast<unsigned>(tuning.rumble));
            return text;
        case ControlsRow::MotionShake: {
            static constexpr const char* kMotion[] = {"OFF", "WII REMOTE", "NUNCHUK"};
            return std::string("MOTION SHAKE: ") + kMotion[static_cast<unsigned>(tuning.motion_shake) % 3u];
        }
        case ControlsRow::ShakeSensitivity:
            std::snprintf(text, sizeof(text), "SHAKE SENSITIVITY: %u", static_cast<unsigned>(tuning.shake_sensitivity));
            return text;
        case ControlsRow::GyroPointer:
            return std::string("GYRO POINTER: ") + on_off(tuning.gyro_pointer) +
                   (tuning.gyro_pointer ? "  (R STICK: CAMERA)" : "  (R STICK: POINTER)");
        case ControlsRow::GyroSensitivity:
            std::snprintf(text, sizeof(text), "GYRO SENSITIVITY: %.1f",
                          static_cast<double>(tuning.gyro_sensitivity));
            return text;
        case ControlsRow::Reset:
            return std::string("RESET ") + galaxy::input::control_scheme_label(s_controls_scheme) + " TO DEFAULTS";
        }
        return {};
    }
    case 3:
        switch (row) {
        case 0: return std::string("MASTER: ") + percent_text(runtime.audio.master);
        case 1: return std::string("MUSIC: ") + percent_text(runtime.audio.music);
        case 2: return std::string("SFX: ") + percent_text(runtime.audio.sfx);
        case 3: return std::string("VOICE: ") + percent_text(runtime.audio.voice);
        case 4: return std::string("AMBIENCE: ") + percent_text(runtime.audio.ambience);
        case 5: return std::string("CUTSCENE: ") + percent_text(runtime.audio.cutscene);
        default: return {};
        }
    case 4:
        if (row == 0u) {
            std::snprintf(text, sizeof(text), "TEXT SPEED: %uX", std::clamp(runtime.text_speed, 1u, 3u));
            return text;
        }
        if (row == 1u) return std::string("LANGUAGE: ") + game_language_name(runtime.language);
        return {};
    default:
        return {};
    }
}

}  // namespace

namespace host_window {

// The internal resolution that fills the display: the smallest scale whose
// 640x456 game picture covers the monitor's current mode, or the swap chain
// in exclusive fullscreen. Higher scales cost GPU time and memory without
// adding pixels the display can show.
unsigned display_efb_scale_cap() {
    UINT width = 0u;
    UINT height = 0u;
    if (host_window::exclusive_fullscreen_active.load(std::memory_order_acquire)) {
        width = host_window::backbuffer_width;
        height = host_window::backbuffer_height;
    } else {
        const HWND window = reinterpret_cast<HWND>(
            host_window::settings_window.load(std::memory_order_acquire));
        const HMONITOR monitor = window != nullptr
            ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                mode.dmPelsWidth != 0u && mode.dmPelsHeight != 0u) {
                width = mode.dmPelsWidth;
                height = mode.dmPelsHeight;
            } else {
                width = static_cast<UINT>(info.rcMonitor.right - info.rcMonitor.left);
                height = static_cast<UINT>(info.rcMonitor.bottom - info.rcMonitor.top);
            }
        }
    }
    if (width == 0u || height == 0u) {
        return kMaxEfbScale;
    }
    const double needed = std::max(width / static_cast<double>(kEfbWidth), height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
}

bool flush_native_settings_if_dirty() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!s_native_settings_dirty.load(std::memory_order_acquire)) {
        return true;
    }
    bool runtime_saved = false;
    bool render_saved = false;
    try {
        runtime_saved = save_runtime_settings_to_file();
    } catch (...) {
        runtime_saved = false;
    }
    try {
        render_saved = save_render_config_to_file();
    } catch (...) {
        render_saved = false;
    }
    if (!runtime_saved || !render_saved) {
        const bool already_reported =
            s_native_settings_save_error.exchange(
                true,
                std::memory_order_acq_rel);
        if (!already_reported) {
            std::cerr
                << "[settings] atomic persistence failed; dirty state "
                   "retained for retry\n";
        }
        return false;
    }
    s_native_settings_dirty.store(false, std::memory_order_release);
    s_native_settings_save_error.store(false, std::memory_order_release);
    return true;
}

// Nebula Settings can save config.ini while the game runs. The game takes the file's settings as the menu's
// apply does, so the next save from the game writes them back rather than over them. Checked twice a second,
// and not while the menu is open: its draft and display confirmation belong to the settings it opened with, and
// a change made meanwhile lands once it closes.
void poll_render_config_file() {
    static std::uint64_t s_next_check_ms = 0u;
    const std::uint64_t now = GetTickCount64();
    if (now < s_next_check_ms) {
        return;
    }
    s_next_check_ms = now + 500u;
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (s_native_settings_visible.load(std::memory_order_relaxed)) {
        return;
    }
    const RenderConfig before = get_render_config();
    if (!reload_render_config_if_changed()) {
        return;
    }
    RenderConfig after = get_render_config();
    if (after.internal_follow_window) {
        // The window decides the scale, as when the menu turns Match Output on.
        after.efb_scale = before.efb_scale;
        set_render_config(after);
        host_window::follow_window_rearm.store(true, std::memory_order_release);
    }
    std::cerr << "[settings] config.ini changed outside the game; applied\n";
}

bool hidden_window_mode_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_HIDE_WINDOW") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

// The window's outer rect for a windowed client size on a monitor: limited to
// the monitor's work area and centered in it.
RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width,
                              unsigned height) {
    RECT frame{0, 0, 0, 0};
    const HMONITOR handle = MonitorFromPoint(
        POINT{monitor.bounds.left + 1, monitor.bounds.top + 1}, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 96u;
    UINT dpi_y = 96u;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const auto get_dpi = reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"));
    if (get_dpi == nullptr || FAILED(get_dpi(handle, 0, &dpi_x, &dpi_y))) {
        dpi_x = GetDpiForSystem();
    }
    (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, dpi_x);
    const long frame_w = frame.right - frame.left;
    const long frame_h = frame.bottom - frame.top;
    const long work_w = monitor.work_area.right - monitor.work_area.left;
    const long work_h = monitor.work_area.bottom - monitor.work_area.top;
    const long client_w = std::clamp<long>(static_cast<long>(width), 320, std::max(320L, work_w - frame_w));
    const long client_h = std::clamp<long>(static_cast<long>(height), 240, std::max(240L, work_h - frame_h));
    const long outer_w = client_w + frame_w;
    const long outer_h = client_h + frame_h;
    const long left = monitor.work_area.left + std::max(0L, (work_w - outer_w) / 2);
    const long top = monitor.work_area.top + std::max(0L, (work_h - outer_h) / 2);
    return RECT{left, top, left + outer_w, top + outer_h};
}

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    OpenedWindow opened;
    quit_request.store(false, std::memory_order_release);
    const bool hidden_window = hidden_window_mode_enabled();
    // The configured monitor and display mode. Exclusive fullscreen starts as
    // borderless on that monitor; the first frame's display check switches
    // the mode once the swap chain exists. Hidden test windows stay windowed.
    const RenderConfig start_config = get_render_config();
    const unsigned start_mode = hidden_window
        ? kDisplayWindowed : std::min(start_config.display_mode, kDisplayExclusive);
    const bool fullscreen_window = start_mode != kDisplayWindowed;
    const std::vector<MonitorInfo> start_monitors = enumerate_monitors();
    const MonitorInfo* start_monitor =
        find_monitor(start_monitors, widen_utf8(start_config.monitor.data()));
    int actual_window_width = window_width;
    int actual_window_height = window_height;
    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    DWORD window_style = kWindowStyle;

    if (fullscreen_window && start_monitor != nullptr) {
        const RECT& bounds = start_monitor->bounds;
        window_x = bounds.left;
        window_y = bounds.top;
        actual_window_width = static_cast<int>(bounds.right - bounds.left);
        actual_window_height = static_cast<int>(bounds.bottom - bounds.top);
        if (actual_window_width <= 0 || actual_window_height <= 0) {
            std::cerr << "[Renderer] invalid monitor dimensions\n";
            return {};
        }
        window_style = WS_POPUP;
    } else if (!hidden_window && start_monitor != nullptr && window_width > 0 &&
               window_height > 0) {
        // Windowed: limited to the monitor's work area and centered on it.
        const RECT outer = windowed_rect_on_monitor(
            *start_monitor, static_cast<unsigned>(window_width),
            static_cast<unsigned>(window_height));
        RECT frame{0, 0, 0, 0};
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW,
                                       GetDpiForSystem());
        window_x = outer.left;
        window_y = outer.top;
        actual_window_width = static_cast<int>(
            (outer.right - outer.left) - (frame.right - frame.left));
        actual_window_height = static_cast<int>(
            (outer.bottom - outer.top) - (frame.bottom - frame.top));
    }

    if (actual_window_width <= 0 || actual_window_height <= 0) {
        std::cerr << "[Renderer] invalid client dimensions\n";
        return {};
    }

    backbuffer_width   = static_cast<UINT>(actual_window_width);
    backbuffer_height  = static_cast<UINT>(actual_window_height);

    // Win32 window.
    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        // Keep a normal class cursor whenever the unfocused window receives
        // hit testing. WM_SETCURSOR hides it only while this game owns focus.
        wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
        wc.hIcon         = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
        wc.hIconSm       = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        RECT frame{0, 0, 0, 0};
        if (!fullscreen_window) {
            if (!AdjustWindowRectExForDpi(&frame, window_style, FALSE, WS_EX_APPWINDOW, GetDpiForSystem())) {
                std::cerr << "[Renderer] AdjustWindowRect failed\n";
                return {};
            }
        }
        const std::int64_t outer_width =
            static_cast<std::int64_t>(actual_window_width) +
            static_cast<std::int64_t>(frame.right) - frame.left;
        const std::int64_t outer_height =
            static_cast<std::int64_t>(actual_window_height) +
            static_cast<std::int64_t>(frame.bottom) - frame.top;
        if (outer_width <= 0 || outer_height <= 0 ||
            outer_width > std::numeric_limits<int>::max() ||
            outer_height > std::numeric_limits<int>::max()) {
            std::cerr << "[Renderer] adjusted window dimensions exceed Win32 limits\n";
            return {};
        }
        const DWORD ex_style =
            hidden_window ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
                          : WS_EX_APPWINDOW;
        opened.window = owner.start({
            kWindowClass, L"Nebula", window_style, ex_style,
            window_x, window_y, static_cast<int>(outer_width),
            static_cast<int>(outer_height), !hidden_window});
        if (opened.window == nullptr) {
            std::cerr << "[Renderer] CreateWindow failed\n";
            return {};
        }
        settings_window.store(
            reinterpret_cast<std::uintptr_t>(opened.window), std::memory_order_release);
        borderless_active.store(fullscreen_window, std::memory_order_release);
        // A centered geometry seed is not evidence that the OS cursor is in
        // the client. The first real message or authoritative cursor poll
        // acquires visibility.
        // The owner publishes real geometry/input through WM_SIZE/WM_SETFOCUS.
        // Do not reseed from this thread after it has begun processing messages.
        opened.shown = !hidden_window;
    }
    return opened;
}

void close_game_window(WindowOwner& owner) noexcept {
    owner.stop();
    pointer_hwnd.store(0u, std::memory_order_release);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kSettingsKeyMessage) {
        (void)handle_native_settings_key(wp, false, true);
        return 0;
    }
    if (msg == kPlaceWindowMessage) {
        const auto* placement = reinterpret_cast<const WindowPlacement*>(lp);
        if (placement != nullptr) {
            // Keep the window's visibility (hidden test windows stay hidden).
            const LONG_PTR visible = GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE;
            (void)SetWindowLongPtrW(
                hwnd, GWL_STYLE, static_cast<LONG_PTR>(placement->style) | visible);
            (void)SetWindowPos(
                hwnd, placement->borderless ? HWND_TOP : nullptr,
                placement->rect.left, placement->rect.top,
                placement->rect.right - placement->rect.left,
                placement->rect.bottom - placement->rect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE |
                    (placement->borderless ? 0u : SWP_NOZORDER));
            host_window::borderless_active.store(placement->borderless, std::memory_order_release);
        }
        return 0;
    }
    if (msg == WM_CREATE) {
        host_window::pointer_hwnd.store(reinterpret_cast<std::uintptr_t>(hwnd),
                             std::memory_order_release);
    }
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE) {
        (msg == WM_ENTERMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(true, std::memory_order_release);
        galaxy::host::clear_native_window_key_presses();
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
    }
    if (msg == WM_EXITSIZEMOVE &&
        !host_window::borderless_active.load(std::memory_order_acquire) &&
        !host_window::exclusive_fullscreen_active.load(std::memory_order_acquire)) {
        // A window size the user dragged to becomes the windowed size.
        RECT client{};
        if (GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
            RenderConfig render = get_render_config();
            if (render.window_width != static_cast<unsigned>(client.right) ||
                render.window_height != static_cast<unsigned>(client.bottom)) {
                render.window_width = static_cast<unsigned>(client.right);
                render.window_height = static_cast<unsigned>(client.bottom);
                set_render_config(render);
                s_native_settings_dirty.store(true, std::memory_order_release);
                // Saved now, not when the game closes: Nebula Settings reads the file meanwhile.
                if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
                    flush_native_settings_if_dirty();
                }
            }
        }
    }
    if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE) {
        (msg == WM_EXITMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(false, std::memory_order_release);
        s_pointer_focus_valid.store(GetForegroundWindow() == hwnd,
                                    std::memory_order_release);
        seed_pointer_from_client(hwnd, false);
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        // Alt+Space belongs to the native system menu, not the game's A key.
        if (msg == WM_SYSKEYDOWN && wp == VK_SPACE) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        const bool was_down =
            (static_cast<std::uintptr_t>(lp) & (std::uintptr_t{1} << 30u)) !=
            0u;
        {
            const UINT raw = static_cast<UINT>(wp);
            const UINT shift = raw == VK_SHIFT
                ? MapVirtualKeyA((static_cast<UINT>(lp) >> 16u) & 0xffu, MAPVK_VSC_TO_VK_EX) : 0u;
            const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
            s_handled_key_normalized = galaxy::input::normalize_native_window_key(
                raw, shift, (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
        }
        if (native_settings_handle_window_key(wp, was_down)) {
            return 0;
        }
        if (!galaxy::host::native_keyboard_capture_active()) {
            const UINT key = static_cast<UINT>(wp);
            const UINT mapped_shift = key == VK_SHIFT
                ? MapVirtualKeyA(
                    (static_cast<UINT>(lp) >> 16u) & 0xffu,
                    MAPVK_VSC_TO_VK_EX) : 0u;
            const std::uint32_t normalized =
                galaxy::input::normalize_native_window_key(key, mapped_shift,
                    (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
            if (!was_down) {
                galaxy::host::publish_native_window_input_origin(normalized, input_message_injected());
                galaxy::host::publish_native_window_key_press(normalized);
            }
            galaxy::host::publish_native_window_key_state(normalized, true);
        }
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        const UINT key = static_cast<UINT>(wp);
        const UINT mapped_shift = key == VK_SHIFT
            ? MapVirtualKeyA(
                (static_cast<UINT>(lp) >> 16u) & 0xffu,
                MAPVK_VSC_TO_VK_EX) : 0u;
        galaxy::host::publish_native_window_key_state(
            galaxy::input::normalize_native_window_key(key, mapped_shift,
                (static_cast<UINT>(lp) & (1u << 24u)) != 0u),
            false);
    }
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        const bool focused =
            s_pointer_focused.load(std::memory_order_relaxed) ||
            GetForegroundWindow() == hwnd;
        if (hide_os_cursor_enabled() && focused) {
            SetCursor(nullptr);
            return TRUE;
        }
    }
    if (msg == WM_MOUSEMOVE) {
        galaxy::host::publish_native_pointer_input_origin(false, input_message_injected());
        update_pointer_absolute(hwnd, lp);
        maybe_hide_os_cursor();
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Left)) ||
        (msg == WM_RBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Right)) ||
        (msg == WM_MBUTTONDOWN && native_settings_capture_mouse_button(galaxy::input::MouseButton::Middle)) ||
        (msg == WM_XBUTTONDOWN &&
         native_settings_capture_mouse_button(GET_XBUTTON_WPARAM(wp) == XBUTTON1
                                                  ? galaxy::input::MouseButton::X1
                                                  : galaxy::input::MouseButton::X2))) {
        return msg == WM_XBUTTONDOWN ? TRUE : 0;
    }
    if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        // Middle and side buttons are bindable like keys (VK_MBUTTON, VK_XBUTTON*).
        const bool down = msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
        const std::uint32_t vk = (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP)
            ? VK_MBUTTON
            : (GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
        if (!galaxy::host::native_keyboard_capture_active()) {
            if (down) galaxy::host::publish_native_window_input_origin(vk, input_message_injected());
            if (down) galaxy::host::publish_native_window_key_press(vk);
            galaxy::host::publish_native_window_key_state(vk, down);
        }
        return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) ? TRUE : 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, true, true);
        if (capture_mouse_buttons_enabled()) {
            SetCapture(hwnd);
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        update_pointer_button(hwnd, lp, true, false);
        if (capture_mouse_buttons_enabled() &&
            !s_pointer_right_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, false, true);
        if (capture_mouse_buttons_enabled()) {
            SetCapture(hwnd);
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        update_pointer_button(hwnd, lp, false, false);
        if (capture_mouse_buttons_enabled() &&
            !s_pointer_left_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
        const bool had_pressed_button =
            s_pointer_left_button.load(std::memory_order_relaxed) ||
            s_pointer_right_button.load(std::memory_order_relaxed);
        const galaxy::host::HostPointerCaptureCleanupDecision cleanup =
            galaxy::host::select_host_pointer_capture_cleanup(
                msg == WM_CANCELMODE,
                had_pressed_button);
        if (cleanup.publish_cancel_all) {
            s_pointer_transitions.cancel_unexpected_capture(
                cached_pointer_coordinates(false),
                static_cast<std::uint64_t>(GetTickCount64()));
        }
        if (cleanup.clear_window_state) {
            clear_pointer_window_state(hwnd);
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        s_tracking_mouse_leave = false;
        mark_pointer_outside();
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        const auto* suggested=reinterpret_cast<const RECT*>(lp);
        if (suggested) SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
            suggested->right-suggested->left,suggested->bottom-suggested->top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        return 0;
    }
    if (msg == WM_SIZE) {
        seed_pointer_from_client(hwnd, false);
        // wp == SIZE_MINIMIZED collapses the client rect to a bogus 0x0 (or
        // near-0) size; ignore it rather than requesting a degenerate resize
        // that would fail ResizeBuffers and leave the swap chain untouched
        // until the next real WM_SIZE anyway.
        if (wp != SIZE_MINIMIZED) {
            const int new_width = LOWORD(lp);
            const int new_height = HIWORD(lp);
            if (new_width > 0 && new_height > 0) {
                host_window::pending_resize_extent.store(
                    (static_cast<std::uint64_t>(new_width) << 32u) |
                    static_cast<std::uint32_t>(new_height), std::memory_order_release);
            }
        }
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        RECT client{};
        if (GetClientRect(hwnd, &client)) {
            FillRect(
                reinterpret_cast<HDC>(wp),
                &client,
                static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        return 1;
    }
    if (msg == WM_SETFOCUS) {
        s_pointer_focused.store(true, std::memory_order_relaxed);
        s_pointer_focus_valid.store(true, std::memory_order_release);
        seed_pointer_from_client(hwnd, false);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_MOVE) {
        (void)s_pointer_snapshot.update([](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::invalidate_host_pointer_geometry(snapshot);
        });
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        galaxy::host::clear_native_window_key_presses();
        s_pointer_focused.store(false, std::memory_order_relaxed);
        invalidate_pointer_focus_once();
        clear_pointer_window_state(hwnd);
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        return 0;
    }
    if (msg == WM_ACTIVATEAPP) {
        s_pointer_focused.store(wp != 0, std::memory_order_relaxed);
        hold_display_on(wp != 0);
        if (wp != 0) {
            s_pointer_focus_valid.store(true, std::memory_order_release);
            seed_pointer_from_client(hwnd, false);
            maybe_hide_os_cursor();
        } else {
            galaxy::host::clear_native_window_key_presses();
            invalidate_pointer_focus_once();
            clear_pointer_window_state(hwnd);
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        flush_native_settings_if_dirty();
        host_window::quit_request.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        flush_native_settings_if_dirty();
        host_window::quit_request.store(true, std::memory_order_release);
        hold_display_on(false);
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
        host_window::pointer_hwnd.store(0u, std::memory_order_release);
        s_system_menu_active.store(false, std::memory_order_release);
        host_window::move_size_active.store(false, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void draw_native_settings_overlay(UiCanvas& renderer) {
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return;
    }

    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const NativeSettingsLayout layout =
        compute_native_settings_layout(host_window::backbuffer_width, host_window::backbuffer_height, tab);
    if (!layout.supported) {
        return;
    }
    const int scale = layout.text_scale;
    const int panel_w = layout.panel_width;
    const int panel_h = layout.panel_height;
    const int panel_x = layout.panel_x;
    const int panel_y = layout.panel_y;
    const std::array<float, 4> panel{0.02f, 0.025f, 0.035f, 1.0f};
    const std::array<float, 4> rail{0.0f, 0.42f, 0.62f, 1.0f};
    const std::array<float, 4> text{0.88f, 0.94f, 0.96f, 1.0f};
    const std::array<float, 4> muted{0.42f, 0.48f, 0.50f, 1.0f};
    const std::array<float, 4> accent{0.15f, 0.72f, 0.85f, 1.0f};
    const std::array<float, 4> error{0.95f, 0.28f, 0.24f, 1.0f};
    const std::array<float, 4> selected{0.10f, 0.18f, 0.22f, 1.0f};

    ui_rect(renderer, panel_x, panel_y, panel_w, panel_h, panel);
    ui_rect(renderer, panel_x, panel_y, 6, panel_h, rail);

    // Corner decoration lifted from the game's own file-select window frame
    // (see galaxy/gx/game_asset_extract.h) so the panel reads as part of
    // Super Mario Galaxy's own UI rather than a flat placeholder. The
    // source texture is one rounded top-left corner; the other three
    // corners reuse it mirrored via UV min/max, so a single small texture
    // covers the whole panel border. Silently does nothing if the asset
    // couldn't be loaded (a different game dump, etc.) -- the solid rect
    // above already stands on its own.
    renderer.ensure_native_settings_corner_texture_loaded();
    {
        constexpr int kCornerPx = 20;
        const std::array<float, 4> corner_tint{1.0f, 1.0f, 1.0f, 0.9f};
        // top-left
        renderer.draw_ui_textured_quad(
            panel_x, panel_y, kCornerPx, kCornerPx,
            0.0f, 0.0f, 1.0f, 1.0f, corner_tint);
        // top-right (mirror U)
        renderer.draw_ui_textured_quad(
            panel_x + panel_w - kCornerPx, panel_y, kCornerPx, kCornerPx,
            1.0f, 0.0f, 0.0f, 1.0f, corner_tint);
        // bottom-left (mirror V)
        renderer.draw_ui_textured_quad(
            panel_x, panel_y + panel_h - kCornerPx, kCornerPx, kCornerPx,
            0.0f, 1.0f, 1.0f, 0.0f, corner_tint);
        // bottom-right (mirror both)
        renderer.draw_ui_textured_quad(
            panel_x + panel_w - kCornerPx, panel_y + panel_h - kCornerPx,
            kCornerPx, kCornerPx,
            1.0f, 1.0f, 0.0f, 0.0f, corner_tint);
    }

    ui_text(
        renderer,
        panel_x + 28,
        panel_y + (scale == 1 ? 10 : 24),
        scale,
        "SETTINGS",
        text);

    const char* tabs[] = {"DISPLAY", "GRAPHICS", "CONTROLS", "AUDIO", "GENERAL"};
    static_assert(std::size(tabs) == kNativeSettingsTabCount);
    int tab_x = panel_x + 28;
    // Five labels share the panel width; the longest is eight 6-unit glyphs
    // plus a gap, so the label scale shrinks on narrow panels.
    const int tab_step = (panel_w - 56) / 5;
    const int tab_scale = std::clamp(tab_step / 54, 1, scale);
    for (unsigned i = 0; i < kNativeSettingsTabCount; ++i) {
        ui_text(
            renderer,
            tab_x,
            panel_y + (scale == 1 ? 30 : 70),
            tab_scale,
            tabs[i],
            i == tab ? accent : muted);
        tab_x += tab_step;
    }

    const std::lock_guard<std::recursive_mutex> settings_lock(s_native_settings_mutex);
    expire_display_confirmation();
    expire_controls_capture();
    const RenderConfig row_render = s_settings_draft;
    const RuntimeSettings row_runtime = get_runtime_settings();
    const unsigned row_count = native_settings_row_count(tab);
    const unsigned selected_row =
        std::min(s_native_settings_row.load(std::memory_order_relaxed),
                 row_count == 0u ? 0u : row_count - 1u);
    // Pages longer than the panel scroll to keep the selection visible.
    const unsigned visible_rows = std::min(row_count, kNativeSettingsVisibleRows);
    s_settings_first_row = native_settings_first_row(s_settings_first_row, selected_row, row_count);
    if (row_count > visible_rows) {
        const int track_x = panel_x + panel_w - 18;
        const int track_y = layout.rows_y - 6;
        const int track_h = static_cast<int>(visible_rows - 1u) * layout.row_step + scale * 7 + 12;
        ui_rect(renderer, track_x, track_y, 4, track_h, selected);
        const int thumb_h = std::max(8, track_h * static_cast<int>(visible_rows) / static_cast<int>(row_count));
        const int thumb_y = track_y + (track_h - thumb_h) * static_cast<int>(s_settings_first_row) /
                                          static_cast<int>(row_count - visible_rows);
        ui_rect(renderer, track_x, thumb_y, 4, thumb_h, accent);
    }
    for (unsigned row = s_settings_first_row; row < s_settings_first_row + visible_rows; ++row) {
        const int y =
            layout.rows_y + static_cast<int>(row - s_settings_first_row) * layout.row_step;
        if (row == selected_row) {
            const int selection_height = std::max(14, scale * 7 + 4);
            ui_rect(
                renderer,
                panel_x + 24,
                y - 6,
                panel_w - 48,
                selection_height,
                selected);
            ui_text(renderer, panel_x + 36, y, scale, ">", accent);
        }
        ui_text(
            renderer,
            panel_x + 70,
            y,
            scale,
            native_settings_row_text(tab, row, row_render, row_runtime),
            text);
    }

    const int footer_y = layout.footer_y;
    const int footer_scale = std::min(2, scale);
    const int footer_step = footer_scale == 1 ? 18 : 28;
    std::string status;
    std::array<float, 4> status_color = muted;
    const std::uint64_t now_ms = GetTickCount64();
    const bool controls_page = tab == kControlsTab;
    if (controls_capturing()) {
        status = s_controls_scheme == galaxy::input::ControlScheme::Keyboard
            ? "PRESS A KEY OR MOUSE BUTTON  ESC CANCEL"
            : "PRESS A BUTTON, STICK OR TRIGGER  ESC CANCEL";
        status_color = accent;
    } else if (s_controls_message_until_ms > now_ms) {
        status = s_controls_message;
        status_color = error;
    } else if (settings_display_confirming()) {
        const std::uint64_t left_ms = s_display_confirm_deadline_ms > now_ms
            ? s_display_confirm_deadline_ms - now_ms : 0u;
        char line[96]{};
        std::snprintf(line, sizeof(line),
                      "KEEP DISPLAY? ENTER/Y KEEP  ESC/B UNDO  %u",
                      static_cast<unsigned>((left_ms + 999u) / 1000u));
        status = line;
        status_color = accent;
    } else if (s_native_settings_save_error.load(std::memory_order_acquire)) {
        status = "SAVE FAILED - RETRY ON NEXT CLOSE";
        status_color = error;
    } else if (const unsigned pending = pending_settings_changes(); pending != 0u) {
        char line[96]{};
        std::snprintf(line, sizeof(line),
                      controls_page ? "%u CHANGE%s  SPACE/Y APPLY  ESC/B DISCARD"
                                    : "%u CHANGE%s  ENTER/Y APPLY  BACKSPACE/X UNDO",
                      pending, pending == 1u ? "" : "S");
        status = line;
        status_color = accent;
    } else if (s_settings_applied_ms != 0u && now_ms - s_settings_applied_ms < 2500u) {
        status = "APPLIED";
        status_color = accent;
    } else if (GameLanguage running{}; tab == 4u && running_game_language(running) && running != row_runtime.language) {
        status = "THE NEW LANGUAGE APPLIES WHEN NEBULA RESTARTS";
        status_color = accent;
    }
    if (!status.empty()) {
        ui_text(renderer, panel_x + 28, footer_y, footer_scale, status, status_color);
    }
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step,
        footer_scale,
        "F1, VIEW+MENU OR MINUS+PLUS TOGGLE  TAB/LB/RB PAGE",
        muted);
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step * 2,
        footer_scale,
        controls_page ? "ENTER/A ADD INPUT  BACKSPACE/X CLEAR  ESC/B CLOSE"
                      : "ARROWS/D-PAD MOVE+CHANGE  ESC/B CLOSE",
        muted);
}

// The HOME Menu panel, centred over the frame. The mouse picks a row by hovering and chooses with a click.
void draw_home_menu(UiCanvas& renderer) {
    static bool s_left_was_down = true;
    if (!s_home_menu_visible.load(std::memory_order_acquire)) {
        s_left_was_down = true;
        return;
    }
    const int bb_w = static_cast<int>(host_window::backbuffer_width);
    const int bb_h = static_cast<int>(host_window::backbuffer_height);
    const int scale = bb_h >= 1440 ? 4 : (bb_h >= 720 ? 3 : 2);
    const int row_h = 15 * scale;
    const int w = std::min(bb_w - 8 * scale, 140 * scale);
    const int h = 26 * scale + row_h * static_cast<int>(kHomeMenuRows) + 14 * scale;
    const int x = (bb_w - w) / 2;
    const int y = (bb_h - h) / 2;
    const int rows_y = y + 24 * scale;
    const std::array<float, 4> panel{0.02f, 0.025f, 0.035f, 1.0f};
    const std::array<float, 4> rail{0.0f, 0.42f, 0.62f, 1.0f};
    const std::array<float, 4> text{0.88f, 0.94f, 0.96f, 1.0f};
    const std::array<float, 4> muted{0.42f, 0.48f, 0.50f, 1.0f};
    const std::array<float, 4> highlight{0.10f, 0.18f, 0.22f, 1.0f};

    const HostPointerState pointer = poll_host_pointer_state();
    if (pointer.absolute_valid && pointer.inside_client && pointer.client_width > 0 && pointer.client_height > 0) {
        const int px = pointer.client_x * bb_w / pointer.client_width;
        const int py = pointer.client_y * bb_h / pointer.client_height;
        if (px >= x && px < x + w && py >= rows_y && py < rows_y + row_h * static_cast<int>(kHomeMenuRows)) {
            const unsigned hovered = static_cast<unsigned>((py - rows_y) / row_h);
            s_home_menu_row.store(hovered, std::memory_order_relaxed);
            if (pointer.left_button && !s_left_was_down) {
                choose_home_menu_row(hovered);
            }
        }
    }
    s_left_was_down = pointer.left_button;

    ui_rect(renderer, x, y, w, h, panel);
    ui_rect(renderer, x, y, 6, h, rail);
    ui_text(renderer, x + 14 * scale, y + 8 * scale, scale, "HOME MENU", text);
    const unsigned selected = s_home_menu_row.load(std::memory_order_relaxed) % kHomeMenuRows;
    for (unsigned row = 0; row < kHomeMenuRows; ++row) {
        const int ry = rows_y + static_cast<int>(row) * row_h;
        if (row == selected) {
            ui_rect(renderer, x + 6, ry, w - 6, row_h, highlight);
        }
        ui_text(renderer, x + 14 * scale, ry + 4 * scale, scale, kHomeMenuRowText[row], row == selected ? text : muted);
    }
}

// For the first seconds of a session, until the overlay is first opened, a
// one-line hint names the keys that open it.
void draw_native_settings_hint(UiCanvas& renderer) {
    static std::uint64_t s_hint_until_ms = 0u;
    static bool s_hint_done = false;
    if (s_hint_done || !native_settings_ui_enabled()) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (s_hint_until_ms == 0u) {
        s_hint_until_ms = now + 8000u;
    }
    if (now >= s_hint_until_ms ||
        s_native_settings_visible.load(std::memory_order_relaxed)) {
        s_hint_done = true;
        return;
    }
    if (host_window::backbuffer_width < 427u || host_window::backbuffer_height < 240u) {
        return;
    }
    static constexpr const char* kHint = "F1, VIEW+MENU OR MINUS+PLUS: SETTINGS";
    const int scale = host_window::backbuffer_height >= 1440u ? 3 : (host_window::backbuffer_height >= 720u ? 2 : 1);
    int text_w = -scale;
    for (const char* ch = kHint; *ch != '\0'; ++ch) {
        text_w += (*ch == ' ' ? 4 : 6) * scale;
    }
    const int pad = 4 * scale;
    const int x = 8 * scale;
    const int y = static_cast<int>(host_window::backbuffer_height) - 8 * scale - 7 * scale - pad * 2;
    ui_rect(renderer, x, y, text_w + pad * 2, 7 * scale + pad * 2,
            {0.02f, 0.025f, 0.035f, 1.0f});
    ui_text(renderer, x + pad, y + pad, scale, kHint,
            {0.88f, 0.94f, 0.96f, 1.0f});
}

}  // namespace host_window

void set_native_settings_content_root(const std::string& utf8_path) {
    host_window::native_settings_content_root = utf8_path;
}

HostClientExtent current_host_client_extent() noexcept {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_acquire));
    RECT client{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &client)) {
        return {};
    }
    return {client.right - client.left, client.bottom - client.top};
}

HostClientExtent aspect_latch_extent() noexcept {
    const unsigned mode = std::min(get_render_config().aspect_mode, kAspectModeCount - 1u);
    if (mode != 0u) {
        // Any extent with the chosen ratio; only the ratio is latched.
        return {static_cast<int>(kAspectRatios[mode][0] * 120u),
                static_cast<int>(kAspectRatios[mode][1] * 120u)};
    }
    const HostClientExtent client = current_host_client_extent();
    if (client.width <= 1 || client.height <= 1) {
        return client;
    }
    const double aspect = static_cast<double>(client.width) / client.height;
    if (aspect < 4.0 / 3.0) {
        return {480, 360};
    }
    if (aspect > 32.0 / 9.0) {
        return {3840, 1080};
    }
    return client;
}

HostPointerState poll_host_pointer_state() noexcept {
    constexpr std::uint32_t kPointerDebugCachedEvent = 1u << 8u;
    constexpr std::uint32_t kPointerDebugCursorPollAttempted = 1u << 9u;
    constexpr std::uint32_t kPointerDebugCursorPollInside = 1u << 10u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPoll = 1u << 11u;
    constexpr std::uint32_t kPointerDebugRejectedStaleCenter = 1u << 12u;
    constexpr std::uint32_t kPointerDebugRecentClientMove = 1u << 13u;
    constexpr std::uint32_t kPointerDebugFallbackCursorPoll = 1u << 14u;
    constexpr std::uint32_t kPointerDebugLocalPollAuthority = 1u << 16u;
    constexpr std::uint32_t kPointerDebugRemoteSession = 1u << 17u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPollOutside = 1u << 18u;
    const std::uint64_t recent_pointer_move_ms = mouse_ir_message_recent_ms();
    const bool message_left_button =
        s_pointer_left_button.load(std::memory_order_relaxed);
    const bool message_right_button =
        s_pointer_right_button.load(std::memory_order_relaxed);
    const HWND hwnd =
        reinterpret_cast<HWND>(host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const galaxy::host::HostPointerSnapshot cached_snapshot =
        s_pointer_snapshot.snapshot();
    HostPointerState state{};
    state.debug_window = reinterpret_cast<std::uintptr_t>(hwnd);
    state.left_button = message_left_button;
    state.right_button = message_right_button;
    state.absolute_acquired_ms = cached_snapshot.absolute_acquired_ms;
    if (s_system_menu_active.load(std::memory_order_acquire) ||
        host_window::move_size_active.load(std::memory_order_acquire)) {
        // Native modal interaction has released guest button ownership.
        // Do not let the authoritative cursor poll reacquire focus inside it.
        state.left_button = false;
        state.right_button = false;
        return state;
    }

    // Prefer real client-area mouse messages over frame-time cursor polling. On Remote Desktop and some
    // cursor-hidden paths, GetCursorPos can report a stale centered position for long stretches while
    // WM_MOUSEMOVE still carries the live client coordinates. The seed path does not increment the event
    // sequence; startup centering is inactive until a real cursor acquisition succeeds.
    const std::uint64_t event_seq = cached_snapshot.event_sequence;
    const std::uint64_t leave_seq = cached_snapshot.leave_sequence;
    const bool cached_inside = cached_snapshot.coordinates.inside_client;
    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerClientMessageTiming client_message_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            pointer_now_ms, cached_snapshot.last_client_message_ms,
            recent_pointer_move_ms, mouse_ir_stale_center_reject_ms());
    const bool recent_client_move = client_message_timing.recent;
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const galaxy::host::HostPointerCursorPollAuthorityDecision
        cursor_poll_authority =
            galaxy::host::select_host_pointer_cursor_poll_authority(
                mouse_ir_cursor_poll_mode(), remote_session,
                recent_client_move);
    if (remote_session) {
        state.debug_flags |= kPointerDebugRemoteSession;
    }
    if (cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative && !remote_session) {
        state.debug_flags |= kPointerDebugLocalPollAuthority;
    }

    state.window_focused = s_pointer_focused.load(std::memory_order_relaxed) ||
                           (hwnd != nullptr && GetForegroundWindow() == hwnd);

    if (event_seq != 0) {
        const int width = cached_snapshot.coordinates.client_width;
        const int height = cached_snapshot.coordinates.client_height;
        if (width > 1 && height > 1) {
            state.debug_flags |= kPointerDebugCachedEvent;
            if (recent_client_move) {
                state.debug_flags |= kPointerDebugRecentClientMove;
            }
            const int cached_x =
                std::clamp(cached_snapshot.coordinates.client_x, 0, width - 1);
            const int cached_y =
                std::clamp(cached_snapshot.coordinates.client_y, 0, height - 1);
            POINT cursor{};
            if (cursor_poll_authority.poll_enabled && hwnd != nullptr &&
                GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
                state.debug_flags |= kPointerDebugCursorPollAttempted;
                const bool cursor_inside = cursor.x >= 0 && cursor.y >= 0 &&
                                           cursor.x < width &&
                                           cursor.y < height;
                if (cursor_inside) {
                    state.debug_flags |= kPointerDebugCursorPollInside;
                    const int cursor_x =
                        std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                    const int cursor_y =
                        std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                    const int previous_cursor_x =
                        s_pointer_last_cursor_poll_x.load(
                            std::memory_order_relaxed);
                    const int previous_cursor_y =
                        s_pointer_last_cursor_poll_y.load(
                            std::memory_order_relaxed);
                    const int center_x = (width - 1) / 2;
                    const int center_y = (height - 1) / 2;
                    const bool cursor_near_center =
                        std::abs(cursor_x - center_x) <= 2 &&
                        std::abs(cursor_y - center_y) <= 2;
                    const bool cached_far_from_center =
                        std::abs(cached_x - center_x) > width / 8 ||
                        std::abs(cached_y - center_y) > height / 8;
                    const bool client_move_recent_for_stale_center =
                        cursor_poll_authority.stale_center_guard_allowed &&
                        client_message_timing.stale_center_guard_active;
                    const bool likely_stale_center_poll =
                        client_move_recent_for_stale_center && cached_inside &&
                        cursor_near_center && cached_far_from_center;
                    if (likely_stale_center_poll) {
                        state.debug_flags |= kPointerDebugRejectedStaleCenter;
                    }
                    // RDP behavior varies by host: on some sessions
                    // GetCursorPos trails WM_MOUSEMOVE, while on others the
                    // client message stream stalls. Let cursor polling take
                    // over once the last client message is no longer fresh,
                    // but keep rejecting the known stale-centered sample.
                    const bool cursor_poll_recovers_visibility =
                        (!cached_inside && !recent_client_move) ||
                        event_seq <= leave_seq;
                    // Local polling is authoritative at the native input
                    // cadence. Adaptive remote polling becomes authoritative
                    // only after the client-message coordinate has aged out.
                    const galaxy::host::HostPointerCursorPollSelection
                        cursor_poll_selection =
                            galaxy::host::select_host_pointer_cursor_poll({
                                previous_cursor_x !=
                                        std::numeric_limits<int>::min() &&
                                    previous_cursor_y !=
                                        std::numeric_limits<int>::min(),
                                previous_cursor_x,
                                previous_cursor_y,
                                cursor_x,
                                cursor_y,
                                likely_stale_center_poll,
                                cursor_poll_recovers_visibility,
                                cursor_poll_authority.authoritative,
                                cached_snapshot.coordinates.absolute_valid,
                                cached_snapshot.coordinates.client_x,
                                cached_snapshot.coordinates.client_y,
                            });
                    if (cursor_poll_selection.should_publish) {
                        bool publication_accepted = false;
                        (void)s_pointer_snapshot.update(
                                [&](galaxy::host::HostPointerSnapshot&
                                        current) noexcept {
                                    const auto result = galaxy::host::
                                        apply_host_pointer_cursor_inside_publication(
                                            current,
                                            {
                                                true,
                                                cursor_x,
                                                cursor_y,
                                                pointer_now_ms,
                                                event_seq,
                                                width,
                                                height,
                                                cached_snapshot.absolute_sequence,
                                            });
                                    publication_accepted =
                                        result == galaxy::host::
                                                      HostPointerCursorInsidePublicationResult::
                                                          PublishedInside;
                                });
                        if (publication_accepted) {
                            // Only a poll that won the snapshot publication
                            // race becomes the accepted baseline. Rejected or
                            // superseded candidates must remain eligible after
                            // the authority/stale-center timeout expires.
                            s_pointer_last_cursor_poll_x.store(
                                cursor_x, std::memory_order_relaxed);
                            s_pointer_last_cursor_poll_y.store(
                                cursor_y, std::memory_order_relaxed);
                            state.debug_flags |=
                                kPointerDebugSelectedCursorPoll;
                        }
                    }
                } else {
                    bool outside_publication_selected = false;
                    (void)s_pointer_snapshot.update(
                            [&](galaxy::host::HostPointerSnapshot& current)
                                noexcept {
                                const auto result = galaxy::host::
                                    apply_host_pointer_cursor_outside_publication(
                                        current,
                                        {
                                            true,
                                            false,
                                            cursor_poll_authority.authoritative,
                                            event_seq,
                                            width,
                                            height,
                                            cached_snapshot.absolute_sequence,
                                        });
                                outside_publication_selected =
                                    result != galaxy::host::
                                                  HostPointerCursorOutsidePublicationResult::
                                                      Rejected;
                            });
                    if (outside_publication_selected) {
                        state.debug_flags |=
                            kPointerDebugSelectedCursorPollOutside;
                    }
                }
            }
            // WndProc can publish a newer movement, leave, or resize while
            // GetCursorPos/ScreenToClient is in flight. A rejected cursor
            // candidate must not return the older cached tuple to HID.
            const galaxy::host::HostPointerSnapshot final_snapshot =
                s_pointer_snapshot.snapshot();
            state.client_width = final_snapshot.coordinates.client_width;
            state.client_height = final_snapshot.coordinates.client_height;
            state.client_x = final_snapshot.coordinates.client_x;
            state.client_y = final_snapshot.coordinates.client_y;
            state.inside_client =
                galaxy::host::select_host_pointer_ir_availability(
                    state.window_focused,
                    final_snapshot.coordinates.absolute_valid,
                    final_snapshot.coordinates.inside_client)
                    .acquired_inside_available;
            if (state.inside_client) {
                maybe_hide_os_cursor();
            }
            state.absolute_valid = final_snapshot.coordinates.absolute_valid;
            state.absolute_sequence = final_snapshot.absolute_sequence;
            state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
            return state;
        }
    }

    if (hwnd != nullptr && cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative) {
        RECT client{};
        POINT cursor{};
        if (GetClientRect(hwnd, &client) && GetCursorPos(&cursor) &&
            ScreenToClient(hwnd, &cursor)) {
            state.debug_flags |= kPointerDebugFallbackCursorPoll;
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            const bool inside = cursor.x >= 0 && cursor.y >= 0 &&
                                cursor.x < width && cursor.y < height;
            if (width > 1 && height > 1 && inside) {
                const int client_x =
                    std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                const int client_y =
                    std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                const std::uint64_t fallback_now_ms =
                    static_cast<std::uint64_t>(GetTickCount64());
                bool publication_accepted = false;
                const galaxy::host::HostPointerSnapshot publication =
                    s_pointer_snapshot.update(
                        [&](galaxy::host::HostPointerSnapshot&
                                current) noexcept {
                            const auto result = galaxy::host::
                                apply_host_pointer_fallback_inside_publication(
                                    current,
                                    {
                                        client_x,
                                        client_y,
                                        width,
                                        height,
                                        fallback_now_ms,
                                        event_seq,
                                        cached_snapshot.absolute_sequence,
                                    });
                            publication_accepted =
                                result == galaxy::host::
                                              HostPointerFallbackInsidePublicationResult::
                                                  PublishedInside;
                        });
                if (publication_accepted) {
                    state.client_width = publication.coordinates.client_width;
                    state.client_height = publication.coordinates.client_height;
                    state.client_x = publication.coordinates.client_x;
                    state.client_y = publication.coordinates.client_y;
                    state.inside_client = true;
                    state.absolute_valid = true;
                    state.absolute_sequence = publication.absolute_sequence;
                    state.absolute_acquired_ms =
                        publication.absolute_acquired_ms;
                    maybe_hide_os_cursor();
                    return state;
                }
            }
        }
    }

    if (hwnd != nullptr) {
        seed_pointer_from_client(hwnd, false);
    }
    const galaxy::host::HostPointerSnapshot final_snapshot =
        s_pointer_snapshot.snapshot();
    state.inside_client =
        galaxy::host::select_host_pointer_ir_availability(
            state.window_focused, final_snapshot.coordinates.absolute_valid,
            final_snapshot.coordinates.inside_client)
            .acquired_inside_available;
    state.client_x = final_snapshot.coordinates.client_x;
    state.client_y = final_snapshot.coordinates.client_y;
    state.client_width = final_snapshot.coordinates.client_width;
    state.client_height = final_snapshot.coordinates.client_height;
    state.absolute_valid = final_snapshot.coordinates.absolute_valid;
    state.absolute_sequence = final_snapshot.absolute_sequence;
    state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
    return state;
}

host::HostPointerTransitionPoll
poll_host_pointer_transition(std::uint64_t after_sequence) noexcept {
    return s_pointer_transitions.read_after(after_sequence);
}

bool native_settings_overlay_visible() noexcept {
    return s_native_settings_visible.load(std::memory_order_relaxed);
}

bool native_window_input_captured() noexcept {
    return s_native_settings_visible.load(std::memory_order_relaxed) ||
        s_home_menu_visible.load(std::memory_order_relaxed) ||
        s_system_menu_active.load(std::memory_order_acquire) ||
        host_window::move_size_active.load(std::memory_order_acquire);
}

NativeSettingsOverlaySnapshot native_settings_overlay_snapshot() noexcept {
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (count != 0u) {
        row = std::min(row, count - 1u);
    } else {
        row = 0u;
    }
    NativeSettingsOverlaySnapshot snapshot{};
    snapshot.visible =
        s_native_settings_visible.load(std::memory_order_relaxed);
    snapshot.dirty =
        s_native_settings_dirty.load(std::memory_order_acquire);
    snapshot.save_error =
        s_native_settings_save_error.load(std::memory_order_acquire);
    snapshot.tab = tab;
    snapshot.row = row;
    snapshot.tab_count = kNativeSettingsTabCount;
    snapshot.row_count = count;
    {
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        snapshot.pending_changes = snapshot.visible ? pending_settings_changes() : 0u;
        snapshot.confirming_display = settings_display_confirming();
    }
    return snapshot;
}

NativeSettingsLayout compute_native_settings_layout(
    unsigned backbuffer_width,
    unsigned backbuffer_height,
    unsigned tab) noexcept {
    NativeSettingsLayout layout{};
    if (backbuffer_width < 427u || backbuffer_height < 240u) {
        return layout;
    }

    const int width = static_cast<int>(backbuffer_width);
    const int height = static_cast<int>(backbuffer_height);
    const bool compact =
        backbuffer_width < 854u || backbuffer_height < 480u;
    const int margin = compact
        ? 8
        : std::clamp(
              static_cast<int>(
                  std::min(backbuffer_width, backbuffer_height) / 24u),
              16,
              48);
    layout.supported = true;
    layout.panel_x = margin;
    layout.panel_y = margin;
    layout.panel_width = std::min(1040, width - margin * 2);
    layout.text_scale = compact
        ? 1
        : (backbuffer_width >= 2560u && backbuffer_height >= 1440u
               ? 4
               : (backbuffer_width >= 1280u ? 3 : 2));
    layout.rows_y = compact
        ? layout.panel_y + 46
        : layout.panel_y + 110 + (layout.text_scale - 2) * 8;
    layout.row_step = compact
        ? 14
        : std::max(32, layout.text_scale * 9 + 8);
    // Tall enough for the longest page (ten rows) above the footer.
    const int footer_block = compact ? 62 : 82;
    constexpr int kLongestPageRows = 10;
    const int needed_height = (layout.rows_y - layout.panel_y) +
        (kLongestPageRows - 1) * layout.row_step + layout.text_scale * 7 + 8 +
        footer_block;
    const int desired_height = compact
        ? 224
        : std::max(layout.text_scale >= 4 ? 480 : 420, needed_height);
    layout.panel_height = std::min(desired_height, height - margin * 2);
    layout.footer_y = layout.panel_y + layout.panel_height - footer_block;
    // A short window tightens the rows rather than dropping the menu.
    const int rows_room = layout.footer_y - 8 - layout.text_scale * 7 - layout.rows_y;
    if ((kLongestPageRows - 1) * layout.row_step > rows_room) {
        layout.row_step = std::max(layout.text_scale * 7 + 2,
                                   rows_room / (kLongestPageRows - 1));
    }

    const unsigned row_count =
        std::min(native_settings_row_count(tab), kNativeSettingsVisibleRows);
    const int final_row_bottom = row_count == 0u
        ? layout.rows_y
        : layout.rows_y +
              static_cast<int>(row_count - 1u) * layout.row_step +
              layout.text_scale * 7;
    if (layout.panel_width <= 0 || layout.panel_height <= 0 ||
        final_row_bottom + 8 > layout.footer_y) {
        return NativeSettingsLayout{};
    }
    return layout;
}

bool native_settings_handle_window_key(
    std::uintptr_t virtual_key,
    bool was_down) {
    return handle_native_settings_key(
        static_cast<WPARAM>(virtual_key),
        was_down);
}

void home_menu_open() noexcept {
    s_home_menu_row.store(0u, std::memory_order_relaxed);
    s_home_menu_choice.store(-1, std::memory_order_relaxed);
    s_home_menu_visible.store(true, std::memory_order_release);
}

int home_menu_poll() noexcept {
    return s_home_menu_choice.load(std::memory_order_acquire);
}

void home_menu_close() noexcept {
    s_home_menu_visible.store(false, std::memory_order_release);
}

bool home_menu_visible() noexcept {
    return s_home_menu_visible.load(std::memory_order_acquire);
}

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const HWND window = reinterpret_cast<HWND>(
        host_window::settings_window.load(std::memory_order_acquire));
    if (window != nullptr) {
        (void)PostMessageW(window, kSettingsKeyMessage,
                           static_cast<WPARAM>(virtual_key), 0);
    }
}

RenderConfig native_settings_draft_for_tests() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    return s_settings_draft;
}

void native_settings_reset_overlay_state_for_tests() noexcept {
    s_display_confirm_deadline_ms = 0u;
    s_capture_target = galaxy::input::kWiiTargetCount;
    s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
    s_settings_first_row = 0u;
    s_controls_message_until_ms = 0u;
    s_native_settings_visible.store(false, std::memory_order_relaxed);
    s_native_settings_dirty.store(false, std::memory_order_relaxed);
    s_native_settings_save_error.store(false, std::memory_order_relaxed);
    s_native_settings_tab.store(0u, std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
}

}  // namespace galaxy::gx
