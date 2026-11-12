// SPDX-License-Identifier: GPL-3.0-only
// The settings menu and HOME Menu on every platform: their state, keys and what they show. The window's own side is the
// interface in host_window.h (host_window_win32.cpp, the SDL host).

#include "galaxy/render/host_window.h"
#include "galaxy/launch_args.h"
#include "galaxy/virtual_keys.h"
#include "galaxy/path_text.h"
#include "galaxy/ui/settings_save_model.h"
#include "galaxy/windows_power.h"
#include "galaxy/controller_hid.h"

#include "galaxy/render/display_modes.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/texture_pack.h"
#include "galaxy/render_api.h"
#include "galaxy/mii_import.h"
#include "galaxy/mod_import.h"
#include "galaxy/mod_consent.h"
#include "galaxy/luma_mods_service.h"
#include "galaxy/mod_log.h"
#include "galaxy/mod_screens.h"
#include "galaxy/luma_fs_service.h"
#include "galaxy/input/mod_actions.h"
#include "galaxy/mods.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/ui/home_menu_model.h"
#include "galaxy/ui/settings_model.h"
#include "galaxy/ui/settings_graphics_page.h"
#include "galaxy/ui/settings_general_page.h"
#include "galaxy/ui/settings_display_page.h"
#include "galaxy/ui/settings_advanced_page.h"
#include "galaxy/ui/settings_system_page.h"

#include <luma_services.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <map>
#include <exception>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::render {

namespace host_window {

std::atomic<double> measured_present_hz{0.0};
std::atomic<std::int32_t> pointer_wheel{0};
// The session's GPU is an NVIDIA RTX (RendererD3D12::initialize): DLSS can be offered.
std::atomic<bool> rtx_adapter{false};

}  // namespace host_window

using host_window::flush_native_settings_if_dirty;
using host_window::poll_render_config_file;
using host_window::hidden_window_mode_enabled;

namespace {

#if defined(GALAXY_WITH_STREAMLINE) || defined(GALAXY_STREAMLINE_VK)
constexpr bool kStreamlineBuilt = true;
#else
constexpr bool kStreamlineBuilt = false;
#endif

// DLSS offered before it is installed (defined below).
bool dlss_offered();
bool dlss_installed_now();
bool xess_offered();
bool xess_installed_now();
bool vendor_prompting();
unsigned vendor_upscaler();
bool frame_generation_offered();
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live);
bool dlss_prompt_key(std::uintptr_t key, bool was_down);
bool restart_ask_key(std::uintptr_t key, bool was_down);
bool mod_question_key(std::uintptr_t key, bool was_down);
bool mod_question_up();
bool mods_page_key(std::uintptr_t key, bool accept, bool was_down);
std::vector<std::string> license_pages(const std::string& text);
void open_log_entry(std::size_t at);
unsigned mods_draft_changes();
void commit_mods_draft();
void revert_mods_draft();
void dlss_dismiss_prompt();
void poll_dlss_download();
std::string dlss_footer(std::uint64_t now_ms, bool& error);
NativeSettingsStatus dlss_status(std::uint64_t now_ms);

// Settings pages built from rows (DISPLAY, GRAPHICS, GAME, MODS, SYSTEM: settings_page): a label, the
// value for the draft, how left/right change the draft (none: the row is fixed) and a help line; a
// heading starts a section and is never selected. CONTROLS and AUDIO keep their own rows.
using ui::SettingsRow;
using ui::button_row;
std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps);
bool settings_page_tab(unsigned tab) { return tab != 2u && tab != 3u; }

// The temporal upscaler this configuration runs (2 FSR 3, 3 DLSS, 4 XeSS, 5 SGSR 2, 6 MetalFX's temporal scaler), 0 none.
unsigned temporal_upscaler(const RenderConfig& render) {
    if (render.upscaler == 5u && sgsr2_available()) return 5u;
    if (render.upscaler == 6u && metalfx_temporal_available()) return 6u;
    if (render.upscaler == 2u && fsr3_available()) return 2u;
    if (render.upscaler == 3u && host_window::upscaler_caps().dlss) return 3u;
    if (render.upscaler == 4u && host_window::upscaler_caps().xess) return 4u;
    return 0u;
}

// The upscaler choices this runtime and machine offer, in menu order.
std::vector<unsigned> available_upscalers() {
    std::vector<unsigned> choices{0u, 1u};
    if (metalfx_available()) choices.push_back(6u);
    if (sgsr2_available()) choices.push_back(5u);
    if (fsr3_available()) choices.push_back(2u);
    if (host_window::upscaler_caps().dlss || dlss_offered()) choices.push_back(3u);
    if (host_window::upscaler_caps().xess || xess_offered()) choices.push_back(4u);
    return choices;
}

// Output scale divided by the quality ratio (native, 1.5, 1.7, 2, 3, then ultra quality 1.3), at least 1x.
using ui::quality_render_fraction;

// FSR 1 (or a temporal upscaler this machine cannot run, which falls back to it) with a quality mode: the render
// scale follows the output like FSR 3/DLSS. Upscaling off renders at INTERNAL RESOLUTION instead.
bool spatial_quality_mode(const RenderConfig& render) {
    return temporal_upscaler(render) == 0u && render.upscaler != 0u && render.spatial_quality >= 1u;
}

// The fraction of the output's size the upscaling settings render at; DLSS's mode has a narrowest render width.
float upscaler_render_fraction(const RenderConfig& render, unsigned output_width, unsigned output_height) {
    const unsigned temporal = temporal_upscaler(render);
    if (temporal == 0u) {
        return spatial_quality_mode(render) ? quality_render_fraction(render.spatial_quality) : 1.0f;
    }
    // DLSS has no ultra quality mode (5): it runs quality.
    const unsigned quality = temporal == 3u && render.temporal_quality == 5u ? 1u : std::min(render.temporal_quality, 5u);
    float fraction = quality_render_fraction(quality);
    if (temporal == 3u && output_width != 0u) {
        const unsigned narrowest = host_window::dlss_min_width(quality, output_width, output_height);
        fraction = std::max(fraction, static_cast<float>(narrowest) / static_cast<float>(output_width));
    }
    return std::min(fraction, 1.0f);
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps);

std::recursive_mutex s_native_settings_mutex;

std::atomic<bool> s_native_settings_visible{false};
galaxy::ui::HomeMenuModel s_home_menu;
bool handle_home_menu_key(std::uintptr_t key, bool was_down) {
    using galaxy::ui::HomeAction;
    HomeAction action = HomeAction::Ignore;
    switch (key) {
    case VK_UP:
    case VK_LEFT: action = HomeAction::Previous; break;
    case VK_DOWN:
    case VK_RIGHT: action = HomeAction::Next; break;
    case VK_RETURN:
    case VK_SPACE:
    case galaxy::input::settings_pad::kKeyAccept: action = HomeAction::Accept; break;
    case VK_ESCAPE:
    case VK_BACK: action = HomeAction::Back; break;
    case VK_HOME: action = HomeAction::Close; break;
    default: break;
    }
    s_home_menu.key(action, was_down);
    return true;
}
ui::SettingsSaveModel s_settings_save{
    save_runtime_settings_to_file, save_render_config_to_file,
    [] { std::cerr << "[settings] save failed; kept for retry\n"; },
    [] { galaxy::host::name_current_thread(L"Nebula Settings Save"); }};
std::atomic<unsigned> s_native_settings_tab{0};
std::atomic<unsigned> s_native_settings_row{0};
// The selected row's selected button, for the page and row it was chosen on (page << 16 | row); selecting a row
// starts it at its first.
std::atomic<unsigned> s_native_settings_button{0};
std::atomic<unsigned> s_native_settings_button_row{~0u};
// The game's menu has asked for its view (native_settings_view). Before that an open request is held: the menu opens
// at that first call, and its keys wait.
std::atomic<bool> s_settings_drawn_by_game{false};
// Leaving with changes not applied asks first: apply them, discard them, or stay. Under s_native_settings_mutex.
bool s_settings_leave_asking = false;
// Apply saved mod changes this session did not start with: the menu asks whether to restart now.
std::atomic<bool> s_mods_restart_ask{false};
// RAY TRACING turned on (applied) before its shaders are prepared: the menu asks to restart, which prepares them.
std::atomic<bool> s_rt_restart_ask{false};
// SYSTEM > DIAGNOSTICS > RECORD: the menu asks to restart into a recorded session (the launcher's recording).
std::atomic<bool> s_diagnostics_ask{false};

// A restart question is up (the mods', ray tracing's, the diagnostics run's).
bool restart_asking() {
    return s_mods_restart_ask.load(std::memory_order_acquire) || s_rt_restart_ask.load(std::memory_order_acquire) ||
           s_diagnostics_ask.load(std::memory_order_acquire);
}
// The pages, in order.
constexpr const char* kNativeSettingsTabNames[] = {"DISPLAY", "GRAPHICS", "CONTROLS", "AUDIO", "GENERAL", "MODS", "SYSTEM",
                                                   "ADVANCED"};
// The pages shown. MODS comes back with its feature.
#if defined(__ANDROID__)
constexpr unsigned kShownTabs[] = {0u, 1u, 2u, 3u, 4u, 5u, 7u};  // nothing on SYSTEM there: diagnostics are the launcher's
#else
constexpr unsigned kShownTabs[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
#endif
constexpr unsigned kShownTabCount = static_cast<unsigned>(std::size(kShownTabs));

unsigned shown_position(unsigned tab) {
    for (unsigned i = 0; i < kShownTabCount; ++i) {
        if (kShownTabs[i] == tab) return i;
    }
    return 0u;
}

// The GRAPHICS page's rows for what is not built natively yet. Each goes with its feature.
bool graphics_row_hidden(const char* label) {
    static constexpr const char* kHidden[] = {
        "RAY TRACING", "RAY TRACING QUALITY", "TEXTURE PACKS", "  REPLACEMENTS", "DUMP TEXTURES", "TEXTURE DUMPS FOLDER",
    };
    for (const char* hidden : kHidden) {
        if (std::strcmp(label, hidden) == 0) return true;
    }
    return false;
}

// Rows of the shown pages for what is not there yet or not on this platform.
bool row_hidden(const char* label) {
    static constexpr const char* kHidden[] = {
        "POST-PROCESS", "RAY TRACING HARDWARE", "NVIDIA STREAMLINE", "NVIDIA REFLEX", "DLSS", "DLSS FRAME GENERATION",
#if defined(__ANDROID__)
        // MONITOR, DISPLAY MODE and RESOLUTION: the display page has none on a phone (fullscreen_only).
        "REFRESH RATE", "VSYNC", "DISCORD STATUS", "GRAPHICS CARD", "DIAGNOSTICS",
#endif
    };
    for (const char* hidden : kHidden) {
        if (std::strcmp(label, hidden) == 0) return true;
    }
    return false;
}

// Settings menu draft. Display and graphics rows edit s_settings_draft, the game's settings (the audio mix, text
// speed, button icons, language, Discord status) s_runtime_draft; Enter (Y on a controller) applies them, Backspace
// (X) reverts them and closing the menu discards them. A display change (monitor, mode, resolution, refresh rate)
// stays only when it is kept within kDisplayConfirmMs. All guarded by s_native_settings_mutex.
RenderConfig s_settings_draft{};
RuntimeSettings s_runtime_draft{};
RuntimeSettings s_runtime_base{};  // the game's settings when s_runtime_draft was taken

// CONTROLS' on-screen control rows (the phone's) as the player set them, until Apply hands them to the controls; Back
// and closing the menu drop them. Unedited, the controls' own.
struct TouchDraft {
    bool edited = false;
    galaxy::host::TouchControlsSettings settings{};
    bool reset_places = false;  // BUTTON PLACES' Reset, done at Apply
};
TouchDraft s_touch_draft{};
// BUTTON SIZE's steps, in percent; 0 (Auto): as large as the game makes them for this screen. Kept for the screen as it
// is now: each size and turn of a screen has its own.
constexpr unsigned kTouchSizes[] = {0u, 40u, 50u, 60u, 70u, 80u, 90u, 100u, 110u, 120u, 130u, 140u, 150u, 160u, 180u, 200u};

galaxy::host::TouchControlsSettings touch_draft_settings() {
    return s_touch_draft.edited ? s_touch_draft.settings : galaxy::host::touch_controls_settings();
}

unsigned touch_draft_changes() {
    if (!galaxy::host::touch_controls_present()) return 0u;
    const galaxy::host::TouchControlsSettings live = galaxy::host::touch_controls_settings();
    const galaxy::host::TouchControlsSettings draft = touch_draft_settings();
    return (draft.size != live.size ? 1u : 0u) + (draft.opacity != live.opacity ? 1u : 0u) +
           (draft.vibration != live.vibration ? 1u : 0u) + (s_touch_draft.reset_places ? 1u : 0u);
}

void apply_touch_draft() {
    if (touch_draft_changes() == 0u) {
        s_touch_draft = {};
        return;
    }
    const galaxy::host::TouchControlsSettings live = galaxy::host::touch_controls_settings();
    const galaxy::host::TouchControlsSettings draft = touch_draft_settings();
    if (draft.size != live.size || draft.opacity != live.opacity || draft.vibration != live.vibration) {
        galaxy::host::set_touch_controls_settings(draft);
    }
    if (s_touch_draft.reset_places) {
        galaxy::host::reset_touch_controls_places();
    }
    s_touch_draft = {};
}

void reset_runtime_draft() {
    s_runtime_draft = get_runtime_settings();
    s_runtime_base = s_runtime_draft;
}
std::vector<host_window::SettingsMonitor> s_settings_monitors;
std::uint64_t s_display_confirm_deadline_ms = 0u;
RenderConfig s_display_confirm_previous{};
std::uint64_t s_settings_applied_ms = 0u;
constexpr std::uint64_t kDisplayConfirmMs = 15000u;

// CONTROLS page (tab 2): a draft of the bindings/tuning and of the input source, applied together with the other
// pages. The edited device (keyboard and mouse or an attached controller) selects the scheme whose table the rows
// show. A binding row waiting for an input is s_capture_target (kGameInputCount: none). Guarded by
// s_native_settings_mutex.
constexpr unsigned kControlsTab = 2u;
constexpr unsigned kAudioTab = 3u;
constexpr unsigned kAudioResetRow = 8u;  // after the seven volumes and the sound output
constexpr unsigned kModsTab = 5u;
galaxy::input::ControlsConfig s_controls_draft = galaxy::input::default_controls_config();
RuntimeInputMode s_input_mode_draft = RuntimeInputMode::Auto;
galaxy::input::ControlScheme s_controls_scheme = galaxy::input::ControlScheme::Keyboard;
galaxy::input::DeviceKey s_controls_device{};
galaxy::input::DeviceList s_controls_devices{};
std::uint64_t s_controls_devices_ms = 0u;
std::size_t s_capture_target = galaxy::input::kGameInputCount;
// The mods' input actions (services-input 6; plan 2.5 layer 1b): a row each after the game's bindings, bound per scheme
// as those are. The draft holds, per scheme, only the actions edited since it was taken, by "<mod id>/<action id>"; the
// rest show their live bindings. A capture for action i waits as s_capture_target kModCaptureBase + i.
constexpr std::size_t kModCaptureBase = galaxy::input::kGameInputCount + 1u;
std::array<std::map<std::string, galaxy::input::TargetBindings>, galaxy::input::kControlSchemeCount> s_mod_binds_draft;
// The harness facts controls.<mod id> last written per action ("<mod id>/<action id>"), written when they change.
std::map<std::string, std::string> s_mod_action_facts;
std::uint64_t s_capture_deadline_ms = 0u;
constexpr std::uint64_t kCaptureTimeoutMs = 6000u;
std::string s_controls_message;
std::uint64_t s_controls_message_until_ms = 0u;
// The window thread's left/right-resolved code for the key being handled.
std::uint32_t s_handled_key_normalized = 0u;

constexpr unsigned kNativeSettingsTabCount = 8;

// Each language in its own name (UTF-8: Ç, Ñ, 日本語); a disc offers only its own, whose letters its font has.
using ui::game_language_name;
using ui::native_settings_first_row;
using ui::kNativeSettingsVisibleRows;

using ui::step_gain;
using ui::display_settings_differ;

const host_window::SettingsMonitor* settings_monitor(const RenderConfig& render);

// The refresh rate the menu assumes in place of the display's (tests; 0 in the game).
std::atomic<unsigned> s_refresh_override_hz{0u};

// The refresh rate the picture will be shown at with these settings.
unsigned settings_refresh_hz(const RenderConfig& render) {
    if (const unsigned hz = s_refresh_override_hz.load(std::memory_order_relaxed); hz != 0u) {
        return hz;
    }
    const host_window::SettingsMonitor* monitor = settings_monitor(render);
    return monitor != nullptr ? monitor->refresh_hz : 0u;
}

// GRAPHICS > FRAME RATE. INTERPOLATION OFF presents the game's own frames (FRAME RATE LIMIT 30 or 60); NATIVE adds
// Nebula's interpolated frames up to the TARGET FRAME RATE; DLSS FRAME GENERATION has NVIDIA's generated frames reach
// it. The target is max_fps (0: the display's refresh rate); frame_generation selects DLSS.
using ui::Interpolation;
ui::FrameRateCaps settings_frame_rate_caps(const RenderConfig& render, bool offer = false) {
    return {settings_refresh_hz(render), offer && frame_generation_offered(),
            host_window::upscaler_caps().frame_generation, host_window::upscaler_caps().frame_generation_dynamic,
            host_window::upscaler_caps().frame_generation_max, host_window::upscaler_caps().fsr_frame_generation};
}
Interpolation interpolation_of(const RenderConfig& render) { return ui::interpolation_of(render); }
std::vector<unsigned> interpolation_targets(const RenderConfig& render, Interpolation mode) {
    return ui::interpolation_targets(mode, settings_frame_rate_caps(render));
}
std::vector<Interpolation> interpolation_modes(const RenderConfig& render) {
    return ui::interpolation_modes(settings_frame_rate_caps(render, true));
}
void normalize_interpolation(RenderConfig& render) {
    ui::normalize_interpolation(render, settings_frame_rate_caps(render));
}

const host_window::SettingsMonitor* settings_monitor(const RenderConfig& render) {
    const std::string wanted = render.monitor.data();
    const host_window::SettingsMonitor* primary = nullptr;
    for (const host_window::SettingsMonitor& monitor : s_settings_monitors) {
        if (monitor.id == wanted) return &monitor;
        if (monitor.primary) primary = &monitor;
    }
    return primary != nullptr || s_settings_monitors.empty() ? primary : &s_settings_monitors.front();
}

// The picture size the settings target: the monitor (borderless) or the window.
std::pair<unsigned, unsigned> settings_output_size(const RenderConfig& render) {
    const host_window::SettingsMonitor* monitor = settings_monitor(render);
    const unsigned desktop_w = monitor ? monitor->width : host_window::backbuffer_width;
    const unsigned desktop_h = monitor ? monitor->height : host_window::backbuffer_height;
    switch (render.display_mode) {
    case kDisplayBorderless:
        return {desktop_w, desktop_h};
    default:
        return {render.window_width, render.window_height};
    }
}

// The largest internal resolution the selected display can show.
float settings_render_fraction(const RenderConfig& render) {
    const host_window::SettingsMonitor* monitor = settings_monitor(render);
    unsigned width = monitor ? monitor->width : host_window::backbuffer_width;
    unsigned height = monitor ? monitor->height : host_window::backbuffer_height;
    return upscaler_render_fraction(render, width, height);
}

// Window client sizes that fit the monitor's work area, smallest first: those
// of a fixed aspect (aspect_window_sizes), or common sizes and the monitor's
// own resolutions.
std::vector<std::pair<unsigned, unsigned>> settings_window_sizes(
    const host_window::SettingsMonitor* monitor, unsigned aspect_mode) {
    static constexpr std::pair<unsigned, unsigned> kCommon[] = {
        {854u, 480u}, {1024u, 768u}, {1280u, 720u}, {1280u, 800u}, {1366u, 768u},
        {1440u, 900u}, {1600u, 900u}, {1680u, 1050u}, {1920u, 1080u}, {1920u, 1200u},
        {2560u, 1080u}, {2560u, 1440u}, {2560u, 1600u}, {3440u, 1440u}, {3840u, 1080u},
        {3840u, 2160u}, {5120u, 1440u}, {5120u, 2160u}, {5120u, 2880u}, {7680u, 4320u}};
    long max_w = 7680;
    long max_h = 4320;
    if (monitor != nullptr) {
        max_w = monitor->max_client_width;
        max_h = monitor->max_client_height;
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
        for (const auto& [w, h] : monitor->resolutions) add(w, h);
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
    const host_window::SettingsMonitor* monitor = settings_monitor(render);
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
}

// A binding as the menu shows it: a key by position shows its letter, not the settings file's "Position." form.
std::string menu_bindings(galaxy::input::ControlScheme scheme, const galaxy::input::TargetBindings& bindings) {
    std::string text = galaxy::input::format_target_bindings(scheme, bindings);
    constexpr std::string_view prefix = "Position.";
    for (std::size_t at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at)) {
        text.erase(at, prefix.size());
    }
    return text;
}

unsigned controls_changes();

// The declared actions on the scheme being edited (none until the first code mod begins: services-input 3).
std::vector<galaxy::input::mod_actions::Listed> mod_action_list() {
    if (!galaxy::input::mod_actions::any()) return {};
    return galaxy::input::mod_actions::list(s_controls_scheme);
}

std::string mod_action_key(const galaxy::input::mod_actions::Listed& action) {
    return action.mod_id + "/" + action.id;
}

std::map<std::string, galaxy::input::TargetBindings>& mod_binds_draft(galaxy::input::ControlScheme scheme) {
    return s_mod_binds_draft[static_cast<std::size_t>(scheme) % galaxy::input::kControlSchemeCount];
}

// An action's bindings on `scheme` as the draft has them (none: it follows its game control).
galaxy::input::TargetBindings mod_action_draft(galaxy::input::ControlScheme scheme,
                                               const galaxy::input::mod_actions::Listed& action) {
    const auto& draft = mod_binds_draft(scheme);
    const auto it = draft.find(mod_action_key(action));
    return it != draft.end() ? it->second : action.bindings;
}

bool no_bindings(const galaxy::input::TargetBindings& bindings) {
    return std::all_of(bindings.begin(), bindings.end(), [](const galaxy::input::InputSource& s) { return s.empty(); });
}

// The game control an action follows, in the played language (its CONTROLS label's S. row; a digit or a sign as is).
std::string mod_action_control(const galaxy::input::mod_actions::Listed& action) {
    const std::string label = galaxy::input::game_input_label(action.follows);
    std::string control;
    if (label.find_first_of("0123456789+-") == std::string::npos || label.find(' ') != std::string::npos) {
        std::string row = "S.";
        for (const char c : label) row += c == ' ' ? '_' : c;
        control = galaxy::modlog::row_words(row.c_str(), action.mod_id, {});
    }
    return control.empty() ? label : control;
}

// An unbound action's value: the game control it follows (Mods.ActionFollows).
std::string mod_action_follows(const galaxy::input::mod_actions::Listed& action) {
    const std::string control = mod_action_control(action);
    std::string words = galaxy::modlog::row_words("Mods.ActionFollows", action.mod_id, {{"control", control}});
    return words.empty() ? "SAME AS " + control : words;
}

// The LIGHTING row: 0 per vertex, 1 per pixel, 2 enhanced.
using ui::lighting_mode;

unsigned runtime_draft_changes(RuntimeSettings* live) {
    const RuntimeSettings& d = s_runtime_draft;
    const RuntimeSettings& b = s_runtime_base;
    // A setting added to RuntimeSettings is drafted here too: these bindings stop compiling until it is.
    [[maybe_unused]] const auto& [mode, audio, text_speed, language, discord_status, button_icons, co_star, menu_text_size, focus_loss] = d;
    [[maybe_unused]] const auto& [master, music, sfx, voice, ambience, cutscene, wiimote_speaker, sound_output] = d.audio;
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
    take(d.audio.sound_output, b.audio.sound_output, out.audio.sound_output);
    take(d.text_speed, b.text_speed, out.text_speed);
    take(d.language, b.language, out.language);
    take(d.discord_status, b.discord_status, out.discord_status);
    take(d.button_icons, b.button_icons, out.button_icons);
    take(d.co_star, b.co_star, out.co_star);
    take(d.menu_text_size, b.menu_text_size, out.menu_text_size);
    take(d.focus_loss, b.focus_loss, out.focus_loss);
    return changes;
}

unsigned pending_settings_changes() {
    const RenderConfig live = get_render_config();
    const RenderConfig& d = s_settings_draft;
    unsigned changes = 0u;
    changes += d.display_mode != live.display_mode;
    changes += d.monitor != live.monitor;
    changes += d.display_mode == kDisplayWindowed &&
        (d.window_width != live.window_width || d.window_height != live.window_height);
    changes += d.aspect_mode != live.aspect_mode;
    changes += d.vsync_interval != live.vsync_interval;
    changes += d.max_fps != live.max_fps;
    changes += d.frame_generation != live.frame_generation;
    changes += d.aa_mode != live.aa_mode;
    changes += d.smaa_quality != live.smaa_quality;
    changes += d.msaa_samples != live.msaa_samples;
    changes += d.upscaler != live.upscaler;
    changes += d.temporal_quality != live.temporal_quality;
    changes += d.spatial_quality != live.spatial_quality;
    changes += d.internal_height != live.internal_height;
    changes += d.sharpness != live.sharpness;
    changes += d.hdr != live.hdr || d.paper_white != live.paper_white;
    changes += d.anisotropic_filtering != live.anisotropic_filtering;
    changes += d.enhanced_mipmaps != live.enhanced_mipmaps;
    changes += d.curved_surfaces != live.curved_surfaces;
    changes += lighting_mode(d) != lighting_mode(live);
    changes += d.ambient_occlusion != live.ambient_occlusion;
    changes += d.ambient_occlusion_ssao != live.ambient_occlusion_ssao;
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
    Pointer,
    GyroSensitivity,
    TouchSensitivity,
    WiimoteStatus,  // the Wii Remote modes: what the remote has and how it points (shown, not changed)
    PairJoyCon,
    ArrangeTouch,
    TouchSize,  // the phone's on-screen controls (s_touch_draft)
    TouchOpacity,
    TouchVibration,
    TouchPlaces,
    ModAction,  // a mod's input action (services-input): `action` is its place in mod_actions::list
    Reset,
};

struct ControlsRowInfo {
    ControlsRow kind = ControlsRow::Reset;
    galaxy::input::GameInput target = galaxy::input::GameInput::A;
    std::uint32_t action = 0;
    friend bool operator==(const ControlsRowInfo&, const ControlsRowInfo&) = default;
};

// The Pair Joy-Con screen (CONTROLS > PAIR JOY-CON): open while the controller worker pairs the halves whose L and R
// are held; it closes a moment after a pair is made, or with Escape or B.
bool s_joycon_pairing = false;
std::uint32_t s_joycon_paired_at_open = 0u;
std::uint64_t s_joycon_paired_ms = 0u;
std::uint64_t s_joycon_opened_ms = 0u;
constexpr std::uint64_t kJoyConPairedShowMs = 1500u;

void open_joycon_pairing() {
    s_joycon_pairing = true;
    s_joycon_opened_ms = galaxy::steady_ms();
    s_joycon_paired_at_open = galaxy::host::joycon_status().paired;
    s_joycon_paired_ms = 0u;
    galaxy::host::set_joycon_pairing(true);
    std::cerr << "[settings] pair joy-con\n";
}

void close_joycon_pairing() {
    if (!s_joycon_pairing) return;
    s_joycon_pairing = false;
    galaxy::host::set_joycon_pairing(false);
}

// What the controller being set up has: its family's sensors, less what this one lacks.
struct ControlsSensors {
    bool sensors = false;
    bool gyro = false;
    bool touch = false;
};
ControlsSensors controls_sensors() {
    ControlsSensors have;
    have.sensors = have.gyro = galaxy::input::scheme_has_motion(s_controls_scheme);
    have.touch = galaxy::input::scheme_has_touch(s_controls_scheme);
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    const std::size_t count = galaxy::host::connected_controllers(connected);
    for (std::size_t i = 0; i < count; ++i) {
        if (connected[i].scheme == s_controls_device.scheme && connected[i].slot == s_controls_device.slot) {
            have.sensors = have.sensors && connected[i].sensors;
            have.gyro = have.gyro && connected[i].gyro;
            have.touch = have.touch && connected[i].touch;
        }
    }
    return have;
}

std::vector<ControlsRow> controls_tail_rows() {
    using galaxy::input::scheme_is_gamepad;
    std::vector<ControlsRow> rows{ControlsRow::PointerSpeed, ControlsRow::InvertPointerY};
    if (scheme_is_gamepad(s_controls_scheme)) {
        rows.push_back(ControlsRow::StickDeadzone);
        rows.push_back(ControlsRow::PointerDeadzone);
        rows.push_back(ControlsRow::PointerHide);
        // A generic controller has no standard motor report: no vibration.
        if (s_controls_scheme != galaxy::input::ControlScheme::Generic) {
            rows.push_back(ControlsRow::Vibration);
            rows.push_back(ControlsRow::VibrationStrength);
        }
    }
    // The pointer's source where the controller has more than its stick, each source's sensitivity while it points.
    const galaxy::input::SchemeTuning& tuning = s_controls_draft.tune(s_controls_scheme);
    const ControlsSensors have = controls_sensors();
    if (have.gyro || have.touch) {
        rows.push_back(ControlsRow::Pointer);
        if (tuning.gyro_pointer && have.gyro) rows.push_back(ControlsRow::GyroSensitivity);
        if (tuning.touch_pointer && have.touch) rows.push_back(ControlsRow::TouchSensitivity);
    }
    if (have.sensors) {
        rows.push_back(ControlsRow::MotionShake);
        rows.push_back(ControlsRow::ShakeSensitivity);
    }
    rows.push_back(ControlsRow::Reset);
    return rows;
}

void refresh_controls_devices(bool force);


// The CONTROLS page's rows top to bottom: the input mode; PAIR JOY-CON while a Joy-Con half is connected (Auto,
// Controller only); then, but in the Wii Remote mode (its buttons are its own), the device being set up and, while
// there is one, its bindings (the jump/back row after A and B) and its settings.
std::vector<ControlsRowInfo> controls_rows() {
    using galaxy::input::GameInput;
    std::vector<ControlsRowInfo> rows{{ControlsRow::InputMode}};
    const galaxy::host::JoyConStatus joycons = galaxy::host::joycon_status();
    if ((s_input_mode_draft == RuntimeInputMode::Auto || s_input_mode_draft == RuntimeInputMode::Controller) &&
        joycons.lefts + joycons.rights != 0u) {
        rows.push_back({ControlsRow::PairJoyCon});
    }
    if (galaxy::is_wii_remote_mode(s_input_mode_draft)) {
        rows.push_back({ControlsRow::WiimoteStatus});
        return rows;
    }
    if (s_controls_devices.count == 0u) {
        rows.push_back({ControlsRow::Device});
        return rows;
    }
    if (s_controls_devices.count != 1u || !galaxy::input::is_phone(s_controls_devices.entries[0].key)) {
        rows.push_back({ControlsRow::Device});
    }
    // The phone's own controls are arranged and sized, not bound.
    if (galaxy::input::is_phone(s_controls_device)) {
        rows.push_back({ControlsRow::ArrangeTouch});
        if (galaxy::host::touch_controls_present()) {
            for (const ControlsRow kind : {ControlsRow::TouchSize, ControlsRow::TouchOpacity, ControlsRow::TouchVibration,
                                           ControlsRow::TouchPlaces}) {
                rows.push_back({kind});
            }
        }
        return rows;
    }
    for (unsigned index = 0; index < galaxy::input::kGameInputCount; ++index) {
        const GameInput target = index == 2u ? GameInput::JumpBack
                                 : index > 2u ? static_cast<GameInput>(index - 1u)
                                              : static_cast<GameInput>(index);
        rows.push_back({ControlsRow::Binding, target});
    }
    const std::size_t actions = mod_action_list().size();
    for (std::uint32_t action = 0; action < actions; ++action) {
        rows.push_back({ControlsRow::ModAction, GameInput::A, action});
    }
    for (const ControlsRow kind : controls_tail_rows()) {
        rows.push_back({kind});
    }
    return rows;
}

unsigned controls_row_count() {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    refresh_controls_devices(false);
    return static_cast<unsigned>(controls_rows().size());
}

ControlsRowInfo controls_row(unsigned row) {
    const std::vector<ControlsRowInfo> rows = controls_rows();
    return row < rows.size() ? rows[row] : ControlsRowInfo{};
}

// The rows changed under the selection (a Joy-Con half came or went, a sensitivity row with its source): the selection
// stays on the row it was on.
void keep_controls_selection() {
    static std::vector<ControlsRowInfo> s_last;
    const std::vector<ControlsRowInfo> rows = controls_rows();
    if (rows == s_last) return;
    const unsigned selected = s_native_settings_row.load(std::memory_order_relaxed);
    if (selected < s_last.size()) {
        const auto it = std::find(rows.begin(), rows.end(), s_last[selected]);
        const unsigned row = it != rows.end() ? static_cast<unsigned>(it - rows.begin())
                                              : std::min(selected, static_cast<unsigned>(rows.size()) - 1u);
        s_native_settings_row.store(row, std::memory_order_relaxed);
    }
    s_last = rows;
}

unsigned controls_changes() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    unsigned changes = 0u;
    for (std::size_t i = 0; i < galaxy::input::kControlSchemeCount; ++i) {
        for (std::size_t t = 0; t < galaxy::input::kGameInputCount; ++t) {
            changes += s_controls_draft.tables[i].targets[t] != live.tables[i].targets[t];
        }
        changes += !(s_controls_draft.tuning[i] == live.tuning[i]);
        if (!s_mod_binds_draft[i].empty()) {
            for (const auto& action : galaxy::input::mod_actions::list(static_cast<galaxy::input::ControlScheme>(i))) {
                const auto it = s_mod_binds_draft[i].find(mod_action_key(action));
                changes += it != s_mod_binds_draft[i].end() && it->second != action.bindings;
            }
        }
    }
    changes += s_input_mode_draft != get_runtime_settings().input_mode;
    changes += touch_draft_changes();
    return changes;
}

void show_controls_message(const char* message) {
    s_controls_message = message;
    s_controls_message_until_ms = galaxy::steady_ms() + 2500u;
}

bool controls_capturing() {
    return s_capture_target != galaxy::input::kGameInputCount;
}

void end_controls_capture() {
    if (controls_capturing()) {
        s_capture_target = galaxy::input::kGameInputCount;
        galaxy::host::cancel_controller_binding_capture();
    }
}

// `target`: a GameInput, or kModCaptureBase + a mod action's place.
void start_controls_capture_at(std::size_t target) {
    s_capture_target = target;
    s_capture_deadline_ms = galaxy::steady_ms() + kCaptureTimeoutMs;
    if (galaxy::input::scheme_is_gamepad(s_controls_scheme)) {
        galaxy::host::arm_controller_binding_capture(s_controls_scheme, s_controls_device.slot);
    }
}

void start_controls_capture(galaxy::input::GameInput target) {
    start_controls_capture_at(static_cast<std::size_t>(target));
}

void finish_controls_capture(galaxy::input::InputSource source) {
    if (!controls_capturing()) {
        return;
    }
    if (s_capture_target >= kModCaptureBase) {
        // A mod's action: a source its game control could take (mod_actions::bind refuses the others).
        const std::size_t index = s_capture_target - kModCaptureBase;
        end_controls_capture();
        const std::vector<galaxy::input::mod_actions::Listed> actions = mod_action_list();
        if (index >= actions.size()) return;
        const galaxy::input::mod_actions::Listed& action = actions[index];
        if (!galaxy::input::binding_source_allowed(s_controls_scheme, action.follows, source)) {
            show_controls_message("THAT INPUT CANNOT DRIVE THIS ROW");
            return;
        }
        galaxy::input::TargetBindings bindings = mod_action_draft(s_controls_scheme, action);
        galaxy::input::add_binding(bindings, source);
        mod_binds_draft(s_controls_scheme)[mod_action_key(action)] = bindings;
        std::cerr << "[settings] bind " << galaxy::input::control_scheme_id(s_controls_scheme) << ".mod "
                  << mod_action_key(action) << " = " << galaxy::input::format_target_bindings(s_controls_scheme, bindings)
                  << '\n';
        return;
    }
    const auto target = static_cast<galaxy::input::GameInput>(s_capture_target);
    end_controls_capture();
    if (!galaxy::input::binding_source_allowed(s_controls_scheme, target, source)) {
        show_controls_message("THAT INPUT CANNOT DRIVE THIS ROW");
        return;
    }
    galaxy::input::add_binding(s_controls_draft.table(s_controls_scheme)[target], source);
    std::cerr << "[settings] bind " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
              << galaxy::input::game_input_id(target) << " = "
              << galaxy::input::format_target_bindings(
                     s_controls_scheme, s_controls_draft.table(s_controls_scheme)[target])
              << '\n';
}

void expire_controls_capture() {
    if (controls_capturing() && galaxy::steady_ms() >= s_capture_deadline_ms) {
        end_controls_capture();
    }
}

constexpr std::uint64_t kControlsDevicesRefreshMs = 500u;

// Rebuilds the device list from what is attached and keeps the edited device
// through hot plug (a controller of the same family takes over, else the
// keyboard and mouse).
void refresh_controls_devices(bool force) {
    const std::uint64_t now = galaxy::steady_ms();
    if (!force && s_controls_devices_ms != 0u && now - s_controls_devices_ms < kControlsDevicesRefreshMs) {
        return;
    }
    s_controls_devices_ms = now;
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    const std::size_t count = galaxy::host::connected_controllers(connected);
    const std::optional<galaxy::input::DeviceKey> active = galaxy::host::last_input_device();
#if defined(__ANDROID__)
    constexpr bool kHasPhone = true;
#else
    constexpr bool kHasPhone = false;
#endif
    // Only what is attached, on every system.
    galaxy::input::build_device_list(std::span<const galaxy::input::ControllerInfo>(connected.data(), count), active,
                                     s_controls_devices, galaxy::host::keyboard_or_mouse_attached(), kHasPhone);
    // Only what the input mode reads.
    if (s_input_mode_draft != RuntimeInputMode::Auto) {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < s_controls_devices.count; ++i) {
            const galaxy::input::DeviceKey key = s_controls_devices.entries[i].key;
            const bool phone = galaxy::input::is_phone(key);
            const bool keyboard = !phone && key.scheme == galaxy::input::ControlScheme::Keyboard;
            const bool wanted = s_input_mode_draft == RuntimeInputMode::Phone          ? phone
                                : s_input_mode_draft == RuntimeInputMode::KeyboardMouse ? keyboard
                                                                                        : !phone && !keyboard;
            if (wanted) s_controls_devices.entries[kept++] = s_controls_devices.entries[i];
        }
        s_controls_devices.count = kept;
    }
    if (s_controls_devices.count == 0u) {
        end_controls_capture();
        return;
    }
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
    for (auto& draft : s_mod_binds_draft) draft.clear();
    s_input_mode_draft = get_runtime_settings().input_mode;
    s_touch_draft = {};
}

unsigned native_settings_row_count(unsigned tab) {
    tab %= kNativeSettingsTabCount;
    if (settings_page_tab(tab)) {
        return static_cast<unsigned>(
            settings_page(tab, s_settings_draft, s_runtime_draft, get_render_feature_caps()).size());
    }
    return tab == 2u ? controls_row_count() : kAudioResetRow + 1u;
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
    render.window_width = previous.window_width;
    render.window_height = previous.window_height;
    set_render_config(render);
    s_settings_draft.display_mode = render.display_mode;
    s_settings_draft.monitor = render.monitor;
    s_settings_draft.window_width = render.window_width;
    s_settings_draft.window_height = render.window_height;
    s_settings_save.dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    std::cerr << "[settings] display reverted\n";
}

void expire_display_confirmation() {
    if (settings_display_confirming() && galaxy::steady_ms() >= s_display_confirm_deadline_ms) {
        revert_display_settings();
    }
}

// Applies the CONTROLS draft.
void apply_controls_draft() {
    const galaxy::input::ControlsConfig live = galaxy::input::get_controls_config();
    if (!(s_controls_draft == live)) {
        galaxy::input::set_controls_config(s_controls_draft);
        s_controls_draft = galaxy::input::get_controls_config();
    }
    // The mods' actions: kept in the settings file's modbind. lines with the rest (written by this apply's save).
    for (std::size_t i = 0; i < galaxy::input::kControlSchemeCount; ++i) {
        if (s_mod_binds_draft[i].empty()) continue;
        const auto scheme = static_cast<galaxy::input::ControlScheme>(i);
        for (const auto& action : galaxy::input::mod_actions::list(scheme)) {
            const auto it = s_mod_binds_draft[i].find(mod_action_key(action));
            if (it == s_mod_binds_draft[i].end() || it->second == action.bindings) continue;
            if (!galaxy::input::mod_actions::bind(scheme, action.mod_id, action.id, it->second)) {
                std::cerr << "[settings] mod action " << mod_action_key(action) << ": binding refused\n";
            }
        }
        s_mod_binds_draft[i].clear();
    }
    RuntimeSettings runtime = get_runtime_settings();
    if (s_input_mode_draft != runtime.input_mode) {
        runtime.input_mode = s_input_mode_draft;
        set_runtime_settings(runtime);
        galaxy::host::input_mode_changed();
    }
    apply_touch_draft();
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
    applied.window_width = next.window_width;
    applied.window_height = next.window_height;
    applied.aspect_mode = next.aspect_mode;
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
    applied.internal_height = next.internal_height;
    applied.sharpness = next.sharpness;
    applied.hdr = next.hdr;
    applied.paper_white = next.paper_white;
    applied.anisotropic_filtering = next.anisotropic_filtering;
    applied.enhanced_mipmaps = next.enhanced_mipmaps;
    applied.curved_surfaces = next.curved_surfaces;
    applied.per_pixel_lighting = next.per_pixel_lighting;
    applied.enhanced_lighting = next.enhanced_lighting;
    applied.ambient_occlusion = next.ambient_occlusion;
    applied.ambient_occlusion_ssao = next.ambient_occlusion_ssao;
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
    set_render_config(applied);
    s_settings_draft = get_render_config();
    reset_runtime_draft();
    if (dlss_asked) {
        s_settings_draft.upscaler = vendor_upscaler();  // the row shows what the question is about
    }
    if (display_changed) {
        if (!settings_display_confirming()) {
            s_display_confirm_previous = live;
        }
        s_display_confirm_deadline_ms = galaxy::steady_ms() + kDisplayConfirmMs;
    }
    s_settings_applied_ms = galaxy::steady_ms();
    s_settings_save.dirty.store(true, std::memory_order_release);
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
        (controls_row(row).kind == ControlsRow::Binding || controls_row(row).kind == ControlsRow::ModAction ||
         controls_row(row).kind == ControlsRow::Reset ||
         controls_row(row).kind == ControlsRow::PairJoyCon || controls_row(row).kind == ControlsRow::ArrangeTouch ||
         controls_row(row).kind == ControlsRow::TouchPlaces)) {
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
        case ControlsRow::InputMode: {
#if defined(__ANDROID__)
            static constexpr RuntimeInputMode kModes[] = {RuntimeInputMode::Auto, RuntimeInputMode::Phone,
                                                          RuntimeInputMode::Controller, RuntimeInputMode::KeyboardMouse,
                                                          RuntimeInputMode::RealWiimote, RuntimeInputMode::WiimoteMotion};
#else
            static constexpr RuntimeInputMode kModes[] = {RuntimeInputMode::Auto, RuntimeInputMode::KeyboardMouse,
                                                          RuntimeInputMode::Controller, RuntimeInputMode::RealWiimote,
                                                          RuntimeInputMode::WiimoteMotion};
#endif
            unsigned index = 0u;
            for (unsigned i = 0; i < std::size(kModes); ++i) {
                if (kModes[i] == s_input_mode_draft) index = i;
            }
            s_input_mode_draft = kModes[step(index, static_cast<unsigned>(std::size(kModes)))];
            refresh_controls_devices(true);
            return;
        }
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
        case ControlsRow::ModAction:
            if (delta > 0) start_controls_capture_at(kModCaptureBase + info.action);
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
        case ControlsRow::Pointer: {
            // Its sources by the controller's sensors: bit 0 motion, bit 1 the touchpad.
            const ControlsSensors have = controls_sensors();
            std::vector<unsigned> choices{0u};
            if (have.gyro) choices.push_back(1u);
            if (have.touch) choices.push_back(2u);
            if (choices.size() == 3u) choices.push_back(3u);
            const unsigned current = (tuning.gyro_pointer && have.gyro ? 1u : 0u) | (tuning.touch_pointer && have.touch ? 2u : 0u);
            unsigned index = 0u;
            for (unsigned i = 0; i < choices.size(); ++i) {
                if (choices[i] == current) index = i;
            }
            const unsigned next = choices[step(index, static_cast<unsigned>(choices.size()))];
            tuning.gyro_pointer = (next & 1u) != 0u;
            tuning.touch_pointer = (next & 2u) != 0u;
            return;
        }
        case ControlsRow::GyroSensitivity:
            tuning.gyro_sensitivity = step_float(tuning.gyro_sensitivity, 0.1f, 0.2f, 5.0f);
            return;
        case ControlsRow::TouchSensitivity:
            tuning.touch_sensitivity = step_float(tuning.touch_sensitivity, 0.1f, 0.2f, 5.0f);
            return;
        case ControlsRow::PairJoyCon:
            if (delta > 0) open_joycon_pairing();
            return;
        case ControlsRow::ArrangeTouch:
            if (delta > 0) galaxy::host::arrange_touch_controls();
            return;
        case ControlsRow::TouchSize: {
            galaxy::host::TouchControlsSettings touch = touch_draft_settings();
            unsigned index = 0u;
            for (unsigned i = 0u; i < std::size(kTouchSizes); ++i) {
                if (kTouchSizes[i] == touch.size) index = i;
            }
            touch.size = kTouchSizes[step(index, static_cast<unsigned>(std::size(kTouchSizes)))];
            s_touch_draft.edited = true;
            s_touch_draft.settings = touch;
            return;
        }
        case ControlsRow::TouchOpacity: {
            // 20..100 percent in steps of 10.
            galaxy::host::TouchControlsSettings touch = touch_draft_settings();
            touch.opacity = 20u + step((std::clamp(touch.opacity, 20u, 100u) - 20u) / 10u, 9u) * 10u;
            s_touch_draft.edited = true;
            s_touch_draft.settings = touch;
            return;
        }
        case ControlsRow::TouchVibration: {
            galaxy::host::TouchControlsSettings touch = touch_draft_settings();
            touch.vibration = !touch.vibration;
            s_touch_draft.edited = true;
            s_touch_draft.settings = touch;
            return;
        }
        case ControlsRow::TouchPlaces:
            // Pressed again: kept as they are.
            if (delta > 0) s_touch_draft.reset_places = !s_touch_draft.reset_places;
            return;
        case ControlsRow::WiimoteStatus:
            return;
        case ControlsRow::Reset:
            if (delta > 0) {
                s_controls_draft.table(s_controls_scheme) =
                    galaxy::input::default_binding_table(s_controls_scheme);
                s_controls_draft.tune(s_controls_scheme) =
                    galaxy::input::default_scheme_tuning(s_controls_scheme);
                // The mods' actions back to following their game controls.
                for (const auto& action : mod_action_list()) {
                    mod_binds_draft(s_controls_scheme)[mod_action_key(action)] = {};
                }
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
        if (row == std::size(gains)) {
            const int next = (static_cast<int>(settings.audio.sound_output) + (delta < 0 ? 2 : 1)) % 3;
            settings.audio.sound_output = static_cast<SoundOutput>(next);
            return;
        }
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
    int place = static_cast<int>(shown_position(s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount));
    place = (place + delta) % static_cast<int>(kShownTabCount);
    if (place < 0) {
        place += static_cast<int>(kShownTabCount);
    }
    const int tab = static_cast<int>(kShownTabs[place]);
    s_native_settings_tab.store(
        static_cast<unsigned>(tab), std::memory_order_relaxed);
    unsigned first = 0u;
    const unsigned count = native_settings_row_count(static_cast<unsigned>(tab));
    while (first + 1u < count && native_settings_heading(static_cast<unsigned>(tab), first)) {
        ++first;
    }
    s_native_settings_row.store(first, std::memory_order_relaxed);
}

void deny_asks();
void end_text_edit(bool keep);
bool text_edit_key(std::uintptr_t key, bool was_down, bool accept, bool from_pad);
extern std::atomic<bool> s_text_editing;
std::atomic<bool> s_last_key_from_pad{false};  // the menu's last key came from a pad

void set_native_settings_visible(bool visible) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_settings_leave_asking = false;
    if (visible && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        // A fresh draft, monitor list and controller list each time the menu opens.
        s_settings_monitors = host_window::settings_monitors();
        s_settings_draft = get_render_config();
        reset_runtime_draft();
        reset_controls_draft();
        refresh_controls_devices(true);
        settle_native_settings_row();
    }
    if (!visible) {
        // A mod's request screen left unanswered is the player's no (services-mods 9, services-save 8).
        deny_asks();
        end_text_edit(false);
        // Closing discards an unapplied draft, an unconfirmed display, an
        // unanswered DLSS question and controls being arranged.
        close_joycon_pairing();
        if (galaxy::host::touch_arranging()) {
            galaxy::host::finish_arranging_touch_controls(false);
        }
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
bool handle_controls_key(std::uintptr_t key, bool was_down, bool from_pad) {
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
        if (info.kind == ControlsRow::ModAction) {
            start_controls_capture_at(kModCaptureBase + info.action);
            return true;
        }
        if (info.kind == ControlsRow::Reset || info.kind == ControlsRow::PairJoyCon || info.kind == ControlsRow::ArrangeTouch ||
            info.kind == ControlsRow::TouchPlaces) {
            adjust_native_setting(1);
            return true;
        }
        return false;
    case VK_BACK:
        if (info.kind == ControlsRow::Binding) {
            if (!was_down) {
                s_controls_draft.table(s_controls_scheme)[info.target] = {};
                std::cerr << "[settings] clear " << galaxy::input::control_scheme_id(s_controls_scheme) << '.'
                          << galaxy::input::game_input_id(info.target) << '\n';
            }
            return true;
        }
        if (info.kind == ControlsRow::ModAction) {
            // Back to following its game control.
            const std::vector<galaxy::input::mod_actions::Listed> actions = mod_action_list();
            if (!was_down && info.action < actions.size()) {
                mod_binds_draft(s_controls_scheme)[mod_action_key(actions[info.action])] = {};
                std::cerr << "[settings] clear " << galaxy::input::control_scheme_id(s_controls_scheme) << ".mod "
                          << mod_action_key(actions[info.action]) << '\n';
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
        return kind == ControlsRow::Reset || kind == ControlsRow::PairJoyCon || kind == ControlsRow::ArrangeTouch ||
                       kind == ControlsRow::TouchPlaces
                   ? 1u
                   : 0u;
    }
    if (tab == kAudioTab) {
        return row == kAudioResetRow ? 1u : 0u;
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
    if (tab == kAudioTab) {
        if (row == kAudioResetRow) {
            // Every volume and the output as a new install has them; saved with Apply.
            s_runtime_draft.audio = RuntimeAudioSettings{};
            std::cerr << "[settings] AUDIO: RESET\n";
        }
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
    case ControlsRow::TouchSensitivity: along(tuning.touch_sensitivity, 0.2f, 0.1f, 5.0f); break;
    case ControlsRow::TouchOpacity:
        along(static_cast<float>(std::clamp(touch_draft_settings().opacity, 20u, 100u)), 20.0f, 10.0f, 100.0f);
        break;
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

bool handle_native_settings_key(std::uintptr_t key, bool was_down, bool from_pad = false) {
    if (s_home_menu.visible.load(std::memory_order_acquire)) {
        return handle_home_menu_key(key, was_down);
    }
    // Opened before the game's menu is built (the boot's first seconds): held until the game first asks for its view.
    // F1 and Escape close it; every other key waits.
    if (s_native_settings_visible.load(std::memory_order_relaxed) &&
        !s_settings_drawn_by_game.load(std::memory_order_acquire)) {
        if (!was_down && (key == VK_F1 || key == VK_ESCAPE)) {
            set_native_settings_visible(false);
        }
        return true;
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
    s_last_key_from_pad.store(from_pad, std::memory_order_relaxed);
    if (s_text_editing.load(std::memory_order_acquire) && s_native_settings_visible.load(std::memory_order_relaxed)) {
        return text_edit_key(key, was_down, accept, from_pad);
    }
    if (key == galaxy::host::kBindingCaptureKey && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        (void)galaxy::host::take_controller_binding_capture();
        return true;
    }
    if (galaxy::host::touch_arranging() && s_native_settings_visible.load(std::memory_order_relaxed)) {
        // The on-screen controls' editor: Enter, Space or Y saves the places, Backspace or X puts every control where
        // the game places it (kept with Save), Escape, B or F1 cancels. The rest waits; the controls are dragged on the
        // screen itself.
        if (!was_down) {
            if (key == VK_RETURN || key == VK_SPACE) {
                galaxy::host::finish_arranging_touch_controls(true);
                std::cerr << "[settings] ON-SCREEN CONTROLS: SAVE\n";
            } else if (key == VK_BACK) {
                galaxy::host::reset_arranged_touch_controls();
                std::cerr << "[settings] ON-SCREEN CONTROLS: RESET\n";
            } else if (key == VK_ESCAPE || key == VK_F1) {
                galaxy::host::finish_arranging_touch_controls(false);
                std::cerr << "[settings] ON-SCREEN CONTROLS: CANCEL\n";
            }
        }
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
            const NativeSettingsStatus dlss = dlss_status(galaxy::steady_ms());
            if (!settings_display_confirming() && dlss != NativeSettingsStatus::DlssAsk && dlss != NativeSettingsStatus::XessAsk &&
                dlss != NativeSettingsStatus::DlssRestart && dlss != NativeSettingsStatus::XessRestart && !restart_asking()) {
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
        (mod_question_key(key, was_down) || dlss_prompt_key(key, was_down) || restart_ask_key(key, was_down))) {
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        handle_controls_key(key, was_down, from_pad)) {
        return true;
    }
    if (key == VK_RETURN && !was_down && !s_native_settings_visible.load(std::memory_order_relaxed) &&
        galaxy::host::input_missing_shown()) {
        set_native_settings_visible(true);
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

using ui::percent_text;
using ui::mods_ui_text;
using ui::upper_ascii;
using ui::settings_step;

class WindowDisplayPageHost final : public ui::DisplayPageHost {
public:
    ui::DisplayPageState query(const RenderConfig& render) const override {
        ui::DisplayPageState state;
        const host_window::SettingsMonitor* current = settings_monitor(render);
        for (std::size_t i = 0; i < s_settings_monitors.size(); ++i) {
            const auto& monitor = s_settings_monitors[i];
            state.monitors.push_back({monitor.id, monitor.name});
            if (&monitor == current) state.current_monitor = i;
        }
#if defined(__ANDROID__)
        state.fullscreen_only = true;
#endif
        state.temporal_upscaling = temporal_upscaler(render) != 0u;
        {
            const DisplayHdr hdr = get_display_hdr();
            state.hdr_offered = hdr.available;
            state.hdr_active = hdr.active;
        }
        state.output_size = settings_output_size(render);
        state.window_sizes = settings_window_sizes(current, render.aspect_mode);
        state.frame_rates = settings_frame_rate_caps(render);
        return state;
    }
    void changed(ui::DisplayChange change, RenderConfig& draft) const override {
        if ((change == ui::DisplayChange::Mode || change == ui::DisplayChange::Aspect) &&
            draft.display_mode == kDisplayWindowed && draft.aspect_mode != 0u) snap_window_size(draft);
        if (change != ui::DisplayChange::Aspect) sanitize_settings_draft(draft);
        if (change == ui::DisplayChange::Monitor) normalize_interpolation(draft);
    }
};

std::vector<SettingsRow> display_page(const RenderConfig& render) {
    static const WindowDisplayPageHost host;
    return ui::display_page(render, host);
}

void open_folder_in_explorer(const std::filesystem::path& folder);

class WindowGraphicsPageHost final : public ui::GraphicsPageHost {
public:
    ui::GraphicsPageState query(const RenderConfig& render) const override {
        ui::GraphicsPageState state;
        state.upscalers = available_upscalers();
        state.render_percent = static_cast<unsigned>(std::lround(settings_render_fraction(render) * 100.0f));
        state.dlss_supported = host_window::upscaler_caps().dlss;
        state.dlss_installed = dlss_installed_now();
        state.xess_supported = host_window::upscaler_caps().xess;
        state.xess_installed = xess_installed_now();
        state.ray_tracing_offered = host_window::ray_tracing_offered.load(std::memory_order_relaxed);
        state.temporal_upscaling = temporal_upscaler(render) != 0u;
        state.spatial_upscaling = spatial_quality_mode(render);
        state.curved_surfaces_offered = curved_surfaces_offered();
        state.folders_open = host_window::folders_open();
        state.measured_fps = host_window::measured_present_hz.load(std::memory_order_relaxed) *
                             (frame_generation_running() ? static_cast<double>(host_window::upscaler_caps().frames_presented) : 1.0);
        state.frame_rates = settings_frame_rate_caps(render, true);
        state.anti_aliasing_text = anti_aliasing_row_text(render.aa_mode, render.smaa_quality, get_post_process_status());
        if (render.texture_packs) {
            const auto packs = TexturePacks::summary();
            state.texture_packs = {packs.textures, packs.levels, packs.indexed, packs.dolphin, packs.unknown};
        }
        return state;
    }
    void open_folder(ui::GraphicsFolder folder) const override {
        open_folder_in_explorer(folder == ui::GraphicsFolder::TextureDumps ? texture_dump_directory() : movie_directory());
    }
};

std::vector<SettingsRow> graphics_page(const RenderConfig& render) {
    static const WindowGraphicsPageHost host;
    return ui::graphics_page(render, host);
}

// GAME > GALAXY SELECT: the galaxy and star the TRAVEL row goes to.
ui::AdvancedPageModel s_advanced_page;

// Mii import (GENERAL page): the last import's result and the Mii count (read again after an import).
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

// The file picker (the game keeps running): the files chosen are imported into the Mii database and the running game's.
void start_mii_import_dialog() {
    if (!opens_on_desktop("the Mii picker")) {
        return;
    }
    if (s_mii_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    host_window::pick_files(host_window::FilePick::Miis, [](std::vector<std::filesystem::path> files) {
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
        s_mii_dialog_open.store(false, std::memory_order_release);
    });
}

// A folder shown on the desktop (made if missing).
void open_folder_in_explorer(const std::filesystem::path& folder) {
    if (folder.empty()) {
        return;
    }
    if (!opens_on_desktop(galaxy::path_text(folder).c_str())) {
        return;
    }
    host_window::open_folder(folder);
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
std::string s_mod_options;  // the mod whose page shows instead of the lists (empty: none)
bool s_mod_log_open = false;  // the log page shows instead of the lists (mod-log 4.2)
bool s_mod_log_facts = false; // its rows were written as harness facts since it opened
// The consent question (contract consent 2): a page of rows shown instead of the lists while a mod on waits for an
// answer (first_code_to_allow_locked), so the menu's own rows, scrolling and input drive it. `choosing`: permission by
// permission (Choose), `chosen` the answers so far by name; `focus`: put the focus on Not now when it next builds
// (2.2 item 5); `facts_for`: the mod whose question was written as a harness fact. Under s_mods_mutex.
bool s_consent_open = false;
bool s_consent_choosing = false;
bool s_consent_focus = false;
std::map<std::string, bool> s_consent_chosen;
std::string s_consent_facts_for;

// The Mods screen's lines about each mod as the harness facts `modscreen.<id>` (harness 4): its line in its list, and
// each row of its page while that shows, written when what they say changes (a page each time it opens). `wanted`: a
// harness run with the menu on this tab (read once per build of the page); `shown`: per place and mod ("list:<id>",
// "page:<id>") a hash of the lines last written; `pending`: the facts to write once s_mods_mutex is let go. Under
// s_mods_mutex.
std::atomic<bool> s_modscreen_wanted{false};
std::map<std::string, std::size_t> s_modscreen_shown;
std::vector<std::pair<std::string, std::string>> s_modscreen_pending;

// Rows shown about mod `id` at `page` ("list" or "page"), queued as facts `modscreen.<id>` {"line", "page", "label",
// "value", "help"} (with "buttons" for a row of buttons, "list" "on" or "off" for a mod's line in a list) when they
// differ from the lines last written there. With s_mods_mutex held.
void modscreen_facts_locked(const std::string& id, const char* page, const SettingsRow* rows, std::size_t count) {
    if (!s_modscreen_wanted.load(std::memory_order_relaxed)) return;
    std::vector<std::string> lines;
    lines.reserve(count);
    std::string all;
    for (std::size_t r = 0; r < count; ++r) {
        const SettingsRow& row = rows[r];
        std::string json = "{\"line\":" + std::to_string(r) + ",\"page\":\"" + page + "\",\"label\":";
        galaxy::modlog::append_json_text(json, row.label);
        json += ",\"value\":";
        galaxy::modlog::append_json_text(json, row.value);
        json += ",\"help\":";
        galaxy::modlog::append_json_text(json, row.help);
        if (!row.buttons.empty()) {
            json += ",\"buttons\":[";
            for (std::size_t b = 0; b < row.buttons.size(); ++b) {
                if (b != 0u) json += ',';
                galaxy::modlog::append_json_text(json, row.buttons[b]);
            }
            json += ']';
        }
        if (row.list != 0u) json += row.list == 2u ? ",\"list\":\"on\"" : ",\"list\":\"off\"";
        json += '}';
        all += json;
        all += '\n';
        lines.push_back(std::move(json));
    }
    const std::size_t hash = std::hash<std::string>{}(all);
    const auto [it, added] = s_modscreen_shown.try_emplace(std::string(page) + ":" + id, hash);
    if (!added && it->second == hash) return;
    it->second = hash;
    for (std::string& line : lines) s_modscreen_pending.emplace_back("modscreen." + id, std::move(line));
}

// The mod page's ABOUT window: the mod's name and description in pages, then its pictures (its icon first), a page at a
// time. A picture is read on its own thread when its page first shows (a large one takes a moment). Under s_mods_mutex.
struct ModAbout {
    std::string id;
    std::vector<std::string> pages;
    std::vector<std::filesystem::path> pictures;
    unsigned page = 0u;
    unsigned picture_page = ~0u;  // the page `picture` (or `reading`) is for
    std::shared_ptr<const NativeSettingsPicture> picture;
    std::future<std::shared_ptr<const NativeSettingsPicture>> reading;
    bool unreadable = false;  // the page's picture can't be read
    bool missing = false;     // ... because its file is not there
};
std::optional<ModAbout> s_mod_about;
// The mod whose REMOVE the menu asks about (empty: none). Under s_mods_mutex.
std::string s_mod_remove_ask;
// The removed mod whose kept data the menu asks to forget (empty: none). Under s_mods_mutex.
std::string s_mod_forget_ask;
// The removed mods' kept data for the REMOVED MODS line (services-mods 8): read when the page first lists the mods and
// again after a rescan or a FORGET, never each frame. Under s_mods_mutex.
std::vector<galaxy::luma_mods::KeptMod> s_mods_kept;
bool s_mods_kept_read = false;

// A mod as the MODS page names it: its own name (a .luma's, a resource pack's), else its folder's.
std::string mod_title(const galaxy::mods::ModInfo& mod) {
    return mods_ui_text(mod.pack_title.empty() ? mod.id : mod.pack_title, 40u);
}

// The pictures a mod's page shows: at most kMenuPictureSide texels a side, a larger one shown by the first of its
// halved levels that fits. Files over 64 MB, and pictures over 8192 a side, are not read at all.
constexpr unsigned kMenuPictureSide = 1024u;

// A PNG in a mod as the menu shows it; null when it can't be read.
std::shared_ptr<const NativeSettingsPicture> read_menu_picture(const std::filesystem::path& file) {
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(file, error);
    if (error || bytes < 24u || bytes > 64u * 1024u * 1024u) return nullptr;
    {
        // The PNG signature and its header's size, before anything is decoded.
        std::ifstream in(file, std::ios::binary);
        unsigned char head[24]{};
        if (!in.read(reinterpret_cast<char*>(head), sizeof(head))) return nullptr;
        static constexpr unsigned char kSignature[8] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};
        if (std::memcmp(head, kSignature, sizeof(kSignature)) != 0) return nullptr;
        const auto big_endian = [&head](unsigned at) {
            return std::uint32_t{head[at]} << 24u | std::uint32_t{head[at + 1u]} << 16u | std::uint32_t{head[at + 2u]} << 8u |
                   std::uint32_t{head[at + 3u]};
        };
        const std::uint32_t width = big_endian(16u);
        const std::uint32_t height = big_endian(20u);
        if (width == 0u || height == 0u || width > 8192u || height > 8192u) return nullptr;
    }
    TexturePackPixels pixels;
    if (!load_texture_pack_image(file, false, pixels, nullptr) || pixels.levels.empty()) return nullptr;
    std::size_t level = 0u;
    unsigned width = pixels.width;
    unsigned height = pixels.height;
    while ((width > kMenuPictureSide || height > kMenuPictureSide) && level + 1u < pixels.levels.size()) {
        ++level;
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
    }
    if (width > kMenuPictureSide || height > kMenuPictureSide || pixels.levels[level].size() != std::size_t{width} * height * 4u) {
        return nullptr;
    }
    auto picture = std::make_shared<NativeSettingsPicture>();
    picture->width = width;
    picture->height = height;
    picture->rgba = std::move(pixels.levels[level]);
    return picture;
}

// With s_mods_mutex held.
// The packages this start's check refused (modlog::check_packages, validator 2) say so on their rows, as the start did:
// the list's own pass leaves the validator's findings to it. The check ran before the game started; read only after.
void mark_refused(std::vector<galaxy::mods::ModInfo>& mods) {
    for (galaxy::mods::ModInfo& mod : mods) {
        if (mod.package && mod.problem.empty() && galaxy::modlog::package_refused(mod.id)) {
            mod.problem = galaxy::modlog::refused_sentence();
        }
    }
}

std::vector<galaxy::mods::ModInfo>& mods_list() {
    if (!s_mods_scanned) {
        s_mods = galaxy::mods::scan_mods();
        mark_refused(s_mods);
        s_mods_saved = s_mods;
        s_mods_scanned = true;
    }
    return s_mods;
}

// With s_mods_mutex held: the texture packs and movies of the mods mods.ini has on to the renderer now (disc changes
// wait for the next start).
void apply_live_mods() {
    set_texture_pack_mod_folders(galaxy::mods::texture_folders(s_mods_saved),
                                 galaxy::mods::texture_folder_categories_off(s_mods_saved));
    set_movie_mod_folders(galaxy::mods::movie_folders(s_mods_saved));
}

// With s_mods_mutex held: the list's order, then mods.ini. A mod whose options changed is told (services-mods 8.2:
// options_applied takes the new values and posts its OPTION events for the next frame's start).
void save_mods() {
    for (std::size_t i = 0; i < s_mods.size(); ++i) {
        s_mods[i].order = static_cast<unsigned>(i + 1u);
    }
    if (!galaxy::mods::save_mod_settings(s_mods)) {
        std::cerr << "[settings] mods.ini could not be written\n";
    }
    std::vector<const galaxy::mods::ModInfo*> changed;
    for (const galaxy::mods::ModInfo& mod : s_mods) {
        const auto was = std::find_if(s_mods_saved.begin(), s_mods_saved.end(),
                                      [&](const galaxy::mods::ModInfo& m) { return m.id == mod.id; });
        if (was == s_mods_saved.end() || was->options.size() != mod.options.size()) continue;
        for (std::size_t o = 0; o < mod.options.size(); ++o) {
            if (mod.options[o].selected != was->options[o].selected || mod.options[o].text != was->options[o].text) {
                changed.push_back(&mod);
                break;
            }
        }
    }
    for (const galaxy::mods::ModInfo* mod : changed) galaxy::luma_mods::options_applied(*mod);
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
    mark_refused(fresh);
    std::vector<galaxy::mods::ModInfo> merged;
    for (const galaxy::mods::ModInfo& drafted : s_mods) {
        const auto it = std::find_if(fresh.begin(), fresh.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == drafted.id; });
        if (it == fresh.end()) continue;
        galaxy::mods::ModInfo mod = *it;
        mod.enabled = drafted.enabled;
        mod.own_text = drafted.own_text;
        for (std::size_t o = 0; o < mod.options.size() && mod.options.size() == drafted.options.size(); ++o) {
            mod.options[o].selected = drafted.options[o].selected;
            if (mod.options[o].key == drafted.options[o].key) mod.options[o].text = drafted.options[o].text;
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
    s_mods_kept_read = false;  // a folder that came back, or went
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
                    mod.options.size() == saved->options.size() && mod.code_allowed == saved->code_allowed &&
                    mod.code_declined == saved->code_declined &&
                    mod.permission_answers.size() == saved->permission_answers.size();
        for (auto answer = mod.permission_answers.begin(); same && answer != mod.permission_answers.end(); ++answer) {
            const auto was = saved->permission_answers.find(answer->first);
            same = was != saved->permission_answers.end() && was->second.granted == answer->second.granted &&
                   was->second.digest == answer->second.digest;
        }
        for (std::size_t o = 0; same && o < mod.options.size(); ++o) {
            same = mod.options[o].selected == saved->options[o].selected && mod.options[o].text == saved->options[o].text;
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

// With s_mods_mutex held: the first mod on that waits for the player's answer about its code or a permission
// (consent_needed: never a mod without code, never code the player said Not now to).
galaxy::mods::ModInfo* first_code_to_allow_locked() {
    for (galaxy::mods::ModInfo& mod : s_mods) {
        if (mod.enabled && mod.problem.empty() && galaxy::mods::consent_needed(mod)) return &mod;
    }
    return nullptr;
}

// With s_mods_mutex held: the question shows (or stays), its focus on Not now.
void open_consent_locked() {
    if (!s_consent_open) {
        s_consent_choosing = false;
        s_consent_chosen.clear();
        s_consent_facts_for.clear();
        s_consent_focus = true;
    }
    s_consent_open = true;
    s_mod_options.clear();
    s_mod_log_open = false;
}

enum class ConsentAnswer { Allow, Chosen, NotNow };

// The player's answer for the mod the question shows (consent 2.3): Allow grants its code and every permission and
// keeps it on; Chosen grants its code, the required permissions and the ones chosen; Not now records the code's no for
// this token, leaves the permissions not asked and turns it off. Then the next mod's question, or the restart.
void answer_consent(ConsentAnswer answer) {
    std::string fact_key;
    std::string fact_json;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        if (galaxy::mods::ModInfo* mod = first_code_to_allow_locked()) {
            const galaxy::mods::ModConsent facts = galaxy::mods::consent_facts(*mod);
            const bool yes = answer != ConsentAnswer::NotNow;
            if (facts.code) galaxy::mods::record_code_answer(*mod, yes);
            fact_key = "consent.answer." + mod->id;
            fact_json = std::string("{\"code\":\"") + (yes ? "yes" : "no") + "\",\"permissions\":{";
            bool first = true;
            for (const galaxy::mods::PermissionFacts& permission : facts.permissions) {
                std::string said = "not asked";
                if (yes) {
                    const auto chosen = s_consent_chosen.find(permission.name);
                    const bool granted = answer == ConsentAnswer::Allow || permission.required ||
                                         chosen == s_consent_chosen.end() || chosen->second;
                    (void)galaxy::mods::record_answer(*mod, permission.name, granted);
                    said = granted ? "yes" : "no";
                }
                if (!first) fact_json += ',';
                first = false;
                galaxy::modlog::append_json_text(fact_json, permission.name);
                fact_json += ':';
                galaxy::modlog::append_json_text(fact_json, said);
            }
            fact_json += "}}";
            mod->enabled = yes;
            save_mods();
        }
        s_consent_choosing = false;
        s_consent_chosen.clear();
        s_consent_facts_for.clear();
        if (first_code_to_allow_locked() != nullptr) {
            s_consent_focus = true;
        } else {
            s_consent_open = false;
            if (galaxy::mods::mods_differ_from_session(s_mods)) {
                s_mods_restart_ask.store(true, std::memory_order_release);
            }
            s_mods_scanned = false;
        }
    }
    if (!fact_key.empty()) galaxy::modlog::fact(fact_key, fact_json);
}

// With s_mods_mutex held: the draft to mods.ini; `ask`: the menu asks to restart when this session started otherwise.
// The overlaps follow the saved order at the next read.
void commit_mods_locked(bool ask) {
    if (mods_draft_changes_locked() == 0u) return;
    save_mods();
    if (ask && first_code_to_allow_locked() != nullptr) {
        open_consent_locked();
        s_mods_scanned = false;
        return;
    }
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

// With s_mods_mutex held: the draft's list settled again (the families of saves, overlaps and who wins them) by the
// draft's order and on/off state; another copy, needs and conflicts are the start's check's (mark_refused).
void refresh_overlaps_locked() {
    galaxy::mods::settle_mods(s_mods);
    mark_refused(s_mods);
}

// With s_mods_mutex held: mod `id` into the list of mods on at `index` (0 the top), or off (`on` false: among the
// others by name). The draft only.
// Why the mod's code did not start this run, as its rows say it: a required permission the player declined is their
// choice, Consent.OffDeclined as the mod log says it (consent 2.3), not the loader's L506.
galaxy::mods::NotStartedText mod_not_started_text(const std::string& id) {
    const std::string declined = galaxy::modlog::declined_at_start(id);
    if (declined.empty()) return galaxy::mods::plugin_not_started_text(id);
    return {galaxy::modlog::row_words("Consent.OffDeclined", id, {{"permission", declined}}), true};
}

void place_mod_locked(const std::string& id, bool on, std::size_t index) {
    if (on && galaxy::mods::mod_blocked(id)) return;  // a blocked mod stays off until its page unblocks it (6.6)
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

// The last batch's outcomes for the result page (task 6.1): the same page after a drop, the picker, Open with or a
// link. Under s_mod_import_mutex; the page shows from the batch's end until Done.
struct ImportOutcome {
    std::string source;   // the file or folder's name
    std::string outcome;  // IMPORTED, UPDATED, ALREADY IN YOUR MODS, NOT IMPORTED or FAILED
    std::string title;    // the mod's name (else the source's)
    std::string id;       // its folder in the mods folder ("" when nothing was added)
    std::string message;  // the importer's line (its words before ": " translated)
    bool checked = false;
    bool refused = false;
    std::vector<galaxy::mods::ImportResult::Finding> findings;
    // Its takes_over ids with no mod of theirs in the mods folder: what they kept moves to it at its first start
    // (services-save 6), and the page says so under it ("Takes over what {old} kept"; installing is the answer).
    std::vector<std::string> takes_over;
};
std::vector<ImportOutcome> s_import_outcomes;
std::atomic<bool> s_import_page_open{false};

// A mod's request screens (galaxy/mod_screens.h: mods.install, save.game's offer): at most one up, over the game on
// MODS, answered once through post_screen_answer (the game thread runs it at a frame's start). Under s_ask_mutex, which
// is taken after s_mods_mutex when both are (never the other way) and never with s_mod_import_mutex.
std::mutex s_ask_mutex;
struct InstallAsk {
    std::filesystem::path file;
    std::string asker;
    std::function<void(std::int32_t)> done;
    bool importing = false;  // INSTALL pressed: the import runs, its result page shows, and its outcome answers
    galaxy::mods::ImportPeek peek;  // the file's mod and the installed one of its id, read when asked
};
std::optional<InstallAsk> s_install_ask;
struct OfferAsk {
    galaxy::mods::OfferFacts facts;
    std::string asker;
    std::function<void(std::int32_t, std::uint32_t)> done;
    std::uint32_t picked = 0;  // 1-6: the file the confirm asks about; 0: the files to pick from
};
std::optional<OfferAsk> s_offer_ask;

// With s_ask_mutex held: the install request's answer, its screen closed.
void answer_install_locked(std::int32_t status) {
    if (!s_install_ask) return;
    galaxy::mods::post_screen_answer([done = std::move(s_install_ask->done), status] {
        if (done) done(status);
    });
    s_install_ask.reset();
}

// With s_ask_mutex held: the offer's answer, its screen closed.
void answer_offer_locked(std::int32_t status, std::uint32_t file) {
    if (!s_offer_ask) return;
    galaxy::mods::post_screen_answer([done = std::move(s_offer_ask->done), status, file] {
        if (done) done(status, file);
    });
    s_offer_ask.reset();
}

// A request's screen showing (an install whose import runs shows the lists, then the import's result).
bool request_screen_up() {
    const std::lock_guard<std::mutex> lock(s_ask_mutex);
    return s_offer_ask.has_value() || (s_install_ask && !s_install_ask->importing);
}

// Escape, B, NO or the menu closed: an unanswered request is the player's no. An import already running answers by
// its outcome.
void deny_asks() {
    const std::lock_guard<std::mutex> lock(s_ask_mutex);
    if (s_install_ask && !s_install_ask->importing) answer_install_locked(LUMA_E_DENIED);
    answer_offer_locked(LUMA_E_DENIED, 0u);
}

// The import thread's end: an install request's import answers it by its outcome (J-SM2: it starts at the next start).
void finish_install_ask(const std::vector<ImportOutcome>& outcomes) {
    const std::lock_guard<std::mutex> lock(s_ask_mutex);
    if (!s_install_ask || !s_install_ask->importing) return;
    std::int32_t status = LUMA_E_FORMAT;  // refused by the validator, or failed (the file changed or went)
    if (!outcomes.empty()) {
        const std::string& outcome = outcomes.front().outcome;
        if (outcome == "IMPORTED" || outcome == "UPDATED") {
            status = LUMA_OK;
        } else if (outcome == "ALREADY IN YOUR MODS") {
            status = LUMA_E_EXISTS;
        }
    }
    answer_install_locked(status);
}

// ---- A text option typed in (services-mods 8.4) ----
// The row being typed in: keys and typed text go to it until Enter (kept in the draft, applied as any option) or
// Escape (dropped). A pad edits the last character: up and down change it, right adds one, left or X deletes one,
// A or Y keeps, B drops. Under s_text_mutex, taken last: nothing else is locked while it is held.
struct TextEdit {
    std::string mod;
    std::string key;
    std::string text;
    std::uint32_t max_length = 0;
    bool pad = false;  // the last key was a pad's: its help says the pad's keys
};
std::mutex s_text_mutex;
std::optional<TextEdit> s_text_edit;
std::atomic<bool> s_text_editing{false};

// The characters a pad steps through (the overlay's font draws capitals).
constexpr std::string_view kPadCharacters = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-_'";

std::uint32_t code_points(std::string_view text) {
    std::uint32_t n = 0u;
    for (const char c : text) n += (static_cast<unsigned char>(c) & 0xC0u) != 0x80u ? 1u : 0u;
    return n;
}

void drop_last_point(std::string& text) {
    while (!text.empty()) {
        const bool lead = (static_cast<unsigned char>(text.back()) & 0xC0u) != 0x80u;
        text.pop_back();
        if (lead) return;
    }
}

void begin_text_edit(std::string mod, std::string key, std::string text, std::uint32_t max_length, bool pad) {
    const std::lock_guard<std::mutex> lock(s_text_mutex);
    s_text_edit = TextEdit{std::move(mod), std::move(key), std::move(text), max_length, pad};
    s_text_editing.store(true, std::memory_order_release);
}

// Enter keeps the typed text in the draft (an empty one is the mod's default); Escape, closing the menu or the mod's
// page drops it.
void end_text_edit(bool keep) {
    std::optional<TextEdit> edit;
    {
        const std::lock_guard<std::mutex> lock(s_text_mutex);
        edit.swap(s_text_edit);
        s_text_editing.store(false, std::memory_order_release);
    }
    if (!keep || !edit) return;
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    for (galaxy::mods::ModInfo& mod : s_mods) {
        if (mod.id != edit->mod) continue;
        for (galaxy::mods::ModOption& option : mod.options) {
            if (option.key == edit->key && option.kind == "text") option.text = edit->text;
        }
    }
}

// Typed text (WM_CHAR, SDL's text input): each character that fits is added; controls are not text.
bool type_text(std::string_view utf8) {
    const std::lock_guard<std::mutex> lock(s_text_mutex);
    if (!s_text_edit) return false;
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t length = 1u;
        const auto b = static_cast<unsigned char>(utf8[i]);
        if (b >= 0xF0u) length = 4u; else if (b >= 0xE0u) length = 3u; else if (b >= 0xC0u) length = 2u;
        const std::string_view point = utf8.substr(i, length);
        i += length;
        s_text_edit->pad = false;
        std::string next = s_text_edit->text;
        next.append(point);
        if (galaxy::mods::text_option_fits(next, s_text_edit->max_length)) s_text_edit->text = std::move(next);
    }
    return true;
}

// A key while a text is typed in: every key is the editor's (the game and the menu see none). A keyboard's arrows do
// nothing (the text is typed); a pad's edit the last character.
bool text_edit_key(std::uintptr_t key, bool was_down, bool accept, bool from_pad) {
    if (accept || key == VK_RETURN) {
        if (!was_down) end_text_edit(true);
        return true;
    }
    if (key == VK_ESCAPE || key == VK_F1) {
        if (!was_down) end_text_edit(false);
        return true;
    }
    const std::lock_guard<std::mutex> lock(s_text_mutex);
    if (!s_text_edit) return true;
    std::string& text = s_text_edit->text;
    s_text_edit->pad = from_pad;
    if (!from_pad && key != VK_BACK) return true;
    if (key == VK_BACK || key == VK_LEFT) {
        drop_last_point(text);
    } else if (key == VK_RIGHT) {
        std::string next = text + (text.empty() ? 'A' : text.back() & 0x80 ? 'A' : text.back());
        if (galaxy::mods::text_option_fits(next, s_text_edit->max_length)) text = std::move(next);
    } else if (key == VK_UP || key == VK_DOWN) {
        if (text.empty()) {
            if (galaxy::mods::text_option_fits("A", s_text_edit->max_length)) text = "A";
            return true;
        }
        std::size_t at = kPadCharacters.find(text.back());
        if ((text.back() & 0x80) != 0 || at == std::string_view::npos) {
            drop_last_point(text);
            at = 0u;
        } else {
            text.pop_back();
        }
        const std::size_t count = kPadCharacters.size();
        at = key == VK_UP ? (at + 1u) % count : (at + count - 1u) % count;
        text.push_back(kPadCharacters[at]);
    }
    return true;
}

// With s_mods_mutex held: the player's DON'T ASK AGAIN for `id` (never true) or its undoing, kept in mods.ini at once
// as saved; the draft keeps it too, and nothing else of the draft is written.
void set_install_ask_never_locked(const std::string& id, bool never) {
    (void)mods_list();
    for (galaxy::mods::ModInfo& mod : s_mods) {
        if (mod.id == id) mod.install_ask_never = never;
    }
    bool found = false;
    for (galaxy::mods::ModInfo& mod : s_mods_saved) {
        if (mod.id != id) continue;
        mod.install_ask_never = never;
        found = true;
    }
    if (found && !galaxy::mods::save_mod_settings(s_mods_saved)) {
        std::cerr << "[settings] mods.ini could not be written\n";
    }
}

const char* import_outcome_word(const galaxy::mods::ImportResult& result) {
    if (result.refused) return "NOT IMPORTED";
    if (!result.ok) return "FAILED";
    if (result.already) return "ALREADY IN YOUR MODS";
    return result.replaced ? "UPDATED" : "IMPORTED";
}

// In a harness run, each line the result page shows for a file as the fact import.<file name> (harness 4), in the
// page's order: line 1 what happened {"line": 1, "text": the outcome word, "title", "id" ("" when nothing was added),
// "checked", "refused"}; then each finding {"line", "text", "code", "file", "at" ("<line>:<column>", when it names
// one), "error"}.
void write_import_facts() {
    std::vector<ImportOutcome> outcomes;
    {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        outcomes = s_import_outcomes;
    }
    for (const ImportOutcome& o : outcomes) {
        const std::string key = "import." + o.source;
        std::string json = "{\"line\":1,\"text\":";
        galaxy::modlog::append_json_text(json, o.outcome);
        json += ",\"title\":";
        galaxy::modlog::append_json_text(json, o.title);
        json += ",\"id\":";
        galaxy::modlog::append_json_text(json, o.id);
        json += std::string(",\"checked\":") + (o.checked ? "true" : "false") + ",\"refused\":" + (o.refused ? "true" : "false") + "}";
        galaxy::modlog::fact(key, json);
        std::uint32_t line = 1;
        for (const std::string& old : o.takes_over) {
            std::string one = "{\"line\":" + std::to_string(++line) + ",\"text\":";
            galaxy::modlog::append_json_text(one, galaxy::modlog::row_words("Mods.TakesOver", o.id, {{"old", old}}));
            one += ",\"takes_over\":";
            galaxy::modlog::append_json_text(one, old);
            one += '}';
            galaxy::modlog::fact(key, one);
        }
        for (const galaxy::mods::ImportResult::Finding& f : o.findings) {
            std::string one = "{\"line\":" + std::to_string(++line) + ",\"text\":";
            galaxy::modlog::append_json_text(one, f.text);
            one += ",\"code\":" + std::to_string(f.code) + ",\"file\":";
            galaxy::modlog::append_json_text(one, f.file);
            if (f.line != 0u) {
                one += ",\"at\":\"" + std::to_string(f.line) + (f.column != 0u ? ":" + std::to_string(f.column) : std::string()) + "\"";
            }
            one += std::string(",\"error\":") + (f.error ? "true" : "false") + "}";
            galaxy::modlog::fact(key, one);
        }
    }
}

// The validator on a package before it is placed (validator 2 "at import"), against the packages placed now but the
// one it replaces. Off the UI thread: takes s_mods_mutex only to copy the list.
bool check_import_package(const std::filesystem::path& staged, const std::string& id, galaxy::mods::ImportResult& result) {
    std::vector<galaxy::modlog::StartPackage> others;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        for (const galaxy::mods::ModInfo& mod : mods_list()) {
            if (mod.package && mod.id != id) others.push_back({mod.id, mod.path, mod.enabled});
        }
    }
    const galaxy::modlog::ImportCheck found = galaxy::modlog::check_import(staged, others);
    result.checked = found.ran;
    result.refused = found.refused;
    for (const galaxy::modlog::ImportFinding& f : found.findings) {
        result.findings.push_back({f.error, f.code, f.file, f.text, f.line, f.column});
    }
    return !found.refused;
}

// False when it did not begin (nothing to import, or an import running).
bool run_mod_imports(std::vector<std::filesystem::path> sources) {
    if (sources.empty()) return false;
    if (s_mod_import_busy.exchange(true, std::memory_order_acq_rel)) {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        s_mod_import_message = "AN IMPORT IS RUNNING: TRY AGAIN WHEN IT ENDS";
        return false;
    }
    std::thread([sources = std::move(sources)] {
        const std::filesystem::path folder = galaxy::mods::mods_folder();
        std::string message;
        unsigned imported = 0;
        unsigned failed = 0;
        unsigned added = 0;
        std::vector<ImportOutcome> outcomes;
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
            galaxy::mods::ImportResult result;
            if (folder.empty()) {
                result.message = "NO MODS FOLDER";
            } else {
                // An exception out of this thread ends the process: a failed import is a result.
                try {
                    result = galaxy::mods::import_mod(sources[i], folder, progress, nullptr, check_import_package);
                } catch (const std::exception&) {
                    result = galaxy::mods::ImportResult{};
                    result.message = "THE IMPORT FAILED";
                }
            }
            std::cerr << "[mods] import " << galaxy::path_text(sources[i]) << ": " << result.message << " (" << result.files
                      << " files, " << (result.bytes >> 20u) << " MB)\n";
            (result.ok ? imported : failed) += 1u;
            added += result.ok && !result.already ? 1u : 0u;
            message = message.empty() ? result.message : message + "; " + result.message;
            ImportOutcome one;
            one.source = galaxy::path_text(sources[i].filename());
            one.outcome = import_outcome_word(result);
            one.title = result.title.empty() ? one.source : result.title;
            one.id = result.ok ? result.id : std::string();
            one.message = result.message;
            one.checked = result.checked;
            one.refused = result.refused;
            one.findings = std::move(result.findings);
            if (result.ok && !result.already && !result.id.empty()) {
                if (const std::optional<galaxy::luma::Manifest> manifest = galaxy::luma::read_manifest(folder / result.id)) {
                    for (const std::string& old : manifest->takes_over) {
                        std::error_code exists_error;
                        if (!old.empty() && !std::filesystem::exists(folder / old, exists_error)) one.takes_over.push_back(old);
                    }
                }
            }
            outcomes.push_back(std::move(one));
        }
        finish_install_ask(outcomes);
        {
            const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
            s_mod_import_value = failed == 0u ? "DONE" : imported == 0u ? "FAILED" : "SOME FAILED";
            // The restart note only when a mod was added or updated: one already there changes nothing.
            s_mod_import_message = added != 0u ? message + ". MODS APPLY WHEN NEBULA RESTARTS" : message;
            s_import_outcomes = std::move(outcomes);
        }
        // Its facts once the module's words are there (a takes_over line is a row of its table): an import at the
        // start (Open with, --import) runs before the game module's log is found.
        galaxy::modlog::after_begin(write_import_facts);
        s_import_page_open.store(true, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            reread_mods_locked();
        }
        s_mod_import_busy.store(false, std::memory_order_release);
    }).detach();
    return true;
}

// The picker for archives or folders (the game keeps running).
void start_mod_import_dialog(bool folders) {
    if (!opens_on_desktop("the mod picker")) {
        return;
    }
    if (s_mod_dialog_open.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    host_window::pick_files(folders ? host_window::FilePick::ModFolders : host_window::FilePick::ModArchives, [](std::vector<std::filesystem::path> picked) {
        s_mod_dialog_open.store(false, std::memory_order_release);
        run_mod_imports(std::move(picked));
    });
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
    return galaxy::mods::language_folder(
        game_id, static_cast<unsigned>(galaxy::game_language_resolved(get_runtime_settings().language)));
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
    // A .luma's problem is why it is refused (another region, a newer Nebula...): it does nothing, whatever it holds.
    if (!mod.problem.empty() && (mod.package || mod.unreadable || mod.pack_textures != nullptr || mod.code_patches != 0u || (!mod.files && !mod.textures && !mod.movies && !mod.loose))) {
        return mods_ui_text(mod.problem, 120u);
    }
    std::string kinds = galaxy::mods::change_kinds(mod);
    if (!mod.languages.empty()) {
        std::string languages;
        for (const std::string& folder : mod.languages) {
            const std::string name = language_name(folder);
            if (languages.find(name) == std::string::npos) languages += languages.empty() ? name : ", " + name;
        }
        kinds += ". TEXT: " + languages;
    }
    for (const galaxy::mods::ModOverlap& overlap : mod.overlaps) {
        std::string what;
        if (overlap.files != 0u) what = std::to_string(overlap.files) + (overlap.files == 1u ? " FILE" : " FILES");
        if (overlap.textures != 0u) {
            what += (what.empty() ? "" : ", ") + std::to_string(overlap.textures) +
                    (overlap.textures == 1u ? " TEXTURE" : " TEXTURES");
        }
        if (overlap.settings != 0u) {
            what += (what.empty() ? "" : ", ") + std::to_string(overlap.settings) +
                    (overlap.settings == 1u ? " SETTING" : " SETTINGS");
        }
        // The other mod's name apart from the words, so that the menu can name both ("OVERRIDDEN BY: X (2 FILES)").
        kinds += std::string(overlap.other_above ? ". OVERRIDDEN BY: " : ". OVERRIDES: ") + mods_ui_text(overlap.other, 24u) +
                 " (" + what + ")";
    }
    return kinds.empty() ? std::string("NOTHING THIS GAME USES") : kinds;
}

void close_mod_options(const RenderConfig& render);
void close_import_page(const RenderConfig& render);

void open_mod_about();
void ask_mod_remove();

// Whether a mod's ABOUT has anything to show: its description, icon or pictures.
bool mod_has_about(const galaxy::mods::ModInfo& mod) {
    return !mod.pack_description.empty() || (mod.luma != nullptr && (!mod.luma->icon.empty() || !mod.luma->pictures.empty()));
}

// With s_mods_mutex held: the draft's mod in that folder, or null (a rescan or an import that ended took it). A row's
// change finds its mod by this, not by its place: an import ending while the page shows reorders the list.
galaxy::mods::ModInfo* mod_by_id_locked(const std::string& id) {
    const auto it = std::find_if(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
    return it != s_mods.end() ? &*it : nullptr;
}

// The mod log on the MODS page (contract mod-log 4.2): what was said about the mods this session, kept by the game
// module and read through galaxy::modlog. Its words come in the played language already (names 1, 2 and 4).

// A log row's value: where in the mod, and how many times it was said.
std::string log_place(const galaxy::modlog::PageEntry& entry) {
    std::string place = entry.file;
    if (!place.empty() && entry.line != 0u) place += ":" + std::to_string(entry.line);
    if (entry.count > 1u) {
        if (!place.empty()) place += ' ';
        place += "\xC3\x97";  // the multiplication sign
        place += std::to_string(entry.count);
    }
    return place;
}

// Who wrote an entry and its code's name, in the played language: "Loader (Validator.SignatureMismatch)".
std::string log_source(const galaxy::modlog::PageEntry& entry) {
    static constexpr const char* kSourceRows[] = {"ModLog.Source.Validator", "ModLog.Source.Loader", "ModLog.Source.Cook",
                                                  "ModLog.Source.Code",      "ModLog.Source.Content", "ModLog.Source.Guard"};
    std::string source = entry.source < sizeof(kSourceRows) / sizeof(kSourceRows[0])
                             ? galaxy::modlog::words(kSourceRows[entry.source], entry.mod, 0u)
                             : std::string();
    if (!entry.code_name.empty()) source += (source.empty() ? "" : " (") + entry.code_name + (source.empty() ? "" : ")");
    return source;
}

// The rows of one mod's entries, then its ModLog.TooMany line. The line under the page (the source in words, the
// code, the whole text) is made for the row `selected` only: it is the one shown.
void add_log_rows(std::vector<SettingsRow>& rows, const std::vector<galaxy::modlog::PageEntry>& entries,
                  const galaxy::modlog::PageMod& mod, unsigned selected) {
    for (std::size_t e = 0; e < entries.size(); ++e) {
        const galaxy::modlog::PageEntry& entry = entries[e];
        if (entry.mod != mod.id) continue;
        SettingsRow row{entry.text, log_place(entry), nullptr, {}};
        row.names = 1u | 2u;
        row.key = "log:" + std::to_string(e);
        if (rows.size() == selected) {
            // One line under the page, cut where it does not fit; A or Space opens the entry whole (open_log_entry).
            std::string help = log_source(entry);
            if (!help.empty()) help += ": ";
            help += entry.text;
            row.help = std::move(help);
            row.names |= 4u;
        }
        rows.push_back(std::move(row));
    }
    if (mod.past != 0u) {
        SettingsRow more{galaxy::modlog::words("ModLog.TooMany", mod.id, mod.past), {}, nullptr, {}};
        more.names = 1u;
        rows.push_back(std::move(more));
    }
}

// The log page: the entries grouped under each mod, in the MODS lists' order (the mods the lists do not have, a
// .luma that could not be read or Nebula's own lines, after them, in the log's order); Done goes back. In a harness
// run its rows are written as facts once each time it opens (contract 4.3). With s_mods_mutex held.
std::vector<SettingsRow> log_page(const std::vector<galaxy::mods::ModInfo>& mods) {
    std::vector<SettingsRow> rows;
    const std::vector<galaxy::modlog::PageEntry> entries = galaxy::modlog::entries();
    std::vector<galaxy::modlog::PageMod> said = galaxy::modlog::mods();
    const auto place_of = [&mods](const std::string& id) {
        const auto found = std::find_if(mods.begin(), mods.end(), [&id](const galaxy::mods::ModInfo& m) { return m.id == id; });
        return static_cast<std::size_t>(found - mods.begin());
    };
    std::stable_sort(said.begin(), said.end(), [&place_of](const galaxy::modlog::PageMod& a, const galaxy::modlog::PageMod& b) {
        return place_of(a.id) < place_of(b.id);
    });
    const unsigned selected = s_native_settings_row.load(std::memory_order_relaxed);
    for (const galaxy::modlog::PageMod& mod : said) {
        const std::size_t at = place_of(mod.id);
        SettingsRow heading{at < mods.size() ? mod_title(mods[at]) : mods_ui_text(mod.id, 40u), {}, nullptr, {}, true};
        heading.names = 1u;
        rows.push_back(std::move(heading));
        add_log_rows(rows, entries, mod, selected);
    }
    if (entries.empty()) {
        SettingsRow none{galaxy::modlog::words("ModLog.Empty", {}, 0u), {}, nullptr, {}};
        none.names = 1u;
        rows.push_back(std::move(none));
    }
    SettingsRow done = button_row({}, {"DONE"}, [](RenderConfig& draft, unsigned) { close_mod_options(draft); }, {});
    done.key = "log-buttons";
    rows.push_back(std::move(done));
    if (!s_mod_log_facts) {
        s_mod_log_facts = true;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            galaxy::modlog::page_fact(static_cast<std::uint32_t>(r), rows[r].label, rows[r].value);
        }
    }
    return rows;
}

// The imports' result (task 6.1), one page whatever started them: under each source's mod (its name, as is) what
// happened, its file or folder as the value and the importer's line as the help; then each of the validator's
// findings in the played language (as is), the entry inside the mod as the value; Done goes back to the lists.
// With s_mods_mutex held.
std::vector<SettingsRow> import_page() {
    std::vector<ImportOutcome> outcomes;
    {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        outcomes = s_import_outcomes;
    }
    std::vector<SettingsRow> rows;
    for (const ImportOutcome& o : outcomes) {
        SettingsRow heading{mods_ui_text(o.title, 40u), {}, nullptr, {}, true};
        heading.names = 1u;
        rows.push_back(std::move(heading));
        SettingsRow what{o.outcome, mods_ui_text(o.source, 40u), nullptr, mods_ui_text(o.message, 120u)};
        what.names = 2u;
        rows.push_back(std::move(what));
        for (const std::string& old : o.takes_over) {
            SettingsRow takes{galaxy::modlog::row_words("Mods.TakesOver", o.id, {{"old", old}}), {}, nullptr, {}, true};
            takes.names = 1u;
            rows.push_back(std::move(takes));
        }
        for (const galaxy::mods::ImportResult::Finding& f : o.findings) {
            SettingsRow found{f.text, f.file, nullptr, f.text};
            found.names = 1u | 2u | 4u;
            rows.push_back(std::move(found));
        }
    }
    SettingsRow done = button_row({}, {"DONE"}, [](RenderConfig& draft, unsigned) { close_import_page(draft); }, {});
    done.key = "import-buttons";
    rows.push_back(std::move(done));
    return rows;
}

// The MODS lists' LOG row: how many entries the log keeps, and Open. With s_mods_mutex held.
SettingsRow mod_log_row() {
    const std::uint32_t kept = galaxy::modlog::kept();
    SettingsRow row = button_row(kept != 0u ? std::string("LOG") + " (" + std::to_string(kept) + ")" : std::string("LOG"), {"OPEN"},
                                 [](RenderConfig&, unsigned) {
                                     const std::lock_guard<std::mutex> lock(s_mods_mutex);
                                     s_mod_log_open = true;
                                     s_mod_log_facts = false;
                                     s_mod_options.clear();
                                     s_mod_held.clear();
                                     s_native_settings_row.store(1u, std::memory_order_relaxed);
                                     s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
                                 },
                                 kept == 0u ? galaxy::modlog::words("ModLog.Empty", {}, 0u) : std::string());
    row.names = kept == 0u ? 4u : 0u;
    row.key = "modlog";
    return row;
}

// The consent question for `mod` as rows (consent 2.2): its name and version; the code's warning (and that this version
// changes the code); each declared permission in the manifest's order (`native` first): its short name (new when
// asked again), whether the mod needs it to start, and as the help line what it does with the hosts and the mod's own
// words; then Allow, Choose (with an optional permission) and Not now, the focus on Not now. Choosing, each optional
// permission turns between allowed and not allowed, and Done records them. Every word is in the played language
// (row_words), shown as it is. With s_mods_mutex held.
std::vector<SettingsRow> consent_page(const galaxy::mods::ModInfo& mod) {
    std::vector<SettingsRow> rows;
    const galaxy::mods::ModConsent facts = galaxy::mods::consent_facts(mod);
    const std::string id = mod.id;
    const auto words = [&id](const char* row, std::vector<std::pair<std::string, std::string>> values = {}) {
        return galaxy::modlog::row_words(row, id, values);
    };
    const auto line = [&rows](std::string text) {
        SettingsRow row{std::move(text), {}, nullptr, {}, true};
        row.names = 1u;
        rows.push_back(std::move(row));
    };
    std::string title = mod_title(mod);
    if (mod.luma != nullptr && !mod.luma->version.empty()) title += "  " + mods_ui_text(mod.luma->version, 24u);
    line(std::move(title));
    if (facts.code) {
        if (facts.code_changed) line(words("Consent.CodeChanged"));
        line(words("SettingsModsCodeAsk"));
    }
    bool optional = false;
    std::string fact_rows;
    for (const galaxy::mods::PermissionFacts& permission : facts.permissions) {
        optional = optional || !permission.required;
        const bool fresh = permission.changed ||
                           (permission.answer == galaxy::mods::Answer::NotAsked && facts.code_answer == galaxy::mods::Answer::Granted);
        std::string label = words(permission.row.c_str());
        if (label.empty()) label = permission.name;
        // The aside as the language writes one (full-width brackets in Japanese and Chinese; F-C27-3).
        if (fresh) label = words("SettingsJoinAside", {{"0", label}, {"1", words("Consent.New")}});
        const std::string hosts = permission.hosts == "*" ? words("Permission.net.internet.Any") : permission.hosts;
        std::string help = words(permission.text_row.c_str(), {{"hosts", hosts}});
        if (!permission.roots.empty()) help += ' ' + words("Consent.Roots", {{"roots", permission.roots}});
        if (!permission.why.empty()) {
            help += ' ' + words("Consent.Why") + " \u201C" + mods_ui_text(permission.why, 200u) + "\u201D";
        }
        const auto chosen = s_consent_chosen.find(permission.name);
        const bool allowed = chosen == s_consent_chosen.end() || chosen->second;
        std::string value;
        std::function<void(RenderConfig&, int)> change;
        if (permission.required) {
            value = words("Consent.Required");
        } else if (s_consent_choosing) {
            value = words(allowed ? "Consent.Allowed" : "Consent.Denied");
            const std::string name = permission.name;
            change = [name](RenderConfig&, int delta) {
                if (delta == 0) return;
                const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                const auto at = s_consent_chosen.find(name);
                s_consent_chosen[name] = at != s_consent_chosen.end() && !at->second;
            };
        } else {
            value = words("Consent.Optional");
        }
        SettingsRow row{label, value, std::move(change), help};
        row.names = 1u | 2u | 4u;
        rows.push_back(std::move(row));
        // The harness's view of the row (consent 2.6).
        if (!fact_rows.empty()) fact_rows += ',';
        fact_rows += "{\"name\":";
        galaxy::modlog::append_json_text(fact_rows, permission.name);
        fact_rows += ",\"label\":";
        galaxy::modlog::append_json_text(fact_rows, label);
        fact_rows += ",\"text\":";
        galaxy::modlog::append_json_text(fact_rows, words(permission.text_row.c_str(), {{"hosts", hosts}}));
        fact_rows += ",\"why\":";
        galaxy::modlog::append_json_text(fact_rows, permission.why);
        fact_rows += ",\"roots\":";
        galaxy::modlog::append_json_text(fact_rows, permission.roots);
        fact_rows += std::string(",\"required\":") + (permission.required ? "true" : "false") +
                     ",\"new\":" + (fresh ? "true" : "false") + '}';
    }
    std::vector<std::string> buttons;
    std::vector<ConsentAnswer> answers;
    if (s_consent_choosing) {
        buttons = {"DONE", "NOT NOW"};
        answers = {ConsentAnswer::Chosen, ConsentAnswer::NotNow};
    } else if (optional) {
        buttons = {"ALLOW", "CHOOSE", "NOT NOW"};
        answers = {ConsentAnswer::Allow, ConsentAnswer::Chosen, ConsentAnswer::NotNow};
    } else {
        buttons = {"ALLOW", "NOT NOW"};
        answers = {ConsentAnswer::Allow, ConsentAnswer::NotNow};
    }
    const bool choose_button = !s_consent_choosing && optional;
    SettingsRow choice = button_row({}, buttons,
                                    [answers, choose_button](RenderConfig&, unsigned pressed) {
                                        if (pressed >= answers.size()) return;
                                        if (choose_button && pressed == 1u) {
                                            // Choose: the permissions turn one by one; Done records them.
                                            const std::lock_guard<std::mutex> lock(s_mods_mutex);
                                            s_consent_choosing = true;
                                            return;
                                        }
                                        answer_consent(answers[pressed]);
                                    },
                                    {});
    choice.key = "consent-buttons";
    rows.push_back(std::move(choice));
    if (s_consent_focus) {
        s_consent_focus = false;
        s_native_settings_row.store(static_cast<unsigned>(rows.size() - 1u), std::memory_order_relaxed);
        select_settings_button(static_cast<unsigned>(buttons.size() - 1u));
    }
    if (s_consent_facts_for != id) {
        s_consent_facts_for = id;
        std::string json = std::string("{\"code\":") + (facts.code ? "true" : "false") +
                           ",\"code_changed\":" + (facts.code_changed ? "true" : "false") + ",\"rows\":[" + fact_rows +
                           "],\"focus\":";
        galaxy::modlog::append_json_text(json, buttons.back());
        json += '}';
        galaxy::modlog::fact("consent.shown." + id, json);
    }
    return rows;
}

// A world clock's rate as the page writes it, in thousandths: "2X", "0.5X", "1.25X" (S.#X words it in each language).
std::string clock_rate_text(std::uint32_t rate) {
    std::string text = std::to_string(rate / 1000u);
    const std::uint32_t part = rate % 1000u;
    if (part != 0u) {
        std::string digits{static_cast<char>('0' + part / 100u), static_cast<char>('0' + part / 10u % 10u),
                           static_cast<char>('0' + part % 10u)};
        while (digits.back() == '0') digits.pop_back();
        text += '.' + digits;
    }
    return text + "X";
}

// A mod's page instead of the lists: what it is (version, makers, license, website, folder), whether it is on (left and
// right turn it on or off), what it changes, then its options (Riivolution choices, a .luma's options, the text it
// shows, a pack's texture categories), all part of the draft; ABOUT shows its description and pictures, REMOVE removes
// it, its log entries follow, Done, Escape or B goes back to the lists. With s_mods_mutex held.
std::vector<SettingsRow> mod_page(std::size_t i) {
    std::vector<SettingsRow> rows;
    const galaxy::mods::ModInfo& mod = s_mods[i];
    SettingsRow title{mod_title(mod), {}, nullptr, {}, true};
    title.names = 1u;
    rows.push_back(std::move(title));
    // A line of what it is: its value its maker's words, as they are.
    const auto fact = [&rows](const char* label, std::string value, std::string help) {
        SettingsRow row{label, std::move(value), nullptr, std::move(help)};
        row.names = 2u;
        rows.push_back(std::move(row));
    };
    const std::string id = mod.id;
    const std::size_t on = static_cast<std::size_t>(
        std::count_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.enabled; }));
    // Why its code did not start this run (Z-10): a refusal is already in the played language, shown as is. On the list
    // but its code not running says so in the value (Z-13): "On, not running (2 of 2)".
    const galaxy::mods::NotStartedText not_started =
        mod.problem.empty() ? mod_not_started_text(mod.id) : galaxy::mods::NotStartedText{};
    const std::string place = " (" + std::to_string(mod.order) + " OF " + std::to_string(on) + ")";
    SettingsRow status{"STATUS", !mod.enabled ? std::string("OFF") : not_started.text.empty() ? "ON" + place : "ON, NOT RUNNING" + place,
                       [id](RenderConfig&, int delta) {
                           const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                           const galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                           if (delta == 0 || found == nullptr) return;
                           place_mod_locked(id, !found->enabled, 0u);
                       },
                       mod.problem.empty() ? std::string() : mods_ui_text(mod.problem, 120u)};
    if (!not_started.text.empty()) {
        status.help = mods_ui_text(not_started.text, 160u);
        if (not_started.as_is) status.names |= 4u;
    }
    rows.push_back(std::move(status));
    // It takes over a mod that is still installed (services-save 6): nothing moves while that one is there.
    if (mod.luma != nullptr) {
        for (const std::string& old : mod.luma->takes_over) {
            if (mod_by_id_locked(old) == nullptr) continue;
            SettingsRow still{galaxy::modlog::row_words("Mods.TakesOverInstalled", id, {{"old", old}}), {}, nullptr, {}, true};
            still.names = 1u;
            rows.push_back(std::move(still));
        }
    }
    // The mod changes the game's speed or holds it (services-time 10): shown, and the player's last word, LET GO, which
    // clears its rate, hold and owed steps from the next frame (the row goes when the clock is back to normal).
    if (const std::optional<galaxy::modlog::ModClock> clock = galaxy::modlog::mod_clock(id);
        clock.has_value() && (clock->held || clock->rate != 1000u)) {
        rows.push_back(button_row("TIME: " + (clock->held ? std::string("HELD") : clock_rate_text(clock->rate)), {"LET GO"},
                                  [id](RenderConfig&, unsigned) { galaxy::modlog::let_go(id); }, {}));
    }
    // What its code costs (guard 3b, plan 6.7): the time per frame its hooks, events and calls took over the last 2
    // seconds, average and worst, against the 16.7 ms a frame has at 60 a second. Shown once its code ran.
    if (const std::optional<galaxy::modlog::CodeTime> time = galaxy::modlog::code_time(id); time.has_value()) {
        const auto ms = [](std::uint64_t ns) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(ns) / 1.0e6);
            return std::string(text);
        };
        SettingsRow cost{"CODE TIME",
                         galaxy::modlog::row_words("Mods.CodeTime", id,
                                                   {{"average", ms(time->average_ns)}, {"worst", ms(time->worst_ns)}}),
                         nullptr, galaxy::modlog::row_words("Mods.CodeTimeHelp", id, {})};
        cost.names = 2u | 4u;
        rows.push_back(std::move(cost));
    }
    // The player's DON'T ASK AGAIN to its install requests (services-mods 9, J-SM4), undone here at once.
    if (mod.install_ask_never) {
        SettingsRow may{galaxy::modlog::row_words("Mods.InstallMayAsk", id, {}),
                        galaxy::modlog::row_words("Consent.Denied", id, {}),
                        [id](RenderConfig&, int delta) {
                            if (delta == 0) return;
                            const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                            set_install_ask_never_locked(id, false);
                        },
                        {}};
        may.names = 1u | 2u;
        rows.push_back(std::move(may));
    }
    // The player's answers for its code and permissions, each turned with left and right; a draft like any setting,
    // applied at the next start (consent 2.3). Code not allowed turns the mod off. Shown once the question was answered.
    const galaxy::mods::ModConsent consent = galaxy::mods::consent_facts(mod);
    if (consent.code && consent.code_answer != galaxy::mods::Answer::NotAsked) {
        const auto answer_words = [&id](bool allowed) {
            return galaxy::modlog::row_words(allowed ? "Consent.Allowed" : "Consent.Denied", id, {});
        };
        const bool code_allowed = consent.code_answer == galaxy::mods::Answer::Granted;
        SettingsRow code{galaxy::modlog::row_words("Consent.Code", id, {}), answer_words(code_allowed),
                         [id, code_allowed](RenderConfig&, int delta) {
                             if (delta == 0) return;
                             const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                             galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                             if (found == nullptr) return;
                             galaxy::mods::record_code_answer(*found, !code_allowed);
                             if (code_allowed) found->enabled = false;
                         },
                         galaxy::modlog::row_words("SettingsModsCodeAsk", id, {})};
        code.names = 1u | 2u | 4u;
        rows.push_back(std::move(code));
        std::string declined;
        for (const galaxy::mods::PermissionFacts& permission : consent.permissions) {
            if (permission.answer == galaxy::mods::Answer::NotAsked) continue;
            const bool granted = permission.answer == galaxy::mods::Answer::Granted;
            std::string name = galaxy::modlog::row_words(permission.row.c_str(), id, {});
            if (name.empty()) name = permission.name;
            if (!granted) declined += (declined.empty() ? "" : ", ") + name;
            const std::string hosts = permission.hosts == "*" ? galaxy::modlog::row_words("Permission.net.internet.Any", id, {})
                                                              : permission.hosts;
            std::string help = galaxy::modlog::row_words(permission.text_row.c_str(), id, {{"hosts", hosts}});
            if (!permission.roots.empty()) {
                help += ' ' + galaxy::modlog::row_words("Consent.Roots", id, {{"roots", permission.roots}});
            }
            // A required one turned to Not allowed says so before the restart does (H64-3): off from the next start.
            if (permission.required) {
                help += ' ' + (granted ? galaxy::modlog::row_words("Consent.Required", id, {})
                                         : galaxy::modlog::row_words("Consent.OffDeclined", id, {{"permission", name}}));
            }
            const std::string permission_name = permission.name;
            SettingsRow row{name, answer_words(granted),
                            [id, permission_name, granted](RenderConfig&, int delta) {
                                if (delta == 0) return;
                                const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                                galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                                if (found != nullptr) (void)galaxy::mods::record_answer(*found, permission_name, !granted);
                            },
                            help};
            row.names = 1u | 2u | 4u;
            rows.push_back(std::move(row));
        }
        if (!declined.empty()) {
            SettingsRow not_allowed{galaxy::modlog::row_words("Consent.NotAllowed", id, {{"list", declined}}), {}, nullptr, {}, true};
            not_allowed.names = 1u;
            rows.push_back(std::move(not_allowed));
        }
    }
    const std::string kinds = galaxy::mods::change_kinds(mod);
    rows.push_back({"CHANGES", kinds.empty() ? std::string("NOTHING THIS GAME USES") : kinds, nullptr, mod_details(mod)});
    if (mod.luma != nullptr && !mod.luma->version.empty()) fact("VERSION", mods_ui_text(mod.luma->version, 24u), {});
    if (!mod.pack_author.empty()) fact("BY", mods_ui_text(mod.pack_author, 60u), {});
    if (mod.luma != nullptr && !mod.luma->license.empty()) fact("LICENSE", mods_ui_text(mod.luma->license, 60u), {});
    if (mod.luma != nullptr && (mod.luma->homepage.starts_with("https://") || mod.luma->homepage.starts_with("http://"))) {
        fact("WEBSITE", mods_ui_text(mod.luma->homepage, 60u), {});
    }
    fact("FOLDER", mods_ui_text(mod.id, 60u), {});
    // Key options are the mod's input actions, bound in CONTROLS (services-input 2), not here.
    if (std::any_of(mod.options.begin(), mod.options.end(), [](const galaxy::mods::ModOption& o) { return o.kind != "key"; })) {
        SettingsRow heading{"OPTIONS", {}, nullptr, {}, true};
        rows.push_back(std::move(heading));
    }
    for (std::size_t o = 0; o < mod.options.size(); ++o) {
        const galaxy::mods::ModOption& option = mod.options[o];
        if (option.kind == "key") continue;
        if (option.kind == "text") {
            const galaxy::luma::Option* entry = nullptr;
            if (mod.luma != nullptr) {
                for (const galaxy::luma::Option& e : mod.luma->options) {
                    if (e.id == option.key) entry = &e;
                }
            }
            std::string_view name = option.name;
            while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
            std::string value = option.text.empty() && entry != nullptr ? entry->fallback : option.text;
            std::string help = mods_ui_text(option.section, 30u);
            {
                const std::lock_guard<std::mutex> text_lock(s_text_mutex);
                if (s_text_edit && s_text_edit->mod == id && s_text_edit->key == option.key) {
                    // The end of what is typed, and the place the next character goes.
                    value = s_text_edit->text;
                    while (code_points(value) > 21u) {
                        std::size_t cut = 1u;
                        while (cut < value.size() && (static_cast<unsigned char>(value[cut]) & 0xC0u) == 0x80u) ++cut;
                        value.erase(0, cut);
                    }
                    value += '_';
                    help = galaxy::modlog::row_words(s_text_edit->pad ? "Mods.OptionTypingPad" : "Mods.OptionTyping", id, {});
                } else {
                    value = mods_ui_text(value, 22u);
                }
            }
            const std::uint32_t max_length = entry != nullptr ? entry->max_length : 0u;
            SettingsRow text{mods_ui_text(name, 24u), std::move(value),
                             [id, key = option.key, max_length](RenderConfig&, int delta) {
                                 if (delta == 0) return;
                                 std::string now;
                                 {
                                     const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                                     galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                                     if (found == nullptr) return;
                                     for (const galaxy::mods::ModOption& o : found->options) {
                                         if (o.key == key) now = o.text;
                                     }
                                 }
                                 begin_text_edit(id, key, std::move(now), max_length,
                                                 s_last_key_from_pad.load(std::memory_order_relaxed));
                             },
                             std::move(help)};
            text.names = 1u | 2u | 4u;
            rows.push_back(std::move(text));
            continue;
        }
        const std::string value = option.selected == 0u || option.selected > option.choices.size()
                                      ? std::string("OFF")
                                      : mods_ui_text(option.choices[option.selected - 1u], 22u);
        const bool other_region = option.selected != 0u && option.selected <= option.other_region.size() &&
                                  option.other_region[option.selected - 1u];
        // A mod's option name as its menu shows it, without the colon some end in.
        std::string_view name = option.name;
        while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
        SettingsRow choice{mods_ui_text(name, 24u), value,
                           [id, o](RenderConfig&, int delta) {
                               const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                               galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                               if (found == nullptr || o >= found->options.size()) return;
                               galaxy::mods::ModOption& changed = found->options[o];
                               if (changed.must_choose) {
                                   changed.selected = settings_step(std::max(changed.selected, 1u) - 1u, static_cast<unsigned>(changed.choices.size()), delta) + 1u;
                               } else {
                                   changed.selected = settings_step(
                                       std::min<unsigned>(changed.selected, static_cast<unsigned>(changed.choices.size())),
                                       static_cast<unsigned>(changed.choices.size() + 1u), delta);
                               }
                           },
                           other_region ? std::string("MADE FOR ANOTHER REGION'S DISC: IT MAY NOT WORK WITH THIS ONE")
                                        : mods_ui_text(option.section, 30u)};
        choice.names = option.selected != 0u ? 3u : 1u;
        rows.push_back(std::move(choice));
    }
    // Its input actions (the manifest's key options, services-input 2): the record CONTROLS binds, on the scheme shown
    // there, read here (its own inputs, else the game control it follows); changed in CONTROLS only.
    bool actions_heading = false;
    for (const galaxy::input::mod_actions::Listed& action : mod_action_list()) {
        if (action.mod_id != id) continue;
        if (!actions_heading) {
            SettingsRow heading{"KEYS", {}, nullptr, {}, true};
            rows.push_back(std::move(heading));
            actions_heading = true;
        }
        std::string_view name = action.name.empty() ? std::string_view(action.id) : std::string_view(action.name);
        while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
        const galaxy::input::TargetBindings bindings = mod_action_draft(s_controls_scheme, action);
        SettingsRow shown{mods_ui_text(name, 24u),
                          no_bindings(bindings) ? mod_action_follows(action) : menu_bindings(s_controls_scheme, bindings),
                          nullptr,
                          galaxy::modlog::row_words("Mods.ActionWhere", id, {})};
        shown.names = 1u | 2u | 4u;
        rows.push_back(std::move(shown));
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
                        [id](RenderConfig&, int delta) {
                            const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                            galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                            if (found == nullptr || delta == 0) return;
                            found->own_text = !found->own_text;
                        },
                        "THIS MOD HAS NO " + language_name(playing) + " TEXT. ITS OWN TEXT MATCHES ITS CHANGES"});
    }
    // A pack's textures by what they are: each kind on or off, applied with the rest of the page.
    if (mod.textures) {
        SettingsRow heading{"TEXTURE CATEGORIES", {}, nullptr, {}, true};
        rows.push_back(std::move(heading));
        if (!mod.categories_final) {
            rows.push_back({"SORTING TEXTURES", {}, nullptr, "THE GAME'S TEXTURES ARE BEING SORTED. THIS HAPPENS ONCE"});
        }
        for (std::size_t c = 0; c < galaxy::textures::kCategoryCount; ++c) {
            const unsigned count = mod.category_textures[c];
            if (count == 0u) continue;
            const auto category = static_cast<galaxy::textures::Category>(c);
            const bool off = (mod.categories_off & galaxy::textures::category_bit(category)) != 0u;
            rows.push_back({galaxy::textures::category_name(category),
                            std::string(off ? "OFF" : "ON") + " (" + std::to_string(count) + ")",
                            [id, category](RenderConfig&, int delta) {
                                const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                                galaxy::mods::ModInfo* const found = mod_by_id_locked(id);
                                if (found == nullptr || delta == 0) return;
                                found->categories_off ^= galaxy::textures::category_bit(category);
                            },
                            mods_ui_text(mod.pack_title.empty() ? mod.id : mod.pack_title, 40u)});
        }
        rows.push_back(button_row({}, {"ALL ON", "ALL OFF"},
                                  [id](RenderConfig&, unsigned button) {
                                      const std::lock_guard<std::mutex> change_lock(s_mods_mutex);
                                      if (galaxy::mods::ModInfo* const found = mod_by_id_locked(id)) {
                                          found->categories_off = button == 0u ? 0u : 0xFFFFFFFFu;
                                      }
                                  },
                                  {}));
    }
    // What the mod log has of this mod (contract mod-log 4.2), under its options.
    for (const galaxy::modlog::PageMod& said : galaxy::modlog::mods()) {
        if (said.id != mod.id) continue;
        SettingsRow heading{"LOG", {}, nullptr, {}, true};
        rows.push_back(std::move(heading));
        add_log_rows(rows, galaxy::modlog::entries(), said, s_native_settings_row.load(std::memory_order_relaxed));
    }
    const bool about = mod_has_about(mod);
    // BLOCK (task 6.6): off in the draft and on the blocklist at once (it never starts again until UNBLOCK, even
    // removed and imported again); UNBLOCK leaves it off for the player to turn on.
    SettingsRow buttons = button_row({}, {about ? "ABOUT" : "-ABOUT", "REMOVE",
                                          galaxy::mods::mod_blocked(mod.id) ? "UNBLOCK" : "BLOCK", "DONE"},
                                     [](RenderConfig& draft, unsigned button) {
                                         if (button == 0u) {
                                             open_mod_about();
                                         } else if (button == 1u) {
                                             ask_mod_remove();
                                         } else if (button == 2u) {
                                             const std::lock_guard<std::mutex> lock(s_mods_mutex);
                                             const std::string id = s_mod_options;
                                             const bool blocked = galaxy::mods::mod_blocked(id);
                                             if (!blocked) place_mod_locked(id, false, 0u);
                                             if (galaxy::mods::set_mod_blocked(id, !blocked)) reread_mods_locked();
                                         } else {
                                             close_mod_options(draft);
                                         }
                                     },
                                     {});
    buttons.key = "mod-buttons";
    rows.push_back(std::move(buttons));
    modscreen_facts_locked(mod.id, "page", rows.data(), rows.size());
    return rows;
}

// A mod in its list: its name, and what it changes as the line under the page. With s_mods_mutex held.
SettingsRow mod_row(const galaxy::mods::ModInfo& mod) {
    const std::string title = mod_title(mod);
    const bool twin = std::any_of(s_mods.begin(), s_mods.end(), [&](const galaxy::mods::ModInfo& other) {
        return other.id != mod.id && mod_title(other) == title;
    });
    SettingsRow row{twin ? title + " (" + mods_ui_text(mod.id, 24u) + ")" : title, {}, nullptr, mod_details(mod)};
    row.key = "mod:" + mod.id;
    row.names = 1u;
    if (mod.problem.empty()) {
        const galaxy::mods::NotStartedText not_started = mod_not_started_text(mod.id);
        if (!not_started.text.empty()) {
            row.help = mods_ui_text(not_started.text, 160u);
            if (not_started.as_is) row.names |= 4u;
        }
    }
    if (row.help.empty() && mod.problem.empty() && mod.code_declined) {
        // Not now was the answer for this code: it waits for the player (turning it on asks again).
        row.help = galaxy::modlog::row_words("Consent.Waiting", mod.id, {});
        row.names |= 4u;
    }
    row.list = mod.enabled ? 2u : 1u;
    row.held = s_mod_held == mod.id;
    return row;
}

// The buttons under the lists, as the launcher's: Import file, Import folder, Details (the page of the mod last selected
// in a list; '-' before a word: unavailable), Open mods folder and Rescan. The line under the page says how an import
// goes. With s_mods_mutex held.
SettingsRow mods_buttons_row() {
    std::string help;
    {
        const std::lock_guard<std::mutex> import_lock(s_mod_import_mutex);
        help = !s_mod_import_message.empty() ? mods_ui_text(s_mod_import_message, 120u) : s_mod_import_value;
    }
    if (s_mod_dialog_open.load(std::memory_order_acquire)) help = "CHOOSING";
    const auto focus = std::find_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
    const bool details = focus != s_mods.end();
#if defined(__ANDROID__)
    // A phone picks files (its picker has no folders) and shows no folder of the app's own storage.
    SettingsRow row = button_row(
        {}, {"IMPORT FILE", details ? "DETAILS" : "-DETAILS", "RESCAN"},
        [](RenderConfig&, unsigned shown) {
            static constexpr unsigned kButtons[] = {0u, 2u, 4u};
            const unsigned button = kButtons[std::min(shown, 2u)];
#else
    SettingsRow row = button_row(
        {}, {"IMPORT FILE", "IMPORT FOLDER", details ? "DETAILS" : "-DETAILS", "OPEN FOLDER", "RESCAN", "EXCHANGE FOLDER"},
        [](RenderConfig&, unsigned button) {
            // The Exchange folder (services-fs 1): the player's and every mod's allowed fs.exchange, made if missing.
            if (button == 5u) {
                const std::filesystem::path exchange = galaxy::luma_fs::exchange_folder();
                std::error_code ec;
                if (!exchange.empty()) std::filesystem::create_directories(exchange, ec);
                open_folder_in_explorer(exchange);
                return;
            }
#endif
            switch (button) {
            case 0u:
            case 1u:
                if (!s_mod_import_busy.load(std::memory_order_acquire)) start_mod_import_dialog(button == 1u);
                return;
            case 2u: {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                const auto it = std::find_if(s_mods.begin(), s_mods.end(),
                                             [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_focus; });
                if (it != s_mods.end()) {
                    s_mod_options = s_mod_focus;
                    s_mod_held.clear();
                    s_native_settings_row.store(1u, std::memory_order_relaxed);
                    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
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

// A size as the REMOVED MODS line says it, in the played language's units (Size.* rows): bytes, else whole KB or MB
// (of 1024), rounded up so a few bytes never read 0.
std::string kept_size_text(std::uint64_t bytes, const std::string& id) {
    const char* row = "Size.Bytes";
    std::uint64_t n = bytes;
    if (bytes >= 1024u * 1024u) {
        row = "Size.MB";
        n = (bytes + 1048575u) / 1048576u;
    } else if (bytes >= 1024u) {
        row = "Size.KB";
        n = (bytes + 1023u) / 1024u;
    }
    return galaxy::modlog::row_words(row, id, {{"n", std::to_string(n)}});
}

// The REMOVED MODS line (services-mods 8, services-save 5): a mod whose folder is gone keeps its numbers and saved data
// for when it returns. One row each, "{mod}: kept for when it returns", its numbers and bytes as the help, and FORGET
// behind a question (mod_question_key). Nothing when no removed mod keeps anything. With s_mods_mutex held.
void add_kept_rows_locked(std::vector<SettingsRow>& rows) {
    if (!s_mods_kept_read) {
        galaxy::luma_mods::kept(s_mods_kept);
        s_mods_kept_read = true;
    }
    if (s_mods_kept.empty()) return;
    rows.push_back(SettingsRow{"REMOVED MODS", {}, nullptr, {}, true});
    for (const galaxy::luma_mods::KeptMod& kept : s_mods_kept) {
        const std::string id = kept.id;
        SettingsRow row = button_row(
            galaxy::modlog::row_words("Mods.Kept", id, {}), {"FORGET"},
            [id](RenderConfig&, unsigned) {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                s_mod_forget_ask = id;
            },
            galaxy::modlog::row_words("Mods.KeptHelp", id,
                                      {{"numbers", std::to_string(kept.numbers)},
                                       {"size", kept_size_text(kept.save_bytes + kept.data_bytes, id)}}));
        row.key = "kept:" + id;
        row.names = 1u | 4u;  // both already in the played language
        rows.push_back(std::move(row));
    }
}

// The MODS page as the launcher's Mods tab: the mods off, then the mods on (the higher one wins a conflict), each list
// under its title, then what removed mods keep, the LOG row and the buttons. A mod's options show instead while they
// are open.
// A user file on the offer screen: "Mario, 12 stars, saved 2026-11-12" (Save.OfferSummary, the local date of its
// last save), "Mario, 12 stars" when that is not known (Save.OfferSummaryUndated), or Save.OfferEmpty.
std::string offer_file_words(const galaxy::mods::OfferFile& file, const std::string& asker) {
    if (!file.exists) return galaxy::modlog::row_words("Save.OfferEmpty", asker, {});
    const std::string character = galaxy::modlog::row_words(file.luigi ? "Save.Luigi" : "Save.Mario", asker, {});
    std::tm local{};
    const std::time_t time = static_cast<std::time_t>(file.saved);
#if defined(_WIN32)
    const bool dated = file.saved > 0 && localtime_s(&local, &time) == 0;
#else
    const bool dated = file.saved > 0 && localtime_r(&time, &local) != nullptr;
#endif
    if (!dated) {
        return galaxy::modlog::row_words("Save.OfferSummaryUndated", asker,
                                         {{"character", character}, {"stars", std::to_string(file.stars)}});
    }
    char date[24];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return galaxy::modlog::row_words("Save.OfferSummary", asker,
                                     {{"character", character}, {"stars", std::to_string(file.stars)}, {"date", date}});
}

// mods.install's screen (services-mods 9): who asks, the file's name (never its path), and the answers; the line under
// the buttons says an installed mod starts at the next start (J-SM2). With s_ask_mutex held.
std::vector<SettingsRow> install_page_locked() {
    const InstallAsk& ask = *s_install_ask;
    std::vector<SettingsRow> rows;
    SettingsRow who{galaxy::modlog::row_words("Mods.InstallAsks", ask.asker, {}), {}, nullptr, {}, true};
    who.names = 1u;
    rows.push_back(std::move(who));
    SettingsRow file{"FILE", galaxy::path_text(ask.file.filename()), nullptr, {}};
    file.names = 2u;
    rows.push_back(std::move(file));
    // An installed mod of the file's id at an older version: the file updates it (UPDATE, and both versions under the
    // buttons). The same or a newer one installed: the import answers LUMA_E_EXISTS.
    const bool update = !ask.peek.installed.empty() && !ask.peek.version.empty() &&
                        galaxy::luma::compare_versions(ask.peek.version, ask.peek.installed) > 0;
    SettingsRow buttons = button_row(
        {}, {update ? "UPDATE" : "INSTALL", "NOT NOW", "DON'T ASK AGAIN"},
        [](RenderConfig&, unsigned button) {
            if (button == 0u) {
                std::filesystem::path file;
                {
                    const std::lock_guard<std::mutex> lock(s_ask_mutex);
                    if (!s_install_ask || s_install_ask->importing) return;
                    s_install_ask->importing = true;
                    file = s_install_ask->file;
                }
                if (!run_mod_imports({file})) {
                    const std::lock_guard<std::mutex> lock(s_ask_mutex);
                    if (s_install_ask && s_install_ask->importing) answer_install_locked(LUMA_E_BUSY);
                }
                return;
            }
            std::string asker;
            {
                const std::lock_guard<std::mutex> lock(s_ask_mutex);
                if (!s_install_ask || s_install_ask->importing) return;
                asker = s_install_ask->asker;
            }
            if (button == 2u) {
                const std::lock_guard<std::mutex> lock(s_mods_mutex);
                set_install_ask_never_locked(asker, true);
            }
            const std::lock_guard<std::mutex> lock(s_ask_mutex);
            if (s_install_ask && !s_install_ask->importing) answer_install_locked(LUMA_E_DENIED);
        },
        galaxy::modlog::row_words("Mods.InstallStartsNext", ask.asker, {}));
    buttons.names |= 4u;
    rows.push_back(std::move(buttons));
    if (update) {
        SettingsRow versions{galaxy::modlog::row_words("Mods.InstallUpdates", ask.asker,
                                                       {{"old", mods_ui_text(ask.peek.installed, 24u)},
                                                        {"new", mods_ui_text(ask.peek.version, 24u)}}),
                             {}, nullptr, {}, true};
        versions.names = 1u;
        rows.push_back(std::move(versions));
    }
    return rows;
}

// save.game's offer screen (services-save 8): the offered file, then the six to pick from, then the confirm for the
// one picked. With s_ask_mutex held.
std::vector<SettingsRow> offer_page_locked() {
    const OfferAsk& ask = *s_offer_ask;
    std::vector<SettingsRow> rows;
    SettingsRow title{galaxy::modlog::row_words("Save.OfferTitle", ask.asker, {}), {}, nullptr, {}, true};
    title.names = 1u;
    rows.push_back(std::move(title));
    SettingsRow offered{offer_file_words(ask.facts.offered, ask.asker), {}, nullptr, {}, true};
    offered.names = 1u;
    rows.push_back(std::move(offered));
    if (ask.picked == 0u) {
        SettingsRow pick{galaxy::modlog::row_words("Save.OfferPick", ask.asker, {}), {}, nullptr, {}, true};
        pick.names = 1u;
        rows.push_back(std::move(pick));
        for (std::uint32_t n = 1u; n <= 6u; ++n) {
            const galaxy::mods::OfferFile& file = ask.facts.files[n - 1u];
            std::string label = galaxy::modlog::row_words("Save.OfferFile", ask.asker, {{"file", std::to_string(n)}}) + ": ";
            if (file.in_play) label += galaxy::modlog::row_words("Save.OfferInPlay", ask.asker, {}) + ", ";
            label += offer_file_words(file, ask.asker);
            SettingsRow row = button_row(std::move(label), {file.in_play ? "-PICK" : "PICK"},
                                         [n](RenderConfig&, unsigned) {
                                             const std::lock_guard<std::mutex> lock(s_ask_mutex);
                                             if (!s_offer_ask || s_offer_ask->facts.files[n - 1u].in_play) return;
                                             s_offer_ask->picked = n;
                                             // The confirm's buttons (title, file, question, buttons).
                                             s_native_settings_row.store(3u, std::memory_order_relaxed);
                                             s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
                                         },
                                         {});
            row.names |= 1u;
            rows.push_back(std::move(row));
        }
        rows.push_back(button_row({}, {"NO"}, [](RenderConfig&, unsigned) { deny_asks(); }, {}));
        return rows;
    }
    SettingsRow confirm{galaxy::modlog::row_words("Save.OfferConfirm", ask.asker, {{"file", std::to_string(ask.picked)}}),
                        {}, nullptr, {}, true};
    confirm.names = 1u;
    rows.push_back(std::move(confirm));
    SettingsRow buttons = button_row(
        {}, {"REPLACE", "KEEP"},
        [](RenderConfig&, unsigned button) {
            const std::lock_guard<std::mutex> lock(s_ask_mutex);
            if (!s_offer_ask || s_offer_ask->picked == 0u) return;
            const std::uint32_t picked = s_offer_ask->picked;
            if (button == 0u) {
                answer_offer_locked(LUMA_OK, picked);
                return;
            }
            // KEEP: back to the files, on the one picked (title, file, heading, then files 1-6).
            s_offer_ask->picked = 0u;
            s_native_settings_row.store(2u + picked, std::memory_order_relaxed);
            s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
        },
        galaxy::modlog::row_words("Save.OfferKept", ask.asker, {}));
    buttons.names |= 4u;
    rows.push_back(std::move(buttons));
    return rows;
}

// ---- The hooks the services call (galaxy/mod_screens.h) ----

bool screens_screen_up() {
    if (s_import_page_open.load(std::memory_order_acquire) || s_mod_import_busy.load(std::memory_order_acquire) ||
        s_mod_dialog_open.load(std::memory_order_acquire)) {
        return true;
    }
    {
        const std::lock_guard<std::mutex> lock(s_ask_mutex);
        if (s_install_ask || s_offer_ask) return true;
    }
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    return s_consent_open;
}

// The menu on MODS over the paused game, on `row` of the request's page.
void open_request_screen(unsigned row) {
    galaxy::mods::pause_for_screen();
    host_window::open_native_settings(kModsTab);
    s_native_settings_row.store(row, std::memory_order_relaxed);
    s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
}

std::int32_t screens_ask_install(const std::filesystem::path& file, std::string_view asker,
                                 std::function<void(std::int32_t)> done) {
    if (screens_screen_up()) return LUMA_E_BUSY;
    {
        const std::lock_guard<std::mutex> lock(s_ask_mutex);
        if (s_install_ask || s_offer_ask) return LUMA_E_BUSY;
    }
    // The file's manifest and the installed mod of its id, read before the screen shows (the import reads it again).
    galaxy::mods::ImportPeek peek = galaxy::mods::peek_import(file, galaxy::mods::mods_folder());
    {
        const std::lock_guard<std::mutex> lock(s_ask_mutex);
        if (s_install_ask || s_offer_ask) return LUMA_E_BUSY;
        s_install_ask = InstallAsk{file, std::string(asker), std::move(done), false, std::move(peek)};
    }
    open_request_screen(2u);  // the buttons (who, file, buttons; the versions line under them)
    return LUMA_PENDING;
}

bool screens_install_ask_allowed(std::string_view id) {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    (void)mods_list();
    for (const galaxy::mods::ModInfo& mod : s_mods_saved) {
        if (mod.id == id) return !mod.install_ask_never;
    }
    return true;
}

std::int32_t screens_ask_offer(const galaxy::mods::OfferFacts& facts, std::string_view asker,
                               std::function<void(std::int32_t, std::uint32_t)> done) {
    if (screens_screen_up()) return LUMA_E_BUSY;
    {
        const std::lock_guard<std::mutex> lock(s_ask_mutex);
        if (s_install_ask || s_offer_ask) return LUMA_E_BUSY;
        s_offer_ask = OfferAsk{facts, std::string(asker), std::move(done), 0u};
    }
    open_request_screen(3u);  // file 1 (title, offered, heading, files)
    return LUMA_PENDING;
}

void screens_close_asks(std::string_view asker) {
    const std::lock_guard<std::mutex> lock(s_ask_mutex);
    if (s_install_ask && !s_install_ask->importing && s_install_ask->asker == asker) s_install_ask.reset();
    if (s_offer_ask && s_offer_ask->asker == asker) s_offer_ask.reset();
}

const galaxy::mods::ScreenHooks kScreenHooks = {screens_screen_up, screens_ask_install, screens_install_ask_allowed,
                                                screens_ask_offer, screens_close_asks};
// Given before main: the services ask only once mods start.
[[maybe_unused]] const bool s_screen_hooks_given = (galaxy::mods::set_screen_hooks(&kScreenHooks), true);

std::vector<SettingsRow> mods_page_rows(const RenderConfig&) {
#if defined(__APPLE__)
    // Mods are not on Apple's systems yet (docs/APPLE.md): the page says so, and nothing on it is chosen.
    return {SettingsRow{"THIS IS STILL A WORK IN PROGRESS. PLEASE COME BACK LATER!", {}, nullptr, {}, true}};
#else
    std::vector<SettingsRow> rows;
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    std::vector<galaxy::mods::ModInfo>& mods = mods_list();
    // A mod that is on with code the player hasn't been asked about (imported, or put in the folder: on from the
    // start): the question comes as the page shows, before anything is drafted.
    if (s_native_settings_visible.load(std::memory_order_relaxed) &&
        s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount == kModsTab && !restart_asking() &&
        mods_draft_changes_locked() == 0u && !request_screen_up() && first_code_to_allow_locked() != nullptr) {
        open_consent_locked();
    }
    // The packs' categories, once the game's textures are sorted.
    for (galaxy::mods::ModInfo& mod : mods) {
        (void)galaxy::mods::update_pack_categories(mod);
    }
    // A mod's request first (services-mods 9, services-save 8): the game waits on the player's answer.
    {
        const std::lock_guard<std::mutex> ask_lock(s_ask_mutex);
        if (s_offer_ask) {
            rows = offer_page_locked();
            modscreen_facts_locked(s_offer_ask->asker, "offer", rows.data(), rows.size());
            return rows;
        }
        if (s_install_ask && !s_install_ask->importing) {
            rows = install_page_locked();
            modscreen_facts_locked(s_install_ask->asker, "install", rows.data(), rows.size());
            return rows;
        }
    }
    // An import's result first: a mod it added that runs code is asked about after it.
    if (s_import_page_open.load(std::memory_order_acquire)) {
        return import_page();
    }
    if (s_consent_open) {
        if (galaxy::mods::ModInfo* asked = first_code_to_allow_locked()) return consent_page(*asked);
        s_consent_open = false;  // answered elsewhere (turned off, removed)
    }
    if (s_mod_log_open) {
        return log_page(mods);
    }
    const auto open = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_options; });
    if (open != mods.end()) {
        return mod_page(static_cast<std::size_t>(open - mods.begin()));
    }
    s_mod_options.clear();
    s_mod_about.reset();
    s_mod_remove_ask.clear();
    for (auto it = s_modscreen_shown.begin(); it != s_modscreen_shown.end();) {
        it = it->first.compare(0u, 5u, "page:") == 0 ? s_modscreen_shown.erase(it) : std::next(it);
    }
    // A safe start (task 6.6) on top: every mod is off for this start, and why; the lists keep the player's choices.
    if (const galaxy::mods::SafeStart safe = galaxy::mods::safe_start(); safe != galaxy::mods::SafeStart::kNo) {
        rows.push_back(SettingsRow{safe == galaxy::mods::SafeStart::kAsked
                                       ? "SAFE START: EVERY MOD IS OFF UNTIL THE NEXT START"
                                       : "NEBULA CLOSED UNEXPECTEDLY WITH MODS ON. EVERY MOD IS OFF UNTIL THE NEXT START",
                                   {}, nullptr, {}, true});
    }
    // A part of the game that mods need was missing at this start (contract start-check 2): the log's line for it on
    // top, as the notice said it. Nebula's own lines are under the mod "nebula"; the entries are read only then.
    const std::vector<galaxy::modlog::PageMod> said = galaxy::modlog::mods();
    if (std::any_of(said.begin(), said.end(), [](const galaxy::modlog::PageMod& m) { return m.id == "nebula"; })) {
        for (const galaxy::modlog::PageEntry& entry : galaxy::modlog::entries()) {
            if (entry.mod != "nebula" || entry.source != galaxy::modlog::kSourceCook || entry.code != 0u) continue;
            SettingsRow part{entry.text, {}, nullptr, {}, true};
            part.names = 1u;
            rows.push_back(std::move(part));
        }
    }
    // Nebula's own lines above the lists (a safe start, a missing part) as the facts modscreen.nebula, as the log
    // names Nebula's own entries.
    if (!rows.empty()) modscreen_facts_locked("nebula", "list", rows.data(), rows.size());
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
    for (const SettingsRow& row : rows) {
        if (row.list != 0u && !row.heading && row.key.compare(0u, 4u, "mod:") == 0) {
            modscreen_facts_locked(row.key.substr(4u), "list", &row, 1u);
        }
    }
    // The mod shown selected is the one the buttons act on, from the moment the page opens.
    const unsigned selected = s_native_settings_row.load(std::memory_order_relaxed);
    if (selected < rows.size() && rows[selected].list != 0u && !rows[selected].heading) {
        s_mod_focus = rows[selected].key.substr(4u);
    }
    add_kept_rows_locked(rows);
    rows.push_back(mod_log_row());
    rows.push_back(mods_buttons_row());
    return rows;
#endif
}

std::vector<SettingsRow> mods_page(const RenderConfig& render) {
    // Facts only for what is on screen: the menu open on this tab, in a harness run.
    s_modscreen_wanted.store(s_native_settings_visible.load(std::memory_order_relaxed) &&
                                 s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount ==
                                     kModsTab &&
                                 galaxy::modlog::facts_wanted(),
                             std::memory_order_relaxed);
    std::vector<SettingsRow> rows = mods_page_rows(render);
    std::vector<std::pair<std::string, std::string>> facts;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        facts.swap(s_modscreen_pending);
    }
    for (const auto& [key, json] : facts) galaxy::modlog::fact(key, json);
    return rows;
}

// Back from an import's result to the lists, on the first mod it added, else the buttons.
void close_import_page(const RenderConfig& render) {
    std::string id;
    {
        const std::lock_guard<std::mutex> lock(s_mod_import_mutex);
        for (const ImportOutcome& o : s_import_outcomes) {
            if (!o.id.empty() && o.outcome != "ALREADY IN YOUR MODS") {
                id = o.id;
                break;
            }
        }
    }
    s_import_page_open.store(false, std::memory_order_release);
    select_mods_row(render, id.empty() ? std::string("buttons") : "mod:" + id);
}

// Back from a mod's options, or the log, to the lists, on that mod or the LOG row.
void close_mod_options(const RenderConfig& render) {
    end_text_edit(false);
    std::string id;
    bool log = false;
    {
        const std::lock_guard<std::mutex> lock(s_mods_mutex);
        id.swap(s_mod_options);
        log = s_mod_log_open;
        s_mod_log_open = false;
    }
    select_mods_row(render, log ? std::string("modlog") : "mod:" + id);
}

// The MODS page's keys while its lists show, as the launcher's lists: up and down within a list, left and right to
// the other list at the same place, down from a list's last mod to the rows under the lists (what removed mods keep,
// LOG), then the buttons, and up again. A or Space picks the
// selected mod up; held, up and down move it among the mods on (a higher one wins), left turns it off, right on, and
// A, Space, Enter or Escape puts it down. On a mod's options, Escape goes back to the lists.
bool mods_page_key(std::uintptr_t key, bool accept, bool was_down) {
    if (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount != kModsTab) return false;
#if defined(__APPLE__)
    // The page is one line there (mods_page): its keys are the menu's own.
    (void)key;
    (void)accept;
    (void)was_down;
    return false;
#else
    const std::vector<SettingsRow> rows = mods_page(s_settings_draft);
    bool lists = false;
    for (const SettingsRow& row : rows) lists = lists || row.list != 0u;
    if (!lists) {
        // A log entry (the log page, a mod's page): A or Space opens it whole, a page at a time, in ABOUT's window; the
        // line under the page holds one line of it, and a refusal listing the game's signatures runs longer.
        const unsigned at = std::min(s_native_settings_row.load(std::memory_order_relaxed),
                                     static_cast<unsigned>(rows.size()) - 1u);
        if ((accept || key == VK_SPACE) && !rows.empty() && rows[at].key.rfind("log:", 0) == 0) {
            if (!was_down) open_log_entry(static_cast<std::size_t>(std::strtoul(rows[at].key.c_str() + 4, nullptr, 10)));
            return true;
        }
        if (request_screen_up()) {
            // A mod's request: Escape, Backspace or B is the player's no; the rest are its rows' own.
            if (key != VK_ESCAPE && key != VK_BACK) return false;
            if (!was_down) deny_asks();
            return true;
        }
        if (s_import_page_open.load(std::memory_order_acquire)) {
            // An import's result: Escape, Backspace or B is Done.
            if (key != VK_ESCAPE && key != VK_BACK) return false;
            if (!was_down) close_import_page(s_settings_draft);
            return true;
        }
        bool asking = false;
        {
            const std::lock_guard<std::mutex> lock(s_mods_mutex);
            asking = s_consent_open;
        }
        if (asking) {
            // The consent question: Escape, Backspace or B is Not now (consent 2.4); the rest are the rows' own.
            if (key != VK_ESCAPE && key != VK_BACK) return false;
            if (!was_down) answer_consent(ConsentAnswer::NotNow);
            return true;
        }
        if (key != VK_ESCAPE) return false;
        if (!was_down) close_mod_options(s_settings_draft);
        return true;
    }
    const unsigned selected = std::min(s_native_settings_row.load(std::memory_order_relaxed),
                                       static_cast<unsigned>(rows.size()) - 1u);
    // The rows of each list (1 off, 2 on) in order, and the rows under the lists top to bottom: what removed mods keep
    // (add_kept_rows_locked), the LOG row (mod_log_row) and the buttons.
    std::vector<unsigned> members[3];
    std::vector<unsigned> below;
    for (unsigned r = 0; r < rows.size(); ++r) {
        if (rows[r].list != 0u && !rows[r].heading) members[rows[r].list].push_back(r);
        if (rows[r].key.rfind("kept:", 0) == 0 || rows[r].key == "modlog" || rows[r].key == "buttons") below.push_back(r);
    }
    const unsigned buttons = !below.empty() ? below.back() : 0u;
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
        // Under the lists: down and up step among the rows there; up from the first goes back to the mod last selected
        // (else the first in a list); left, right and pressing are the rows' buttons.
        const auto at = std::find(below.begin(), below.end(), selected);
        if (key == VK_DOWN) {
            if (at != below.end() && at + 1 != below.end()) select(*(at + 1));
            return true;
        }
        if (key != VK_UP) return false;
        if (at != below.end() && at != below.begin()) {
            select(*(at - 1));
            return true;
        }
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
        select(place + 1u < members[list].size() ? members[list][place + 1u] : !below.empty() ? below.front() : buttons);
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
#endif
}

class WindowGeneralPageHost final : public ui::GeneralPageHost {
public:
    ui::GeneralPageState query(const RuntimeSettings& settings) const override {
        ui::GeneralPageState state;
        if (settings.button_icons == ButtonIcons::Auto) {
            using input::ButtonIconStyle;
            switch (galaxy::host::button_icons_auto_style()) {
            case ButtonIconStyle::Xbox: state.auto_icons = ButtonIcons::Xbox; break;
            case ButtonIconStyle::Nintendo: state.auto_icons = ButtonIcons::Nintendo; break;
            case ButtonIconStyle::PlayStation: state.auto_icons = ButtonIcons::PlayStation; break;
            case ButtonIconStyle::Keyboard: state.auto_icons = ButtonIcons::Keyboard; break;
            default: state.auto_icons = ButtonIcons::Game; break;
            }
        }
        state.display_language = game_language_resolved(settings.language);
        for (const GameLanguage language : kGameLanguages) {
            if (game_language_available(language)) state.languages.push_back(language);
        }
        state.saved_language = get_runtime_settings().language;
        state.resolved_saved_language = game_language_resolved(state.saved_language);
        {
            const std::lock_guard<std::mutex> lock(s_mii_mutex);
            state.mii_status = s_mii_status;
        }
        state.choosing_miis = s_mii_dialog_open.load(std::memory_order_acquire);
        state.folders_open = host_window::folders_open();
#if defined(_WIN32)
        state.focus_loss_offered = true;  // the Windows host pauses or mutes a game whose window is not in front (host_main.cpp)
#endif
        state.mii_count = s_mii_count.load(std::memory_order_acquire);
        if (state.mii_count < 0) {
            state.mii_count = static_cast<int>(galaxy::host::mii_library_count());
            s_mii_count.store(state.mii_count, std::memory_order_release);
        }
        return state;
    }
    void act(ui::GeneralPageAction action) const override {
        if (action == ui::GeneralPageAction::ImportMiis) start_mii_import_dialog();
        else if (action == ui::GeneralPageAction::OpenMiiFolder) open_mii_folder();
    }
};

std::vector<SettingsRow> game_page(const RuntimeSettings& runtime) {
    static const WindowGeneralPageHost host;
    return ui::general_page(runtime, s_runtime_draft, host);
}

// ADVANCED: any galaxy and star, entered with the game's own stage change (names in its own words), and the Grand
// Stars that power the Observatory.
std::vector<SettingsRow> advanced_page() {
    return s_advanced_page.rows([] { set_native_settings_visible(false); });
}

std::vector<SettingsRow> system_page(const RenderFeatureCaps& caps) {
    ui::GpuTimingView timing;
    timing.on = gpu_timing_on();
    timing.toggle = [] { set_gpu_timing(!gpu_timing_on()); };
    if (timing.on) {
        GpuPassTime times[32];
        const std::size_t count = gpu_pass_times(times, std::size(times));
        for (std::size_t i = 0u; i < count; ++i) {
            timing.passes.emplace_back(times[i].name, times[i].ms);
        }
    }
    return ui::system_page(caps, post_process_caps_text(get_post_process_status()),
                           [] { s_diagnostics_ask.store(true, std::memory_order_release); }, &timing);
}

std::vector<SettingsRow> settings_page(unsigned tab, const RenderConfig& draft, const RuntimeSettings& runtime,
                                       const RenderFeatureCaps& caps) {
    std::vector<SettingsRow> rows;
    switch (tab % kNativeSettingsTabCount) {
    case 0: rows = display_page(draft); break;
    case 1:
        rows = graphics_page(draft);
        std::erase_if(rows, [](const SettingsRow& row) { return graphics_row_hidden(std::string(row.label).c_str()); });
        break;
    case 4: rows = game_page(runtime); break;
    case 5: rows = mods_page(draft); break;
    case 6: rows = system_page(caps); break;
    case 7: rows = advanced_page(); break;
    default: break;
    }
    std::erase_if(rows, [](const SettingsRow& row) { return row_hidden(std::string(row.label).c_str()); });
    return rows;
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
            case RuntimeInputMode::RealWiimote: return "INPUT: WII REMOTE (SENSOR BAR)";
            case RuntimeInputMode::WiimoteMotion: return "INPUT: WII REMOTE (MOTION)";
            case RuntimeInputMode::KeyboardMouse: return "INPUT: KEYBOARD+MOUSE ONLY";
            case RuntimeInputMode::Phone: return "INPUT: PHONE";
            default: {
                // Auto names what it is using now.
                for (std::size_t i = 0; i < s_controls_devices.count; ++i) {
                    if (s_controls_devices.entries[i].active) {
                        return "INPUT: AUTO (" + upper_ascii(s_controls_devices.entries[i].label.data()) + ")";
                    }
                }
                return "INPUT: AUTO";
            }
            }
        case ControlsRow::Device: {
            if (s_controls_devices.count == 0u) {
                return s_input_mode_draft == RuntimeInputMode::KeyboardMouse ? "NO KEYBOARD OR MOUSE CONNECTED" : "NO CONTROLLER CONNECTED";
            }
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
            std::string line = std::string(galaxy::input::game_input_label(info.target)) + ": ";
            if (controls_capturing() && s_capture_target == static_cast<std::size_t>(info.target)) {
                return line + "PRESS AN INPUT";
            }
            std::string bound = upper_ascii(menu_bindings(
                s_controls_scheme, s_controls_draft.table(s_controls_scheme)[info.target]));
            if (bound.size() > 34u) bound = bound.substr(0, 32u) + "..";
            return line + bound;
        }
        case ControlsRow::ModAction: {
            const std::vector<galaxy::input::mod_actions::Listed> actions = mod_action_list();
            if (info.action >= actions.size()) return {};
            const galaxy::input::mod_actions::Listed& action = actions[info.action];
            std::string_view name = action.name.empty() ? std::string_view(action.id) : std::string_view(action.name);
            while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
            const std::string line = mods_ui_text(name, 24u) + ": ";
            if (s_capture_target == kModCaptureBase + info.action) return line + "PRESS AN INPUT";
            const galaxy::input::TargetBindings bindings = mod_action_draft(s_controls_scheme, action);
            if (no_bindings(bindings)) return line + mod_action_follows(action);
            std::string bound = upper_ascii(menu_bindings(s_controls_scheme, bindings));
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
        case ControlsRow::Pointer: {
            static constexpr const char* kSources[] = {"STICK", "MOTION", "TOUCHPAD", "MOTION + TOUCHPAD"};
            const ControlsSensors have = controls_sensors();
            return std::string("POINTER: ") +
                   kSources[(tuning.gyro_pointer && have.gyro ? 1u : 0u) | (tuning.touch_pointer && have.touch ? 2u : 0u)];
        }
        case ControlsRow::TouchSensitivity:
            std::snprintf(text, sizeof(text), "TOUCHPAD SENSITIVITY: %.1f",
                          static_cast<double>(tuning.touch_sensitivity));
            return text;
        case ControlsRow::GyroSensitivity:
            std::snprintf(text, sizeof(text), "GYRO SENSITIVITY: %.1f",
                          static_cast<double>(tuning.gyro_sensitivity));
            return text;
        case ControlsRow::WiimoteStatus: {
            const galaxy::host::WiiRemoteState remote = galaxy::host::wii_remote_state();
            // One line, no value: a state, not a setting (NebulaText has each whole).
            if (!remote.connected) return "NO WII REMOTE CONNECTED";
            if (s_input_mode_draft == RuntimeInputMode::WiimoteMotion) {
                if (remote.motion_plus) return "PRESS 2 TO CENTER THE POINTER";
                if (remote.has_motion_plus) return "TURNING ON THE MOTIONPLUS";
                return "NO MOTIONPLUS. POINTING AT THE SENSOR BAR";
            }
            return remote.nunchuk ? "WII REMOTE AND NUNCHUK CONNECTED" : "WII REMOTE CONNECTED";
        }
        case ControlsRow::PairJoyCon:
            return "PAIR JOY-CON";
        case ControlsRow::ArrangeTouch:
            return "ON-SCREEN CONTROLS";
        case ControlsRow::TouchSize: {
            const unsigned size = touch_draft_settings().size;
            return size == 0u ? std::string("BUTTON SIZE: AUTO") : "BUTTON SIZE: " + std::to_string(size) + "%";
        }
        case ControlsRow::TouchOpacity:
            return "BUTTON OPACITY: " + std::to_string(std::clamp(touch_draft_settings().opacity, 20u, 100u)) + "%";
        case ControlsRow::TouchVibration:
            return std::string("VIBRATION: ") + on_off(touch_draft_settings().vibration);
        case ControlsRow::TouchPlaces:
            return std::string("BUTTON PLACES: ") + (s_touch_draft.reset_places ? "KEEP" : "RESET");
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
        case 7:
            switch (runtime.audio.sound_output) {
            case SoundOutput::Mono: return "SOUND OUTPUT: MONO";
            case SoundOutput::Surround: return "SOUND OUTPUT: SURROUND";
            default: return "SOUND OUTPUT: STEREO";
            }
        case kAudioResetRow: return "AUDIO DEFAULTS: RESET";
        default: return {};
        }
    default:
        return {};
    }
}

enum class VendorOffer : std::uint8_t { None, Ask, License, Downloading, Ready, Failed };
VendorOffer s_vendor_offer = VendorOffer::None;
unsigned s_vendor_upscaler = 3u;  // what it is about: 3 DLSS, 4 XeSS
std::atomic<bool> s_vendor_downloading{false};
std::atomic<unsigned> s_vendor_percent{0u};
bool s_dlss_installed_now = false;
bool s_xess_installed_now = false;
std::uint64_t s_vendor_failed_until_ms = 0u;
std::vector<std::string> s_license_pages;
unsigned s_license_page = 0u;

bool dlss_offered() {
    if (!kStreamlineBuilt || host_window::upscaler_caps().dlss) {
        return false;
    }
    if (s_dlss_installed_now) {
        return true;
    }
    static const bool offered = host_window::rtx_adapter.load(std::memory_order_relaxed) && !host_window::dlss_files_present();
    return offered;
}

bool dlss_installed_now() {
    return s_dlss_installed_now && !host_window::upscaler_caps().dlss;
}

// Where the machine has a download for it (host_window::vendor_megabytes); it runs on any GPU that takes its shaders.
bool xess_offered() {
    if (host_window::upscaler_caps().xess || host_window::vendor_megabytes(4u) == 0u) {
        return false;
    }
    if (s_xess_installed_now) {
        return true;
    }
    static const bool offered = !host_window::xess_files_present();
    return offered;
}

bool xess_installed_now() {
    return s_xess_installed_now && !host_window::upscaler_caps().xess;
}

unsigned vendor_upscaler() { return s_vendor_upscaler; }

// A question of its own is up: the page and its rows wait.
bool vendor_prompting() {
    return s_vendor_offer == VendorOffer::Ask || s_vendor_offer == VendorOffer::License || s_vendor_offer == VendorOffer::Downloading ||
           s_vendor_offer == VendorOffer::Ready;
}

// DLSS Frame Generation (GRAPHICS > INTERPOLATION): offered while it runs, or with DLSS once its plugins are
// installed (it loads when the game starts with it on).
bool frame_generation_offered() {
    if (host_window::upscaler_caps().frame_generation) {
        return true;
    }
    static const bool installed = host_window::frame_generation_files_present();
    return installed && host_window::upscaler_caps().dlss;
}

// DLSS or XeSS chosen before it is installed is not applied: the menu asks to download it. Once downloaded this session it
// applies and runs after a restart (FSR 1 until then).
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live) {
    const unsigned wanted = applied.upscaler;
    if ((wanted != 3u && wanted != 4u) || live.upscaler == wanted) {
        return false;
    }
    if (wanted == 3u ? !dlss_offered() || s_dlss_installed_now : !xess_offered() || s_xess_installed_now) {
        return false;
    }
    applied.upscaler = live.upscaler;
    if (s_vendor_offer != VendorOffer::Downloading) {
        s_vendor_offer = VendorOffer::Ask;
        s_vendor_upscaler = wanted;
    }
    return true;
}

void dlss_dismiss_prompt() {
    if (s_vendor_offer == VendorOffer::Ask || s_vendor_offer == VendorOffer::License || s_vendor_offer == VendorOffer::Ready) {
        s_vendor_offer = VendorOffer::None;
    }
}

void vendor_revert_draft() {
    if (s_settings_draft.upscaler == s_vendor_upscaler &&
        !(s_vendor_upscaler == 3u ? s_dlss_installed_now : s_xess_installed_now)) {
        s_settings_draft.upscaler = get_render_config().upscaler;
    }
}

void vendor_fail(const char* why) {
    vendor_revert_draft();
    s_vendor_offer = VendorOffer::Failed;
    s_vendor_failed_until_ms = galaxy::steady_ms() + 4000u;
    std::cerr << "[download] not installed: " << why << '\n';
}

// The license in pages the menu's window holds, broken between words.
std::vector<std::string> license_pages(const std::string& text) {
    constexpr std::size_t kPageBytes = 1100u;
    std::string clean;
    clean.reserve(text.size());
    for (const char c : text) {
        if (c != '\r') {
            clean.push_back(c == '\t' || c == '\f' ? ' ' : c);
        }
    }
    std::vector<std::string> pages;
    std::size_t at = 0u;
    while (at < clean.size()) {
        std::size_t end = std::min(at + kPageBytes, clean.size());
        if (end < clean.size()) {
            const std::size_t paragraph = clean.rfind("\n\n", end);
            const std::size_t space = clean.rfind(' ', end);
            if (paragraph != std::string::npos && paragraph > at + kPageBytes / 2u) {
                end = paragraph + 2u;
            } else if (space != std::string::npos && space > at) {
                end = space + 1u;
            }
        }
        pages.push_back(clean.substr(at, end - at));
        at = end;
    }
    return pages;
}

// The vendor questions' keys: Enter/Y, Backspace/X and Esc/B; other keys wait for an answer.
bool dlss_prompt_key(std::uintptr_t key, bool was_down) {
    if (!vendor_prompting()) {
        return false;
    }
    if (was_down) {
        return true;
    }
    switch (s_vendor_offer) {
    case VendorOffer::Ask:
        if (key == VK_ESCAPE) {
            vendor_revert_draft();
            s_vendor_offer = VendorOffer::None;
        } else if (key == VK_BACK) {
            host_window::fetch_vendor_license(s_vendor_upscaler);
            s_license_pages.clear();
            s_license_page = 0u;
            s_vendor_offer = VendorOffer::License;
        } else if (key == VK_RETURN) {
            if (host_window::start_vendor_install(s_vendor_upscaler)) {
                s_vendor_percent.store(0u, std::memory_order_relaxed);
                s_vendor_downloading.store(true, std::memory_order_release);
                s_vendor_offer = VendorOffer::Downloading;
                std::cerr << "[download] started\n";
            } else {
                vendor_fail("could not start");
            }
        }
        break;
    case VendorOffer::License:
        if (key == VK_ESCAPE) {
            s_vendor_offer = VendorOffer::Ask;
        } else if (key == VK_BACK && s_license_page > 0u) {
            --s_license_page;
        } else if (key == VK_RETURN && s_license_page + 1u < s_license_pages.size()) {
            ++s_license_page;
        }
        break;
    case VendorOffer::Downloading:
        if (key == VK_ESCAPE) {
            host_window::cancel_vendor_install();
        }
        break;
    case VendorOffer::Ready:
        if (key == VK_ESCAPE) {
            s_vendor_offer = VendorOffer::None;
        } else if (key == VK_RETURN) {
            s_vendor_offer = VendorOffer::None;
            if (host_window::restart_game(false)) {
                std::cerr << "[download] restarting\n";
            }
        }
        break;
    default:
        break;
    }
    return true;
}

// After changes that take a restart were applied (mods; ray tracing turned on before its shaders are prepared): Enter,
// Space or Y restarts the game now; Escape, Backspace or B keeps them for the next start. Every other key waits for
// the answer.
bool restart_ask_key(std::uintptr_t key, bool was_down) {
    if (!restart_asking()) return false;
    if (was_down) return true;
    if (key == VK_RETURN || key == VK_SPACE) {
        s_mods_restart_ask.store(false, std::memory_order_release);
        s_rt_restart_ask.store(false, std::memory_order_release);
        const bool record = s_diagnostics_ask.exchange(false, std::memory_order_acq_rel);
        if (host_window::restart_game(record)) {
            std::cerr << (record ? "[settings] restarting into a diagnostics run\n" : "[settings] restarting\n");
        } else {
            show_controls_message("COULD NOT RESTART");
        }
    } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
        s_mods_restart_ask.store(false, std::memory_order_release);
        s_rt_restart_ask.store(false, std::memory_order_release);
        s_diagnostics_ask.store(false, std::memory_order_release);
    }
    return true;
}

// Render thread, every frame: follows the download, and takes the license when it arrives.
void poll_dlss_download() {
    if (s_vendor_offer == VendorOffer::License && s_license_pages.empty()) {
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        std::string text;
        if (host_window::vendor_license(text) == host_window::VendorLicense::Ready) {
            s_license_pages = license_pages(text);
        }
    }
    if (!s_vendor_downloading.load(std::memory_order_acquire)) {
        return;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    unsigned percent = 0u;
    const host_window::VendorInstall install = host_window::poll_vendor_install(percent);
    s_vendor_percent.store(percent, std::memory_order_relaxed);
    if (install == host_window::VendorInstall::Running) {
        return;
    }
    s_vendor_downloading.store(false, std::memory_order_release);
    if (install == host_window::VendorInstall::Canceled) {
        vendor_revert_draft();
        s_vendor_offer = VendorOffer::None;
        std::cerr << "[download] stopped\n";
        return;
    }
    if (install != host_window::VendorInstall::Done) {
        vendor_fail("download failed");
        return;
    }
    (s_vendor_upscaler == 3u ? s_dlss_installed_now : s_xess_installed_now) = true;
    RenderConfig render = get_render_config();
    render.upscaler = s_vendor_upscaler;
    set_render_config(render);
    s_settings_draft.upscaler = s_vendor_upscaler;
    s_settings_save.dirty.store(true, std::memory_order_release);
    flush_native_settings_if_dirty();
    s_vendor_offer = VendorOffer::Ready;
    std::cerr << "[download] installed; it starts with the next launch\n";
}

// The overlay's line for these questions.
std::string dlss_footer(std::uint64_t now_ms, bool& error) {
    const std::string name = s_vendor_upscaler == 3u ? "NVIDIA DLSS" : "INTEL XESS";
    switch (s_vendor_offer) {
    case VendorOffer::Ask:
        return "DOWNLOAD " + name + " (" + std::to_string(host_window::vendor_megabytes(s_vendor_upscaler)) + " MB)? ENTER/Y YES  ESC/B NO";
    case VendorOffer::License:
        return name + " LICENSE";
    case VendorOffer::Downloading:
        return "DOWNLOADING " + name + " " + std::to_string(s_vendor_percent.load(std::memory_order_relaxed)) + "%";
    case VendorOffer::Ready:
        return name + " READY. RESTART NOW? ENTER/Y YES  ESC/B LATER";
    case VendorOffer::Failed:
        if (now_ms < s_vendor_failed_until_ms) {
            error = true;
            return name + " WAS NOT INSTALLED";
        }
        s_vendor_offer = VendorOffer::None;
        return {};
    default:
        return {};
    }
}

// The offer as the menu's status: its questions, the download, a failure for a moment; Help when there is none.
NativeSettingsStatus dlss_status(std::uint64_t now_ms) {
    bool error = false;
    if (dlss_footer(now_ms, error).empty()) {
        return NativeSettingsStatus::Help;
    }
    const bool dlss = s_vendor_upscaler == 3u;
    switch (s_vendor_offer) {
    case VendorOffer::Ask: return dlss ? NativeSettingsStatus::DlssAsk : NativeSettingsStatus::XessAsk;
    case VendorOffer::License: return NativeSettingsStatus::License;
    case VendorOffer::Downloading: return dlss ? NativeSettingsStatus::DlssInstalling : NativeSettingsStatus::XessInstalling;
    case VendorOffer::Ready: return dlss ? NativeSettingsStatus::DlssRestart : NativeSettingsStatus::XessRestart;
    default: return dlss ? NativeSettingsStatus::DlssFailed : NativeSettingsStatus::XessFailed;
    }
}

// What the question says with it.
void fill_vendor_view(NativeSettingsView& view) {
    switch (s_vendor_offer) {
    case VendorOffer::Ask:
        view.status_number = static_cast<int>(host_window::vendor_megabytes(s_vendor_upscaler));
        break;
    case VendorOffer::Downloading:
        view.status_number = static_cast<int>(s_vendor_percent.load(std::memory_order_relaxed));
        break;
    case VendorOffer::License:
        if (s_license_page < s_license_pages.size()) {
            view.status_number = static_cast<int>(s_license_page) + 1;
            view.long_text = s_license_pages[s_license_page];
        } else {
            std::string unused;
            view.status_number = host_window::vendor_license(unused) == host_window::VendorLicense::Failed ? -2 : -1;
        }
        break;
    default:
        break;
    }
}

// The open mod page's ABOUT: its name and description in pages, then its icon and pictures. Nothing to show: nothing.
void open_mod_about() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    const auto it = std::find_if(s_mods.begin(), s_mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == s_mod_options; });
    if (it == s_mods.end() || !mod_has_about(*it)) return;
    ModAbout about;
    about.id = it->id;
    if (!it->pack_description.empty()) about.pages = license_pages(mod_title(*it) + "\n\n" + it->pack_description);
    if (it->luma != nullptr) {
        if (!it->luma->icon.empty()) about.pictures.push_back(it->path / galaxy::text_path(it->luma->icon));
        for (const std::string& picture : it->luma->pictures) about.pictures.push_back(it->path / galaxy::text_path(picture));
    }
    s_mod_about = std::move(about);
}

// One mod-log entry in ABOUT's window: who wrote it, its place, then its whole text, in pages. The entry is the one at
// `at` in the log as it is now (a row's key keeps the number; the log only grows while the menu is up).
void open_log_entry(std::size_t at) {
    const std::vector<galaxy::modlog::PageEntry> entries = galaxy::modlog::entries();
    if (at >= entries.size()) return;
    const galaxy::modlog::PageEntry& entry = entries[at];
    std::string head = log_source(entry);
    const std::string place = log_place(entry);
    if (!place.empty()) head += (head.empty() ? "" : "  ") + place;
    ModAbout about;
    about.id = entry.mod;
    about.pages = license_pages(head.empty() ? entry.text : head + "\n\n" + entry.text);
    galaxy::modlog::entry_fact(entry.mod, static_cast<std::uint32_t>(about.pages.size()), entry.text);
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    s_mod_about = std::move(about);
}

// REMOVE on the open mod page: asked first. A mod this session started with stays until the game restarts without it
// (its files are read while the game plays).
void ask_mod_remove() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    if (s_mod_options.empty()) return;
    // An import may be writing this mod's folder: removal waits for it to end.
    if (s_mod_import_busy.load(std::memory_order_acquire)) {
        show_controls_message("AN IMPORT IS RUNNING: TRY AGAIN WHEN IT ENDS");
        return;
    }
    const galaxy::mods::SessionMods session = galaxy::mods::session_mods();
    if (std::find(session.active.begin(), session.active.end(), s_mod_options) != session.active.end()) {
        show_controls_message("IN USE UNTIL NEBULA RESTARTS: TURN IT OFF AND RESTART FIRST");
        return;
    }
    s_mod_remove_ask = s_mod_options;
}

// Either is up on the MODS page. Left otherwise (the menu closed with F1 and opened on another page): both end.
bool mod_question_up() {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    if (s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount != kModsTab) {
        s_mod_about.reset();
        s_mod_remove_ask.clear();
        s_mod_forget_ask.clear();
    }
    return s_mod_about.has_value() || !s_mod_remove_ask.empty() || !s_mod_forget_ask.empty();
}

// The ABOUT window's keys: Enter, Space or Y the next page, Backspace or X the one before, Escape, B or F1 back to the
// mod's page. REMOVE's: Enter, Space or Y removes it (to the recycle bin where there is one) and goes back to the lists,
// Escape, Backspace, B or F1 keeps it. Every other key waits.
bool mod_question_key(std::uintptr_t key, bool was_down) {
    if (!mod_question_up()) return false;
    std::unique_lock<std::mutex> lock(s_mods_mutex);
    if (s_mod_about) {
        if (was_down) return true;
        ModAbout& about = *s_mod_about;
        const unsigned count = static_cast<unsigned>(about.pages.size() + about.pictures.size());
        if (key == VK_ESCAPE || key == VK_F1) {
            s_mod_about.reset();
        } else if (key == VK_BACK && about.page > 0u) {
            --about.page;
        } else if ((key == VK_RETURN || key == VK_SPACE) && about.page + 1u < count) {
            ++about.page;
        }
        return true;
    }
    if (!s_mod_forget_ask.empty()) {
        // FORGET's: Enter, Space or Y drops what the removed mod kept (services-mods 8: its numbers at once, its saved
        // data at each file's next save; one Note Mods.Forgotten), Escape, Backspace, B or F1 keeps it.
        if (was_down) return true;
        if (key == VK_RETURN || key == VK_SPACE) {
            const std::string id = s_mod_forget_ask;
            s_mod_forget_ask.clear();
            lock.unlock();
            galaxy::luma_mods::forget(id);
            {
                const std::lock_guard<std::mutex> relock(s_mods_mutex);
                s_mods_kept_read = false;  // read again after the forget, so the row is gone
            }
            select_mods_row(s_settings_draft, "modlog");
        } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
            s_mod_forget_ask.clear();
        }
        return true;
    }
    if (s_mod_remove_ask.empty()) return false;
    if (was_down) return true;
    if (key == VK_RETURN || key == VK_SPACE) {
        const std::string id = s_mod_remove_ask;
        s_mod_remove_ask.clear();
        std::string message;
        const bool removed = galaxy::mods::remove_mod(galaxy::mods::mods_folder(), id, &message);
        std::cerr << "[settings] mod remove: " << message << '\n';
        if (removed) {
            s_mod_options.clear();
            s_mod_held.clear();
            reread_mods_locked();
        }
        lock.unlock();
        {
            const std::lock_guard<std::mutex> import_lock(s_mod_import_mutex);
            s_mod_import_message = message;
        }
        if (removed) select_mods_row(s_settings_draft, "buttons");
    } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
        s_mod_remove_ask.clear();
    }
    return true;
}

// The ABOUT window or the REMOVE question as the menu's status; false when neither is up. A page's picture is read on
// its own thread; until it is in, the page says it loads.
bool fill_mod_question_view(NativeSettingsView& view) {
    const std::lock_guard<std::mutex> lock(s_mods_mutex);
    if (!s_mod_forget_ask.empty()) {
        view.status = NativeSettingsStatus::ModForgetAsk;
        view.status_text = s_mod_forget_ask;  // its folder is gone: its id is what names it
        return true;
    }
    if (!s_mod_remove_ask.empty()) {
        view.status = NativeSettingsStatus::ModRemoveAsk;
#if defined(_WIN32)
        view.status_number = 1;
#endif
        return true;
    }
    if (!s_mod_about) return false;
    ModAbout& about = *s_mod_about;
    view.status = NativeSettingsStatus::ModAbout;
    view.status_number = static_cast<int>(about.page) + 1;
    if (about.page < about.pages.size()) {
        view.long_text = about.pages[about.page];
        return true;
    }
    const std::size_t picture = about.page - about.pages.size();
    if (picture >= about.pictures.size()) return true;
    if (about.picture_page != about.page) {
        // A page turned while one was read: that one is waited for (it is never more than a moment).
        if (about.reading.valid()) about.reading.wait();
        about.picture.reset();
        about.unreadable = false;
        about.missing = false;
        about.picture_page = about.page;
        about.reading = std::async(std::launch::async, read_menu_picture, about.pictures[picture]);
    }
    if (about.reading.valid() && about.reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        about.picture = about.reading.get();
        about.unreadable = about.picture == nullptr;
        std::error_code exists_error;
        about.missing = about.unreadable && !std::filesystem::is_regular_file(about.pictures[picture], exists_error);
    }
    // No picture to show: the page's number with what keeps it away (nebula_game_api.h MOD_ABOUT).
    const int page_number = static_cast<int>(about.page) + 1;
    if (about.unreadable) {
        view.status_number = -(page_number * 4 + (about.missing ? 3 : 2));
    } else if (about.picture == nullptr) {
        view.status_number = -(page_number * 4 + 1);
    }
    view.picture = about.picture;
    return true;
}

}  // namespace

bool native_settings_mods_changed() {
    return mods_restart_pending();
}

namespace host_window {

// DXR 1.1 with shader model 6.6 and resource binding tier 3 on an adapter with dedicated memory (the RAY TRACING row).
std::atomic<bool> ray_tracing_offered{false};
// Queues a save of the settings files when something changed; never waits on the disk.
bool flush_native_settings_if_dirty() {
    return s_settings_save.queue();
}

float upscaler_render_fraction(const RenderConfig& render, unsigned output_width, unsigned output_height) {
    return render::upscaler_render_fraction(render, output_width, output_height);
}

// Exit paths: waits for a queued or running save (at most 5 s), then writes what is still unsaved here.
void save_native_settings_now() { s_settings_save.save_now(); }

// Nebula Settings can save config.ini while the game runs. The game takes the file's settings as the menu's
// apply does, so the next save from the game writes them back rather than over them. Checked twice a second,
// and not while the menu is open: its draft and display confirmation belong to the settings it opened with, and
// a change made meanwhile lands once it closes.
void poll_render_config_file() {
    static std::uint64_t s_next_check_ms = 0u;
    const std::uint64_t now = galaxy::steady_ms();
    if (now < s_next_check_ms) {
        return;
    }
    s_next_check_ms = now + 500u;
    // A save of the game's own still in flight would read back as a change made outside it.
    if (s_settings_save.pending()) {
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
    return ::galaxy::launch_args().started_hidden;
}

NativeSettingsView build_native_settings_view();

void open_native_settings(unsigned page) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    s_native_settings_tab.store(page % kNativeSettingsTabCount, std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
    set_native_settings_visible(true);
}

void native_settings_present() {
    poll_dlss_download();
}

// The menu as it stands: its pages, the open page's rows (label, value, a heading or not, whether it changes), the
// selection and what the line under the page says, in the order the overlay always chose it. Takes the menu's lock.
NativeSettingsView build_native_settings_view() {
    NativeSettingsView view;
    for (const unsigned shown : kShownTabs) {
        view.pages.push_back(kNativeSettingsTabNames[shown]);
    }
    view.controls_page = shown_position(kControlsTab);
    // The hints show the buttons the icons do: a chosen style whatever is in hand (the keyboard's: its keys), else what
    // the player last used.
    const ButtonIcons icons = get_runtime_settings().button_icons;
#if defined(__ANDROID__)
    // Touch: no keys or buttons to name.
    if (icons == ButtonIcons::Auto && !galaxy::host::last_input_device()) {
        view.input = 4u;  // NEBULA_SETTINGS_INPUT_TOUCH
    }
#endif
    if (icons == ButtonIcons::Xbox || icons == ButtonIcons::Nintendo || icons == ButtonIcons::PlayStation) {
        view.input = icons == ButtonIcons::Xbox ? 1u : icons == ButtonIcons::Nintendo ? 2u : 3u;
    } else if (const auto device = galaxy::host::last_input_device(); device && icons != ButtonIcons::Keyboard) {
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
    view.page = shown_position(tab);
    if (!view.open) {
        return view;
    }
    const std::lock_guard<std::recursive_mutex> settings_lock(s_native_settings_mutex);
    expire_display_confirmation();
    expire_controls_capture();
    if (tab == kControlsTab) {
        refresh_controls_devices(false);
        keep_controls_selection();
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
    const std::uint32_t shared_targets =
        tab == kControlsTab ? galaxy::input::shared_binding_targets(s_controls_draft.table(s_controls_scheme)) : 0u;
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
            if (row == view.selected) {
                view.help = page[row].help;
                view.help_as_is = (page[row].names & 4u) != 0u;
            }
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
            // Nothing to set up (controller only, none attached): the device row shows none, fixed.
            // The Wii Remote's state is a line to read, nothing to change.
            shown.changes = (info.kind != ControlsRow::Device || s_controls_devices.count != 0u) &&
                            info.kind != ControlsRow::WiimoteStatus;
            if (info.kind == ControlsRow::Device && s_controls_devices.count != 0u) {
                const galaxy::input::DeviceEntry& entry =
                    s_controls_devices.entries[galaxy::input::resolve_device(s_controls_devices, s_controls_device)];
                shown.value = entry.label.data();
                shown.name = true;
                shown.active = entry.active;
            }
            const bool capturing = controls_capturing() && s_capture_target == static_cast<std::size_t>(info.target);
            if (info.kind == ControlsRow::Binding && !capturing) {
                shown.value = menu_bindings(s_controls_scheme,
                                                                    s_controls_draft.table(s_controls_scheme)[info.target]);
            }
            shown.inputs = info.kind == ControlsRow::Binding;
            shown.shared = shown.inputs && (shared_targets & galaxy::input::game_input_bit(info.target)) != 0u;
            if (info.kind == ControlsRow::ModAction) {
                // A mod's action: its name as the mod gave it; its own inputs, else the game control it follows (in
                // the played language already), or the wait for an input.
                const std::vector<galaxy::input::mod_actions::Listed> actions = mod_action_list();
                if (info.action < actions.size()) {
                    const galaxy::input::mod_actions::Listed& action = actions[info.action];
                    std::string_view name = action.name.empty() ? std::string_view(action.id) : std::string_view(action.name);
                    while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.remove_suffix(1);
                    shown.label = mods_ui_text(name, 24u);
                    shown.label_name = true;
                    const galaxy::input::TargetBindings bindings = mod_action_draft(s_controls_scheme, action);
                    if (s_capture_target == kModCaptureBase + info.action) {
                        shown.value = "PRESS AN INPUT";
                    } else if (no_bindings(bindings)) {
                        shown.value = mod_action_follows(action);
                        shown.name = true;
                    } else {
                        shown.value = menu_bindings(s_controls_scheme, bindings);
                        shown.inputs = true;
                    }
                    if (galaxy::modlog::facts_wanted()) {
                        // controls.<mod id> {"row", "action", "scheme", "label", "value", "inputs"} (harness), as shown.
                        std::string json = "{\"row\":" + std::to_string(row) + ",\"action\":";
                        galaxy::modlog::append_json_text(json, action.id);
                        json += ",\"scheme\":\"";
                        json += galaxy::input::control_scheme_id(s_controls_scheme);
                        json += "\",\"label\":";
                        galaxy::modlog::append_json_text(json, shown.label);
                        json += ",\"value\":";
                        galaxy::modlog::append_json_text(json, shown.value);
                        json += shown.inputs ? ",\"inputs\":true}" : ",\"inputs\":false}";
                        std::string& last = s_mod_action_facts[mod_action_key(action)];
                        if (last != json) {
                            last = json;
                            galaxy::modlog::fact("controls." + action.mod_id, json);
                        }
                    }
                    if (row == view.selected) {
                        std::string mod_name = action.mod_id;
                        {
                            const std::lock_guard<std::mutex> mods_lock(s_mods_mutex);
                            if (const galaxy::mods::ModInfo* const mod = mod_by_id_locked(action.mod_id)) mod_name = mod_title(*mod);
                        }
                        view.help = galaxy::modlog::row_words(
                            "Mods.ActionHelp", action.mod_id,
                            {{"mod", mod_name}, {"control", mod_action_control(action)}});
                        view.help_as_is = !view.help.empty();
                    }
                }
            }
            if (info.kind == ControlsRow::Reset) {
                shown.buttons = true;
                shown.value = "RESET";
            } else if (info.kind == ControlsRow::PairJoyCon) {
                shown.buttons = true;
                shown.value = "PAIR";
            } else if (info.kind == ControlsRow::ArrangeTouch) {
                shown.buttons = true;
                shown.value = "ARRANGE";
            } else if (info.kind == ControlsRow::TouchPlaces) {
                shown.buttons = true;
            }
            if (row == view.selected) {
                switch (info.kind) {
                case ControlsRow::TouchSize:
                    view.help = "REMEMBERED FOR EACH SIZE AND TURN OF THE SCREEN. AUTO: FOR THIS SCREEN'S SIZE";
                    break;
                case ControlsRow::TouchOpacity: view.help = "HOW SOLID THE BUTTONS ARE DRAWN"; break;
                case ControlsRow::TouchVibration: view.help = "A SHORT BUZZ WHEN A BUTTON IS PRESSED"; break;
                case ControlsRow::TouchPlaces:
                    view.help = s_touch_draft.reset_places ? "THE BUTTONS GO BACK TO THEIR FIRST PLACES WHEN APPLIED"
                                                           : "PUTS THE BUTTONS BACK WHERE THE GAME PLACES THEM ON THIS SCREEN";
                    break;
                default: break;
                }
            }
        }
        if (tab == kAudioTab && row == kAudioResetRow) {
            shown.buttons = true;
            if (row == view.selected) view.help = "EVERY VOLUME AND THE SOUND OUTPUT BACK TO THEIR DEFAULTS WHEN APPLIED";
        }
        view.rows.push_back(std::move(shown));
    }
    view.selected_button = view.selected < view.rows.size() && view.rows[view.selected].buttons ? selected_settings_button() : 0u;
    for (unsigned row = 0; row < view.rows.size(); ++row) {
        const SliderPlace place = settings_slider(tab, row, page);
        view.rows[row].slider_steps = place.steps;
        view.rows[row].slider_at = place.at;
    }

    const std::uint64_t now_ms = galaxy::steady_ms();
    if (galaxy::host::touch_arranging()) {
        view.status = NativeSettingsStatus::Arrange;
    } else if (s_joycon_pairing) {
        // The halves; a pair made since the screen opened shows a moment, then the screen closes.
        const galaxy::host::JoyConStatus joycons = galaxy::host::joycon_status();
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
    } else if (tab == kModsTab && fill_mod_question_view(view)) {
        // The mod page's ABOUT or REMOVE.
    } else if (s_settings_leave_asking) {
        view.status = NativeSettingsStatus::Leave;
        view.status_number = static_cast<int>(pending_settings_changes());
    } else if (s_mods_restart_ask.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::ModsRestartAsk;
    } else if (s_rt_restart_ask.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::RtRestartAsk;
    } else if (s_diagnostics_ask.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::DiagnosticsAsk;
    } else if (controls_capturing()) {
        view.status = s_controls_scheme == galaxy::input::ControlScheme::Keyboard ? NativeSettingsStatus::CaptureKey
                                                                                   : NativeSettingsStatus::CapturePad;
    } else if (s_controls_message_until_ms > now_ms) {
        view.status = NativeSettingsStatus::Message;
        view.status_text = s_controls_message;
    } else if (const NativeSettingsStatus dlss = dlss_status(now_ms); dlss != NativeSettingsStatus::Help) {
        view.status = dlss;
        fill_vendor_view(view);
    } else if (settings_display_confirming()) {
        const std::uint64_t left_ms = s_display_confirm_deadline_ms > now_ms ? s_display_confirm_deadline_ms - now_ms : 0u;
        view.status = NativeSettingsStatus::KeepDisplay;
        view.status_number = static_cast<int>((left_ms + 999u) / 1000u);
    } else if (s_settings_save.error.load(std::memory_order_acquire)) {
        view.status = NativeSettingsStatus::SaveFailed;
    } else if (const unsigned pending = pending_settings_changes(); pending != 0u) {
        view.status = NativeSettingsStatus::Pending;
        view.status_number = static_cast<int>(pending);
    } else if (s_settings_applied_ms != 0u && now_ms - s_settings_applied_ms < 2500u) {
        view.status = NativeSettingsStatus::Applied;
    } else if (const PipelineProgress pipelines = pipeline_progress(); pipelines.left != 0u) {
        view.status = NativeSettingsStatus::Preparing;
        view.status_number = static_cast<int>(std::uint64_t{pipelines.done} * 100u / (std::uint64_t{pipelines.done} + pipelines.left));
    } else if (GameLanguage running{}; tab == 4u && running_game_language(running) &&
                                       running != galaxy::game_language_resolved(row_runtime.language)) {
        view.status = NativeSettingsStatus::LanguageRestart;
    } else if (tab == 5u && mods_restart_pending()) {
        view.status = NativeSettingsStatus::ModsRestart;
    } else if (tab == 6u && gpu_change_pending(row_render, row_caps)) {
        view.status = NativeSettingsStatus::GpuRestart;
    }
    return view;
}

}  // namespace host_window

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
        s_home_menu.visible.load(std::memory_order_relaxed) ||
        host_window::window_modal_active();
}

bool native_settings_handle_window_key(
    std::uintptr_t virtual_key,
    bool was_down,
    std::uint32_t normalized) {
    {
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        s_handled_key_normalized = normalized;
    }
    return handle_native_settings_key(virtual_key, was_down);
}

bool native_settings_handle_window_text(std::string_view utf8) {
    return s_text_editing.load(std::memory_order_acquire) && type_text(utf8);
}

bool native_settings_text_wanted() noexcept {
    return s_text_editing.load(std::memory_order_acquire) && s_native_settings_visible.load(std::memory_order_relaxed);
}

bool native_settings_pad_key(std::uintptr_t virtual_key) {
    return handle_native_settings_key(virtual_key, false, true);
}

bool native_settings_capture_mouse(galaxy::input::MouseButton button) {
    return native_settings_capture_mouse_button(button);
}

// The wheel moves the selection a row a notch, as the arrow keys, never while a binding waits for its input; false
// while the menu is closed: the game's own screens take it.
bool native_settings_wheel(int delta) {
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return false;
    }
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (controls_capturing()) {
        return true;
    }
    static int s_wheel = 0;
    constexpr int kNotch = 120;
    s_wheel += delta;
    for (; s_wheel >= kNotch; s_wheel -= kNotch) (void)handle_native_settings_key(VK_UP, false);
    for (; s_wheel <= -kNotch; s_wheel += kNotch) (void)handle_native_settings_key(VK_DOWN, false);
    return true;
}

// Dropped files: mod archives and folders into the mods folder (the MODS page opens on the import), anything else to
// the Mii import.
void native_settings_files_dropped(std::vector<std::filesystem::path> files) {
    std::vector<std::filesystem::path> mods;
    std::vector<std::filesystem::path> miis;
    for (std::filesystem::path& path : files) {
        std::error_code error;
        if (std::filesystem::is_directory(path, error) || galaxy::mods::is_mod_archive(path)) {
            mods.push_back(std::move(path));
        } else {
            miis.push_back(std::move(path));
        }
    }
    if (!mods.empty()) {
        run_mod_imports(std::move(mods));
        const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
        s_native_settings_tab.store(kModsTab, std::memory_order_relaxed);
        s_native_settings_row.store(1u, std::memory_order_relaxed);
        s_native_settings_button_row.store(~0u, std::memory_order_relaxed);
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
}

// A size the player drags the window to becomes the windowed size. Saved now, not when the game closes: Nebula Settings
// reads the file meanwhile.
void native_settings_window_resized(unsigned width, unsigned height) {
    RenderConfig render = get_render_config();
    if (render.window_width != width || render.window_height != height) {
        render.window_width = width;
        render.window_height = height;
        set_render_config(render);
        s_settings_save.dirty.store(true, std::memory_order_release);
        if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
            flush_native_settings_if_dirty();
        }
    }
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
    if (!visible || s_settings_leave_asking || s_joycon_pairing || galaxy::host::touch_arranging() || settings_display_confirming() ||
        restart_asking() || vendor_prompting() || mod_question_up()) {
        return;
    }
    switch (input) {
    case NativeSettingsInput::SelectPage: {
        const int current = static_cast<int>(shown_position(s_native_settings_tab.load(std::memory_order_relaxed) % kNativeSettingsTabCount));
        if (arg >= 0 && arg < static_cast<int>(kShownTabCount) && arg != current) {
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

void home_menu_open() noexcept { s_home_menu.open(); }
int home_menu_poll() noexcept { return s_home_menu.poll(); }
void home_menu_close() noexcept { s_home_menu.hide(); }
bool home_menu_visible() noexcept { return s_home_menu.visible.load(std::memory_order_acquire); }
int home_menu_view() noexcept { return s_home_menu.view(); }
void home_menu_point(unsigned answer) noexcept { s_home_menu.point(answer); }
void home_menu_choose(unsigned answer) noexcept { s_home_menu.click(answer); }
std::int32_t take_pointer_wheel() noexcept {
    return host_window::pointer_wheel.exchange(0, std::memory_order_relaxed);
}

}  // namespace galaxy::render
