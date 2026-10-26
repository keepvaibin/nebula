// SPDX-License-Identifier: GPL-3.0-only
// The game's Win32 window on both renderers (host_window_win32.h): the window procedure, the pointer, the settings
// overlay and HOME Menu (state, keys, drawing through a renderer's UiCanvas) and the host API that reads them.

#include "galaxy/gx/host_window_win32.h"
#include "galaxy/controller_hid.h"

#include "galaxy/gx/display_modes.h"
#include "galaxy/gx/galaxy_select.h"
#include "galaxy/gx/post_process_settings.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/streamline_bridge.h"
#include "galaxy/gx/texture_pack.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/mii_import.h"
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"
#include "galaxy/native_host.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

#include <shellapi.h>
#include <shobjidl.h>

namespace galaxy::gx {

namespace host_window {

UINT backbuffer_width = 1280;
UINT backbuffer_height = 720;
std::atomic<double> measured_present_hz{0.0};
std::atomic<std::int32_t> pointer_wheel{0};
std::string native_settings_content_root;
std::atomic<std::uintptr_t> settings_window{0};
std::atomic<bool> borderless_active{false};
std::atomic<bool> exclusive_fullscreen_active{false};
std::atomic<unsigned> efb_scale_rejected{0u};
std::atomic<std::uintptr_t> pointer_hwnd{0};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;
std::atomic<bool> follow_window_rearm{false};
// RendererD3D12::suspend_temporal_interpolation (the FRAME RATE row says so).
std::atomic<bool> temporal_interp_suspended{false};
// The session's GPU is an NVIDIA RTX (RendererD3D12::initialize): DLSS can be offered.
std::atomic<bool> rtx_adapter{false};

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
using host_window::kTestResizeMessage;
using host_window::WindowPlacement;

namespace {

#if defined(GALAXY_WITH_STREAMLINE)
constexpr bool kStreamlineBuilt = true;
#else
constexpr bool kStreamlineBuilt = false;
#endif

// DLSS offered before it is installed (defined below).
bool dlss_offered();
bool dlss_installed_now();
bool frame_generation_offered();
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live);
bool dlss_prompt_key(WPARAM key, bool was_down);
bool mods_restart_key(WPARAM key, bool was_down);
bool mods_page_key(WPARAM key, bool accept, bool was_down);
unsigned mods_draft_changes();
void commit_mods_draft();
void revert_mods_draft();
void dlss_dismiss_prompt();
void poll_dlss_download();
std::string dlss_footer(std::uint64_t now_ms, bool& error);
NativeSettingsStatus dlss_status(std::uint64_t now_ms);
constexpr int kDlssDownloadMegabytes = 31;  // NVIDIA's DLSS files, as downloaded

// Settings pages built from rows (DISPLAY, GRAPHICS, GAME, MODS, SYSTEM: settings_page): a label, the
// value for the draft, how left/right change the draft (none: the row is fixed) and a help line; a
// heading starts a section and is never selected. CONTROLS and AUDIO keep their own rows.
struct SettingsRow {
    std::string label;
    std::string value;
    std::function<void(RenderConfig&, int)> change;
    std::string help;
    bool heading = false;
    std::string key;  // names the row across rebuilds (a mod's buttons)
    std::uint8_t names = 0u;  // the mod's own words, shown as they are: 1 its label, 2 its value
    // A row of buttons (nothing to change): their words, and what pressing the one at an index does. Left and right
    // move between them; A, Space or a click presses one (Right too when there is only one).
    std::vector<std::string> buttons;
    std::function<void(RenderConfig&, unsigned)> press;
    std::uint8_t list = 0u;  // the MODS page's lists: 1 off, 2 on (a mod, or the list's title)
    bool held = false;       // a mod picked up to move
    // A setting along a range (a slider): its steps from the first value and where it is.
    std::uint16_t slider_steps = 0u;
    std::uint16_t slider_at = 0u;
};

// A row of buttons; its value is their words with '|' between them (NEBULA_SETTINGS_ROW_BUTTONS).
SettingsRow button_row(std::string label, std::vector<std::string> buttons, std::function<void(RenderConfig&, unsigned)> press,
                       std::string help) {
    SettingsRow row{std::move(label), {}, nullptr, std::move(help)};
    for (const std::string& word : buttons) {
        row.value += (row.value.empty() ? "" : "|") + word;
    }
    row.buttons = std::move(buttons);
    row.press = std::move(press);
    return row;
}
std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps);
bool settings_page_tab(unsigned tab) { return tab != 2u && tab != 3u; }

// The temporal upscaler this configuration runs (2 FSR 3, 3 DLSS), 0 none.
unsigned temporal_upscaler(const RenderConfig& render) {
    if (render.upscaler == 2u && fsr3_available()) return 2u;
    if (render.upscaler == 3u && streamline::dlss_supported()) return 3u;
    return 0u;
}

// The upscaler choices this runtime and machine offer, in menu order.
std::vector<unsigned> available_upscalers() {
    std::vector<unsigned> choices{0u, 1u};
    if (fsr3_available()) choices.push_back(2u);
    if (galaxy::gx::streamline::dlss_supported() || dlss_offered()) choices.push_back(3u);
    return choices;
}

// Output scale divided by the quality ratio (native, 1.5, 1.7, 2, 3), at least 1x.
unsigned quality_render_scale(unsigned output_scale, unsigned quality) {
    static constexpr float kRatio[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    const long scale = std::lround(static_cast<float>(output_scale) / kRatio[std::min(quality, 4u)]);
    return std::clamp(static_cast<unsigned>(std::max(scale, 1l)), 1u, std::max(output_scale, 1u));
}

// Bilinear or FSR 1 with a quality mode: the render scale follows the output like FSR 3/DLSS.
bool spatial_quality_mode(const RenderConfig& render) {
    return temporal_upscaler(render) == 0u && std::min(render.spatial_quality, 4u) >= 1u;
}

// DLSS's narrowest render scale for a quality and output scale (its mode's range starts above
// ultra performance's 3x ratio at low outputs).
unsigned dlss_min_render_scale(unsigned quality, unsigned output_scale) {
    static std::mutex mutex;
    static unsigned cached_quality = ~0u;
    static unsigned cached_output = 0u;
    static unsigned cached_scale = 1u;
    const std::lock_guard<std::mutex> lock(mutex);
    if (quality != cached_quality || output_scale != cached_output) {
        const unsigned width = dlss_min_render_width(quality, kEfbWidth * output_scale, kEfbHeight * output_scale);
        cached_quality = quality;
        cached_output = output_scale;
        cached_scale = std::max((width + kEfbWidth - 1u) / kEfbWidth, 1u);
    }
    return cached_scale;
}

// The render scale the upscaling settings give below the internal resolution `internal`.
unsigned upscaler_render_scale(const RenderConfig& render, unsigned internal) {
    const unsigned temporal = temporal_upscaler(render);
    if (temporal == 0u) {
        return spatial_quality_mode(render) ? quality_render_scale(internal, render.spatial_quality) : internal;
    }
    const unsigned quality = std::min(render.temporal_quality, 4u);
    unsigned scale = quality_render_scale(internal, quality);
    if (temporal == 3u) {
        scale = std::max(scale, dlss_min_render_scale(quality, internal));
    }
    return std::clamp(scale, 1u, std::max(internal, 1u));
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps);

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
// The HOME Menu (home_menu_open): its list, or "are you sure?" for reset and leaving; the selected answer; the choice
// the game polls (-1 none yet). The game draws it once it reads home_menu_view; draw_home_menu is the fallback. The
// window, simulation and render threads change it under s_home_menu_mutex.
std::mutex s_home_menu_mutex;
std::atomic<bool> s_home_menu_visible{false};
std::atomic<unsigned> s_home_menu_row{0};
std::atomic<int> s_home_menu_asking{0};  // the choice asked about: 0 none (the list), 1 leave, 2 reset
std::atomic<int> s_home_menu_choice{-1};
std::atomic<bool> s_home_menu_drawn_by_game{false};
constexpr unsigned kHomeMenuRows = 3;
// The list top to bottom and the choice each makes: return to the game, reset, leave (the console's Wii Menu).
constexpr const char* kHomeMenuRowText[kHomeMenuRows] = {"RETURN TO GAME", "RESET", "QUIT TO DESKTOP"};
constexpr int kHomeMenuRowChoice[kHomeMenuRows] = {0, 2, 1};
// "Are you sure?": no (selected first), then yes, as the game's own questions place them.
constexpr unsigned kHomeMenuAnswers = 2;
constexpr const char* kHomeMenuAnswerText[kHomeMenuAnswers] = {"NO", "YES"};

unsigned home_menu_answer_count() {
    return s_home_menu_asking.load(std::memory_order_relaxed) != 0 ? kHomeMenuAnswers : kHomeMenuRows;
}

void close_home_menu_with(int choice) {
    s_home_menu_choice.store(choice, std::memory_order_release);
    s_home_menu_visible.store(false, std::memory_order_release);
}

// Back (Escape, B, no): from the question to its row in the list, from the list to the game.
void back_home_menu() {
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    if (asking == 0) {
        close_home_menu_with(0);
        return;
    }
    s_home_menu_asking.store(0, std::memory_order_relaxed);
    for (unsigned row = 0; row < kHomeMenuRows; ++row) {
        if (kHomeMenuRowChoice[row] == asking) {
            s_home_menu_row.store(row, std::memory_order_relaxed);
        }
    }
}

// An answer chosen (Enter, A, a click): returning to the game closes at once; reset and leaving ask first.
void choose_home_menu_answer(unsigned answer) {
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    if (asking != 0) {
        if (answer == 1u) {
            close_home_menu_with(asking);
        } else {
            back_home_menu();
        }
        return;
    }
    const int choice = kHomeMenuRowChoice[answer % kHomeMenuRows];
    if (choice == 0) {
        close_home_menu_with(0);
        return;
    }
    s_home_menu_asking.store(choice, std::memory_order_relaxed);
    s_home_menu_row.store(0u, std::memory_order_relaxed);
}

// Keys while the HOME Menu is open (controllers arrive as keys too: the D-pad as the arrows, A as kAcceptKey, Y as
// Return, B and Menu as Escape). Every key is the menu's: the game and the settings menu wait.
bool handle_home_menu_key(WPARAM key, bool was_down) {
    if (was_down && key != VK_UP && key != VK_DOWN && key != VK_LEFT && key != VK_RIGHT) {
        return true;
    }
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    const unsigned count = home_menu_answer_count();
    const unsigned row = s_home_menu_row.load(std::memory_order_relaxed) % count;
    switch (key) {
    case VK_UP:
    case VK_LEFT:
        s_home_menu_row.store((row + count - 1u) % count, std::memory_order_relaxed);
        break;
    case VK_DOWN:
    case VK_RIGHT:
        s_home_menu_row.store((row + 1u) % count, std::memory_order_relaxed);
        break;
    case VK_RETURN:
    case VK_SPACE:
    case galaxy::input::settings_pad::kKeyAccept:
        choose_home_menu_answer(row);
        break;
    case VK_ESCAPE:
    case VK_BACK:
        back_home_menu();
        break;
    case VK_HOME:
        close_home_menu_with(0);
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
// The selected row's selected button, for the page and row it was chosen on (page << 16 | row); selecting a row
// starts it at its first.
std::atomic<unsigned> s_native_settings_button{0};
std::atomic<unsigned> s_native_settings_button_row{~0u};
// The native game draws the menu (it read native_settings_view): the overlay draws nothing of its own.
std::atomic<bool> s_settings_drawn_by_game{false};
// Leaving with changes not applied asks first: apply them, discard them, or stay. Under s_native_settings_mutex.
bool s_settings_leave_asking = false;
// Apply saved mod changes this session did not start with: the menu asks whether to restart now.
std::atomic<bool> s_mods_restart_ask{false};
// The pages, in order.
constexpr const char* kNativeSettingsTabNames[] = {"DISPLAY", "GRAPHICS", "CONTROLS", "AUDIO", "GENERAL", "MODS", "SYSTEM",
                                                   "ADVANCED"};

// Settings menu draft. Display and graphics rows edit s_settings_draft, the game's settings (the audio mix, text
// speed, button icons, language, Discord status) s_runtime_draft; Enter (Y on a controller) applies them, Backspace
// (X) reverts them and closing the menu discards them. A display change (monitor, mode, resolution, refresh rate)
// stays only when it is kept within kDisplayConfirmMs. All guarded by s_native_settings_mutex.
RenderConfig s_settings_draft{};
RuntimeSettings s_runtime_draft{};
RuntimeSettings s_runtime_base{};  // the game's settings when s_runtime_draft was taken

void reset_runtime_draft() {
    s_runtime_draft = get_runtime_settings();
    s_runtime_base = s_runtime_draft;
}
std::vector<MonitorInfo> s_settings_monitors;
std::uint64_t s_display_confirm_deadline_ms = 0u;
RenderConfig s_display_confirm_previous{};
std::uint64_t s_settings_applied_ms = 0u;
constexpr std::uint64_t kDisplayConfirmMs = 15000u;

// The display driver's monitors and modes, read at start and on WM_DISPLAYCHANGE: enumerating them can take tens of
// milliseconds, which opening the settings menu (under the lock the render thread takes) must not wait on.
std::mutex s_monitor_cache_mutex;
std::vector<MonitorInfo> s_monitor_cache;

void store_monitor_cache(std::vector<MonitorInfo> monitors) {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    s_monitor_cache = std::move(monitors);
}

std::vector<MonitorInfo> cached_monitors() {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    if (s_monitor_cache.empty()) {
        // Before the renderer has started (tests), the list is read now.
        s_monitor_cache = enumerate_monitors();
    }
    return s_monitor_cache;
}

// CONTROLS page (tab 2): a draft of the bindings/tuning and of the input source, applied together with the other
// pages. The edited device (keyboard and mouse or an attached controller) selects the scheme whose table the rows
// show. A binding row waiting for an input is s_capture_target (kWiiTargetCount: none). Guarded by
// s_native_settings_mutex.
constexpr unsigned kControlsTab = 2u;
constexpr unsigned kModsTab = 5u;
galaxy::input::ControlsConfig s_controls_draft = galaxy::input::default_controls_config();
RuntimeInputMode s_input_mode_draft = RuntimeInputMode::Auto;
galaxy::input::ControlScheme s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
galaxy::input::DeviceKey s_controls_device{};
galaxy::input::DeviceList s_controls_devices{};
std::uint64_t s_controls_devices_ms = 0u;
// Tests stand in for the attached controllers.
std::optional<std::vector<galaxy::input::ControllerInfo>> s_test_controllers;
std::optional<galaxy::input::DeviceKey> s_test_active_device;
std::size_t s_capture_target = galaxy::input::kWiiTargetCount;
std::uint64_t s_capture_deadline_ms = 0u;
constexpr std::uint64_t kCaptureTimeoutMs = 6000u;
std::string s_controls_message;
std::uint64_t s_controls_message_until_ms = 0u;
unsigned s_settings_first_row = 0u;
// The window thread's left/right-resolved code for the key being handled.
std::uint32_t s_handled_key_normalized = 0u;

constexpr unsigned kNativeSettingsTabCount = 8;

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

// Writes both settings files when something changed. Each atomic replace flushes the disk, which a busy or
// scanned disk can hold for tens of milliseconds and more.
bool write_dirty_settings_files() {
    if (!s_native_settings_dirty.exchange(false, std::memory_order_acq_rel)) {
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
        s_native_settings_dirty.store(true, std::memory_order_release);
        const bool already_reported =
            s_native_settings_save_error.exchange(
                true,
                std::memory_order_acq_rel);
        if (!already_reported) {
            std::cerr << "[settings] save failed; kept for retry\n";
        }
        return false;
    }
    s_native_settings_save_error.store(false, std::memory_order_release);
    return true;
}

// Saves run on a worker: the menu, the window thread and the render thread hold the settings lock while they
// ask for one, and the render thread takes that lock every frame the menu is open.
std::mutex s_settings_save_mutex;
std::condition_variable s_settings_save_changed;
bool s_settings_save_requested = false;  // guarded by s_settings_save_mutex
bool s_settings_save_running = false;    // guarded by s_settings_save_mutex

void settings_save_worker() {
    SetThreadDescription(GetCurrentThread(), L"Nebula Settings Save");
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(s_settings_save_mutex);
            s_settings_save_changed.wait(lock, [] { return s_settings_save_requested; });
            s_settings_save_requested = false;
            s_settings_save_running = true;
        }
        (void)write_dirty_settings_files();
        {
            const std::lock_guard<std::mutex> lock(s_settings_save_mutex);
            s_settings_save_running = false;
        }
        s_settings_save_changed.notify_all();
    }
}

bool settings_save_pending() {
    const std::lock_guard<std::mutex> lock(s_settings_save_mutex);
    return s_settings_save_requested || s_settings_save_running;
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

// The refresh rate the menu assumes in place of the display's (tests; 0 in the game).
std::atomic<unsigned> s_refresh_override_hz{0u};

// The refresh rate the picture will be shown at with these settings.
unsigned settings_refresh_hz(const RenderConfig& render) {
    if (const unsigned hz = s_refresh_override_hz.load(std::memory_order_relaxed); hz != 0u) {
        return hz;
    }
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayExclusive && render.fullscreen_refresh_hz != 0u) {
        return render.fullscreen_refresh_hz;
    }
    return monitor != nullptr ? monitor->current.refresh_hz : 0u;
}

// GRAPHICS > FRAME RATE. INTERPOLATION OFF presents the game's own frames (FRAME RATE LIMIT 30 or 60); NATIVE adds
// Nebula's interpolated frames up to the TARGET FRAME RATE; DLSS FRAME GENERATION has NVIDIA's generated frames reach
// it. The target is max_fps (0: the display's refresh rate); frame_generation selects DLSS.
enum class Interpolation : unsigned { kOff, kNative, kFrameGeneration };

Interpolation interpolation_of(const RenderConfig& render) {
    if (render.frame_generation != 0u) {
        return Interpolation::kFrameGeneration;
    }
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    return rate != 0u && rate <= 61u ? Interpolation::kOff : Interpolation::kNative;
}

// The targets a mode offers, lowest first (0: the display's refresh rate): the standard rates
// below the display's, then its own. With a temporal upscaler native interpolation runs at
// multiples of 60, as does frame generation without dynamic multi frame generation (and at most
// the multiplier the GPU runs).
std::vector<unsigned> interpolation_targets(const RenderConfig& render, Interpolation mode) {
    std::vector<unsigned> targets;
    if (mode == Interpolation::kOff) {
        return targets;
    }
    const unsigned refresh = settings_refresh_hz(render);
    const bool generated = mode == Interpolation::kFrameGeneration;
    // NVIDIA's fixed frame generation makes whole multiples of the game's 60.
    const bool sixties = generated && streamline::frame_generation_supported() && !streamline::frame_generation_dynamic();
    const unsigned most = generated && streamline::frame_generation_supported()
        ? 60u * (streamline::frame_generation_max() + 1u) : 1000u;
    for (const unsigned rate : {120u, 144u, 165u, 180u, 240u, 360u}) {
        if (refresh != 0u && rate + 1u >= refresh) {
            break;
        }
        if ((rate == 180u && !sixties) || (sixties && rate % 60u != 0u) || rate > most) {
            continue;
        }
        targets.push_back(rate);
    }
    // Its own rate: above 60, and with the multiples of 60 only where it is one (frame generation
    // takes the largest multiple at or below it).
    const bool own = refresh == 0u || (refresh > 61u && (!sixties || generated || refresh % 60u <= 1u ||
                                                          refresh % 60u >= 59u));
    if (own) {
        targets.push_back(0u);
    }
    return targets;
}

std::vector<Interpolation> interpolation_modes(const RenderConfig& render) {
    std::vector<Interpolation> modes{Interpolation::kOff};
    if (!interpolation_targets(render, Interpolation::kNative).empty()) {
        modes.push_back(Interpolation::kNative);
    }
    if (frame_generation_offered() && !interpolation_targets(render, Interpolation::kFrameGeneration).empty()) {
        modes.push_back(Interpolation::kFrameGeneration);
    }
    return modes;
}

// Keeps the draft's target one its mode offers: the same, else the highest at or below it (the
// display's rate counting as its refresh), else the lowest offered; a mode with none turns off.
void normalize_interpolation(RenderConfig& render) {
    const Interpolation mode = interpolation_of(render);
    const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
    if (mode == Interpolation::kOff) {
        render.max_fps = rate != 0u && rate <= 30u ? 30.0f : 60.0f;
        return;
    }
    const auto targets = interpolation_targets(render, mode);
    if (targets.empty()) {
        render.frame_generation = 0u;
        render.max_fps = 60.0f;
        return;
    }
    if (std::find(targets.begin(), targets.end(), rate) != targets.end()) {
        return;
    }
    const unsigned refresh = settings_refresh_hz(render);
    const unsigned wanted = rate == 0u ? (refresh != 0u ? refresh : 1000u) : rate;
    unsigned chosen = targets.front();
    for (const unsigned target : targets) {
        const unsigned value = target == 0u ? (refresh != 0u ? refresh : 1000u) : target;
        if (value <= wanted) {
            chosen = target;
        }
    }
    render.max_fps = static_cast<float>(chosen);
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

// The LIGHTING row: 0 per vertex, 1 per pixel, 2 enhanced.
unsigned lighting_mode(const RenderConfig& render) {
    return render.enhanced_lighting ? 2u : render.per_pixel_lighting ? 1u : 0u;
}

// The game's settings the menu changed since it took them (its input mode is the controls draft's), each merged into
// `live` when given: a setting changed elsewhere meanwhile keeps that value unless the menu changed it too.
unsigned runtime_draft_changes(RuntimeSettings* live) {
    const RuntimeSettings& d = s_runtime_draft;
    const RuntimeSettings& b = s_runtime_base;
    // A setting added to RuntimeSettings is drafted here too: these bindings stop compiling until it is.
    [[maybe_unused]] const auto& [mode, audio, text_speed, language, discord_status, button_icons] = d;
    [[maybe_unused]] const auto& [master, music, sfx, voice, ambience, cutscene, wiimote_speaker] = d.audio;
    RuntimeSettings scratch{};
    RuntimeSettings& out = live != nullptr ? *live : scratch;
    unsigned changes = 0u;
    const auto take = [&changes](const auto& draft, const auto& base, auto& into) {
        if (draft != base) {
            into = draft;
            ++changes;
        }
    };
    take(d.audio.master, b.audio.master, out.audio.master);
    take(d.audio.music, b.audio.music, out.audio.music);
    take(d.audio.sfx, b.audio.sfx, out.audio.sfx);
    take(d.audio.voice, b.audio.voice, out.audio.voice);
    take(d.audio.ambience, b.audio.ambience, out.audio.ambience);
    take(d.audio.cutscene, b.audio.cutscene, out.audio.cutscene);
    take(d.audio.wiimote_speaker, b.audio.wiimote_speaker, out.audio.wiimote_speaker);
    take(d.text_speed, b.text_speed, out.text_speed);
    take(d.language, b.language, out.language);
    take(d.discord_status, b.discord_status, out.discord_status);
    take(d.button_icons, b.button_icons, out.button_icons);
    return changes;
}

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
    changes += d.frame_generation != live.frame_generation;
    changes += d.aa_mode != live.aa_mode;
    changes += d.smaa_quality != live.smaa_quality;
    changes += d.msaa_samples != live.msaa_samples;
    changes += d.upscaler != live.upscaler;
    changes += d.temporal_quality != live.temporal_quality;
    changes += d.spatial_quality != live.spatial_quality;
    changes += d.sharpness != live.sharpness;
    changes += d.anisotropic_filtering != live.anisotropic_filtering;
    changes += d.enhanced_mipmaps != live.enhanced_mipmaps;
    changes += d.curved_surfaces != live.curved_surfaces;
    changes += lighting_mode(d) != lighting_mode(live);
    changes += d.ambient_occlusion != live.ambient_occlusion;
    changes += d.bloom_mode != live.bloom_mode;
    changes += d.bloom != live.bloom;
    changes += d.hud_output_resolution != live.hud_output_resolution;
    changes += d.ray_tracing_enabled != live.ray_tracing_enabled;
    changes += d.rt_quality != live.rt_quality;
    changes += d.rt_reflections != live.rt_reflections;
    changes += d.rt_water != live.rt_water;
    changes += d.rt_sun_rays != live.rt_sun_rays;
    changes += d.rt_lens_bloom != live.rt_lens_bloom;
    changes += d.texture_packs != live.texture_packs ||
        (d.texture_packs && d.texture_pack_preload != live.texture_pack_preload);
    changes += d.texture_dump != live.texture_dump;
    changes += controls_changes();
    changes += runtime_draft_changes(nullptr);
    changes += mods_draft_changes();
    return changes;
}

enum class ControlsRow {
    InputMode,
    Device,
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
    PairJoyCon,
    Reset,
};

struct ControlsRowInfo {
    ControlsRow kind = ControlsRow::Reset;
    galaxy::input::WiiTarget target = galaxy::input::WiiTarget::A;
};

constexpr unsigned kControlsBindingRow0 = 2u;

// The Pair Joy-Con screen (CONTROLS > PAIR JOY-CON): open while the controller worker pairs the halves whose L and R
// are held; it closes a moment after a pair is made, or with Escape or B.
bool s_joycon_pairing = false;
std::uint32_t s_joycon_paired_at_open = 0u;
std::uint64_t s_joycon_paired_ms = 0u;
std::uint64_t s_joycon_opened_ms = 0u;
constexpr std::uint64_t kJoyConPairedShowMs = 1500u;

// The Joy-Con halves as the controller worker has them. GALAXY_TEST_JOYCON=<bits>[,<bits>@<ms>...] (NEBULA_JOYCON_*
// bits, each from that many milliseconds after the Pair Joy-Con screen opens) stands in for them: screenshots and
// recordings of the screen without Joy-Con, 16 making a pair.
galaxy::host::JoyConStatus joycon_status_now() {
    struct Step {
        int bits = 0;
        std::uint64_t at_ms = 0;
    };
    static const std::vector<Step> test = [] {
        std::vector<Step> steps;
        char value[128]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_JOYCON") != 0 || length <= 1u) return steps;
        for (const char* p = value; *p != '\0';) {
            Step step;
            step.bits = std::atoi(p);
            while (*p != '\0' && *p != '@' && *p != ',') ++p;
            if (*p == '@') step.at_ms = static_cast<std::uint64_t>(std::atoll(++p));
            while (*p != '\0' && *p != ',') ++p;
            if (*p == ',') ++p;
            steps.push_back(step);
        }
        return steps;
    }();
    if (test.empty()) {
        return galaxy::host::joycon_status();
    }
    const std::uint64_t since = s_joycon_pairing ? GetTickCount64() - s_joycon_opened_ms : 0u;
    int bits = test.front().bits;
    for (const Step& step : test) {
        if (step.at_ms <= since) bits = step.bits;
    }
    galaxy::host::JoyConStatus status{};
    status.lefts = (bits & kJoyConLeftBit) != 0 ? 1u : 0u;
    status.rights = (bits & kJoyConRightBit) != 0 ? 1u : 0u;
    status.left_held = (bits & kJoyConLeftHeldBit) != 0;
    status.right_held = (bits & kJoyConRightHeldBit) != 0;
    status.paired = (bits & kJoyConPairedBit) != 0 ? 1u : 0u;
    status.left_presses = static_cast<std::uint8_t>(bits >> kJoyConLeftPressesShift & 15);
    status.right_presses = static_cast<std::uint8_t>(bits >> kJoyConRightPressesShift & 15);
    return status;
}

void open_joycon_pairing() {
    s_joycon_pairing = true;
    s_joycon_opened_ms = GetTickCount64();
    s_joycon_paired_at_open = joycon_status_now().paired;
    s_joycon_paired_ms = 0u;
    galaxy::host::set_joycon_pairing(true);
    std::cerr << "[settings] pair joy-con\n";
}

void close_joycon_pairing() {
    if (!s_joycon_pairing) return;
    s_joycon_pairing = false;
    galaxy::host::set_joycon_pairing(false);
}

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
    // Pairing Joy-Con is on every page (its place never moves as halves come and go).
    rows.push_back(ControlsRow::PairJoyCon);
    rows.push_back(ControlsRow::Reset);
    return rows;
}

void refresh_controls_devices(bool force);

unsigned controls_row_count() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    refresh_controls_devices(false);
    return kControlsBindingRow0 + static_cast<unsigned>(galaxy::input::kWiiTargetCount) +
           static_cast<unsigned>(controls_tail_rows().size());
}

ControlsRowInfo controls_row(unsigned row) {
    ControlsRowInfo info{};
    if (row == 0u) {
        info.kind = ControlsRow::InputMode;
    } else if (row == 1u) {
        info.kind = ControlsRow::Device;
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
        galaxy::host::arm_controller_binding_capture(s_controls_scheme, s_controls_device.slot);
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

constexpr std::uint64_t kControlsDevicesRefreshMs = 500u;

// Rebuilds the device list from what is attached and keeps the edited device
// through hot plug (a controller of the same family takes over, else the
// keyboard and mouse).
void refresh_controls_devices(bool force) {
    const std::uint64_t now = GetTickCount64();
    if (!force && s_controls_devices.count != 0u && now - s_controls_devices_ms < kControlsDevicesRefreshMs) {
        return;
    }
    s_controls_devices_ms = now;
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    std::size_t count = 0;
    std::optional<galaxy::input::DeviceKey> active;
    if (s_test_controllers) {
        count = std::min(s_test_controllers->size(), connected.size());
        std::copy_n(s_test_controllers->begin(), count, connected.begin());
        active = s_test_active_device;
    } else {
        count = galaxy::host::connected_controllers(connected);
        active = galaxy::host::last_input_device();
    }
    galaxy::input::build_device_list(std::span<const galaxy::input::ControllerInfo>(connected.data(), count),
                                     active, s_controls_devices);
    const galaxy::input::DeviceKey edited =
        s_controls_devices.entries[galaxy::input::resolve_device(s_controls_devices, s_controls_device)].key;
    if (!(edited == s_controls_device)) {
        end_controls_capture();
        s_controls_device = edited;
        s_controls_scheme = edited.scheme;
    }
}

void reset_controls_draft() {
    end_controls_capture();
    s_controls_draft = galaxy::input::get_controls_config();
    s_input_mode_draft = get_runtime_settings().input_mode;
}

unsigned native_settings_row_count(unsigned tab) {
    tab %= kNativeSettingsTabCount;
    if (settings_page_tab(tab)) {
        return static_cast<unsigned>(
            settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps()).size());
    }
    return tab == 2u ? controls_row_count() : 7u;
}

// A heading row (never selected).
bool native_settings_heading(unsigned tab, unsigned row) {
    tab %= kNativeSettingsTabCount;
    if (!settings_page_tab(tab)) {
        return false;
    }
    const auto page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    return row < page.size() && page[row].heading;
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
    applied.frame_generation = next.frame_generation;
    applied.aa_mode = next.aa_mode;
    applied.smaa_quality = next.smaa_quality;
    applied.msaa_samples = next.msaa_samples;
    applied.upscaler = next.upscaler;
    const bool dlss_asked = dlss_hold_back(applied, live);
    applied.temporal_quality = next.temporal_quality;
    applied.spatial_quality = next.spatial_quality;
    applied.sharpness = next.sharpness;
    applied.anisotropic_filtering = next.anisotropic_filtering;
    applied.enhanced_mipmaps = next.enhanced_mipmaps;
    applied.curved_surfaces = next.curved_surfaces;
    applied.per_pixel_lighting = next.per_pixel_lighting;
    applied.enhanced_lighting = next.enhanced_lighting;
    applied.ambient_occlusion = next.ambient_occlusion;
    applied.bloom_mode = next.bloom_mode;
    applied.bloom = next.bloom;
    applied.hud_output_resolution = next.hud_output_resolution;
    applied.ray_tracing_enabled = next.ray_tracing_enabled;
    applied.rt_quality = next.rt_quality;
    applied.rt_reflections = next.rt_reflections;
    applied.rt_water = next.rt_water;
    applied.rt_sun_rays = next.rt_sun_rays;
    applied.rt_lens_bloom = next.rt_lens_bloom;
    applied.texture_packs = next.texture_packs;
    applied.texture_pack_preload = next.texture_pack_preload;
    applied.texture_dump = next.texture_dump;
    const bool display_changed = display_settings_differ(live, applied);
    const unsigned changes = pending_settings_changes();
    commit_mods_draft();
    // The game's settings the menu changed, over the live ones (the controls applied the input mode).
    RuntimeSettings runtime = get_runtime_settings();
    if (runtime_draft_changes(&runtime) != 0u) {
        set_runtime_settings(runtime);
    }
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
    reset_runtime_draft();
    if (dlss_asked) {
        s_settings_draft.upscaler = 3u;  // the row shows what the question is about
    }
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
    reset_runtime_draft();
    reset_controls_draft();
    revert_mods_draft();
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
                                        s_runtime_draft,
                                        get_render_feature_caps());
    };
    const std::string before = text();
    adjust_native_setting_unlogged(delta);
    const std::string after = text();
    if (after != before) {
        std::cerr << "[settings] " << after << '\n';
    }
}

// What a step of a setting can change: the drafts, the settings that change at once, and the row's text (for what lives
// elsewhere, as the galaxy chosen to travel to). Two values may read the same (anti-aliasing's modes while MSAA runs).
struct SettingsStepState {
    RenderConfig render;
    RuntimeSettings runtime;
    galaxy::input::ControlsConfig controls;
    RuntimeInputMode input_mode;
    galaxy::input::DeviceKey device;
    std::string text;
    friend bool operator==(const SettingsStepState&, const SettingsStepState&) = default;
};

// A step that goes round at the ends (the pointer's arrows): on, or where it can go no further, back to the other end.
void step_native_setting_round(int direction) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    // A binding (on: wait for an input) and the reset (on: the defaults) are no lists: stepped as the keys step them.
    if (tab == kControlsTab &&
        (controls_row(row).kind == ControlsRow::Binding || controls_row(row).kind == ControlsRow::Reset ||
         controls_row(row).kind == ControlsRow::PairJoyCon)) {
        adjust_native_setting(direction);
        return;
    }
    const auto state = [tab, row] {
        return SettingsStepState{s_settings_draft, s_runtime_draft, s_controls_draft, s_input_mode_draft,
                                 s_controls_device,
                                 native_settings_row_text(tab, row, s_settings_draft, s_runtime_draft,
                                                          get_render_feature_caps())};
    };
    const SettingsStepState before = state();
    adjust_native_setting_unlogged(direction);
    if (state() == before) {
        // At its end: stepped back as far as it goes.
        for (unsigned guard = 0u; guard < 256u; ++guard) {
            const SettingsStepState was = state();
            adjust_native_setting_unlogged(-direction);
            if (state() == was) break;
        }
    }
    const SettingsStepState after = state();
    if (after != before) {
        std::cerr << "[settings] " << after.text << '\n';
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

    if (settings_page_tab(tab)) {
        const auto page = settings_page(tab, draft, s_runtime_draft, get_render_feature_caps());
        if (row < page.size() && page[row].change) {
            page[row].change(draft, delta);
        }
        return;
    }

    RuntimeSettings& settings = s_runtime_draft;
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
        case ControlsRow::Device: {
            refresh_controls_devices(true);
            if (s_controls_devices.count == 0u) return;
            const unsigned current = static_cast<unsigned>(
                galaxy::input::resolve_device(s_controls_devices, s_controls_device));
            const unsigned next = step(current, static_cast<unsigned>(s_controls_devices.count));
            s_controls_device = s_controls_devices.entries[next].key;
            s_controls_scheme = s_controls_device.scheme;
            return;
        }
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
        case ControlsRow::PairJoyCon:
            if (delta > 0) open_joycon_pairing();
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
        float* const gains[] = {&settings.audio.master, &settings.audio.music,
                                &settings.audio.sfx, &settings.audio.voice,
                                &settings.audio.ambience, &settings.audio.cutscene,
                                &settings.audio.wiimote_speaker};
        if (row >= std::size(gains)) return;
        *gains[row] = step_gain(*gains[row], delta);
    }
}

void move_native_setting_row(int delta) {
    end_controls_capture();
    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    if (count == 0u) {
        s_native_settings_row.store(0u, std::memory_order_relaxed);
        return;
    }
    int row = static_cast<int>(s_native_settings_row.load(std::memory_order_relaxed));
    for (unsigned tries = 0u; tries < count; ++tries) {
        row = (row + delta) % static_cast<int>(count);
        if (row < 0) {
            row += static_cast<int>(count);
        }
        if (!native_settings_heading(tab, static_cast<unsigned>(row))) {
            break;
        }
    }
    s_native_settings_row.store(static_cast<unsigned>(row), std::memory_order_relaxed);
}

// The selection never rests on a heading or past the page's end.
void settle_native_settings_row() {
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned count = native_settings_row_count(tab);
    unsigned row = std::min(s_native_settings_row.load(std::memory_order_relaxed), count == 0u ? 0u : count - 1u);
    while (row + 1u < count && native_settings_heading(tab, row)) {
        ++row;
    }
    s_native_settings_row.store(row, std::memory_order_relaxed);
}

void cycle_native_settings_tab(int delta) {
    end_controls_capture();
    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
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
    unsigned first = 0u;
    const unsigned count = native_settings_row_count(static_cast<unsigned>(tab));
    while (first + 1u < count && native_settings_heading(static_cast<unsigned>(tab), first)) {
        ++first;
    }
    s_native_settings_row.store(first, std::memory_order_relaxed);
}

void set_native_settings_visible(bool visible) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_settings_leave_asking = false;
    if (visible && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        // A fresh draft, monitor list and controller list each time the menu opens.
        s_settings_monitors = cached_monitors();
        s_settings_draft = get_render_config();
        reset_runtime_draft();
        reset_controls_draft();
        refresh_controls_devices(true);
        settle_native_settings_row();
    }
    if (!visible) {
        // Closing discards an unapplied draft, an unconfirmed display and an
        // unanswered DLSS question.
        close_joycon_pairing();
        dlss_dismiss_prompt();
        revert_display_settings();
        s_settings_draft = get_render_config();
        reset_runtime_draft();
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
        if (info.kind == ControlsRow::Reset || info.kind == ControlsRow::PairJoyCon) {
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

// The selected row's buttons: a page's row of buttons, or the CONTROLS page's reset.
std::size_t selected_settings_buttons() {
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (tab == kControlsTab) {
        const ControlsRow kind = controls_row(row).kind;
        return kind == ControlsRow::Reset || kind == ControlsRow::PairJoyCon ? 1u : 0u;
    }
    if (!settings_page_tab(tab)) {
        return 0u;
    }
    const std::vector<SettingsRow> page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    return row < page.size() ? page[row].buttons.size() : 0u;
}

unsigned selected_row_key() {
    return (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount) << 16u |
           s_native_settings_row.load(std::memory_order_relaxed);
}

unsigned selected_settings_button() {
    return s_native_settings_button_row.load(std::memory_order_relaxed) == selected_row_key()
               ? s_native_settings_button.load(std::memory_order_relaxed)
               : 0u;
}

void select_settings_button(unsigned button) {
    s_native_settings_button.store(button, std::memory_order_relaxed);
    s_native_settings_button_row.store(selected_row_key(), std::memory_order_relaxed);
}

// Presses the selected row's button `button`, logged with the row's label and the button's word.
void press_settings_button(unsigned button) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    select_settings_button(button);
    if (tab == kControlsTab) {
        adjust_native_setting(1);
        return;
    }
    if (!settings_page_tab(tab) || settings_display_confirming()) {
        return;
    }
    const std::vector<SettingsRow> page = settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    if (row >= page.size() || button >= page[row].buttons.size() || !page[row].press) {
        return;
    }
    std::cerr << "[settings] " << page[row].label << ": " << page[row].buttons[button] << '\n';
    page[row].press(s_settings_draft, button);
}

// A setting along a range, as its slider shows it: its steps from the first value and the step it is at; no steps for
// any other row. The mix's volumes run 0 to 200% in fives, the controls' numbers as adjust_native_setting steps them.
struct SliderPlace {
    unsigned steps = 0u;
    unsigned at = 0u;
};

SliderPlace settings_slider(unsigned tab, unsigned row, const std::vector<SettingsRow>& page) {
    SliderPlace place;
    if (settings_page_tab(tab)) {
        if (row < page.size()) {
            place.steps = page[row].slider_steps;
            place.at = page[row].slider_at;
        }
        return place;
    }
    const auto along = [&place](float value, float first, float step, float last) {
        place.steps = static_cast<unsigned>(std::lround((last - first) / step));
        place.at = static_cast<unsigned>(std::clamp(std::lround((value - first) / step), 0l, static_cast<long>(place.steps)));
    };
    if (tab == 3u) {
        const RuntimeSettings& settings = s_runtime_draft;
        const float gains[] = {settings.audio.master, settings.audio.music, settings.audio.sfx, settings.audio.voice,
                               settings.audio.ambience, settings.audio.cutscene, settings.audio.wiimote_speaker};
        if (row < std::size(gains)) along(gains[row], 0.0f, 0.05f, 2.0f);
        return place;
    }
    if (tab != kControlsTab) return place;
    const galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
    switch (controls_row(row).kind) {
    case ControlsRow::PointerSpeed: along(tuning.pointer_speed, 0.2f, 0.2f, 6.0f); break;
    case ControlsRow::StickDeadzone: along(tuning.stick_deadzone, 0.0f, 0.02f, 0.5f); break;
    case ControlsRow::PointerDeadzone: along(tuning.pointer_deadzone, 0.0f, 0.02f, 0.5f); break;
    case ControlsRow::PointerHide: along(tuning.pointer_hide_seconds, 0.0f, 1.0f, 30.0f); break;
    case ControlsRow::VibrationStrength: along(static_cast<float>(tuning.rumble), 10.0f, 10.0f, 100.0f); break;
    case ControlsRow::ShakeSensitivity:
        along(static_cast<float>(tuning.shake_sensitivity), 1.0f, 1.0f, static_cast<float>(galaxy::input::kShakeSensitivitySteps));
        break;
    case ControlsRow::GyroSensitivity: along(tuning.gyro_sensitivity, 0.2f, 0.1f, 5.0f); break;
    default: break;
    }
    return place;
}

// The selected setting's slider moved to step `target` (the pointer dragging it), stepped there as Left and Right would.
void set_slider(unsigned target) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    const auto place = [&] {
        return settings_slider(tab, row, settings_page_tab(tab) ? settings_page(tab, s_settings_draft, s_runtime_draft,
                                                                                get_render_feature_caps())
                                                                  : std::vector<SettingsRow>{});
    };
    const auto text = [tab, row] {
        return native_settings_row_text(tab, row, s_settings_draft, s_runtime_draft, get_render_feature_caps());
    };
    const std::string before = text();
    SliderPlace now = place();
    target = std::min(target, now.steps);
    if (tab == 3u && now.steps != 0u && now.at != target) {
        // A volume straight to its step.
        RuntimeSettings& settings = s_runtime_draft;
        float* const gains[] = {&settings.audio.master, &settings.audio.music,    &settings.audio.sfx,
                                &settings.audio.voice,  &settings.audio.ambience, &settings.audio.cutscene,
                                &settings.audio.wiimote_speaker};
        if (row < std::size(gains)) {
            *gains[row] = static_cast<float>(target) * 0.05f;
        }
    }
    for (unsigned guard = 0u; tab != 3u && now.steps != 0u && now.at != target && guard < 64u; ++guard) {
        adjust_native_setting_unlogged(now.at < target ? 1 : -1);
        const unsigned was = now.at;
        now = place();
        if (now.at == was) break;
    }
    const std::string after = text();
    if (after != before) {
        std::cerr << "[settings] " << after << '\n';
    }
}

// Closing: at once, or with changes not applied, after asking (apply them, discard them, or stay).
void request_native_settings_close() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (pending_settings_changes() != 0u) {
        s_settings_leave_asking = true;
        return;
    }
    set_native_settings_visible(false);
}

bool handle_native_settings_key(WPARAM key, bool was_down, bool from_pad = false) {
    if (s_home_menu_visible.load(std::memory_order_acquire)) {
        return handle_home_menu_key(key, was_down);
    }
    // A controller's A changes the selected setting, as Right does, and presses the selected button.
    const bool accept = key == galaxy::input::settings_pad::kKeyAccept;
    if (accept) {
        key = VK_RIGHT;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!from_pad) {
        galaxy::host::note_keyboard_input();
    }
    if (key == galaxy::host::kBindingCaptureKey && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        (void)galaxy::host::take_controller_binding_capture();
        return true;
    }
    if (s_joycon_pairing && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // The Pair Joy-Con screen: its one answer (Back) is Escape, B, A, Enter, Space, Backspace or F1; the rest
        // waits (a paired Joy-Con's L and R must not turn the pages meanwhile).
        if (!was_down && (accept || key == VK_ESCAPE || key == VK_F1 || key == VK_RETURN || key == VK_SPACE ||
                          key == VK_BACK)) {
            close_joycon_pairing();
        }
        return true;
    }
    if (s_settings_leave_asking && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // Changes not applied: Enter, Space or Y applies them and closes, Backspace or X discards them and closes,
        // Escape, B or F1 goes back to the page. Every other key waits for the answer.
        if (was_down) {
            return true;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            s_settings_leave_asking = false;
            apply_native_settings_draft();
            // A new display waits to be kept, DLSS to be downloaded and new mods to be restarted for, on the page;
            // anything else closes.
            const NativeSettingsStatus dlss = dlss_status(GetTickCount64());
            if (!settings_display_confirming() && dlss != NativeSettingsStatus::DlssAsk &&
                dlss != NativeSettingsStatus::DlssRestart && !s_mods_restart_ask.load(std::memory_order_acquire)) {
                set_native_settings_visible(false);
            }
        } else if (key == VK_BACK) {
            s_settings_leave_asking = false;
            revert_native_settings_draft();
            set_native_settings_visible(false);
        } else if (key == VK_ESCAPE || key == VK_F1) {
            s_settings_leave_asking = false;
        }
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        (dlss_prompt_key(key, was_down) || mods_restart_key(key, was_down))) {
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
            request_native_settings_close();
            return true;
        }
        set_native_settings_visible(true);
        return true;
    }
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return false;
    }
    expire_display_confirmation();
    if (mods_page_key(key, accept, was_down)) {
        return true;
    }
    switch (key) {
    case VK_ESCAPE:
        if (settings_display_confirming()) {
            revert_display_settings();
            return true;
        }
        request_native_settings_close();
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
    case VK_RIGHT:
    case VK_SPACE: {
        // A row of buttons: left and right move between them, A or Space presses one (Right too when it is the only
        // one); held, only the moves repeat.
        const std::size_t buttons = selected_settings_buttons();
        if (buttons == 0u) {
            if (key == VK_SPACE) return false;
            adjust_native_setting(key == VK_LEFT ? -1 : 1);
            return true;
        }
        const unsigned button = std::min<unsigned>(selected_settings_button(), static_cast<unsigned>(buttons) - 1u);
        if (key == VK_SPACE || accept || (key == VK_RIGHT && buttons == 1u)) {
            if (!was_down) press_settings_button(button);
        } else if (key == VK_LEFT) {
            select_settings_button(button == 0u ? 0u : button - 1u);
        } else {
            select_settings_button(std::min<unsigned>(button + 1u, static_cast<unsigned>(buttons) - 1u));
        }
        return true;
    }
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

constexpr galaxy::host::HostPointerCursorPollMode kCursorPollMode =
    galaxy::host::HostPointerCursorPollMode::Adaptive;
// A client-area mouse message counts as recent for 120 ms; a centered cursor sample after one is rejected as stale for
// 5 s (classify_host_pointer_client_message_timing).
constexpr std::uint64_t kPointerMessageRecentMs = 120u;
constexpr std::uint64_t kStaleCenterRejectMs = 5000u;

void maybe_hide_os_cursor() {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const bool focused =
        s_pointer_focused.load(std::memory_order_relaxed) ||
        (hwnd != nullptr && GetForegroundWindow() == hwnd);
    if (focused) {
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
    // Losing focus or capture releases the buttons, not the pointer: the cursor poll stays authoritative while the
    // window is unfocused, and only WM_MOUSELEAVE or a sample outside the window reports the pointer lost.
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

    // A stationary cursor inside the window sends no WM_MOUSEMOVE after launch, resize or refocus, and the seed above
    // is inactive: publish the real cursor now, with the first-poll fallback's compare-before-publish rule, so the
    // synthetic seed does not stand in for it.
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const auto poll_authority =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            kCursorPollMode, remote_session, false);
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
    // The time the message was queued, not dispatched.
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

// While the game window is in front the display stays on and the PC stays awake: Windows does not count controller
// input as activity. Window thread only.
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
    case '?': return {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u};
    case '<': return {0x02u, 0x04u, 0x08u, 0x10u, 0x08u, 0x04u, 0x02u};
    case '(': return {0x02u, 0x04u, 0x08u, 0x08u, 0x08u, 0x04u, 0x02u};
    case ')': return {0x08u, 0x04u, 0x02u, 0x02u, 0x02u, 0x04u, 0x08u};
    case '>': return {0x08u, 0x04u, 0x02u, 0x01u, 0x02u, 0x04u, 0x08u};
    // Mod names and messages use these too.
    case '\'': return {0x0Cu, 0x04u, 0x08u, 0x00u, 0x00u, 0x00u, 0x00u};
    case '"': return {0x0Au, 0x0Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x00u};
    case '!': return {0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x00u, 0x04u};
    case '#': return {0x0Au, 0x0Au, 0x1Fu, 0x0Au, 0x1Fu, 0x0Au, 0x0Au};
    case '&': return {0x0Cu, 0x12u, 0x14u, 0x08u, 0x15u, 0x12u, 0x0Du};
    case '_': return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x1Fu};
    case '[': return {0x0Eu, 0x08u, 0x08u, 0x08u, 0x08u, 0x08u, 0x0Eu};
    case ']': return {0x0Eu, 0x02u, 0x02u, 0x02u, 0x02u, 0x02u, 0x0Eu};
    case '*': return {0x00u, 0x04u, 0x15u, 0x0Eu, 0x15u, 0x04u, 0x00u};
    case ';': return {0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x0Cu, 0x04u, 0x08u};
    case '@': return {0x0Eu, 0x11u, 0x17u, 0x15u, 0x17u, 0x10u, 0x0Eu};
    case '~': return {0x00u, 0x00u, 0x08u, 0x15u, 0x02u, 0x00u, 0x00u};
    case '|': return {0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u};
    case '\\': return {0x10u, 0x10u, 0x08u, 0x04u, 0x02u, 0x01u, 0x01u};
    case '$': return {0x04u, 0x0Fu, 0x14u, 0x0Eu, 0x05u, 0x1Eu, 0x04u};
    case '{': return {0x02u, 0x04u, 0x04u, 0x08u, 0x04u, 0x04u, 0x02u};
    case '}': return {0x08u, 0x04u, 0x04u, 0x02u, 0x04u, 0x04u, 0x08u};
    case '^': return {0x04u, 0x0Au, 0x11u, 0x00u, 0x00u, 0x00u, 0x00u};
    case '`': return {0x08u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    case kGlyphCCedilla: return {0x0Eu, 0x11u, 0x10u, 0x10u, 0x11u, 0x0Eu, 0x04u};
    // A full N; ui_text draws the tilde above the cell.
    case kGlyphNTilde: return {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u};
    default: return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    }
}

// A drawn quad, not ClearRenderTargetView with a rect list: rect-limited clears fault inside the NVIDIA driver
// (nvwgf2umx.dll, STATUS_STACK_BUFFER_OVERRUN, driver 32.0.15.9597), a fail-fast that bypasses the process's SEH
// and terminate handlers. Full-target clears are unaffected.
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
        // UTF-8 Ç/ç and Ñ/ñ (C3 87/A7, C3 91/B1); any other letter of several bytes draws as one blank cell.
        if (static_cast<unsigned char>(ch) == 0xC3u && i + 1u < text.size()) {
            const auto next = static_cast<unsigned char>(text[++i]);
            ch = next == 0x87u || next == 0xA7u ? kGlyphCCedilla
               : next == 0x91u || next == 0xB1u ? kGlyphNTilde : '\0';
        } else if (static_cast<unsigned char>(ch) >= 0xC0u) {
            while (i + 1u < text.size() && (static_cast<unsigned char>(text[i + 1u]) & 0xC0u) == 0x80u) ++i;
            ch = '\0';
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
            // A two-row tilde above the cell, one row clear of the N: squeezed into the cell it reads as a plain N.
            static const std::uint64_t tilde = bitmap_of({0x0Du, 0x16u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u});
            renderer.draw_ui_glyph(cursor_x, y - 3 * scale, scale, tilde, color);
        }
        renderer.draw_ui_glyph(cursor_x, y, scale, bitmap_of(ui_glyph(ch)), color);
        cursor_x += 6 * scale;
    }
}

// The width ui_text draws `text` at (a two-byte letter is one cell).
int ui_text_width(std::string_view text, int scale) {
    int width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        // A letter of several UTF-8 bytes is one cell.
        while (i + 1u < text.size() && (static_cast<unsigned char>(text[i + 1u]) & 0xC0u) == 0x80u) {
            ++i;
        }
        width += (text[i] == ' ' ? 4 : 6) * scale;
    }
    return width;
}

// `text` shortened with "..." to `max_width`, whole letters at a time: menu text never runs into its neighbour or out
// of the panel, at any window size.
std::string ui_fit_text(std::string text, int scale, int max_width) {
    if (ui_text_width(text, scale) <= max_width) return text;
    const int dots = ui_text_width("...", scale);
    while (!text.empty() && (ui_text_width(text, scale) + dots > max_width || text.back() == ' ')) {
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    return text.empty() ? std::string() : text + "...";
}

// The footer hints name Xbox buttons; a PlayStation controller in use gets its own.
std::string pad_hints(std::string text) {
    const auto device = galaxy::host::last_input_device();
    if (!device || device->scheme != galaxy::input::ControlScheme::PlayStation) {
        return text;
    }
    static constexpr std::array<std::array<const char*, 2>, 6> kNames{{
        {"VIEW+MENU OR MINUS+PLUS", "SHARE+OPTIONS"}, {"LB/RB", "L1/R1"}, {"/A ", "/CROSS "},
        {"/B ", "/CIRCLE "}, {"/X ", "/SQUARE "}, {"/Y ", "/TRIANGLE "}}};
    for (const auto& name : kNames) {
        const std::size_t from_size = std::strlen(name[0]);
        const std::size_t to_size = std::strlen(name[1]);
        for (std::size_t at = text.find(name[0]); at != std::string::npos; at = text.find(name[0], at + to_size)) {
            text.replace(at, from_size, name[1]);
        }
    }
    return text;
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

const char* exposed_text(const RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

// A name as the menu shows it, shortened to `width` characters (the overlay's font draws it in capitals).
std::string mods_ui_text(std::string_view text, std::size_t width) {
    std::size_t characters = 0u;
    std::size_t cut = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) continue;
        if (characters == width - 3u) cut = i;
        ++characters;
    }
    return characters <= width ? std::string(text) : std::string(text.substr(0, cut)) + "...";
}

std::string upper_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

// One step of a row's choice, clamped to its ends.
unsigned settings_step(unsigned value, unsigned count, int delta) {
    return static_cast<unsigned>(std::clamp(static_cast<int>(value) + delta, 0, static_cast<int>(count) - 1));
}

std::vector<SettingsRow> display_page(const RenderConfig& render) {
    std::vector<SettingsRow> rows;
    char text[96]{};
    const MonitorInfo* monitor = settings_monitor(render);
    rows.push_back({"SCREEN", {}, nullptr, {}, true});
    {
        std::string value = "PRIMARY";
        if (monitor != nullptr) {
            std::size_t index = 0u;
            for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                if (&s_settings_monitors[i] == monitor) index = i;
            }
            const std::string name = monitor->name.empty() ? std::string("Display") : mods_ui_text(monitor->name, 22u);
            std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(index + 1u), name.c_str());
            value = text;
        }
        SettingsRow row{"MONITOR", value, nullptr, "THE DISPLAY THE GAME RUNS ON"};
        if (s_settings_monitors.size() >= 2u) {
            row.change = [](RenderConfig& draft, int delta) {
                const MonitorInfo* current = settings_monitor(draft);
                std::size_t index = 0u;
                for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
                    if (&s_settings_monitors[i] == current) index = i;
                }
                const std::size_t next = settings_step(static_cast<unsigned>(index),
                                                       static_cast<unsigned>(s_settings_monitors.size()), delta);
                if (next == index) return;
                const std::string device = narrow_utf8(s_settings_monitors[next].device);
                draft.monitor.fill('\0');
                std::memcpy(draft.monitor.data(), device.data(), std::min(device.size(), draft.monitor.size() - 1u));
                // A fullscreen mode belongs to its monitor; start from the new one's desktop mode.
                draft.fullscreen_width = 0u;
                draft.fullscreen_height = 0u;
                draft.fullscreen_refresh_hz = 0u;
                sanitize_settings_draft(draft);
                normalize_interpolation(draft);
            };
        }
        rows.push_back(std::move(row));
    }
    {
        static constexpr const char* kModes[] = {"WINDOWED", "BORDERLESS FULLSCREEN"};
        rows.push_back({"DISPLAY MODE", kModes[std::min(render.display_mode, kDisplayBorderless)],
                        [](RenderConfig& draft, int delta) {
                            draft.display_mode = settings_step(std::min(draft.display_mode, kDisplayBorderless), 2u, delta);
                            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                                snap_window_size(draft);
                            }
                            sanitize_settings_draft(draft);
                        },
                        "BORDERLESS FILLS THE SCREEN AND SWITCHES INSTANTLY"});
    }
    {
        const auto [w, h] = settings_output_size(render);
        std::snprintf(text, sizeof(text), "%uX%u", w, h);
        SettingsRow row{"RESOLUTION", text, nullptr, "BORDERLESS USES THE DESKTOP RESOLUTION"};
        if (render.display_mode == kDisplayWindowed) {
            row.help = "THE WINDOW SIZE";
            row.change = [](RenderConfig& draft, int delta) {
                const auto sizes = settings_window_sizes(settings_monitor(draft), draft.aspect_mode);  // smallest first
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
                sanitize_settings_draft(draft);
            };
        }
        rows.push_back(std::move(row));
    }
    {
        const unsigned hz = settings_refresh_hz(render);
        std::snprintf(text, sizeof(text), "%u HZ", hz != 0u ? hz : 60u);
        rows.push_back({"REFRESH RATE", text, nullptr, "SET IN THE WINDOWS DISPLAY SETTINGS"});
    }
    {
        static constexpr const char* kAspects[] = {"AUTO", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9"};
        static_assert(std::size(kAspects) == kAspectModeCount);
        rows.push_back({"ASPECT RATIO", kAspects[std::min(render.aspect_mode, kAspectModeCount - 1u)],
                        [](RenderConfig& draft, int delta) {
                            draft.aspect_mode = settings_step(draft.aspect_mode, kAspectModeCount, delta);
                            if (draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) {
                                snap_window_size(draft);
                            }
                        },
                        "AUTO MATCHES THE WINDOW. WIDER SHOWS MORE OF THE WORLD"});
    }
    rows.push_back({"VSYNC", render.vsync_interval == 0u ? "OFF" : "ON",
                    [](RenderConfig& draft, int) {
                        draft.vsync_interval = draft.vsync_interval == 0u ? 1u : 0u;
                        normalize_interpolation(draft);
                    },
                    "WAITS FOR THE DISPLAY: NO TEARING, A LITTLE MORE LATENCY"});
    rows.push_back({"INTERFACE", {}, nullptr, {}, true});
    rows.push_back({"HUD RESOLUTION", render.hud_output_resolution ? "DISPLAY" : "INTERNAL",
                    [](RenderConfig& draft, int) { draft.hud_output_resolution = !draft.hud_output_resolution; },
                    "DISPLAY DRAWS MENUS AND TEXT SHARP AT THE SCREEN RESOLUTION"});
    return rows;
}

void open_folder_in_explorer(const std::filesystem::path& folder);

std::vector<SettingsRow> graphics_page(const RenderConfig& render) {
    std::vector<SettingsRow> rows;
    char text[96]{};
    rows.push_back({"RESOLUTION", {}, nullptr, {}, true});
    {
        static constexpr const char* kUpscalers[] = {"OFF", "FSR 1", "FSR 3", "DLSS"};
        const std::vector<unsigned> choices = available_upscalers();
        const bool offered = std::find(choices.begin(), choices.end(), render.upscaler) != choices.end();
        std::string value = kUpscalers[offered ? std::min(render.upscaler, 3u) : 1u];
        if (offered && render.upscaler == 3u && !streamline::dlss_supported()) {
            value += dlss_installed_now() ? " (AFTER RESTART)" : " (DOWNLOAD)";
        }
        rows.push_back({"UPSCALING", value,
                        [](RenderConfig& draft, int delta) {
                            const std::vector<unsigned> options = available_upscalers();
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < options.size(); ++i) {
                                if (options[i] == draft.upscaler) index = i;
                            }
                            draft.upscaler = options[settings_step(static_cast<unsigned>(index),
                                                                   static_cast<unsigned>(options.size()), delta)];
                            normalize_interpolation(draft);
                        },
                        "DLSS AND FSR 3 RENDER FEWER PIXELS AND REBUILD A SHARP IMAGE"});
    }
    const unsigned internal = std::clamp(render.efb_scale, 1u, settings_scale_cap(render));
    const unsigned render_scale = upscaler_render_scale(render, internal);
    // Ray tracing replaces MSAA, ambient occlusion and the lighting modes: their rows are hidden while it is on.
    const bool rt_offered = host_window::ray_tracing_offered.load(std::memory_order_relaxed);
    const bool rt_on = rt_offered && render.ray_tracing_enabled;
    const bool upscaled = temporal_upscaler(render) != 0u || spatial_quality_mode(render);
    {
        static constexpr const char* kTemporal[] = {"NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kDlss[] = {"DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        static constexpr const char* kSpatial[] = {"NATIVE", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
        const bool temporal = render.upscaler >= 2u;
        const unsigned quality = temporal ? std::min(render.temporal_quality, 4u) : std::min(render.spatial_quality, 4u);
        const unsigned upscaler = std::min(render.upscaler, 3u);
        std::snprintf(text, sizeof(text), "%s (%uX)",
                      upscaler == 3u ? kDlss[quality] : upscaler == 2u ? kTemporal[quality] : kSpatial[quality],
                      render_scale);
        rows.push_back({upscaler == 3u ? "DLSS MODE" : upscaler == 2u ? "FSR 3 MODE" : "QUALITY MODE", text,
                        [](RenderConfig& draft, int delta) {
                            if (draft.upscaler >= 2u) {
                                draft.temporal_quality = settings_step(std::min(draft.temporal_quality, 4u), 5u, delta);
                            } else {
                                draft.spatial_quality = settings_step(std::min(draft.spatial_quality, 4u), 5u, delta);
                            }
                        },
                        "THE RENDER RESOLUTION BELOW THE INTERNAL ONE. QUALITY RENDERS THE MOST PIXELS"});
    }
    {
        std::string value = "MATCH OUTPUT";
        if (!render.internal_follow_window) {
            std::snprintf(text, sizeof(text), "%uX %uX%u%s", internal, kEfbWidth * internal, kEfbHeight * internal,
                          internal >= settings_scale_cap(render) ? " MAX" : "");
            value = text;
        }
        rows.push_back({"INTERNAL RESOLUTION", value,
                        [](RenderConfig& draft, int delta) {
                            const unsigned cap = settings_scale_cap(draft);
                            const unsigned current = draft.internal_follow_window ? 0u
                                                                                  : std::clamp(draft.efb_scale, 1u, cap);
                            const unsigned next = settings_step(current, cap + 1u, delta);
                            if (next == 0u) {
                                draft.internal_follow_window = true;
                            } else {
                                draft.internal_follow_window = false;
                                draft.efb_scale = next;
                            }
                        },
                        upscaled ? "THE UPSCALER'S OUTPUT. MATCH OUTPUT FOLLOWS THE SCREEN"
                                 : "MATCH OUTPUT RENDERS AT THE SCREEN RESOLUTION"});
    }
    std::snprintf(text, sizeof(text), "%u%%", std::min(render.sharpness, 100u));
    rows.push_back({"SHARPENING", text,
                    [](RenderConfig& draft, int delta) {
                        draft.sharpness = static_cast<unsigned>(
                            std::clamp(static_cast<int>(std::min(draft.sharpness, 100u)) + delta * 10, 0, 100));
                    },
                    "CONTRAST-ADAPTIVE SHARPENING OF THE FINAL IMAGE"});
    rows.back().slider_steps = 10u;
    rows.back().slider_at = static_cast<std::uint16_t>(std::min(render.sharpness, 100u) / 10u);
    {
        std::string value = anti_aliasing_row_text(render.aa_mode, render.smaa_quality, get_post_process_status());
        if (const std::size_t colon = value.find(": "); colon != std::string::npos) value.erase(0, colon + 2u);
        if (render.aa_mode != 0u && render.msaa_samples > 1u && render.upscaler != 2u && render.upscaler != 3u &&
            !render.ray_tracing_enabled) {
            value += " (OFF WITH MSAA)";
        }
        rows.push_back({"ANTI-ALIASING", value,
                        [](RenderConfig& draft, int delta) { draft.aa_mode = settings_step(std::min(draft.aa_mode, 3u), 4u, delta); },
                        "SMOOTHS EDGES. AUTO SKIPS IT WHEN THE IMAGE IS ALREADY SUPERSAMPLED"});
    }
    if (render.aa_mode == 1u || render.aa_mode == 2u) {
        static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
        rows.push_back({"SMAA QUALITY", kQuality[std::min(render.smaa_quality, 3u)],
                        [](RenderConfig& draft, int delta) {
                            draft.smaa_quality = settings_step(std::min(draft.smaa_quality, 3u), 4u, delta);
                        },
                        "HIGHER FINDS MORE EDGES"});
    }
    if (!rt_on) {
        const unsigned samples = std::bit_floor(std::clamp(render.msaa_samples, 1u, 8u));
        std::string value = samples <= 1u ? "OFF" : std::to_string(samples) + "X";
        if (samples > 1u && (render.upscaler == 2u || render.upscaler == 3u)) value += " (OFF WITH DLSS / FSR 3)";
        rows.push_back({"MSAA", value,
                        [](RenderConfig& draft, int delta) {
                            const unsigned index = static_cast<unsigned>(
                                std::countr_zero(std::bit_floor(std::clamp(draft.msaa_samples, 1u, 8u))));
                            draft.msaa_samples = 1u << settings_step(index, 4u, delta);
                        },
                        "SAMPLES EACH EDGE OF THE 3D SCENE SEVERAL TIMES. COSTS GPU TIME"});
    }

    if (rt_offered) {
        rows.push_back({"RAY TRACING", {}, nullptr, {}, true});
        rows.push_back({"RAY TRACING", render.ray_tracing_enabled ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.ray_tracing_enabled = !draft.ray_tracing_enabled; },
                        "TRACED SHADOWS, SKY AND BOUNCE LIGHT, GLOWS THAT LIGHT THE WORLD"});
    }
    if (rt_on) {
        // rt_quality: 0 medium, 1 high, 2 low; a change restarts the ray tracer (RendererD3D12::reconcile_ray_tracing).
        static constexpr unsigned kOrder[] = {2u, 0u, 1u};
        static constexpr const char* kNames[] = {"MEDIUM", "HIGH", "LOW"};
        rows.push_back({"RAY TRACING QUALITY", kNames[std::min(render.rt_quality, 2u)],
                        [](RenderConfig& draft, int delta) {
                            const unsigned quality = std::min(draft.rt_quality, 2u);
                            const unsigned at = quality == 2u ? 0u : quality == 0u ? 1u : 2u;
                            draft.rt_quality = kOrder[settings_step(at, 3u, delta)];
                        },
                        "HIGH: MORE LIGHTS PER PIXEL, FULL-RESOLUTION WATER"});
        // The effects players choose, each switched live.
        const auto effect_row = [&](const char* name, bool RenderConfig::*field, const char* help) {
            rows.push_back({name, render.*field ? "ON" : "OFF",
                            [field](RenderConfig& draft, int) { draft.*field = !(draft.*field); }, help});
        };
        effect_row("REFLECTIONS", &RenderConfig::rt_reflections,
                   "TRACED REFLECTIONS ON METAL, GLOSSY FLOORS, GLASS AND CRYSTAL");
        effect_row("WATER REFLECTIONS", &RenderConfig::rt_water,
                   "TRACED REFLECTIONS AND REFRACTION ON SEAS, LAKES AND POOLS");
        effect_row("SUN RAYS", &RenderConfig::rt_sun_rays, "SHAFTS OF LIGHT THE AIR SCATTERS AROUND THE SUN");
        effect_row("LENS BLOOM", &RenderConfig::rt_lens_bloom, "LIGHT THE LENS SCATTERS AROUND EVERY BRIGHT SURFACE");
    }

    rows.push_back({"FRAME RATE", {}, nullptr, {}, true});
    const Interpolation mode = interpolation_of(render);
    {
        std::string value = mode == Interpolation::kOff      ? "OFF"
                            : mode == Interpolation::kNative ? "NATIVE"
                                                             : "DLSS FRAME GENERATION";
        if (mode == Interpolation::kFrameGeneration) {
            if (!streamline::frame_generation_supported()) value += " (AFTER RESTART)";
            else if (render.upscaler != 3u) value += " (NEEDS DLSS)";
        }
        rows.push_back({"INTERPOLATION", value,
                        [](RenderConfig& draft, int delta) {
                            const auto modes = interpolation_modes(draft);
                            const Interpolation current = interpolation_of(draft);
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < modes.size(); ++i) {
                                if (modes[i] == current) index = i;
                            }
                            const Interpolation next = modes[settings_step(static_cast<unsigned>(index),
                                                                           static_cast<unsigned>(modes.size()), delta)];
                            if (next == current) return;
                            draft.frame_generation = next == Interpolation::kFrameGeneration ? 1u : 0u;
                            // Interpolation starts at the display's own rate.
                            draft.max_fps = next == Interpolation::kOff ? 60.0f : 0.0f;
                            if (next == Interpolation::kFrameGeneration && draft.upscaler != 3u) {
                                draft.upscaler = 3u;  // frame generation runs with DLSS
                            }
                            normalize_interpolation(draft);
                        },
                        mode == Interpolation::kOff ? "THE GAME RUNS AT 60. NATIVE OR DLSS ADD FRAMES BETWEEN"
                        : mode == Interpolation::kNative
                            ? "NEBULA DRAWS EXTRA FRAMES FROM THE GAME MOTION"
                            : "NVIDIA GENERATES THE FRAMES BETWEEN THE GAME'S 60"});
    }
    const double measured = host_window::measured_present_hz.load(std::memory_order_relaxed) *
                            (frame_generation_running() ? static_cast<double>(streamline::frames_presented()) : 1.0);
    char now[48]{};
    std::snprintf(now, sizeof(now), "SHOWING %.1f FPS", measured);
    if (mode == Interpolation::kOff) {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        rows.push_back({"FRAME RATE LIMIT", rate <= 30u ? "30 FPS" : "60 FPS",
                        [](RenderConfig& draft, int delta) { draft.max_fps = delta < 0 ? 30.0f : 60.0f; },
                        now});
    } else {
        const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
        if (rate == 0u) {
            const unsigned refresh = settings_refresh_hz(render);
            std::snprintf(text, sizeof(text), refresh != 0u ? "MATCH REFRESH (%u HZ)" : "MATCH REFRESH", refresh);
        } else {
            std::snprintf(text, sizeof(text), "%u FPS", rate);
        }
        rows.push_back({"TARGET FRAME RATE", text,
                        [](RenderConfig& draft, int delta) {
                            const auto targets = interpolation_targets(draft, interpolation_of(draft));
                            if (targets.empty()) return;
                            const auto current = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
                            std::size_t index = targets.size() - 1u;
                            for (std::size_t i = 0; i < targets.size(); ++i) {
                                if (targets[i] == current) index = i;
                            }
                            draft.max_fps = static_cast<float>(targets[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(targets.size()), delta)]);
                        },
                        now});
    }

    rows.push_back({"DETAIL", {}, nullptr, {}, true});
    {
        static constexpr std::array<unsigned, 5> kAnisotropy{1u, 2u, 4u, 8u, 16u};
        if (render.anisotropic_filtering <= 1u) {
            std::snprintf(text, sizeof(text), "TRILINEAR");
        } else {
            std::snprintf(text, sizeof(text), "%uX ANISOTROPIC", std::min(render.anisotropic_filtering, 16u));
        }
        rows.push_back({"TEXTURE FILTERING", text,
                        [](RenderConfig& draft, int delta) {
                            std::size_t index = 0u;
                            for (std::size_t i = 0; i < kAnisotropy.size(); ++i) {
                                if (kAnisotropy[i] <= draft.anisotropic_filtering) index = i;
                            }
                            draft.anisotropic_filtering = kAnisotropy[settings_step(
                                static_cast<unsigned>(index), static_cast<unsigned>(kAnisotropy.size()), delta)];
                        },
                        "SHARPER TEXTURES ON FLOORS AND WALLS AT AN ANGLE"});
    }
    rows.push_back({"MIPMAPS", render.enhanced_mipmaps ? "GENERATED" : "ORIGINAL",
                    [](RenderConfig& draft, int) { draft.enhanced_mipmaps = !draft.enhanced_mipmaps; },
                    "GENERATED ADDS SMOOTH DISTANT DETAIL WHERE THE GAME HAS NONE"});
    if (curved_surfaces_offered()) {
        rows.push_back({"CURVED SURFACES", render.curved_surfaces ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.curved_surfaces = !draft.curved_surfaces; },
                        "ROUNDS THE OUTLINES OF CURVED MODELS"});
    }
    if (!rt_on) {
        static constexpr const char* kLighting[] = {"PER VERTEX", "PER PIXEL", "ENHANCED"};
        rows.push_back({"LIGHTING", kLighting[lighting_mode(render)],
                        [](RenderConfig& draft, int delta) {
                            const unsigned mode = settings_step(lighting_mode(draft), 3u, delta);
                            draft.per_pixel_lighting = mode != 0u;
                            draft.enhanced_lighting = mode == 2u;
                        },
                        lighting_mode(render) == 2u
                            ? "HIGHLIGHTS, SOFT SKY LIGHT AND SHINIER METALS FROM THE GAME'S OWN LIGHTS"
                            : "PER PIXEL LIGHTS EVERY PIXEL INSTEAD OF EVERY CORNER"});
    }
    if (!rt_on) {
        rows.push_back({"AMBIENT OCCLUSION", render.ambient_occlusion ? "ON" : "OFF",
                        [](RenderConfig& draft, int) { draft.ambient_occlusion = !draft.ambient_occlusion; },
                        "SOFT SHADOWS IN CORNERS, CREASES AND WHERE THINGS MEET THE GROUND"});
    }
    {
        const bool enhanced = render.bloom_mode == kBloomEnhanced;
        rows.push_back({"BLOOM", enhanced ? "ENHANCED" : "NATIVE",
                        [](RenderConfig& draft, int) {
                            draft.bloom_mode = draft.bloom_mode == kBloomEnhanced ? kBloomNative : kBloomEnhanced;
                        },
                        enhanced ? "THE GAME'S GLOW REBUILT AT THE FULL RESOLUTION"
                                 : "THE GAME'S OWN GLOW, AS THE CONSOLE DRAWS IT"});
        if (enhanced) {
            // In steps of ten percent; 0 off.
            const unsigned value = std::min(render.bloom, kMaxBloom);
            if (value == 0u) {
                std::snprintf(text, sizeof(text), "OFF");
            } else {
                std::snprintf(text, sizeof(text), "%u%%", value);
            }
            rows.push_back({"  INTENSITY", text,
                            [](RenderConfig& draft, int delta) {
                                draft.bloom = static_cast<unsigned>(std::clamp(
                                    static_cast<int>(std::min(draft.bloom, kMaxBloom)) + delta * 10, 0,
                                    static_cast<int>(kMaxBloom)));
                            },
                            "THE ENHANCED GLOW'S STRENGTH"});
            rows.back().slider_steps = static_cast<std::uint16_t>(kMaxBloom / 10u);
            rows.back().slider_at = static_cast<std::uint16_t>(value / 10u);
        }
    }
    // Texture packs (the enabled mods') apply with the settings above; the folders of dumped textures and movies.
    rows.push_back({"TEXTURE PACKS", {}, nullptr, {}, true});
    rows.push_back({"TEXTURE PACKS", !render.texture_packs ? "OFF" : render.texture_pack_preload ? "PRELOAD" : "ON",
                    [](RenderConfig& draft, int delta) {
                        const int mode = !draft.texture_packs ? 0 : draft.texture_pack_preload ? 2 : 1;
                        const unsigned next = settings_step(static_cast<unsigned>(mode), 3u, delta);
                        draft.texture_packs = next != 0u;
                        draft.texture_pack_preload = next == 2u;
                    },
                    "ON LOADS TEXTURES AS THEY APPEAR. PRELOAD LOADS THEM ALL AT ONCE"});
    if (render.texture_packs) {
        const galaxy::gx::TexturePacks::Summary packs = galaxy::gx::TexturePacks::summary();
        char value[32]{};
        std::snprintf(value, sizeof(value), "%llu TEXTURES", static_cast<unsigned long long>(packs.textures));
        std::string help = "REPLACEMENT TEXTURES IN THE ENABLED MODS";
        if (packs.levels != 0u) help += ", WITH " + std::to_string(packs.levels) + " MIP LEVELS";
        if (!packs.unreadable.empty()) {
            help += ". " + std::to_string(packs.unreadable.size()) + " FILE NAME" +
                    (packs.unreadable.size() == 1u ? " IS" : "S ARE") + " NOT TEXTURE NAMES: " +
                    mods_ui_text(packs.unreadable.front(), 48u);
        }
        rows.push_back({"  REPLACEMENTS", packs.indexed ? std::string(value) : std::string("READING"), nullptr, help});
    }
    rows.push_back({"DUMP TEXTURES", render.texture_dump ? "ON" : "OFF",
                    [](RenderConfig& draft, int) { draft.texture_dump = !draft.texture_dump; },
                    "SAVES EVERY TEXTURE THE GAME SHOWS, FOR MAKING A PACK"});
    rows.push_back(button_row("TEXTURE DUMPS FOLDER", {"OPEN"},
                              [](RenderConfig&, unsigned) { open_folder_in_explorer(texture_dump_directory()); },
                              "WHERE DUMPED TEXTURES GO"));
    rows.push_back({"MOVIES", {}, nullptr, {}, true});
    rows.push_back(button_row("MOVIES FOLDER", {"OPEN"}, [](RenderConfig&, unsigned) { open_folder_in_explorer(movie_directory()); },
                              "VIDEOS NAMED LIKE THE GAME'S (PROLOGUEA.MP4) PLAY INSTEAD"));
    return rows;
}

// GAME > GALAXY SELECT: the galaxy and star the TRAVEL row goes to.
std::size_t s_galaxy_select_stage = 0u;
std::uint32_t s_galaxy_select_star = 1u;

// Mii import (GENERAL page): the last import's result and the console's Mii count (read again after an import).
std::mutex s_mii_mutex;
std::string s_mii_status;
std::atomic<int> s_mii_count{-1};
std::atomic<bool> s_mii_dialog_open{false};

// A picker or Explorer may open on the player's desktop: never from a hidden test run (logged instead).
bool opens_on_desktop(const char* what) {
    if (!hidden_window_mode_enabled()) {
        return true;
    }
    std::cerr << "[settings] not opened in a hidden run: " << what << '\n';
    return false;
}

// The file picker on its own thread (a COM apartment of its own; the game keeps running): the files chosen are
// imported into the console's Mii database and the running game's.
void start_mii_import_dialog() {
    if (!opens_on_desktop("the Mii picker")) {
        return;
    }
    if (s_mii_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const HWND owner = reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire));
    std::thread([owner] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> files;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            // Every console's Mii files (galaxy::host::read_mii_file); the Mii Studio code as text.
            const COMDLG_FILTERSPEC filters[] = {
                {L"Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)",
                 L"*.mii;*.rcd;*.rsd;*.mae;*.miigx;*.ncd;*.nsd;*.cfcd;*.cfsd;*.ffcd;*.ffsd;*.charinfo;*.nfsd;*.nfcd;"
                 L"*.mnms;*.emnms;*.miic;*.dat;*.bin;*.txt"},
                {L"All files", L"*.*"}};
            dialog->SetFileTypes(2u, filters);
            dialog->SetTitle(L"Import Miis");
            DWORD options = 0u;
            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
            }
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0u;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            files.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (!files.empty()) {
            std::string message;
            (void)galaxy::host::import_mii_files(files, &message);
            std::cerr << "[settings] mii import: " << message << '\n';
            {
                const std::lock_guard<std::mutex> lock(s_mii_mutex);
                s_mii_status = message;
            }
            s_mii_count.store(-1, std::memory_order_release);
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        s_mii_dialog_open.store(false, std::memory_order_release);
    }).detach();
}

// Explorer at a folder (made if missing), off the window's thread.
void open_folder_in_explorer(const std::filesystem::path& folder) {
    if (folder.empty()) {
        return;
    }
    if (!opens_on_desktop(folder.string().c_str())) {
        return;
    }
    std::thread([folder] {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    }).detach();
}

// Explorer at the Mii import folder (its files join the database at every start).
void open_mii_folder() { open_folder_in_explorer(galaxy::host::mii_import_folder()); }

// The MODS page: the mods folder's mods (galaxy::mods), read when the page first shows and on a rescan (pages are
// rebuilt for every draw). Which are on, their order and their choices are a draft until Apply saves them to mods.ini
// (they apply at the next start, and Apply offers a restart); importing, removing and rescanning save the draft first.
std::mutex s_mods_mutex;
std::vector<galaxy::mods::ModInfo> s_mods;
std::vector<galaxy::mods::ModInfo> s_mods_saved;  // as mods.ini has them
bool s_mods_scanned = false;
std::string s_mod_focus;    // the mod last selected in a list: the buttons' Options act on it
std::string s_mod_held;     // a mod picked up to move with the arrows (empty: none)
std::string s_mod_options;  // the mod whose options show instead of the lists (empty: none)

// With s_mods_mutex held.
std::vector<galaxy::mods::ModInfo>& mods_list() {
    if (!s_mods_scanned) {
        s_mods = galaxy::mods::scan_mods();
        s_mods_saved = s_mods;
        s_mods_scanned = true;
    }
    return s_mods;
}

// With s_mods_mutex held: the texture packs and movies of the mods mods.ini has on to the renderer now (disc changes
// wait for the next start).
void apply_live_mods() {
    set_texture_pack_mod_folders(galaxy::mods::texture_folders(s_mods_saved));
    set_movie_mod_folders(galaxy::mods::movie_folders(s_mods_saved));
}

// With s_mods_mutex held: the list's order, then mods.ini.
void save_mods() {
    for (std::size_t i = 0; i < s_mods.size(); ++i) {
        s_mods[i].order = static_cast<unsigned>(i + 1u);
    }
    if (!galaxy::mods::save_mod_settings(s_mods)) {
        std::cerr << "[settings] mods.ini could not be written\n";
    }
    s_mods_saved = s_mods;
    apply_live_mods();
}

bool mods_restart_pending() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    return s_mods_scanned && galaxy::mods::mods_differ_from_session(s_mods_saved);
}

void refresh_overlaps_locked();

// With s_mods_mutex held: the mods folder read again (mods.ini's state), the draft's choices kept for the mods still
// there in the draft's order; mods new to the list as the read has them, the ones on at the top.
void reread_mods_locked() {
    std::vector<galaxy::mods::ModInfo> fresh = galaxy::mods::scan_mods();
    std::vector<galaxy::mods::ModInfo> merged;
    for (const galaxy::mods::ModInfo& drafted : s_mods) {
        const auto it = std::find_if(fresh.begin(), fresh.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == drafted.id; });
        if (it == fresh.end()) continue;
        galaxy::mods::ModInfo mod = *it;
        mod.enabled = drafted.enabled;
        mod.own_text = drafted.own_text;
        for (std::size_t o = 0; o < mod.options.size() && mod.options.size() == drafted.options.size(); ++o) {
            mod.options[o].selected = drafted.options[o].selected;
        }
        merged.push_back(std::move(mod));
    }
    for (const galaxy::mods::ModInfo& mod : fresh) {
        if (std::none_of(merged.begin(), merged.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == mod.id; })) {
            merged.insert(mod.enabled ? merged.begin() : merged.end(), mod);
        }
    }
    std::stable_partition(merged.begin(), merged.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; });
    for (std::size_t i = 0; i < merged.size(); ++i) {
        merged[i].order = static_cast<unsigned>(i + 1u);
    }
    s_mods = std::move(merged);
    s_mods_saved = std::move(fresh);
    s_mods_scanned = true;
    refresh_overlaps_locked();
    apply_live_mods();
}

// With s_mods_mutex held: the mods whose state or choices differ from mods.ini's, and one more when the order of the
// mods on does.
unsigned mods_draft_changes_locked() {
    if (!s_mods_scanned) return 0u;
    unsigned changes = 0u;
    std::vector<std::string> order;
    std::vector<std::string> saved_order;
    for (const galaxy::mods::ModInfo& mod : s_mods) {
        const auto saved = std::find_if(s_mods_saved.begin(), s_mods_saved.end(),
                                        [&](const galaxy::mods::ModInfo& m) { return m.id == mod.id; });
        if (saved == s_mods_saved.end()) {
            ++changes;
            continue;
        }
        bool same = mod.enabled == saved->enabled && mod.own_text == saved->own_text &&
                    mod.options.size() == saved->options.size();
        for (std::size_t o = 0; same && o < mod.options.size(); ++o) {
            same = mod.options[o].selected == saved->options[o].selected;
        }
        changes += same ? 0u : 1u;
        if (mod.enabled && saved->enabled) order.push_back(mod.id);
    }
    for (const galaxy::mods::ModInfo& mod : s_mods_saved) {
        if (mod.enabled && std::find(order.begin(), order.end(), mod.id) != order.end()) saved_order.push_back(mod.id);
    }
    return changes + (order != saved_order ? 1u : 0u);
}

unsigned mods_draft_changes() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    return mods_draft_changes_locked();
}

// With s_mods_mutex held: the draft to mods.ini; `ask`: the menu asks to restart when this session started otherwise.
// The overlaps follow the saved order at the next read.
void commit_mods_locked(bool ask) {
    if (mods_draft_changes_locked() == 0u) return;
    save_mods();
    s_mod_held.clear();
    if (ask && galaxy::mods::mods_differ_from_session(s_mods)) {
        s_mods_restart_ask.store(true, std::memory_order_release);
    }
    s_mods_scanned = false;
}

void commit_mods_draft() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    commit_mods_locked(true);
}

void revert_mods_draft() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    s_mods = s_mods_saved;
    s_mod_held.clear();
}

// With s_mods_mutex held: who wins each conflict by the draft's order (their sizes come from the last read).
void refresh_overlaps_locked() {
    const auto place = [](const std::string& id) {
        return std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; }) -
               s_mods.begin();
    };
    for (galaxy::mods::ModInfo& mod : s_mods) {
        const auto self = place(mod.id);
        for (galaxy::mods::ModOverlap& overlap : mod.overlaps) {
            overlap.other_above = place(overlap.other) < self;
        }
    }
}

// With s_mods_mutex held: mod `id` into the list of mods on at `index` (0 the top), or off (`on` false: among the
// others by name). The draft only.
void place_mod_locked(const std::string& id, bool on, std::size_t index) {
    const auto find = [&] {
        return std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
    };
    auto it = find();
    if (it == s_mods.end()) return;
    if (it->enabled != on) {
        galaxy::mods::set_mod_enabled(s_mods, static_cast<std::size_t>(it - s_mods.begin()), on);
        it = find();
    }
    if (on) {
        const std::size_t enabled = static_cast<std::size_t>(
            std::count_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; }));
        const std::size_t from = static_cast<std::size_t>(it - s_mods.begin());
        const std::size_t to = std::min(index, enabled - 1u);
        if (from < to) {
            std::rotate(s_mods.begin() + from, s_mods.begin() + from + 1, s_mods.begin() + to + 1);
        } else if (from > to) {
            std::rotate(s_mods.begin() + to, s_mods.begin() + from, s_mods.begin() + from + 1);
        }
    }
    for (std::size_t i = 0; i < s_mods.size(); ++i) {
        s_mods[i].order = static_cast<unsigned>(i + 1u);
    }
    refresh_overlaps_locked();
}

// Mod import (the MODS page's rows and files dropped on the window): one batch at a time on its own thread; the
// IMPORT MOD row shows its progress and result.
std::mutex s_mod_import_mutex;
std::string s_mod_import_value;    // the row's value: progress, then the outcome
std::string s_mod_import_message;  // the outcome, the row's help
std::atomic<bool> s_mod_import_busy{false};
std::atomic<bool> s_mod_dialog_open{false};

void run_mod_imports(std::vector<std::filesystem::path> sources) {
    if (sources.empty()) return;
    if (s_mod_import_busy.exchange(true, std::memory_order_acq_rel)) {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        s_mod_import_message = "AN IMPORT IS RUNNING: TRY AGAIN WHEN IT ENDS";
        return;
    }
    std::thread([sources = std::move(sources)] {
        const std::filesystem::path folder = galaxy::mods::mods_folder();
        std::string message;
        unsigned imported = 0;
        unsigned failed = 0;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto progress = [&](const galaxy::mods::ImportProgress& p) {
                char text[32]{};
                const unsigned percent =
                    p.total != 0u ? static_cast<unsigned>(std::min<std::uint64_t>(99u, p.done * 100u / p.total)) : 0u;
                if (sources.size() > 1u) {
                    std::snprintf(text, sizeof(text), "%zu/%zu: %u%%", i + 1u, sources.size(), percent);
                } else {
                    std::snprintf(text, sizeof(text), "IMPORTING %u%%", percent);
                }
                const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
                s_mod_import_value = text;
            };
            const galaxy::mods::ImportResult result =
                folder.empty() ? galaxy::mods::ImportResult{false, {}, false, 0, 0, {}, "NO MODS FOLDER"}
                               : galaxy::mods::import_mod(sources[i], folder, progress);
            std::cerr << "[mods] import " << sources[i].string() << ": " << result.message << " (" << result.files
                      << " files, " << (result.bytes >> 20u) << " MB)\n";
            (result.ok ? imported : failed) += 1u;
            message = message.empty() ? result.message : message + "; " + result.message;
        }
        {
            const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
            s_mod_import_value = failed == 0u ? "DONE" : imported == 0u ? "FAILED" : "SOME FAILED";
            s_mod_import_message = message + ". MODS APPLY WHEN NEBULA RESTARTS";
        }
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            reread_mods_locked();
        }
        s_mod_import_busy.store(false, std::memory_order_release);
    }).detach();
}

// The picker for archives or folders on its own thread (the game keeps running).
void start_mod_import_dialog(bool folders) {
    if (!opens_on_desktop("the mod picker")) {
        return;
    }
    if (s_mod_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const HWND owner = reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire));
    std::thread([owner, folders] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> picked;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0u;
            dialog->GetOptions(&options);
            options |= FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            if (folders) {
                options |= FOS_PICKFOLDERS;
                dialog->SetTitle(L"Import mod folders");
            } else {
                const COMDLG_FILTERSPEC filters[] = {
                    {L"Mods and texture packs (zip, 7z, rar, tar)",
                     L"*.zip;*.7z;*.rar;*.tar;*.tgz;*.gz;*.txz;*.xz;*.tbz2;*.bz2;*.tzst;*.zst"},
                    {L"All files", L"*.*"}};
                dialog->SetFileTypes(2u, filters);
                dialog->SetTitle(L"Import mods");
                options |= FOS_FILEMUSTEXIST;
            }
            dialog->SetOptions(options);
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0u;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            picked.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) CoUninitialize();
        s_mod_dialog_open.store(false, std::memory_order_release);
        run_mod_imports(std::move(picked));
    }).detach();
}

std::vector<SettingsRow> mods_page(const RenderConfig& render);

// The name of a language folder as the menu says it.
std::string language_name(std::string_view folder) {
    static constexpr std::pair<const char*, const char*> kNames[] = {
        {"English", "ENGLISH"}, {"French", "FRENCH"}, {"Spanish", "SPANISH"}, {"German", "GERMAN"},
        {"Italian", "ITALIAN"}, {"Dutch", "DUTCH"}, {"Japanese", "JAPANESE"}, {"Korean", "KOREAN"},
        {"SimpChinese", "CHINESE"}, {"TradChinese", "CHINESE"}};
    for (const auto& [suffix, name] : kNames) {
        if (folder.ends_with(suffix)) return name;
    }
    return mods_ui_text(folder, 12u);
}

// The language folder the next start plays in (the language setting applies at the next start, as mods do).
std::string next_start_language_folder() {
    std::string game_id = galaxy::mods::mods_game_id();
    if (game_id.empty()) game_id = "RMGE01";
    return galaxy::mods::language_folder(game_id, static_cast<unsigned>(get_runtime_settings().language));
}

// The selection to a row of the MODS page (after a change moved it).
void select_mods_row(const RenderConfig& render, const std::string& key) {
    const std::vector<SettingsRow> rows = mods_page(render);
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (rows[r].key == key) {
            s_native_settings_row.store(static_cast<unsigned>(r), std::memory_order_relaxed);
            return;
        }
    }
}

// What a mod changes, its conflicts by the draft's order, or why it does nothing: the line under the page.
std::string mod_details(const galaxy::mods::ModInfo& mod) {
    if (!mod.problem.empty() && !mod.files && !mod.textures && !mod.movies && !mod.loose) {
        return mods_ui_text(mod.problem, 120u);
    }
    std::string kinds;
    for (const auto& [on, name] : {std::pair{mod.riivolution, "RIIVOLUTION"}, std::pair{mod.files || mod.loose, "FILES"},
                                   std::pair{mod.textures, "TEXTURES"}, std::pair{mod.movies, "MOVIES"}}) {
        if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
    }
    if (!mod.languages.empty()) {
        std::string languages;
        for (const std::string& folder : mod.languages) {
            const std::string name = language_name(folder);
            if (languages.find(name) == std::string::npos) languages += languages.empty() ? name : ", " + name;
        }
        kinds += ". TEXT: " + languages;
    }
    if (mod.code_patches != 0u) {
        kinds += ". " + std::to_string(mod.code_patches) + " CODE CHANGE" + (mod.code_patches == 1u ? "" : "S") +
                 " CANNOT RUN IN NEBULA";
    }
    for (const galaxy::mods::ModOverlap& overlap : mod.overlaps) {
        std::string what;
        if (overlap.files != 0u) what = std::to_string(overlap.files) + (overlap.files == 1u ? " FILE" : " FILES");
        if (overlap.textures != 0u) {
            what += (what.empty() ? "" : ", ") + std::to_string(overlap.textures) +
                    (overlap.textures == 1u ? " TEXTURE" : " TEXTURES");
        }
        // The other mod's name apart from the words, so that the menu can name both ("OVERRIDDEN BY: X (2 FILES)").
        kinds += std::string(overlap.other_above ? ". OVERRIDDEN BY: " : ". OVERRIDES: ") + mods_ui_text(overlap.other, 24u) +
                 " (" + what + ")";
    }
    return kinds.empty() ? std::string("NOTHING THIS GAME USES") : kinds;
}

void close_mod_options(const RenderConfig& render);

// Whether a mod has options to show: its Riivolution choices, or the text it shows in a language it lacks.
bool mod_has_options(const galaxy::mods::ModInfo& mod, const std::string& playing) {
    const bool covers = std::find(mod.languages.begin(), mod.languages.end(), playing) != mod.languages.end();
    return !mod.options.empty() || (!mod.languages.empty() && !covers && !playing.empty());
}

// A mod's options instead of the lists: its Riivolution choices and the text it shows, part of the draft. Done,
// Escape or B goes back to the lists. With s_mods_mutex held.
std::vector<SettingsRow> mod_options_page(std::size_t i) {
    std::vector<SettingsRow> rows;
    const galaxy::mods::ModInfo& mod = s_mods[i];
    SettingsRow title{mods_ui_text(mod.id, 26u), {}, nullptr, {}, true};
    title.names = 1u;
    rows.push_back(std::move(title));
    for (std::size_t o = 0; o < mod.options.size(); ++o) {
        const galaxy::mods::ModOption& option = mod.options[o];
        const std::string value = option.selected == 0u || option.selected > option.choices.size()
                                      ? std::string("OFF")
                                      : mods_ui_text(option.choices[option.selected - 1u], 22u);
        const bool other_region = option.selected != 0u && option.selected <= option.other_region.size() &&
                                  option.other_region[option.selected - 1u];
        // A mod's option name as its menu shows it, without the colon some end in.
        std::string_view name = option.name;
        while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
        SettingsRow choice{mods_ui_text(name, 24u), value,
                           [i, o](RenderConfig&, int delta) {
                               const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                               if (i >= s_mods.size() || o >= s_mods[i].options.size()) return;
                               galaxy::mods::ModOption& changed = s_mods[i].options[o];
                               changed.selected = settings_step(
                                   std::min<unsigned>(changed.selected, static_cast<unsigned>(changed.choices.size())),
                                   static_cast<unsigned>(changed.choices.size() + 1u), delta);
                           },
                           other_region ? std::string("MADE FOR ANOTHER REGION'S DISC: IT MAY NOT WORK WITH THIS ONE")
                                        : mods_ui_text(option.section, 30u)};
        choice.names = option.selected != 0u ? 3u : 1u;
        rows.push_back(std::move(choice));
    }
    // Text in the language played: the mod's own (in a language it has) or the game's.
    const std::string playing = next_start_language_folder();
    const bool covers = std::find(mod.languages.begin(), mod.languages.end(), playing) != mod.languages.end();
    if (!mod.languages.empty() && !covers && !playing.empty()) {
        std::string theirs = language_name(mod.languages.front());
        for (const std::string& folder : mod.languages) {
            if (language_name(folder) == "ENGLISH") theirs = "ENGLISH";
        }
        rows.push_back({"TEXT", mod.own_text ? "THE MOD'S " + theirs : "THE GAME'S " + language_name(playing),
                        [i](RenderConfig&, int delta) {
                            const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                            if (i >= s_mods.size() || delta == 0) return;
                            s_mods[i].own_text = !s_mods[i].own_text;
                        },
                        "THIS MOD HAS NO " + language_name(playing) + " TEXT. ITS OWN TEXT MATCHES ITS CHANGES"});
    }
    rows.push_back(button_row({}, {"DONE"}, [](RenderConfig& draft, unsigned) { close_mod_options(draft); }, {}));
    return rows;
}

// A mod in its list: its name, and what it changes as the line under the page. With s_mods_mutex held.
SettingsRow mod_row(const galaxy::mods::ModInfo& mod) {
    SettingsRow row{mods_ui_text(mod.id, 26u), {}, nullptr, mod_details(mod)};
    row.key = "mod:" + mod.id;
    row.names = 1u;
    row.list = mod.enabled ? 2u : 1u;
    row.held = s_mod_held == mod.id;
    return row;
}

// The buttons under the lists, as the launcher's: Import file, Import folder, Options (of the mod last selected in a
// list; '-' before a word: unavailable), Open mods folder and Rescan. The line under the page says how an import goes.
// With s_mods_mutex held.
SettingsRow mods_buttons_row() {
    std::string help;
    {
        const std::lock_guard<std::mutex> import_lock(s_mod_import_mutex);
        help = !s_mod_import_message.empty() ? mods_ui_text(s_mod_import_message, 120u) : s_mod_import_value;
    }
    if (s_mod_dialog_open.load(std::memory_order_acquire)) help = "CHOOSING";
    const auto focus = std::find_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
    const bool options = focus != s_mods.end() && mod_has_options(*focus, next_start_language_folder());
    SettingsRow row = button_row(
        {}, {"IMPORT FILE", "IMPORT FOLDER", options ? "OPTIONS" : "-OPTIONS", "OPEN FOLDER", "RESCAN"},
        [](RenderConfig&, unsigned button) {
            switch (button) {
            case 0u:
            case 1u:
                if (!s_mod_import_busy.load(std::memory_order_acquire)) start_mod_import_dialog(button == 1u);
                return;
            case 2u: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                const auto it = std::find_if(s_mods.begin(), s_mods.end(),
                                             [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
                if (it != s_mods.end() && mod_has_options(*it, next_start_language_folder())) {
                    s_mod_options = s_mod_focus;
                    s_mod_held.clear();
                }
                return;
            }
            case 3u:
                open_folder_in_explorer(galaxy::mods::mods_folder());
                return;
            default: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                reread_mods_locked();
                return;
            }
            }
        },
        std::move(help));
    row.key = "buttons";
    return row;
}

// The MODS page as the launcher's Mods tab: the mods off, then the mods on (the higher one wins a conflict), each list
// under its title, then the buttons. A mod's options show instead while they are open.
std::vector<SettingsRow> mods_page(const RenderConfig&) {
    std::vector<SettingsRow> rows;
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    std::vector<galaxy::mods::ModInfo>& mods = mods_list();
    const auto open = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_options; });
    if (open != mods.end()) {
        return mod_options_page(static_cast<std::size_t>(open - mods.begin()));
    }
    s_mod_options.clear();
    SettingsRow off{"DISABLED", {}, nullptr, {}, true};
    off.list = 1u;
    rows.push_back(std::move(off));
    for (const galaxy::mods::ModInfo& mod : mods) {
        if (!mod.enabled) rows.push_back(mod_row(mod));
    }
    SettingsRow on{"ENABLED (TOP HAS PRIORITY)", {}, nullptr, {}, true};
    on.list = 2u;
    rows.push_back(std::move(on));
    for (const galaxy::mods::ModInfo& mod : mods) {
        if (mod.enabled) rows.push_back(mod_row(mod));
    }
    rows.push_back(mods_buttons_row());
    return rows;
}

// Back from a mod's options to the lists, on that mod.
void close_mod_options(const RenderConfig& render) {
    std::string id;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        id.swap(s_mod_options);
    }
    select_mods_row(render, "mod:" + id);
}

// The MODS page's keys while its lists show, as the launcher's lists: up and down within a list, left and right to
// the other list at the same place, down from a list's last mod to the buttons and up again. A or Space picks the
// selected mod up; held, up and down move it among the mods on (a higher one wins), left turns it off, right on, and
// A, Space, Enter or Escape puts it down. On a mod's options, Escape goes back to the lists.
bool mods_page_key(WPARAM key, bool accept, bool was_down) {
    if (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount != kModsTab) return false;
    const std::vector<SettingsRow> rows = mods_page(s_settings_draft);
    bool lists = false;
    for (const SettingsRow& row : rows) lists = lists || row.list != 0u;
    if (!lists) {
        if (key != VK_ESCAPE) return false;
        if (!was_down) close_mod_options(s_settings_draft);
        return true;
    }
    const unsigned selected = std::min(s_native_settings_row.load(std::memory_order_relaxed),
                                       static_cast<unsigned>(rows.size()) - 1u);
    // The rows of each list (1 off, 2 on) in order, and the buttons' row.
    std::vector<unsigned> members[3];
    unsigned buttons = 0u;
    for (unsigned r = 0; r < rows.size(); ++r) {
        if (rows[r].list != 0u && !rows[r].heading) members[rows[r].list].push_back(r);
        if (rows[r].key == "buttons") buttons = r;
    }
    const std::uint8_t list = rows[selected].heading ? 0u : rows[selected].list;
    const std::size_t place = list != 0u ? static_cast<std::size_t>(
        std::find(members[list].begin(), members[list].end(), selected) - members[list].begin()) : 0u;
    const auto select = [&](unsigned row) {
        s_native_settings_row.store(row, std::memory_order_relaxed);
        s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
        if (rows[row].list != 0u && !rows[row].heading) {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            s_mod_focus = rows[row].key.substr(4u);
        }
    };
    if (rows[selected].heading) {
        // The rows moved under the selection (a rescan, an import): the first mod, else the buttons.
        select(!members[1].empty() ? members[1].front() : !members[2].empty() ? members[2].front() : buttons);
        return true;
    }
    std::string held;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        held = s_mod_held;
        // The mod the buttons act on: the one selected, however it was.
        if (list != 0u) s_mod_focus = rows[selected].key.substr(4u);
    }
    if (!held.empty()) {
        if (key != VK_UP && key != VK_DOWN && key != VK_LEFT && key != VK_RIGHT && key != VK_SPACE &&
            key != VK_RETURN && key != VK_ESCAPE) {
            return true;
        }
        const bool moves = (key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT) && !accept;
        if (!moves) {
            if (!was_down) {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                s_mod_held.clear();
            }
            return true;
        }
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            const auto it = std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == held; });
            if (it == s_mods.end()) {
                s_mod_held.clear();
                return true;
            }
            const std::size_t enabled = static_cast<std::size_t>(
                std::count_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; }));
            const std::size_t at = static_cast<std::size_t>(it - s_mods.begin());
            if (key == VK_LEFT && it->enabled) {
                place_mod_locked(held, false, 0u);
            } else if (key == VK_RIGHT && !it->enabled) {
                place_mod_locked(held, true, std::min(place, enabled));
            } else if (key == VK_UP && it->enabled && at > 0u) {
                place_mod_locked(held, true, at - 1u);
            } else if (key == VK_DOWN && it->enabled) {
                place_mod_locked(held, true, at + 1u);
            }
        }
        select_mods_row(s_settings_draft, "mod:" + held);
        return true;
    }
    if (key == VK_RETURN || key == VK_ESCAPE) return false;  // apply and close, as on every page
    if (list == 0u) {
        // The buttons: up goes back to the mod last selected (else the first in a list); left, right and pressing
        // are the buttons'.
        if (key != VK_UP) return key == VK_DOWN;
        std::string focus;
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            focus = s_mod_focus;
        }
        for (unsigned r = 0; r < rows.size(); ++r) {
            if (rows[r].key == "mod:" + focus) {
                select(r);
                return true;
            }
        }
        if (!members[2].empty() || !members[1].empty()) select(!members[1].empty() ? members[1].front() : members[2].front());
        return true;
    }
    switch (key) {
    case VK_UP:
        if (place > 0u) select(members[list][place - 1u]);
        return true;
    case VK_DOWN:
        select(place + 1u < members[list].size() ? members[list][place + 1u] : buttons);
        return true;
    case VK_LEFT:
    case VK_RIGHT: {
        if (accept) break;
        const std::uint8_t other = key == VK_LEFT ? 1u : 2u;
        if (other != list && !members[other].empty()) select(members[other][std::min(place, members[other].size() - 1u)]);
        return true;
    }
    case VK_SPACE:
        break;
    default:
        return false;
    }
    // A or Space: picked up.
    if (!was_down) {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        s_mod_held = rows[selected].key.substr(4u);
        s_mod_focus = s_mod_held;
    }
    return true;
}

std::vector<SettingsRow> game_page(const RuntimeSettings& runtime) {
    std::vector<SettingsRow> rows;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%uX", std::clamp(runtime.text_speed, 1u, 4u));
    rows.push_back({"TEXT SPEED", runtime.text_speed >= 4u ? std::string("INSTANT") : std::string(text),
                    [](RenderConfig&, int delta) {
                        RuntimeSettings& settings = s_runtime_draft;
                        settings.text_speed = settings_step(std::clamp(settings.text_speed, 1u, 4u) - 1u, 4u, delta) + 1u;
                    },
                    "HOW FAST DIALOGUE TEXT APPEARS. INSTANT: EACH PAGE AT ONCE"});
    {
        static constexpr const char* kIcons[] = {"AUTO", "WII", "XBOX", "NINTENDO SWITCH", "PLAYSTATION"};
        rows.push_back({"BUTTON ICONS", kIcons[std::min<std::size_t>(static_cast<std::size_t>(runtime.button_icons), 4u)],
                        [](RenderConfig&, int delta) {
                            RuntimeSettings& settings = s_runtime_draft;
                            settings.button_icons = static_cast<ButtonIcons>(
                                settings_step(static_cast<unsigned>(settings.button_icons), 5u, delta));
                        },
                        "THE BUTTONS THE GAME'S ICONS SHOW. AUTO: THE CONTROLLER IN USE"});
    }
    rows.push_back({"LANGUAGE", game_language_name(runtime.language),
                    [](RenderConfig&, int delta) {
                        // Saved when applied; the game switches at the next start (game_language_for_boot).
                        // The languages on the disc as it is read (galaxy::set_disc_languages).
                        RuntimeSettings& settings = s_runtime_draft;
                        std::vector<GameLanguage> offered;
                        for (const GameLanguage language : kGameLanguages) {
                            if (galaxy::game_language_on_disc(language)) offered.push_back(language);
                        }
                        std::size_t index = 0u;
                        for (std::size_t i = 0; i < offered.size(); ++i) {
                            if (offered[i] == settings.language) index = i;
                        }
                        settings.language = offered[settings_step(static_cast<unsigned>(index),
                                                                  static_cast<unsigned>(offered.size()), delta)];
                    },
                    "THE GAME'S TEXT AND MENUS"});
    rows.push_back({"DISCORD STATUS", runtime.discord_status ? std::string("ON") : std::string("OFF"),
                    [](RenderConfig&, int delta) {
                        if (delta == 0) return;
                        s_runtime_draft.discord_status = !s_runtime_draft.discord_status;
                    },
                    "SHOWS YOUR CURRENT GALAXY AND MISSION IN DISCORD"});
    {
        rows.push_back({"MIIS", {}, nullptr, {}, true});
        std::string status;
        {
            const std::lock_guard<std::mutex> lock(s_mii_mutex);
            status = s_mii_status;
        }
        if (s_mii_dialog_open.load(std::memory_order_acquire)) {
            status = "CHOOSING FILES";
        }
        rows.push_back(button_row("IMPORT MIIS", {"IMPORT"}, [](RenderConfig&, unsigned) { start_mii_import_dialog(); },
                                  status.empty() ? std::string("MIIS FROM A WII, DS, 3DS, WII U, SWITCH, MII STUDIO OR MII "
                                                               "CREATOR, FOR THE MII ICONS")
                                                 : status));
        int count = s_mii_count.load(std::memory_order_acquire);
        if (count < 0) {
            count = static_cast<int>(galaxy::host::mii_library_count());
            s_mii_count.store(count, std::memory_order_release);
        }
        char mii_text[32]{};
        std::snprintf(mii_text, sizeof(mii_text), "%d OF 100", count);
        rows.push_back({"MIIS ON THIS CONSOLE", mii_text, nullptr, "THE MII ICON PICKER ON THE FILE SELECT SHOWS THEM"});
        rows.push_back(button_row("MIIS FOLDER", {"OPEN"}, [](RenderConfig&, unsigned) { open_mii_folder(); },
                                  "FILES PUT HERE ARE IMPORTED AT EVERY START"));
    }
    return rows;
}

// ADVANCED: any galaxy and star, entered with the game's own stage change (names in its own words), and the Grand
// Stars that power the Observatory.
std::vector<SettingsRow> advanced_page() {
    std::vector<SettingsRow> rows;
    want_galaxy_select_names();
    const std::size_t stage = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
    const std::uint32_t star = std::clamp(s_galaxy_select_star, 1u, kGalaxyStages[stage].stars);
    std::string name = galaxy_select_name(stage);
    if (name.empty()) {
        name = std::string_view(kGalaxyStages[stage].id) == kObservatoryId ? std::string(kObservatoryName)
                                                                            : upper_ascii(kGalaxyStages[stage].id);
    }
    rows.push_back({"GALAXY SELECT", {}, nullptr, {}, true});
    rows.push_back({"GALAXY", name,
                    [](RenderConfig&, int delta) {
                        s_galaxy_select_stage = settings_step(
                            static_cast<unsigned>(std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u)),
                            static_cast<unsigned>(kGalaxyStageCount), delta);
                        s_galaxy_select_star = 1u;
                    },
                    "ANY GALAXY, FOR TESTING. STARS COLLECTED THERE ARE SAVED"});
    rows.back().names = 2u;  // the game's own name in the played language
    std::string star_name = galaxy_select_star_name(stage, star);
    char star_text[96]{};
    std::snprintf(star_text, sizeof(star_text), star_name.empty() ? "%u" : "%u  %s", star, star_name.c_str());
    rows.push_back({"STAR", star_text,
                    [](RenderConfig&, int delta) {
                        const std::size_t at = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
                        s_galaxy_select_star = settings_step(s_galaxy_select_star - 1u, kGalaxyStages[at].stars,
                                                             delta) + 1u;
                    },
                    "THE MISSION TO PLAY"});
    rows.back().names = 2u;
    rows.push_back(button_row("TRAVEL", {"GO"},
                    [](RenderConfig&, unsigned) {
                        const std::size_t at = std::min(s_galaxy_select_stage, kGalaxyStageCount - 1u);
                        request_galaxy_travel(at, std::clamp(s_galaxy_select_star, 1u, kGalaxyStages[at].stars));
                        std::cerr << "[settings] travel to " << kGalaxyStages[at].id << ' '
                                  << s_galaxy_select_star << '\n';
                        set_native_settings_visible(false);
                    },
                    "LOADS IT WITH THE GAME OWN STAGE CHANGE, FROM ANY GALAXY"));
    rows.push_back(button_row("GRAND STARS", {"GET"},
                    [](RenderConfig&, unsigned) {
                        request_grand_stars();
                        std::cerr << "[settings] grand stars\n";
                        set_native_settings_visible(false);
                    },
                    "GIVES THIS SAVE FILE GRAND STARS 1-6 (EVERY DOME POWERED) AND LOADS THE OBSERVATORY. "
                    "USE A SPARE FILE"));
    return rows;
}

std::vector<SettingsRow> system_page(const RenderFeatureCaps& caps) {
    std::vector<SettingsRow> rows;
    {
        std::string name = mods_ui_text(caps.adapter_description.data(), 32u);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        rows.push_back({"GRAPHICS CARD", name, nullptr, "CHOSEN IN NEBULA SETTINGS. A CHANGE APPLIES AT THE NEXT START"});
    }
    char text[64]{};
    if (caps.dxr_raytracing_tier != 0u) {
        std::snprintf(text, sizeof(text), "TIER %u.%u %s", caps.dxr_raytracing_tier / 10u, caps.dxr_raytracing_tier % 10u,
                      exposed_text(caps.dxr));
    } else {
        std::snprintf(text, sizeof(text), "NONE");
    }
    rows.push_back({"RAY TRACING HARDWARE", text, nullptr, "DIRECTX RAYTRACING ON THIS GPU"});
    rows.push_back({"NVIDIA STREAMLINE", exposed_text(caps.streamline), nullptr, "LOADS DLSS FROM THE NVIDIA DOWNLOAD"});
    rows.push_back({"NVIDIA REFLEX", exposed_text(caps.reflex), nullptr, "LOW LATENCY WITH FRAME GENERATION"});
    rows.push_back({"DLSS", exposed_text(caps.dlss_super_resolution), nullptr, "UPSCALING ON NVIDIA RTX GPUS"});
    rows.push_back({"DLSS FRAME GENERATION", exposed_text(caps.dlss_frame_generation), nullptr,
                    "GENERATED FRAMES ON NVIDIA RTX 40 AND 50 GPUS"});
    std::string post = post_process_caps_text(get_post_process_status());
    if (const std::size_t colon = post.find(": "); colon != std::string::npos) post.erase(0, colon + 2u);
    rows.push_back({"POST-PROCESS", post, nullptr, "ANTI-ALIASING, FSR 1 AND SHARPENING"});
    return rows;
}

std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps) {
    switch (tab % kNativeSettingsTabCount) {
    case 0: return display_page(draft);
    case 1: return graphics_page(draft);
    case 4: return game_page(runtime);
    case 5: return mods_page(draft);
    case 6: return system_page(caps);
    case 7: return advanced_page();
    default: return {};
    }
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps) {
    char text[128]{};
    if (settings_page_tab(tab % kNativeSettingsTabCount)) {
        const auto page = settings_page(tab % kNativeSettingsTabCount, render, runtime, caps);
        if (row >= page.size()) {
            return {};
        }
        return page[row].heading ? page[row].label : page[row].label + ": " + page[row].value;
    }
    switch (tab % kNativeSettingsTabCount) {
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
        case ControlsRow::Device: {
            const galaxy::input::DeviceEntry& entry = s_controls_devices.entries[
                galaxy::input::resolve_device(s_controls_devices, s_controls_device)];
            // Bluetooth is abbreviated: the row has no room for the longest names.
            std::string line = upper_ascii(entry.label.data());
            const std::size_t bluetooth = line.find("(BLUETOOTH)");
            if (bluetooth != std::string::npos) line.replace(bluetooth, 11u, "(BT)");
            if (line.size() > 40u) line = line.substr(0, 38u) + "..";
            return "EDIT: " + line + (entry.active ? "  ACTIVE" : "");
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
        case ControlsRow::PairJoyCon:
            return "PAIR JOY-CON";
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
        case 6: return std::string("WIIMOTE SOUNDS: ") + percent_text(runtime.audio.wiimote_speaker);
        default: return {};
        }
    default:
        return {};
    }
}

enum class DlssOffer : std::uint8_t { None, Ask, Downloading, Ready, Failed };
DlssOffer s_dlss_offer = DlssOffer::None;
HANDLE s_dlss_download = nullptr;
std::atomic<bool> s_dlss_downloading{false};
bool s_dlss_installed_now = false;
std::uint64_t s_dlss_failed_until_ms = 0u;

std::wstring launcher_path() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH) {
        return {};
    }
    std::wstring launcher(path, length);
    const std::size_t slash = launcher.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    launcher.resize(slash + 1u);
    launcher += L"Nebula.exe";
    return GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring{} : launcher;
}

bool streamline_files_present() {
    const std::wstring interposer = streamline::default_directory() + L"\\sl.interposer.dll";
    return GetFileAttributesW(interposer.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool start_launcher(const std::wstring& arguments, HANDLE* process_out) {
    const std::wstring launcher = launcher_path();
    if (launcher.empty()) {
        return false;
    }
    std::wstring command = L"\"" + launcher + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0u, nullptr, nullptr,
                        &startup, &process)) {
        std::cerr << "[dlss] could not start Nebula.exe: win32=" << GetLastError() << '\n';
        return false;
    }
    // Its window comes to the front over the game.
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    if (process_out != nullptr) {
        *process_out = process.hProcess;
    } else {
        CloseHandle(process.hProcess);
    }
    return true;
}

bool dlss_offered() {
    if (!kStreamlineBuilt || streamline::dlss_supported()) {
        return false;
    }
    if (s_dlss_installed_now) {
        return true;
    }
    static const bool offered = host_window::rtx_adapter.load(std::memory_order_relaxed) &&
                                !streamline_files_present() && !launcher_path().empty();
    return offered;
}

bool dlss_installed_now() {
    return s_dlss_installed_now && !streamline::dlss_supported();
}

// DLSS Frame Generation (GRAPHICS > INTERPOLATION): offered while it runs, or with DLSS once its plugins are
// installed (it loads when the game starts with it on).
bool frame_generation_offered() {
    if (streamline::frame_generation_supported()) {
        return true;
    }
    static const bool installed = [] {
        const std::wstring directory = streamline::default_directory();
        for (const wchar_t* name : {L"sl.dlss_g.dll", L"nvngx_dlssg.dll", L"sl.reflex.dll", L"sl.pcl.dll"}) {
            if (GetFileAttributesW((directory + L"\\" + name).c_str()) == INVALID_FILE_ATTRIBUTES) {
                return false;
            }
        }
        return true;
    }();
    return installed && streamline::dlss_supported();
}

// DLSS chosen before it is installed is not applied: the menu asks to download it. Once downloaded this session it
// applies and runs after a restart (FSR 1 until then).
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live) {
    if (applied.upscaler != 3u || live.upscaler == 3u || !dlss_offered() || s_dlss_installed_now) {
        return false;
    }
    applied.upscaler = live.upscaler;
    if (s_dlss_offer != DlssOffer::Downloading) {
        s_dlss_offer = DlssOffer::Ask;
    }
    return true;
}

void dlss_dismiss_prompt() {
    if (s_dlss_offer == DlssOffer::Ask || s_dlss_offer == DlssOffer::Ready) {
        s_dlss_offer = DlssOffer::None;
    }
}

void dlss_fail(const char* why) {
    if (s_settings_draft.upscaler == 3u && !s_dlss_installed_now) {
        s_settings_draft.upscaler = get_render_config().upscaler;
    }
    s_dlss_offer = DlssOffer::Failed;
    s_dlss_failed_until_ms = GetTickCount64() + 4000u;
    std::cerr << "[dlss] not installed: " << why << '\n';
}

// The launcher starts the game again once this process has exited; this one closes.
bool restart_through_launcher() {
    if (!start_launcher(L"--restart-after " + std::to_wstring(GetCurrentProcessId()), nullptr)) {
        return false;
    }
    PostMessageW(reinterpret_cast<HWND>(host_window::settings_window.load(std::memory_order_acquire)), WM_CLOSE, 0, 0);
    return true;
}

// Enter/Y and Esc/B answer the DLSS questions; other keys wait for an answer.
bool dlss_prompt_key(WPARAM key, bool was_down) {
    if (s_dlss_offer != DlssOffer::Ask && s_dlss_offer != DlssOffer::Ready) {
        return false;
    }
    if (was_down || (key != VK_RETURN && key != VK_ESCAPE)) {
        return true;
    }
    if (key == VK_ESCAPE) {
        if (s_dlss_offer == DlssOffer::Ask) {
            s_settings_draft.upscaler = get_render_config().upscaler;
        }
        s_dlss_offer = DlssOffer::None;
        return true;
    }
    if (s_dlss_offer == DlssOffer::Ask) {
        if (start_launcher(L"--install-dlss", &s_dlss_download)) {
            s_dlss_downloading.store(true, std::memory_order_release);
            s_dlss_offer = DlssOffer::Downloading;
            std::cerr << "[dlss] download started\n";
        } else {
            dlss_fail("no launcher");
        }
        return true;
    }
    // Ready: the game starts again.
    s_dlss_offer = DlssOffer::None;
    if (restart_through_launcher()) {
        std::cerr << "[dlss] restarting\n";
    }
    return true;
}

// After mod changes were applied: Enter, Space or Y restarts the game now; Escape, Backspace or B keeps them for the
// next start. Every other key waits for the answer.
bool mods_restart_key(WPARAM key, bool was_down) {
    if (!s_mods_restart_ask.load(std::memory_order_acquire)) return false;
    if (was_down) return true;
    if (key == VK_RETURN || key == VK_SPACE) {
        s_mods_restart_ask.store(false, std::memory_order_release);
        if (restart_through_launcher()) {
            std::cerr << "[mods] restarting\n";
        } else {
            show_controls_message("COULD NOT RESTART");
        }
    } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
        s_mods_restart_ask.store(false, std::memory_order_release);
    }
    return true;
}

// Render thread, every frame: notices when the download window closes.
void poll_dlss_download() {
    if (!s_dlss_downloading.load(std::memory_order_acquire)) {
        return;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (s_dlss_download == nullptr || WaitForSingleObject(s_dlss_download, 0u) != WAIT_OBJECT_0) {
        return;
    }
    DWORD code = 1u;
    GetExitCodeProcess(s_dlss_download, &code);
    CloseHandle(s_dlss_download);
    s_dlss_download = nullptr;
    s_dlss_downloading.store(false, std::memory_order_release);
    if (code != 0u || !streamline_files_present()) {
        dlss_fail(code == 1u ? "closed" : "download failed");
        return;
    }
    s_dlss_installed_now = true;
    RenderConfig render = get_render_config();
    render.upscaler = 3u;
    set_render_config(render);
    s_settings_draft.upscaler = 3u;
    s_native_settings_dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    s_dlss_offer = DlssOffer::Ready;
    std::cerr << "[dlss] installed; it starts with the next launch\n";
}

std::string dlss_footer(std::uint64_t now_ms, bool& error) {
    switch (s_dlss_offer) {
    case DlssOffer::Ask:
        return "DOWNLOAD NVIDIA DLSS (" + std::to_string(kDlssDownloadMegabytes) + " MB)? ENTER/Y YES  ESC/B NO";
    case DlssOffer::Downloading:
        return "INSTALLING NVIDIA DLSS...";
    case DlssOffer::Ready:
        return "DLSS READY. RESTART NOW? ENTER/Y YES  ESC/B LATER";
    case DlssOffer::Failed:
        if (now_ms < s_dlss_failed_until_ms) {
            error = true;
            return "DLSS WAS NOT INSTALLED";
        }
        s_dlss_offer = DlssOffer::None;
        return {};
    default:
        return {};
    }
}

// The DLSS offer as the menu's status: its two questions, the install, a failure for a moment; Help when there is
// none.
NativeSettingsStatus dlss_status(std::uint64_t now_ms) {
    bool error = false;
    if (dlss_footer(now_ms, error).empty()) {
        return NativeSettingsStatus::Help;
    }
    switch (s_dlss_offer) {
    case DlssOffer::Ask: return NativeSettingsStatus::DlssAsk;
    case DlssOffer::Downloading: return NativeSettingsStatus::DlssInstalling;
    case DlssOffer::Ready: return NativeSettingsStatus::DlssRestart;
    default: return NativeSettingsStatus::DlssFailed;
    }
}

}  // namespace

namespace host_window {

// The internal resolution that fills the display: the smallest scale whose 640x456 game picture covers the monitor's
// current mode, or the swap chain in exclusive fullscreen. Higher scales cost GPU time and memory without adding
// pixels the display can show.
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

// DXR 1.1 with shader model 6.5 on an adapter with dedicated memory (the RAY TRACING row).
std::atomic<bool> ray_tracing_offered{false};
// Queues a save of the settings files when something changed; never waits on the disk.
bool flush_native_settings_if_dirty() {
    if (!s_native_settings_dirty.load(std::memory_order_acquire)) {
        return true;
    }
    static std::once_flag started;
    std::call_once(started, [] { std::thread(settings_save_worker).detach(); });
    {
        const std::lock_guard<std::mutex> lock(s_settings_save_mutex);
        s_settings_save_requested = true;
    }
    s_settings_save_changed.notify_all();
    return true;  // queued: the worker reports a failed write and keeps the settings dirty
}

unsigned upscaler_render_scale(const RenderConfig& render, unsigned internal) {
    return gx::upscaler_render_scale(render, internal);
}

// Exit paths: waits for a queued or running save (at most 5 s), then writes what is still unsaved here.
void save_native_settings_now() {
    {
        std::unique_lock<std::mutex> lock(s_settings_save_mutex);
        s_settings_save_changed.wait_for(lock, std::chrono::seconds(5), [] {
            return !s_settings_save_requested && !s_settings_save_running;
        });
    }
    (void)write_dirty_settings_files();
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
    // A save of the game's own still in flight would read back as a change made outside it.
    if (settings_save_pending()) {
        return;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (s_native_settings_visible.load(std::memory_order_relaxed)) {
        return;
    }
    const RenderConfig before = get_render_config();
    const std::string gpu_before = next_start_gpu();
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
    std::cerr << "[settings] config.ini reloaded\n";
    if (const std::string gpu = next_start_gpu(); gpu != gpu_before) {
        std::cerr << "[settings] gpu=" << gpu << " applies at the next start\n";
    }
}

// A GPU chosen in Nebula Settings during the session that is not the one the game runs on.
bool gpu_change_pending(const RenderConfig& render, const RenderFeatureCaps& caps) {
    const std::string next = next_start_gpu();
    return next != render.gpu.data() &&
           next != gpu_identity(caps.adapter_vendor_id, caps.adapter_device_id, caps.adapter_subsys_id);
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

unsigned shown_test_window_mode() {
    static const unsigned mode = [] {
        char value[16]{};
        std::size_t length = 0;
        if (!hidden_window_mode_enabled() ||
            getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SHOWN_WINDOW") != 0 || length <= 1) {
            return 0u;
        }
        return std::strcmp(value, "corner") == 0 ? 2u : std::strcmp(value, "edge") == 0 ? 1u : 0u;
    }();
    return mode;
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
    // The configured monitor and display mode. Exclusive fullscreen starts as borderless on that monitor; the first
    // frame's display check switches the mode once the swap chain exists. Hidden test windows stay windowed.
    const RenderConfig start_config = get_render_config();
    const unsigned start_mode = hidden_window
        ? kDisplayWindowed : std::min(start_config.display_mode, kDisplayExclusive);
    const bool fullscreen_window = start_mode != kDisplayWindowed;
    const std::vector<MonitorInfo> start_monitors = enumerate_monitors();
    store_monitor_cache(start_monitors);
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

    // A test window shown on the main monitor (GALAXY_TEST_SHOWN_WINDOW), so its frames reach the display as a
    // player's do and Windows captures them: "corner" whole in the work area's bottom-right corner, "edge" with one
    // pixel there. Never focused, so nobody types into it.
    if (const unsigned shown = shown_test_window_mode(); shown != 0u) {
        RECT work{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == 0) {
            work = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        window_x = shown == 2u ? work.right - actual_window_width : work.right - 1;
        window_y = shown == 2u ? work.bottom - actual_window_height : work.bottom - 1;
    }

    if (actual_window_width <= 0 || actual_window_height <= 0) {
        std::cerr << "[Renderer] invalid client dimensions\n";
        return {};
    }

    backbuffer_width   = static_cast<UINT>(actual_window_width);
    backbuffer_height  = static_cast<UINT>(actual_window_height);

    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        // The arrow while the window is unfocused; WM_SETCURSOR hides it while the game has focus.
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
        // Files dropped on the window import (WM_DROPFILES).
        const DWORD ex_style =
            hidden_window ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
                          : (WS_EX_APPWINDOW | WS_EX_ACCEPTFILES);
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
        // No pointer seed from this thread: the window thread publishes geometry and input through WM_SIZE and
        // WM_SETFOCUS, and the first real message or cursor poll places the pointer.
        opened.shown = !hidden_window;
        if (shown_test_window_mode() != 0u) {
            ShowWindowAsync(opened.window, SW_SHOWNOACTIVATE);
        }
    }
    return opened;
}

void close_game_window(WindowOwner& owner) noexcept {
    owner.stop();
    pointer_hwnd.store(0u, std::memory_order_release);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void show_window(NativeWindow window) noexcept {
    ShowWindowAsync(window, SW_SHOWNORMAL);
}

bool present_grid(std::int64_t&, std::int64_t&, std::int64_t&, std::int64_t&) noexcept {
    return false;  // the presents pace themselves (present_pacer)
}

// The vblanks' own rate when they are timed (the mode reports whole hertz: 239 or 240 for 239.76).
double display_refresh_hz(NativeWindow window) noexcept {
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    if (vblank_grid(window, vblank, period)) {
        return std::round(static_cast<double>(galaxy::qpc_frequency()) / static_cast<double>(period) * 100.0) / 100.0;
    }
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info) ||
        !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1u) {
        return 0.0;
    }
    return static_cast<double>(mode.dmDisplayFrequency);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DROPFILES) {
        // Dropped files: mod archives and folders into the mods folder (the MODS page opens on the import),
        // anything else to the Mii import.
        const auto drop = reinterpret_cast<HDROP>(wp);
        std::vector<std::filesystem::path> mods;
        std::vector<std::filesystem::path> miis;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0u);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0u);
            std::wstring name(length + 1u, L'\0');
            DragQueryFileW(drop, i, name.data(), length + 1u);
            name.resize(length);
            const std::filesystem::path path(name);
            std::error_code error;
            if (std::filesystem::is_directory(path, error) || galaxy::mods::is_mod_archive(path)) {
                mods.push_back(path);
            } else {
                miis.push_back(path);
            }
        }
        DragFinish(drop);
        if (!mods.empty()) {
            run_mod_imports(std::move(mods));
            s_native_settings_tab.store(5u, std::memory_order_relaxed);
            s_native_settings_row.store(1u, std::memory_order_relaxed);
            set_native_settings_visible(true);
        }
        if (!miis.empty()) {
            std::thread([miis = std::move(miis)] {
                std::string message;
                (void)galaxy::host::import_mii_files(miis, &message);
                std::cerr << "[settings] mii import (dropped): " << message << '\n';
                {
                    const std::lock_guard<std::mutex> lock(s_mii_mutex);
                    s_mii_status = message;
                }
                s_mii_count.store(-1, std::memory_order_release);
            }).detach();
        }
        return 0;
    }
    if (msg == kSettingsKeyMessage) {
        (void)handle_native_settings_key(wp, false, true);
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        store_monitor_cache(enumerate_monitors());
    }
    if (msg == kTestResizeMessage) {
        RECT rect{0, 0, static_cast<LONG>(wp), static_cast<LONG>(lp)};
        (void)AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        (void)SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
        // A size the player drags the window to becomes the windowed size.
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
        if (focused) {
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
    if (msg == WM_MOUSEWHEEL && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // The wheel moves the settings menu's selection a row a notch, as the arrow keys; never while a binding
        // waits for its input.
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        if (controls_capturing()) return 0;
        static int s_wheel = 0;
        s_wheel += GET_WHEEL_DELTA_WPARAM(wp);
        for (; s_wheel >= WHEEL_DELTA; s_wheel -= WHEEL_DELTA) (void)handle_native_settings_key(VK_UP, false);
        for (; s_wheel <= -WHEEL_DELTA; s_wheel += WHEEL_DELTA) (void)handle_native_settings_key(VK_DOWN, false);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        // The game's own screens scroll by it (the achievement board).
        host_window::pointer_wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp), std::memory_order_relaxed);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, true, true);
        // Win32 delivers a release outside the client only to the window that captured the mouse; a lost release
        // would leave the Wii Remote's A or B held.
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        update_pointer_button(hwnd, lp, true, false);
        if (!s_pointer_right_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, false, true);
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        update_pointer_button(hwnd, lp, false, false);
        if (!s_pointer_left_button.load(std::memory_order_relaxed) &&
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
        // Minimizing reports a 0x0 (or near-0) client, which ResizeBuffers would refuse.
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
        save_native_settings_now();
        host_window::quit_request.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        save_native_settings_now();
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

NativeSettingsView build_native_settings_view();

void draw_native_settings_overlay(UiCanvas& renderer) {
    poll_dlss_download();
    {
        // GALAXY_TEST_SETTINGS_OPEN=<page>[:<present>]: hidden test runs open the menu on that page
        // once, at that present.
        static int test_page = -1;
        static unsigned long test_present = 0u;
        static const bool test_read = [] {
            char value[32]{};
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SETTINGS_OPEN") == 0 && length > 1u) {
                char* end = nullptr;
                test_page = static_cast<int>(std::strtol(value, &end, 10));
                if (end != nullptr && *end == ':') test_present = std::strtoul(end + 1, nullptr, 10);
            }
            return true;
        }();
        (void)test_read;
        static unsigned long presents = 0u;
        static bool test_opened = false;
        if (test_page >= 0 && !test_opened && ++presents >= test_present) {
            test_opened = true;
            const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
            s_native_settings_tab.store(static_cast<unsigned>(test_page) % kNativeSettingsTabCount,
                                        std::memory_order_relaxed);
            s_native_settings_row.store(0u, std::memory_order_relaxed);
            set_native_settings_visible(true);
        }
        // GALAXY_TEST_SETTINGS_KEYS=<present>:<key>,...: keys pressed in the menu at those presents (up, down, left,
        // right, enter, esc, tab, backspace, space; f1 opens and closes the menu).
        static const std::vector<std::pair<unsigned long, WPARAM>> test_keys = [] {
            std::vector<std::pair<unsigned long, WPARAM>> keys;
            static char value[32768]{};  // a walk through every page's rows runs to some 500 keys
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_SETTINGS_KEYS") != 0 || length <= 1u) return keys;
            static constexpr std::pair<std::string_view, WPARAM> kNames[] = {
                {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"enter", VK_RETURN},
                {"esc", VK_ESCAPE}, {"tab", VK_TAB}, {"backspace", VK_BACK}, {"space", VK_SPACE}, {"f1", VK_F1}};
            std::string_view rest(value);
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view item = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1u);
                const std::size_t colon = item.find(':');
                if (colon == std::string_view::npos) continue;
                const unsigned long at = std::strtoul(std::string(item.substr(0, colon)).c_str(), nullptr, 10);
                for (const auto& [name, key] : kNames) {
                    if (item.substr(colon + 1u) == name) keys.emplace_back(at, key);
                }
            }
            return keys;
        }();
        static unsigned long key_presents = 0u;
        static std::size_t next_key = 0u;
        ++key_presents;
        while (next_key < test_keys.size() && key_presents >= test_keys[next_key].first) {
            handle_native_settings_key(test_keys[next_key].second, false);
            ++next_key;
        }
        // GALAXY_TEST_WINDOW_SIZES=<present>:<width>x<height>,...: the window resized at those presents, whatever is
        // open (a live resize).
        struct TestSize {
            unsigned long at;
            unsigned long width;
            unsigned long height;
        };
        static const std::vector<TestSize> test_sizes = [] {
            std::vector<TestSize> sizes;
            char value[512]{};
            std::size_t length = 0u;
            if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_WINDOW_SIZES") != 0 || length <= 1u) return sizes;
            for (char* item = value; item != nullptr && *item != '\0';) {
                char* end = nullptr;
                TestSize size{std::strtoul(item, &end, 10), 0u, 0u};
                if (end != nullptr && *end == ':') size.width = std::strtoul(end + 1, &end, 10);
                if (end != nullptr && *end == 'x') size.height = std::strtoul(end + 1, &end, 10);
                if (size.width != 0u && size.height != 0u) sizes.push_back(size);
                item = end != nullptr ? std::strchr(end, ',') : nullptr;
                if (item != nullptr) ++item;
            }
            return sizes;
        }();
        static std::size_t next_size = 0u;
        while (next_size < test_sizes.size() && key_presents >= test_sizes[next_size].at) {
            const HWND window = reinterpret_cast<HWND>(host_window::pointer_hwnd.load(std::memory_order_acquire));
            if (window != nullptr) {
                (void)PostMessageW(window, kTestResizeMessage, test_sizes[next_size].width, test_sizes[next_size].height);
            }
            ++next_size;
        }
    }
    // The native game draws the menu itself (native_settings_view).
    if (!s_native_settings_visible.load(std::memory_order_relaxed) ||
        s_settings_drawn_by_game.load(std::memory_order_acquire)) {
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

    // Corners from the game's own file-select window frame (game_asset_extract.h): one rounded top-left corner,
    // mirrored through the UVs for the other three. Nothing is drawn when the texture could not be loaded.
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

    const char* const* tabs = kNativeSettingsTabNames;
    static_assert(std::size(kNativeSettingsTabNames) == kNativeSettingsTabCount);
    int tab_x = panel_x + 28;
    // The labels share the panel width; the longest is eight 6-unit glyphs plus a gap, so the label scale shrinks on
    // narrow panels.
    const int tab_step = (panel_w - 56) / static_cast<int>(kNativeSettingsTabCount);
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
    const NativeSettingsView view = build_native_settings_view();
    const unsigned row_count = static_cast<unsigned>(view.rows.size());
    const unsigned selected_row = view.selected;
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
    // Rows: the label on the left, the value on the right between arrows where it can change;
    // section headings in the accent colour. A value takes up to three fifths of the row, the label the rest.
    const int row_left = panel_x + 70;
    const int row_right = panel_x + panel_w - 48;
    const int row_gap = 12 * scale;
    for (unsigned row = s_settings_first_row; row < s_settings_first_row + visible_rows; ++row) {
        const int y =
            layout.rows_y + static_cast<int>(row - s_settings_first_row) * layout.row_step;
        std::string label = view.rows[row].label;
        const std::string& value = view.rows[row].value;
        const bool heading = view.rows[row].heading;
        const bool changes = view.rows[row].changes;
        if (heading) {
            label = ui_fit_text(std::move(label), scale, row_right - (panel_x + 36));
            const int rule_x = panel_x + 44 + ui_text_width(label, scale);
            ui_text(renderer, panel_x + 36, y, scale, label, accent);
            if (row_right - rule_x > 0) ui_rect(renderer, rule_x, y + scale * 3, row_right - rule_x, std::max(1, scale / 2), muted);
            continue;
        }
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
        int value_x = row_right;
        if (!value.empty()) {
            const bool arrows = changes && row == selected_row;
            const int arrow_width = arrows ? ui_text_width("<  >", scale) : 0;
            const std::string fitted = ui_fit_text(value, scale, (row_right - row_left) * 3 / 5 - arrow_width);
            const std::string shown = arrows ? "< " + fitted + " >" : fitted;
            value_x = row_right - ui_text_width(shown, scale);
            ui_text(renderer, value_x, y, scale, shown, row == selected_row ? accent : (changes ? text : muted));
            value_x -= row_gap;
        }
        ui_text(renderer, row_left, y, scale, ui_fit_text(std::move(label), scale, value_x - row_left), changes ? text : muted);
    }

    const int footer_y = layout.footer_y;
    const int footer_scale = std::min(2, scale);
    const int footer_step = footer_scale == 1 ? 18 : 28;
    std::string status;
    std::array<float, 4> status_color = accent;
    const bool controls_page = tab == kControlsTab;
    char line[96]{};
    switch (view.status) {
    case NativeSettingsStatus::CaptureKey:
        status = "PRESS A KEY OR MOUSE BUTTON  ESC CANCEL";
        break;
    case NativeSettingsStatus::CapturePad:
        status = "PRESS A BUTTON, STICK OR TRIGGER  ESC CANCEL";
        break;
    case NativeSettingsStatus::Message:
        status = view.status_text;
        status_color = error;
        break;
    case NativeSettingsStatus::DlssAsk:
    case NativeSettingsStatus::DlssInstalling:
    case NativeSettingsStatus::DlssRestart:
    case NativeSettingsStatus::DlssFailed: {
        bool dlss_error = false;
        status = dlss_footer(GetTickCount64(), dlss_error);
        status_color = dlss_error ? error : accent;
        break;
    }
    case NativeSettingsStatus::KeepDisplay:
        std::snprintf(line, sizeof(line), "KEEP DISPLAY? ENTER/Y KEEP  ESC/B UNDO  %d", view.status_number);
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::SaveFailed:
        status = "SAVE FAILED - RETRY ON NEXT CLOSE";
        status_color = error;
        break;
    case NativeSettingsStatus::Pending:
        std::snprintf(line, sizeof(line),
                      controls_page ? "%d CHANGE%s  SPACE/Y APPLY  ESC/B DISCARD"
                                    : "%d CHANGE%s  ENTER/Y APPLY  BACKSPACE/X UNDO",
                      view.status_number, view.status_number == 1 ? "" : "S");
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::Leave:
        std::snprintf(line, sizeof(line), "%d CHANGE%s NOT APPLIED  ENTER/Y APPLY  BACKSPACE/X DISCARD  ESC/B STAY",
                      view.status_number, view.status_number == 1 ? "" : "S");
        status = pad_hints(line);
        break;
    case NativeSettingsStatus::Applied:
        status = "APPLIED";
        break;
    case NativeSettingsStatus::LanguageRestart:
        status = "THE NEW LANGUAGE APPLIES WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::ModsRestart:
        status = "MOD CHANGES APPLY WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::GpuRestart:
        status = "THE NEW GRAPHICS CARD APPLIES WHEN NEBULA RESTARTS";
        break;
    case NativeSettingsStatus::Help:
        status = view.help;
        status_color = muted;
        break;
    case NativeSettingsStatus::JoyCon:
        std::snprintf(line, sizeof(line), "%s  ESC/B BACK",
                      (view.status_number & kJoyConPairedBit) != 0 ? "JOY-CON PAIRED"
                      : (view.status_number & (kJoyConLeftBit | kJoyConRightBit)) == (kJoyConLeftBit | kJoyConRightBit)
                          ? "HOLD L ON THE LEFT JOY-CON AND R ON THE RIGHT ONE"
                          : "CONNECT A LEFT AND A RIGHT JOY-CON");
        status = pad_hints(line);
        break;
    default:
        break;
    }
    if (!status.empty()) {
        ui_text(renderer, panel_x + 28, footer_y, footer_scale, ui_fit_text(std::move(status), footer_scale, panel_w - 56),
                status_color);
    }
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step,
        footer_scale,
        pad_hints("F1, VIEW+MENU OR MINUS+PLUS TOGGLE  TAB/LB/RB PAGE"),
        muted);
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step * 2,
        footer_scale,
        pad_hints(controls_page ? "ENTER/A ADD INPUT  BACKSPACE/X CLEAR  ESC/B CLOSE"
                                : "ARROWS/D-PAD MOVE+CHANGE  ESC/B CLOSE"),
        muted);
}

// The menu as it stands: its pages, the open page's rows (label, value, a heading or not, whether it changes), the
// selection and what the line under the page says, in the order the overlay always chose it. Takes the menu's lock.
NativeSettingsView build_native_settings_view() {
    NativeSettingsView view;
    view.pages.assign(std::begin(kNativeSettingsTabNames), std::end(kNativeSettingsTabNames));
    view.controls_page = kControlsTab;
    // The hints show the buttons the icons do: a chosen style whatever is in hand, else what the player last used.
    const ButtonIcons icons = get_runtime_settings().button_icons;
    if (icons == ButtonIcons::Xbox || icons == ButtonIcons::Nintendo || icons == ButtonIcons::PlayStation) {
        view.input = icons == ButtonIcons::Xbox ? 1u : icons == ButtonIcons::Nintendo ? 2u : 3u;
    } else if (const auto device = galaxy::host::last_input_device()) {
        switch (device->scheme) {
        case galaxy::input::ControlScheme::Keyboard: break;
        case galaxy::input::ControlScheme::SwitchPro:
        case galaxy::input::ControlScheme::SwitchPro2: view.input = 2u; break;
        case galaxy::input::ControlScheme::PlayStation: view.input = 3u; break;
        default: view.input = 1u; break;
        }
    }
    view.open = s_native_settings_visible.load(std::memory_order_relaxed);
    const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
    view.page = tab;
    if (!view.open) {
        return view;
    }
    const std::lock_guard<std::recursive_mutex> settings_lock(s_native_settings_mutex);
    expire_display_confirmation();
    expire_controls_capture();
    if (tab == kControlsTab) {
        refresh_controls_devices(false);
    }
    const RenderConfig row_render = s_settings_draft;
    const RuntimeSettings row_runtime = s_runtime_draft;
    const RenderFeatureCaps row_caps = get_render_feature_caps();
    const unsigned row_count = native_settings_row_count(tab);
    view.selected = std::min(s_native_settings_row.load(std::memory_order_relaxed), row_count == 0u ? 0u : row_count - 1u);
    const bool page_tab = settings_page_tab(tab);
    const std::vector<SettingsRow> page =
        page_tab ? settings_page(tab, row_render, row_runtime, row_caps) : std::vector<SettingsRow>{};
    view.rows.reserve(row_count);
    for (unsigned row = 0; row < row_count; ++row) {
        NativeSettingsViewRow shown;
        shown.changes = true;
        if (page_tab && row < page.size()) {
            shown.label = page[row].label;
            shown.value = page[row].value;
            shown.heading = page[row].heading;
            shown.changes = static_cast<bool>(page[row].change);
            shown.label_name = (page[row].names & 1u) != 0u;
            shown.name = (page[row].names & 2u) != 0u;
            shown.mod_list = page[row].list;
            shown.held = page[row].held;
            shown.buttons = !page[row].buttons.empty();
            if (row == view.selected) view.help = page[row].help;
        } else {
            shown.label = native_settings_row_text(tab, row, row_render, row_runtime, row_caps);
            if (const std::size_t colon = shown.label.find(": "); colon != std::string::npos) {
                shown.value = shown.label.substr(colon + 2u);
                shown.label.resize(colon);
            }
        }
        shown.name = shown.name || (tab == 0u && shown.label == "MONITOR") || (tab == 6u && shown.label == "GRAPHICS CARD") ||
                     (tab == 4u && (shown.label == "GALAXY" || shown.label == "STAR"));
        if (tab == kControlsTab) {
            // The controller being set up by its own name (the overlay's row has it in capitals), and the bindings as
            // lists of inputs.
            const ControlsRowInfo info = controls_row(row);
            if (info.kind == ControlsRow::Device) {
                const galaxy::input::DeviceEntry& entry =
                    s_controls_devices.entries[galaxy::input::resolve_device(s_controls_devices, s_controls_device)];
                shown.value = entry.label.data();
                shown.name = true;
                shown.active = entry.active;
            }
            const bool capturing = controls_capturing() && s_capture_target == static_cast<std::size_t>(info.target);
            if (info.kind == ControlsRow::Binding && !capturing) {
                shown.value = galaxy::input::format_target_bindings(s_controls_scheme,
                                                                    s_controls_draft.table(s_controls_scheme)[info.target]);
            }
            shown.inputs = info.kind == ControlsRow::Binding;
            if (info.kind == ControlsRow::Reset) {
                shown.buttons = true;
                shown.value = "RESET";
            } else if (info.kind == ControlsRow::PairJoyCon) {
                shown.buttons = true;
                shown.value = "PAIR";
            }
        }
        view.rows.push_back(std::move(shown));
    }
    view.selected_button = view.selected < view.rows.size() && view.rows[view.selected].buttons ? selected_settings_button() : 0u;
    for (unsigned row = 0; row < view.rows.size(); ++row) {
        const SliderPlace place = settings_slider(tab, row, page);
        view.rows[row].slider_steps = place.steps;
        view.rows[row].slider_at = place.at;
    }

    const std::uint64_t now_ms = GetTickCount64();
    if (s_joycon_pairing) {
        // The halves; a pair made since the screen opened shows a moment, then the screen closes.
        const galaxy::host::JoyConStatus joycons = joycon_status_now();
        if (joycons.paired != s_joycon_paired_at_open && s_joycon_paired_ms == 0u) {
            s_joycon_paired_ms = now_ms;
            std::cerr << "[settings] joy-con paired\n";
        }
        view.status = NativeSettingsStatus::JoyCon;
        view.status_number = (joycons.lefts != 0u ? kJoyConLeftBit : 0) | (joycons.rights != 0u ? kJoyConRightBit : 0) |
                             (joycons.left_held ? kJoyConLeftHeldBit : 0) | (joycons.right_held ? kJoyConRightHeldBit : 0) |
                             (s_joycon_paired_ms != 0u ? kJoyConPairedBit : 0) |
                             (joycons.left_presses & 15) << kJoyConLeftPressesShift |
                             (joycons.right_presses & 15) << kJoyConRightPressesShift;
        if (s_joycon_paired_ms != 0u && now_ms - s_joycon_paired_ms >= kJoyConPairedShowMs) {
            close_joycon_pairing();
        }
    } else if (s_settings_leave_asking) {
        view.status = NativeSettingsStatus::Leave;
        view.status_number = static_cast<int>(pending_settings_changes());
    } else if (s_mods_restart_ask.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::ModsRestartAsk;
    } else if (controls_capturing()) {
        view.status = s_controls_scheme == galaxy::input::ControlScheme::Keyboard ? NativeSettingsStatus::CaptureKey
                                                                                   : NativeSettingsStatus::CapturePad;
    } else if (s_controls_message_until_ms > now_ms) {
        view.status = NativeSettingsStatus::Message;
        view.status_text = s_controls_message;
    } else if (const NativeSettingsStatus dlss = dlss_status(now_ms); dlss != NativeSettingsStatus::Help) {
        view.status = dlss;
        view.status_number = kDlssDownloadMegabytes;
    } else if (settings_display_confirming()) {
        const std::uint64_t left_ms = s_display_confirm_deadline_ms > now_ms ? s_display_confirm_deadline_ms - now_ms : 0u;
        view.status = NativeSettingsStatus::KeepDisplay;
        view.status_number = static_cast<int>((left_ms + 999u) / 1000u);
    } else if (s_native_settings_save_error.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::SaveFailed;
    } else if (const unsigned pending = pending_settings_changes(); pending != 0u) {
        view.status = NativeSettingsStatus::Pending;
        view.status_number = static_cast<int>(pending);
    } else if (s_settings_applied_ms != 0u && now_ms - s_settings_applied_ms < 2500u) {
        view.status = NativeSettingsStatus::Applied;
    } else if (GameLanguage running{}; tab == 4u && running_game_language(running) && running != row_runtime.language) {
        view.status = NativeSettingsStatus::LanguageRestart;
    } else if (tab == 5u && mods_restart_pending()) {
        view.status = NativeSettingsStatus::ModsRestart;
    } else if (tab == 6u && gpu_change_pending(row_render, row_caps)) {
        view.status = NativeSettingsStatus::GpuRestart;
    }
    return view;
}

// The HOME Menu panel, centred over the frame, until the game draws the menu itself. The mouse picks an answer by
// hovering and chooses with a click.
void draw_home_menu(UiCanvas& renderer) {
    static bool s_left_was_down = true;
    if (!s_home_menu_visible.load(std::memory_order_acquire) ||
        s_home_menu_drawn_by_game.load(std::memory_order_relaxed)) {
        s_left_was_down = true;
        return;
    }
    const int asking = s_home_menu_asking.load(std::memory_order_relaxed);
    const unsigned count = asking != 0 ? kHomeMenuAnswers : kHomeMenuRows;
    const char* const* rows = asking != 0 ? kHomeMenuAnswerText : kHomeMenuRowText;
    const int bb_w = static_cast<int>(host_window::backbuffer_width);
    const int bb_h = static_cast<int>(host_window::backbuffer_height);
    const int scale = bb_h >= 1440 ? 4 : (bb_h >= 720 ? 3 : 2);
    const int row_h = 15 * scale;
    const int w = std::min(bb_w - 8 * scale, 140 * scale);
    const int h = 26 * scale + row_h * static_cast<int>(count) + 14 * scale;
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
        if (px >= x && px < x + w && py >= rows_y && py < rows_y + row_h * static_cast<int>(count)) {
            const unsigned hovered = static_cast<unsigned>((py - rows_y) / row_h);
            if (pointer.left_button && !s_left_was_down) {
                home_menu_choose(hovered);
            } else {
                home_menu_point(hovered);
            }
        }
    }
    s_left_was_down = pointer.left_button;

    ui_rect(renderer, x, y, w, h, panel);
    ui_rect(renderer, x, y, 6, h, rail);
    ui_text(renderer, x + 14 * scale, y + 8 * scale, scale,
            asking == 0 ? "HOME MENU" : (asking == 1 ? "QUIT TO DESKTOP?" : "RESET?"), text);
    const unsigned selected = s_home_menu_row.load(std::memory_order_relaxed) % count;
    for (unsigned row = 0; row < count; ++row) {
        const int ry = rows_y + static_cast<int>(row) * row_h;
        if (row == selected) {
            ui_rect(renderer, x + 6, ry, w - 6, row_h, highlight);
        }
        ui_text(renderer, x + 14 * scale, ry + 4 * scale, scale, rows[row], row == selected ? text : muted);
    }
}

// For the first seconds of a session, until the overlay is first opened, a one-line hint names the keys that open it.
void draw_native_settings_hint(UiCanvas& renderer) {
    static std::uint64_t s_hint_until_ms = 0u;
    static bool s_hint_done = false;
    if (s_hint_done) {
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
    // Every shape from 9:21 (narrower than 4:3: Vert+, make_experimental_ultrawide_aspect) to 32:9.
    if (aspect < 9.0 / 21.0) {
        return {900, 2100};
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
        // A system menu or move/size loop owns the mouse: the game sees no buttons, and the cursor poll does not take
        // the pointer back meanwhile.
        state.left_button = false;
        state.right_button = false;
        return state;
    }

    // Client-area mouse messages win over the per-frame cursor poll: on Remote Desktop and with a hidden cursor,
    // GetCursorPos can report a stale centered position for long stretches while WM_MOUSEMOVE carries the live
    // coordinates. The seed does not advance the event sequence, so startup centering stays inactive until a real
    // cursor sample arrives.
    const std::uint64_t event_seq = cached_snapshot.event_sequence;
    const std::uint64_t leave_seq = cached_snapshot.leave_sequence;
    const bool cached_inside = cached_snapshot.coordinates.inside_client;
    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerClientMessageTiming client_message_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            pointer_now_ms, cached_snapshot.last_client_message_ms,
            kPointerMessageRecentMs, kStaleCenterRejectMs);
    const bool recent_client_move = client_message_timing.recent;
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const galaxy::host::HostPointerCursorPollAuthorityDecision
        cursor_poll_authority =
            galaxy::host::select_host_pointer_cursor_poll_authority(
                kCursorPollMode, remote_session,
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
                    // Under Remote Desktop GetCursorPos trails WM_MOUSEMOVE on some hosts while on others the
                    // messages stall: the poll takes over once the last client message is no longer fresh, still
                    // rejecting the stale centered sample.
                    const bool cursor_poll_recovers_visibility =
                        (!cached_inside && !recent_client_move) ||
                        event_seq <= leave_seq;
                    // Local polling is authoritative at the input cadence; adaptive remote polling only once the
                    // client-message coordinate has aged out.
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
                            // Only a poll that won the publication becomes the baseline; rejected or
                            // superseded samples stay eligible once the authority/stale-center timeout expires.
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
            // The window procedure may have published a newer move, leave or resize during
            // GetCursorPos/ScreenToClient: report the latest snapshot, not the older cached one.
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

namespace {
std::atomic<bool> s_sharp_2d_requested{false};
}  // namespace

void request_sharp_2d(bool on) noexcept {
    s_sharp_2d_requested.store(on, std::memory_order_relaxed);
}

bool sharp_2d_requested() noexcept {
    return s_sharp_2d_requested.load(std::memory_order_relaxed);
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

NativeSettingsView native_settings_view() {
    s_settings_drawn_by_game.store(true, std::memory_order_release);
    return host_window::build_native_settings_view();
}

void native_settings_input(NativeSettingsInput input, int arg) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const bool visible = s_native_settings_visible.load(std::memory_order_relaxed);
    switch (input) {
    case NativeSettingsInput::Open:
        set_native_settings_visible(true);
        return;
    case NativeSettingsInput::Close:
        if (visible) (void)handle_native_settings_key(VK_ESCAPE, false, true);
        return;
    case NativeSettingsInput::Apply:
        if (visible) (void)handle_native_settings_key(VK_RETURN, false, true);
        return;
    case NativeSettingsInput::Revert:
        if (visible) (void)handle_native_settings_key(VK_BACK, false, true);
        return;
    default:
        break;
    }
    // The page and its rows wait while a question is up (keep the display? leave without applying?).
    if (!visible || s_settings_leave_asking || s_joycon_pairing || settings_display_confirming() ||
        s_mods_restart_ask.load(std::memory_order_acquire)) {
        return;
    }
    switch (input) {
    case NativeSettingsInput::SelectPage: {
        const int current = static_cast<int>(s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount);
        if (arg >= 0 && arg < static_cast<int>(kNativeSettingsTabCount) && arg != current) {
            cycle_native_settings_tab(arg - current);
        }
        return;
    }
    case NativeSettingsInput::SelectRow: {
        const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
        if (arg >= 0 && static_cast<unsigned>(arg) < native_settings_row_count(tab) &&
            !native_settings_heading(tab, static_cast<unsigned>(arg))) {
            end_controls_capture();
            if (static_cast<unsigned>(arg) != s_native_settings_row.load(std::memory_order_relaxed)) {
                s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
            }
            s_native_settings_row.store(static_cast<unsigned>(arg), std::memory_order_relaxed);
            if (tab == kModsTab) {
                // The mod the buttons' Options and Remove act on.
                const std::vector<SettingsRow> rows = mods_page(s_settings_draft);
                if (static_cast<unsigned>(arg) < rows.size() && rows[static_cast<unsigned>(arg)].list != 0u) {
                    const std::lock_guard<std::mutex> mods_lock(s_mods_mutex);
                    s_mod_focus = rows[static_cast<unsigned>(arg)].key.substr(4u);
                }
            }
        }
        return;
    }
    case NativeSettingsInput::Change:
        if (arg == -2 || arg == 2) {
            step_native_setting_round(arg / 2);
        } else if (arg != 0) {
            adjust_native_setting(arg < 0 ? -1 : 1);
        }
        return;
    case NativeSettingsInput::Press:
        if (arg >= 0 && static_cast<std::size_t>(arg) < selected_settings_buttons()) {
            press_settings_button(static_cast<unsigned>(arg));
        }
        return;
    case NativeSettingsInput::SetValue:
        if (arg >= 0) set_slider(static_cast<unsigned>(arg));
        return;
    case NativeSettingsInput::MoveMod: {
        // The selected mod dropped into a list (arg >> 16: 1 off, 2 on) at a place (its low bits; 0 the top).
        const unsigned tab = s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount;
        const std::vector<SettingsRow> rows = tab == kModsTab ? mods_page(s_settings_draft) : std::vector<SettingsRow>{};
        const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
        if (arg < 0 || row >= rows.size() || rows[row].list == 0u || rows[row].heading) return;
        const std::string id = rows[row].key.substr(4u);
        {
            const std::lock_guard<std::mutex> mods_lock(s_mods_mutex);
            place_mod_locked(id, (arg >> 16) == 2, static_cast<std::size_t>(arg & 0xFFFF));
            s_mod_held.clear();
            s_mod_focus = id;
        }
        select_mods_row(s_settings_draft, "mod:" + id);
        return;
    }
    default:
        return;
    }
}

void home_menu_open() noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    s_home_menu_row.store(0u, std::memory_order_relaxed);
    s_home_menu_asking.store(0, std::memory_order_relaxed);
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

int home_menu_view(bool drawing) noexcept {
    if (!s_home_menu_visible.load(std::memory_order_acquire)) {
        return -1;
    }
    if (drawing) {
        s_home_menu_drawn_by_game.store(true, std::memory_order_relaxed);
    }
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    const unsigned row = s_home_menu_row.load(std::memory_order_relaxed) % home_menu_answer_count();
    return static_cast<int>(row | static_cast<unsigned>(s_home_menu_asking.load(std::memory_order_relaxed)) << 8u);
}

void home_menu_point(unsigned answer) noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    if (s_home_menu_visible.load(std::memory_order_relaxed) && answer < home_menu_answer_count()) {
        s_home_menu_row.store(answer, std::memory_order_relaxed);
    }
}

void home_menu_choose(unsigned answer) noexcept {
    const std::lock_guard<std::mutex> lock(s_home_menu_mutex);
    if (s_home_menu_visible.load(std::memory_order_relaxed) && answer < home_menu_answer_count()) {
        s_home_menu_row.store(answer, std::memory_order_relaxed);
        choose_home_menu_answer(answer);
    }
}

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const HWND window = reinterpret_cast<HWND>(
        host_window::settings_window.load(std::memory_order_acquire));
    if (window != nullptr) {
        (void)PostMessageW(window, kSettingsKeyMessage,
                           static_cast<WPARAM>(virtual_key), 0);
    }
}

void native_settings_reset_overlay_state_for_tests() noexcept {
    s_display_confirm_deadline_ms = 0u;
    s_settings_leave_asking = false;
    s_settings_drawn_by_game.store(false, std::memory_order_relaxed);
    s_capture_target = galaxy::input::kWiiTargetCount;
    s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
    s_controls_device = {};
    s_controls_devices = {};
    s_test_controllers.reset();
    s_test_active_device.reset();
    s_settings_first_row = 0u;
    s_controls_message_until_ms = 0u;
    s_native_settings_visible.store(false, std::memory_order_relaxed);
    s_native_settings_dirty.store(false, std::memory_order_relaxed);
    s_native_settings_save_error.store(false, std::memory_order_relaxed);
    s_native_settings_tab.store(0u, std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
    settle_native_settings_row();
}
std::string native_settings_row_text_for_tests(unsigned row) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    return native_settings_row_text(s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount,
                                   row, s_settings_draft, s_runtime_draft, get_render_feature_caps());
}
void native_settings_set_controllers_for_tests(std::span<const galaxy::input::ControllerInfo> controllers,
                                                std::optional<galaxy::input::DeviceKey> active) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_test_controllers = std::vector<galaxy::input::ControllerInfo>(controllers.begin(), controllers.end());
    s_test_active_device = active;
    refresh_controls_devices(true);
}
RenderConfig native_settings_draft_for_tests() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    return s_settings_draft;
}
void native_settings_wait_saved_for_tests() {
    std::unique_lock<std::mutex> save_lock(s_settings_save_mutex);
    s_settings_save_changed.wait(save_lock, [] { return !s_settings_save_requested && !s_settings_save_running; });
}
void host_window::native_settings_set_refresh_override_for_tests(unsigned hz) noexcept {
    s_refresh_override_hz.store(hz, std::memory_order_relaxed);
}

std::int32_t take_pointer_wheel() noexcept {
    return host_window::pointer_wheel.exchange(0, std::memory_order_relaxed);
}
}  // namespace galaxy::gx
