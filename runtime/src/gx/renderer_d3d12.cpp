#include "galaxy/gx/renderer_d3d12.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/game_asset_extract.h"
#include "galaxy/host/scene_hint.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/native_host.h"

#pragma warning(push, 0)
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace galaxy::gx {

// ---------------------------------------------------------------------------
// Module-level state for things the frozen header can't hold
// ---------------------------------------------------------------------------

namespace {

// Blit root signature — lifetime matches RendererD3D12; stored here because
// the frozen header has no slot for it.
static ComPtr<ID3D12RootSignature> s_blit_root_sig;
// EFB-copy conversion blit root signature (same lifetime pattern).
static ComPtr<ID3D12RootSignature> s_conv_root_sig;
static ComPtr<ID3D12RootSignature> s_clear_root_sig;
// Cached backbuffer dimensions set during initialize.
static UINT s_bb_width  = 1280;
static UINT s_bb_height =  720;

// See set_native_settings_content_root in gx_d3d12.h. The setter itself is
// defined below, outside this anonymous namespace, so it gets the external
// linkage its gx_d3d12.h declaration promises -- everything in here has
// internal linkage by construction, which would otherwise silently produce
// an unresolved-external at link time for any other translation unit.
static std::string s_native_settings_content_root;

static constexpr wchar_t kWindowClass[] = L"NebulaRuntimeWndR";
static constexpr DWORD kWindowStyle =
    WS_OVERLAPPEDWINDOW;

#ifndef GALAXY_WITH_STREAMLINE_BRIDGE
#define GALAXY_WITH_STREAMLINE_BRIDGE 0
#endif
#if GALAXY_WITH_STREAMLINE_BRIDGE != 0 && !defined(GALAXY_STREAMLINE_BRIDGE_READY)
#error "GALAXY_WITH_STREAMLINE_BRIDGE requires the real Streamline bridge to define GALAXY_STREAMLINE_BRIDGE_READY"
#endif

std::atomic<bool> s_pointer_focused{false};
galaxy::host::HostPointerSnapshotCache s_pointer_snapshot;
std::atomic<std::uintptr_t> s_pointer_hwnd{0};
std::atomic<int> s_pointer_last_cursor_poll_x{std::numeric_limits<int>::min()};
std::atomic<int> s_pointer_last_cursor_poll_y{std::numeric_limits<int>::min()};
std::atomic<bool> s_pointer_left_button{false};
std::atomic<bool> s_pointer_right_button{false};
std::atomic<bool> s_pointer_focus_valid{false};
galaxy::host::HostPointerEventRing s_pointer_transitions;
bool s_tracking_mouse_leave = false;
std::atomic<bool> s_system_menu_active{false};
std::atomic<bool> s_move_size_active{false};
std::recursive_mutex s_native_settings_mutex;

std::atomic<bool> s_native_settings_visible{false};
std::atomic<bool> s_native_settings_dirty{false};
std::atomic<bool> s_native_settings_save_error{false};
std::atomic<unsigned> s_native_settings_tab{0};
std::atomic<unsigned> s_native_settings_row{0};
std::atomic<bool> s_quit_requested{false};

// Live window resize (arbitrary resolution/aspect ratio without a restart).
// wnd_proc only records the latest client size here; the actual swap-chain
// resize happens on the render thread at the top of begin_frame(), the one
// place already guaranteed to be between frames with no in-flight command
// list recording. This keeps every D3D12 object mutation on the thread that
// owns them and off the Win32 message thread. Values are the *client area*
// size (matches s_bb_width/height, which the blit viewport already reads
// live every present() call).
// Publish/consume both dimensions together; exchange cannot clear a newer size.
std::atomic<std::uint64_t> s_pending_resize_extent{0};

constexpr unsigned kNativeSettingsTabCount = 4;

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - begin).count());
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

bool trace_renderer_startup_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_RENDERER_STARTUP") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

unsigned raytracing_tier_code(D3D12_RAYTRACING_TIER tier) {
    switch (tier) {
    case D3D12_RAYTRACING_TIER_1_0:
        return 10;
#ifdef D3D12_RAYTRACING_TIER_1_1
    case D3D12_RAYTRACING_TIER_1_1:
        return 11;
#endif
#ifdef D3D12_RAYTRACING_TIER_1_2
    case D3D12_RAYTRACING_TIER_1_2:
        return 12;
#endif
    default:
        return 0;
    }
}

void copy_adapter_description(
    const DXGI_ADAPTER_DESC1& desc,
    std::array<char, 128>& out) {
    out.fill('\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        desc.Description,
        -1,
        out.data(),
        static_cast<int>(out.size()),
        nullptr,
        nullptr);
    if (written <= 0) {
        std::snprintf(out.data(), out.size(), "unknown");
    }
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

float step_gain(float current, int delta) {
    const float snapped = std::round(current * 20.0f) / 20.0f;
    return std::clamp(snapped + static_cast<float>(delta) * 0.05f, 0.0f, 2.0f);
}

unsigned native_settings_row_count(unsigned tab) {
    switch (tab % kNativeSettingsTabCount) {
    case 0: return 2u;
    case 1: return 1u;
    case 2: return 5u;
    case 3: return 5u;
    default: return 0u;
    }
}

void adjust_native_setting(int delta) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const unsigned row = s_native_settings_row.load(std::memory_order_relaxed);
    if (delta == 0) {
        return;
    }

    if (tab == 0u) {
        if (row == 0u) {
            const unsigned current = get_next_start_efb_scale();
            const unsigned next = static_cast<unsigned>(std::clamp(
                static_cast<int>(current) + delta,
                1,
                static_cast<int>(kMaxEfbScale)));
            if (next != current) {
                stage_next_start_efb_scale(next);
                s_native_settings_dirty.store(true, std::memory_order_release);
            }
        }
        return;
    }

    RuntimeSettings settings = get_runtime_settings();
    if (tab == 1u) {
        // Input-source changes require device capability/preflight ownership.
        // Keep the live source visible, but never switch to absent hardware
        // from an in-game key press.
        return;
    }
    if (tab == 2u) {
        bool changed = false;
        switch (row) {
        case 0: {
            const float next = step_gain(settings.audio.master, delta);
            changed = next != settings.audio.master;
            settings.audio.master = next;
            break;
        }
        case 1: {
            const float next = step_gain(settings.audio.music, delta);
            changed = next != settings.audio.music;
            settings.audio.music = next;
            break;
        }
        case 2: {
            const float next = step_gain(settings.audio.sfx, delta);
            changed = next != settings.audio.sfx;
            settings.audio.sfx = next;
            break;
        }
        case 3: {
            const float next = step_gain(settings.audio.voice, delta);
            changed = next != settings.audio.voice;
            settings.audio.voice = next;
            break;
        }
        case 4: {
            const float next = step_gain(settings.audio.ambience, delta);
            changed = next != settings.audio.ambience;
            settings.audio.ambience = next;
            break;
        }
        default:
            break;
        }
        if (changed) {
            set_runtime_settings(settings);
            s_native_settings_dirty.store(true, std::memory_order_release);
        }
    }
}

void move_native_setting_row(int delta) {
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
    if (!visible) {
        flush_native_settings_if_dirty();
    }
    s_native_settings_visible.store(visible, std::memory_order_relaxed);
}

bool handle_native_settings_key(WPARAM key, bool was_down) {
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!native_settings_ui_enabled()) {
        return false;
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
    switch (key) {
    case VK_ESCAPE:
        set_native_settings_visible(false);
        return true;
    case VK_TAB:
        cycle_native_settings_tab(1);
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

bool trace_present_stats_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_PRESENT_STATS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// Textual per-frame probes can block in redirected Windows stderr. Keep the
// independent cadence/Present counters enabled for measurement; request these
// additional messages explicitly when investigating a particular stall.
bool trace_stall_watch_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
                        "GALAXY_TRACE_STALL_WATCH") == 0 &&
               length > 1 && value[0] == '1';
    }();
    return enabled;
}

bool trace_present_luma_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_PRESENT_LUMA") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t trace_present_luma_interval() {
    static const std::uint64_t interval = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_TRACE_PRESENT_LUMA_INTERVAL") != 0 ||
            length <= 1) {
            return std::uint64_t{60};
        }
        return std::clamp<std::uint64_t>(
            std::strtoull(value, nullptr, 10),
            std::uint64_t{1},
            std::uint64_t{3600});
    }();
    return interval;
}

bool trace_present_luma_frame(std::uint64_t frame) {
    return trace_present_luma_enabled() &&
           frame % trace_present_luma_interval() == 0u;
}

bool realtime_vi_paces_present() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value), "GALAXY_REALTIME_VI") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool smooth_realtime_present_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_SMOOTH_REALTIME_PRESENT") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t smooth_realtime_present_max_wait_us() {
    static const std::uint64_t wait_us = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_SMOOTH_REALTIME_PRESENT_MAX_WAIT_US") != 0 ||
            length <= 1) {
            return std::uint64_t{2000};
        }
        return std::clamp<std::uint64_t>(
            std::strtoull(value, nullptr, 10),
            0u,
            8000u);
    }();
    return wait_us;
}

enum class BackbufferPreBlitClearMode {
    Disabled,
    Black,
    Diagnostic,
};

// Truthy values seed the backbuffer with a magenta sentinel that the XFB blit
// should overwrite completely; use "black" when testing a black scrub instead.
BackbufferPreBlitClearMode backbuffer_pre_blit_clear_mode() {
    static const BackbufferPreBlitClearMode mode = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_CLEAR_BACKBUFFER_BEFORE_XFB_BLIT") != 0 ||
            length <= 1) {
            return BackbufferPreBlitClearMode::Disabled;
        }
        for (char& ch : value) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        if (std::strcmp(value, "0") == 0 ||
            std::strcmp(value, "false") == 0 ||
            std::strcmp(value, "no") == 0 ||
            std::strcmp(value, "off") == 0) {
            return BackbufferPreBlitClearMode::Disabled;
        }
        if (std::strcmp(value, "black") == 0) {
            return BackbufferPreBlitClearMode::Black;
        }
        return BackbufferPreBlitClearMode::Diagnostic;
    }();
    return mode;
}

std::uint32_t crc32_update_raw(
    std::uint32_t crc,
    const std::uint8_t* data,
    std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::uint32_t c = static_cast<std::uint32_t>(i);
            for (unsigned bit = 0; bit < 8u; ++bit) {
                c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1u) : c >> 1u;
            }
            values[i] = c;
        }
        return values;
    }();

    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc;
}

bool gpu_timestamps_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GPU_TIMESTAMPS") == 0 &&
            length > 1) {
            return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
        }
        return trace_present_stats_enabled();
    }();
    return enabled;
}

bool allow_tearing_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_ALLOW_TEARING") != 0 ||
            length <= 1) {
            return get_render_config().allow_tearing;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool flip_discard_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_FLIP_DISCARD") != 0 ||
            length <= 1) {
            return get_render_config().flip_discard;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
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

bool fullscreen_window_mode_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_FULLSCREEN") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

bool exclusive_fullscreen_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_EXCLUSIVE_FULLSCREEN") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

bool dred_diagnostics_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_D3D_DRED") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

unsigned swap_chain_frame_latency() {
    static const unsigned latency = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_SWAP_CHAIN_FRAME_LATENCY") != 0 ||
            length <= 1) {
            return std::clamp(
                get_render_config().swap_chain_frame_latency,
                1u,
                kFramesInFlight);
        }
        return std::clamp(
            static_cast<unsigned>(std::strtoul(value, nullptr, 10)),
            1u,
            kFramesInFlight);
    }();
    return latency;
}

bool frame_latency_wait_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_WAIT_FOR_FRAME_LATENCY") != 0 ||
            length <= 1) {
            return get_render_config().wait_for_frame_latency;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
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
        s_pointer_hwnd.load(std::memory_order_relaxed));
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

    // A stationary cursor already inside the game window may not generate
    // WM_MOUSEMOVE after launch, resize, or refocus. The geometry seed above
    // is deliberately inactive, so acquire the real local cursor here and
    // publish it with the same compare-before-publish rule used by the normal
    // first-poll fallback. Without this, the synthetic valid/outside seed can
    // mask native_runtime's independent cursor recovery indefinitely.
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

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        s_pointer_hwnd.store(reinterpret_cast<std::uintptr_t>(hwnd),
                             std::memory_order_release);
    }
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE) {
        (msg == WM_ENTERMENULOOP ? s_system_menu_active : s_move_size_active)
            .store(true, std::memory_order_release);
        galaxy::host::clear_native_window_key_presses();
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
    }
    if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE) {
        (msg == WM_EXITMENULOOP ? s_system_menu_active : s_move_size_active)
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
        if (native_settings_handle_window_key(wp, was_down)) {
            return 0;
        }
        if (!was_down && !galaxy::host::native_keyboard_capture_active()) {
            const UINT key = static_cast<UINT>(wp);
            const UINT mapped_shift = key == VK_SHIFT
                ? MapVirtualKeyA(
                    (static_cast<UINT>(lp) >> 16u) & 0xffu,
                    MAPVK_VSC_TO_VK_EX) : 0u;
            galaxy::host::publish_native_window_key_press(
                galaxy::input::normalize_native_window_key(key, mapped_shift,
                    (static_cast<UINT>(lp) & (1u << 24u)) != 0u));
        }
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
        update_pointer_absolute(hwnd, lp);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
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
                s_pending_resize_extent.store(
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
        s_quit_requested.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        flush_native_settings_if_dirty();
        s_quit_requested.store(true, std::memory_order_release);
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
        s_pointer_hwnd.store(0u, std::memory_order_release);
        s_system_menu_active.store(false, std::memory_order_release);
        s_move_size_active.store(false, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void d3d_check(HRESULT hr, const char* label) {
    if (FAILED(hr)) {
        std::ostringstream ss;
        ss << "[RendererD3D12] " << label << " HRESULT=0x"
           << std::hex << std::uppercase
           << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(hr));
        throw std::runtime_error(ss.str());
    }
}

// Once-per-process device-removal triage: removal reason, DRED auto
// breadcrumbs (last executed op per command list), page-fault VA, and any
// stored debug-layer messages.
static void dump_device_removal(ID3D12Device* dev) {
    if (dev == nullptr) return;
    const HRESULT reason = dev->GetDeviceRemovedReason();
    if (reason == S_OK) return;

    static bool s_dumped = false;
    if (s_dumped) return;
    s_dumped = true;

    std::cerr << "[Renderer] DEVICE REMOVED reason=0x" << std::hex
              << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(reason))
              << std::dec << '\n';

    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dred)))) {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bo{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bo))) {
            int node_idx = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* n = bo.pHeadAutoBreadcrumbNode;
                 n != nullptr && node_idx < 8; n = n->pNext, ++node_idx) {
                const UINT32 last = n->pLastBreadcrumbValue
                    ? *n->pLastBreadcrumbValue : 0u;
                std::cerr << "[DRED] cmdlist "
                          << (n->pCommandListDebugNameA
                                  ? n->pCommandListDebugNameA : "(unnamed)")
                          << " executed " << last << "/" << n->BreadcrumbCount
                          << " ops; ops around stop:";
                const UINT32 from = (last > 6u) ? last - 6u : 0u;
                const UINT32 to = (last + 6u < n->BreadcrumbCount)
                    ? last + 6u : n->BreadcrumbCount;
                for (UINT32 i = from; i < to; ++i) {
                    std::cerr << ' ' << static_cast<unsigned>(n->pCommandHistory[i])
                              << (i + 1u == last ? "<" : "");
                }
                std::cerr << '\n';
            }
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) &&
            pf.PageFaultVA != 0) {
            std::cerr << "[DRED] page fault VA=0x" << std::hex
                      << pf.PageFaultVA << std::dec << '\n';
            const auto dump_allocations = [](
                const char* label,
                const D3D12_DRED_ALLOCATION_NODE* head) {
                int index = 0;
                for (const D3D12_DRED_ALLOCATION_NODE* node = head;
                     node != nullptr && index < 16;
                     node = node->pNext, ++index) {
                    const char* name =
                        node->ObjectNameA != nullptr
                            ? node->ObjectNameA
                            : "(unnamed)";
                    std::cerr << "[DRED] " << label << '[' << index
                              << "] type="
                              << static_cast<unsigned>(node->AllocationType)
                              << " name=" << name << '\n';
                }
                if (head == nullptr) {
                    std::cerr << "[DRED] " << label << " none\n";
                } else if (index == 16) {
                    std::cerr << "[DRED] " << label
                              << " truncated after 16 nodes\n";
                }
            };
            dump_allocations("existing", pf.pHeadExistingAllocationNode);
            dump_allocations("recently-freed", pf.pHeadRecentFreedAllocationNode);
        }
    }

    ComPtr<ID3D12InfoQueue> iq;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&iq)))) {
        // Print only ERROR/CORRUPTION severity — warnings (e.g. clear-value
        // mismatches) flood the tail and bury the actual illegal call.
        const UINT64 count = iq->GetNumStoredMessages();
        int printed = 0;
        for (UINT64 i = 0; i < count && printed < 20; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            if (len == 0) continue;
            std::vector<char> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
                (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                 msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::cerr << "[D3D12-ERR] " << msg->pDescription << '\n';
                ++printed;
            }
        }
        if (printed == 0) {
            std::cerr << "[D3D12-ERR] no ERROR-severity messages stored ("
                      << count << " total)\n";
        }
    }
}

static ComPtr<ID3DBlob> compile_hlsl(
    const char* src, std::size_t len, const char* target, const char* entry) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(
        src, len, nullptr, nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (FAILED(hr)) {
        const char* msg = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "(no error blob)";
        throw std::runtime_error(std::string("D3DCompile(") + target + "): " + msg);
    }
    return code;
}

// Fullscreen triangle with UV — no vertex buffer needed.
static constexpr char kBlitVs[] =
    "struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };"
    "V main(uint id:SV_VertexID){"
    " V o;"
    " o.uv = float2(id==1?2:0, id==2?2:0);"
    " o.pos = float4(o.uv.x*2-1, 1-o.uv.y*2, 0.5, 1);"
    " return o;"
    "}";

static constexpr char kBlitPs[] =
    "Texture2D t:register(t0);"
    "SamplerState s:register(s0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float4 main(V v):SV_Target{return t.Sample(s,v.uv);}";

// Native settings overlay corner decoration: one small textured quad in
// screen-pixel coordinates. Root constants (b0, all-stage-visible):
//   rect      = x, y, w, h in backbuffer pixels
//   uv_minmax = u0, v0, u1, v1 (lets one square corner texture be mirrored
//               per panel corner instead of needing four textures)
//   tint      = RGBA multiplier (alpha also modulates the overlay's own
//               translucency)
//   screen    = backbuffer width, height (for the pixel->NDC divide)
static constexpr char kUiTexturedQuadVs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 uv_minmax;
    float4 tint;
    float2 screen;
};
struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
V main(uint id : SV_VertexID) {
    V o;
    float2 corner = float2(float(id & 1u), float((id >> 1u) & 1u));
    float2 px = rect.xy + corner * rect.zw;
    float2 ndc = float2(px.x / screen.x * 2.0 - 1.0, 1.0 - px.y / screen.y * 2.0);
    o.pos = float4(ndc, 0.5, 1.0);
    o.uv = lerp(uv_minmax.xy, uv_minmax.zw, corner);
    return o;
}
)HLSL";

static constexpr char kUiTexturedQuadPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 uv_minmax;
    float4 tint;
    float2 screen;
};
Texture2D t : register(t0);
SamplerState s : register(s0);
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
float4 main(V v) : SV_Target { return t.Sample(s, v.uv) * tint; }
)HLSL";

// Flat-colored quad, draw-call based. Used by the native settings overlay's
// panel background and text glyphs instead of a rect-limited
// ClearRenderTargetView -- see ensure_native_settings_solid_pipeline_loaded.
static constexpr char kUiSolidQuadVs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 color;
    float2 screen;
};
struct V { float4 pos:SV_Position; };
V main(uint id : SV_VertexID) {
    V o;
    float2 corner = float2(float(id & 1u), float((id >> 1u) & 1u));
    float2 px = rect.xy + corner * rect.zw;
    float2 ndc = float2(px.x / screen.x * 2.0 - 1.0, 1.0 - px.y / screen.y * 2.0);
    o.pos = float4(ndc, 0.5, 1.0);
    return o;
}
)HLSL";

static constexpr char kUiSolidQuadPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 color;
    float2 screen;
};
float4 main() : SV_Target { return color; }
)HLSL";

static constexpr char kClearPs[] =
    "cbuffer C:register(b0){float4 clear_color;};"
    "float4 main():SV_Target{return clear_color;}";

// EFB-copy conversion pixel shader. Hardware-verified operation order is:
// quantize source samples to bytes -> integer 3-row filter -> gamma ->
// optional RGB-to-YUV conversion -> destination-format channel mapping.
// The filter applies to color and packed depth RGB alike; alpha is always the
// unfiltered center sample. Our depth buffer is reversed, so guest z24 is
// reconstructed from 1 - sampled depth before filtering its three bytes.
//
// Root constants (b0):
//   src_xform = src origin.xy, src texels per dest pixel .zw (scaled EFB px)
//   modes     = intensity, gamma, is_depth, keep_alpha
//   filt      = integer filter upper/middle/lower (1/64 units), clamp_y min
//   filt2     = clamp_y max, decoded EFB-copy format (15 = XFB), scaled
//               one-logical-row filter offset, EFB-peek reduction width
static constexpr char kConvPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    float4 modes;
    float4 filt;
    float4 filt2;
};
Texture2D srctex : register(t0);
SamplerState s_lin : register(s0);
SamplerState s_pt  : register(s1);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
uint4 pack_depth(float depth_sample) {
    uint z24 = uint(clamp(
        (1.0 - depth_sample) * 16777216.0, 0.0, 16777215.0));
    return uint4(
        (z24 >> 16) & 255u,
        (z24 >> 8) & 255u,
        z24 & 255u,
        255u);
}
uint4 sample_efb(float2 sample_pos, float2 inv) {
    float4 tex_sample = srctex.SampleLevel(s_lin, sample_pos * inv, 0);
    if (modes.z != 0.0) {
        return pack_depth(tex_sample.r);
    }
    return uint4(saturate(tex_sample) * 255.0);
}
float4 main(V v) : SV_Target {
    float2 dims;
    srctex.GetDimensions(dims.x, dims.y);
    float2 inv = 1.0 / dims;
    float2 sp = src_xform.xy + v.pos.xy * src_xform.zw;
    uint reduction_width = filt2.w < 0.0 ? 1u : uint(filt2.w + 0.5);
    uint4 texcol_raw;
    if (reduction_width != 0u || filt2.w < 0.0) {
        if (modes.z != 0.0) {
            // Depth peeks are point reads. src_xform.xy is the exact physical
            // texel selected by the CPU geometry policy; Load prevents the
            // multi-texel blend that linear sampling produced at scaled EFB sizes.
            texcol_raw = pack_depth(
                srctex.Load(int3(
                    filt2.w < 0.0 ? int2(src_xform.xy + floor(v.pos.xy) * src_xform.zw)
                                 : int2(src_xform.xy), 0)).r);
        } else {
            // Color peeks reduce the complete scaled logical-pixel block.
            float4 sum = 0.0;
            int2 origin = int2(src_xform.xy);
            [unroll] for (uint py = 0u; py < 6u; ++py) {
                [unroll] for (uint px = 0u; px < 6u; ++px) {
                    if (px < reduction_width && py < reduction_width) {
                        sum += srctex.Load(int3(origin + int2(px, py), 0));
                    }
                }
            }
            float4 reduced = sum / float(reduction_width * reduction_width);
            texcol_raw = uint4(round(saturate(reduced) * 255.0));
        }
    } else {
        float ylo = filt.w, yhi = filt2.x;
        float filter_offset = max(filt2.z, 1.0);
        uint4 prev_row = sample_efb(
            float2(sp.x, clamp(sp.y - filter_offset, ylo, yhi)), inv);
        uint4 current_row = sample_efb(
            float2(sp.x, clamp(sp.y, ylo, yhi)), inv);
        uint4 next_row = sample_efb(
            float2(sp.x, clamp(sp.y + filter_offset, ylo, yhi)), inv);
        uint3 coefficients = uint3(filt.xyz + 0.5);
        uint3 combined_rows =
            prev_row.rgb * coefficients.x +
            current_row.rgb * coefficients.y +
            next_row.rgb * coefficients.z;
        texcol_raw = uint4(combined_rows >> 6, current_row.a);
        if (coefficients.x + coefficients.y + coefficients.z >= 128u) {
            texcol_raw &= 0x1ffu;
        }
        texcol_raw = min(texcol_raw, uint4(255u, 255u, 255u, 255u));
    }
    if (modes.y != 1.0) {
        texcol_raw = uint4(round(
            pow(abs(float4(texcol_raw) / 255.0),
                float4(1.0 / modes.y, 1.0 / modes.y, 1.0 / modes.y, 1.0)) *
            255.0));
    }
    if (modes.x != 0.0) {
        int3 rgb = int3(texcol_raw.rgb);
        uint3 yuv = uint3(
            66 * rgb.r + 129 * rgb.g + 25 * rgb.b + 4096,
            -38 * rgb.r - 74 * rgb.g + 112 * rgb.b + 32768,
            112 * rgb.r - 94 * rgb.g - 18 * rgb.b + 32768);
        texcol_raw.rgb = (yuv >> 8) + ((yuv >> 7) & 1u);
    }

    // The GPU alias represents the texture after the EFB copy bytes are
    // decoded again, not the unencoded render target. Apply the copy format's
    // channel/precision behavior here so direct aliases match a RAM roundtrip.
    uint fmt = uint(filt2.y + 0.5);
    if (fmt == 0u) {             // R4 -> I4
        uint i = (texcol_raw.r & 0xF0u) | (texcol_raw.r >> 4);
        texcol_raw = uint4(i, i, i, i);
    } else if (fmt == 1u || fmt == 8u) { // R8 -> I8
        texcol_raw = texcol_raw.rrrr;
    } else if (fmt == 2u) {      // RA4 -> IA4
        uint i = (texcol_raw.r & 0xF0u) | (texcol_raw.r >> 4);
        uint a = (texcol_raw.a & 0xF0u) | (texcol_raw.a >> 4);
        texcol_raw = uint4(i, i, i, a);
    } else if (fmt == 3u) {      // RA8 -> IA8
        texcol_raw = texcol_raw.rrra;
    } else if (fmt == 4u) {      // RGB565
        texcol_raw.r = (texcol_raw.r & 0xF8u) | (texcol_raw.r >> 5);
        texcol_raw.g = (texcol_raw.g & 0xFCu) | (texcol_raw.g >> 6);
        texcol_raw.b = (texcol_raw.b & 0xF8u) | (texcol_raw.b >> 5);
        texcol_raw.a = 255u;
    } else if (fmt == 5u) {      // RGB5A3
        if (texcol_raw.a > 224u) {
            texcol_raw.r = (texcol_raw.r & 0xF8u) | (texcol_raw.r >> 5);
            texcol_raw.g = (texcol_raw.g & 0xF8u) | (texcol_raw.g >> 5);
            texcol_raw.b = (texcol_raw.b & 0xF8u) | (texcol_raw.b >> 5);
            texcol_raw.a = 255u;
        } else {
            texcol_raw.r = (texcol_raw.r & 0xF0u) | (texcol_raw.r >> 4);
            texcol_raw.g = (texcol_raw.g & 0xF0u) | (texcol_raw.g >> 4);
            texcol_raw.b = (texcol_raw.b & 0xF0u) | (texcol_raw.b >> 4);
            texcol_raw.a =
                (texcol_raw.a & 0xE0u) | (texcol_raw.a >> 3) |
                (texcol_raw.a >> 6);
        }
    } else if (fmt == 7u) {      // A8 bytes are loaded as I8
        texcol_raw = texcol_raw.aaaa;
    } else if (fmt == 9u) {      // G8 -> I8
        texcol_raw = texcol_raw.gggg;
    } else if (fmt == 10u) {     // B8 -> I8
        texcol_raw = texcol_raw.bbbb;
    } else if (fmt == 11u) {     // RG8 -> IA8
        texcol_raw = texcol_raw.rrrg;
    } else if (fmt == 12u) {     // GB8 -> IA8
        texcol_raw = texcol_raw.gggb;
    } else if (fmt == 15u) {     // XFB: full RGB precision, opaque alpha
        texcol_raw.a = 255u;
    }
    return float4(texcol_raw) / 255.0;
}
)HLSL";

}  // namespace

void set_native_settings_content_root(const std::string& utf8_path) {
    s_native_settings_content_root = utf8_path;
}

bool validate_efb_conversion_shader(std::string& diagnostics) noexcept {
    try {
        const ComPtr<ID3DBlob> code = compile_hlsl(
            kConvPs, sizeof(kConvPs) - 1u, "ps_5_0", "main");
        diagnostics.clear();
        return code != nullptr;
    } catch (const std::exception& error) {
        diagnostics = error.what();
        return false;
    } catch (...) {
        diagnostics = "unknown D3DCompile failure";
        return false;
    }
}

std::string_view efb_conversion_shader_source() noexcept {
    return std::string_view{kConvPs, sizeof(kConvPs) - 1u};
}

EfbConversionShaderConstants make_efb_conversion_constants(
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset,
    unsigned efb_scale,
    unsigned logical_efb_height,
    unsigned reduction_width) noexcept {
    const float scale = static_cast<float>(
        std::clamp(efb_scale, 1u, kMaxEfbScale));
    float fw_up = static_cast<float>(params.filter_upper());
    float fw_mid = static_cast<float>(params.filter_middle());
    float fw_low = static_cast<float>(params.filter_lower());
    if (fw_up == 0.0f && fw_mid == 0.0f && fw_low == 0.0f) {
        // Preserve the existing startup behavior when GXInit has not yet
        // programmed BP 0x53/0x54.
        fw_mid = 64.0f;
    }

    const float copy_y0 = static_cast<float>(params.src_y) * scale;
    const float copy_y1 =
        copy_y0 + static_cast<float>(params.src_height) * scale;
    const float full_y1 =
        static_cast<float>(std::max(logical_efb_height, 1u)) * scale;
    const float clamp_y_min =
        (params.clamp_top ? copy_y0 : 0.0f) + 0.5f;
    const float clamp_y_max = std::max(
        clamp_y_min,
        (params.clamp_bottom ? copy_y1 : full_y1) - 0.5f);
    static constexpr float kGammaLut[4] = {1.0f, 1.7f, 2.2f, 2.2f};

    return EfbConversionShaderConstants{{
        src_x, src_y, step_x, step_y,
        intensity ? 1.0f : 0.0f,
        kGammaLut[params.gamma & 3u],
        is_depth_copy ? 1.0f : 0.0f,
        keep_alpha ? 1.0f : 0.0f,
        fw_up, fw_mid, fw_low, clamp_y_min,
        clamp_y_max,
        static_cast<float>(copy_format),
        filter_row_offset,
        static_cast<float>(reduction_width),
    }};
}

EfbConversionShaderConstants make_efb_peek_conversion_constants(
    const EfbPeekSamplingGeometry& sampling,
    EfbPeekKind kind) noexcept {
    const bool is_depth = kind == EfbPeekKind::Depth;
    const float src_x = is_depth
        ? static_cast<float>(sampling.depth_texel_x)
        : sampling.src_x;
    const float src_y = is_depth
        ? static_cast<float>(sampling.depth_texel_y)
        : sampling.src_y;
    return EfbConversionShaderConstants{{
        src_x,
        src_y,
        sampling.step,
        sampling.step,
        0.0f,
        1.0f,
        is_depth ? 1.0f : 0.0f,
        1.0f,
        0.0f,
        64.0f,
        0.0f,
        sampling.center_y,
        sampling.center_y,
        255.0f,
        sampling.step,
        sampling.step,
    }};
}

// ---------------------------------------------------------------------------
// UploadRing
// ---------------------------------------------------------------------------

bool UploadRing::initialize(
    ID3D12Device* device, const char* name, std::size_t bytes_per_frame,
    unsigned frames_in_flight) {
    name_             = name;
    bytes_per_frame_  = bytes_per_frame;
    frames_in_flight_ = frames_in_flight;

    const std::size_t total = bytes_per_frame * frames_in_flight;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = static_cast<UINT64>(total);
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&buffer_))))
        return false;
    D3D12_RANGE no_read{0, 0};
    void* ptr = nullptr;
    if (FAILED(buffer_->Map(0, &no_read, &ptr))) return false;
    mapped_ = static_cast<std::byte*>(ptr);
    static std::atomic<std::uint64_t> next_identity{1};
    resource_identity_ = next_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_.assign(frames_in_flight, 0);
    return true;
}

void UploadRing::begin_frame(unsigned frame_slot) {
    if (frame_slot >= frames_in_flight_) {
        throw std::runtime_error("UploadRing frame slot is out of range");
    }
    active_slot_ = frame_slot;
    segment_allocation_pending_ = true;
    reused_bytes_ = 0;
    copied_bytes_ = 0;
    segment_base_ = static_cast<std::size_t>(frame_slot) * bytes_per_frame_;
    cursor_       = segment_base_;
}

UploadRing::Allocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    const std::size_t aligned = (cursor_ + alignment - 1) & ~(alignment - 1);
    const std::size_t end     = aligned + size;
    if (end > segment_base_ + bytes_per_frame_)
        throw std::runtime_error(
            std::string("UploadRing '") + name_ + "' exhausted: request " +
            std::to_string(size) + " B at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(bytes_per_frame_) + " B");
    cursor_ = end;
    // Fence-only chunks reset the frame slot but never touch vertex bytes.
    // Count only segment uses that can actually replace their contents.
    if (size != 0 && segment_allocation_pending_) {
        ++slot_generations_[active_slot_];
        segment_allocation_pending_ = false;
    }
    return Allocation{
        mapped_ + aligned,
        buffer_->GetGPUVirtualAddress() + static_cast<UINT64>(aligned),
        aligned
    };
}

UploadRing::Allocation UploadRing::upload_immutable(
    const void* source, std::size_t size, std::size_t alignment,
    ImmutableUploadToken& token) {
    const Allocation result = allocate(size, alignment);
    if (token.slots.size() != frames_in_flight_) {
        token.slots.assign(frames_in_flight_, {});
    }
    auto& previous = token.slots[active_slot_];
    const std::uint64_t generation = slot_generations_[active_slot_];
    // Monotonic allocation prevents earlier writes in this segment from
    // overlapping this allocation. Requiring the previous segment generation
    // excludes intervening frames that might have overwritten the old bytes.
    const bool reuse = generation > 1 &&
        previous.resource_identity == resource_identity_ &&
        previous.generation == generation - 1 &&
        previous.offset == result.offset && previous.size == size;
    if (reuse) {
        reused_bytes_ += size;
    } else {
        std::memcpy(result.cpu, source, size);
        copied_bytes_ += size;
    }
    previous = {resource_identity_, generation, result.offset, size};
    return result;
}

// ---------------------------------------------------------------------------
// DescriptorRing
// ---------------------------------------------------------------------------

bool DescriptorRing::initialize(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    unsigned descriptors_per_frame,
    unsigned frames_in_flight,
    unsigned persistent_descriptors) {
    persistent_descriptors_ = persistent_descriptors;
    persistent_cursor_      = 0;
    descriptors_per_frame_ = descriptors_per_frame;
    frames_in_flight_      = frames_in_flight;
    increment_             = device->GetDescriptorHandleIncrementSize(type);

    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type;
    dhd.NumDescriptors =
        persistent_descriptors + descriptors_per_frame * frames_in_flight;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    return SUCCEEDED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap_)));
}

void DescriptorRing::begin_frame(unsigned frame_slot) {
    segment_base_ = persistent_descriptors_ +
        frame_slot * descriptors_per_frame_;
    cursor_       = segment_base_;
}

DescriptorRing::Table DescriptorRing::allocate_persistent(unsigned count) {
    if (persistent_cursor_ + count > persistent_descriptors_) {
        throw std::runtime_error(
            "DescriptorRing persistent region exhausted: request " +
            std::to_string(count) + " descriptors at used " +
            std::to_string(persistent_cursor_) + "/" +
            std::to_string(persistent_descriptors_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap_->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(persistent_cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>(persistent_cursor_) * increment_;
    persistent_cursor_ += count;
    return Table{cpu, gpu};
}

DescriptorRing::Table DescriptorRing::allocate(unsigned count) {
    if (cursor_ + count > segment_base_ + descriptors_per_frame_) {
        throw std::runtime_error(
            "DescriptorRing exhausted: request " + std::to_string(count) +
            " descriptors at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(descriptors_per_frame_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap_->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>  (cursor_) * increment_;
    cursor_ += count;
    return Table{cpu, gpu};
}

// ---------------------------------------------------------------------------
// RendererD3D12 helpers
// ---------------------------------------------------------------------------

static constexpr std::size_t kVbPerFrame  = 64u * 1024u * 1024u;
static constexpr std::size_t kIbPerFrame  = 16u * 1024u * 1024u;
static constexpr std::size_t kCbPerFrame  =  8u * 1024u * 1024u;
// 32 MiB: even with the snapshot-only-on-matrix-change fix, a busy galaxy
// frame legitimately re-loads the XF palette many times (per-object / skinned
// matrices via LOAD_INDX); headroom prevents the ring-exhaustion crash that
// appeared once the scene actually started rendering.
static constexpr std::size_t kMtxPerFrame = 32u * 1024u * 1024u;
static constexpr unsigned    kSrvPerFrame = 32768u;
static constexpr unsigned    kPersistentSrvDescriptors = 131072u;

void RendererD3D12::wait_for_gpu() {
    const std::uint64_t val = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), val);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    if (fence_->GetCompletedValue() < val) {
        const HRESULT event_hr =
            fence_->SetEventOnCompletion(val, fence_event_);
        if (FAILED(event_hr)) {
            dump_device_removal(device_.Get());
            d3d_check(event_hr, "SetEventOnCompletion");
        }
        const DWORD wait_result =
            WaitForSingleObjectEx(fence_event_, 30000, FALSE);
        if (wait_result != WAIT_OBJECT_0) {
            dump_device_removal(device_.Get());
            throw std::runtime_error("GPU fence wait timed out");
        }
    }
}

bool RendererD3D12::create_efb_targets(unsigned efb_scale) {
    // Aurora MIT lib/window.cpp and lib/gx/gx.cpp keep logical Wii size
    // separate from the physical target. Apply that rule to Galaxy's D3D12
    // resources without changing guest EFB/XFB addresses or dimensions.
    const std::uint64_t width =
        static_cast<std::uint64_t>(kEfbWidth) * efb_scale;
    const std::uint64_t height =
        static_cast<std::uint64_t>(kEfbHeight) * efb_scale;
    if (efb_scale < 1u || efb_scale > kMaxEfbScale ||
        width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        width > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max()) ||
        height > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max())) {
        std::cerr << "[Renderer] invalid physical EFB extent\n";
        return false;
    }
    const UINT w = static_cast<UINT>(width);
    const UINT efb_h = static_cast<UINT>(height);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Color RTV heap: kSwapChainBuffers backbuffers + 1 EFB slot.
    // The heap was already created in initialize() for backbuffers; the EFB RTV
    // sits in the extra slot at index kSwapChainBuffers.
    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = w;
        rd.Height             = efb_h;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = kEfbColorFormat;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format   = kEfbColorFormat;
        cv.Color[3] = 1.0f;
        d3d_check(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
            IID_PPV_ARGS(&efb_color_)), "EFB color create");
    }

    // DSV heap for EFB depth.
    {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap_)),
                  "DSV heap");

        D3D12_RESOURCE_DESC drd{};
        drd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        drd.Width              = w;
        drd.Height             = efb_h;
        drd.DepthOrArraySize   = 1;
        drd.MipLevels          = 1;
        drd.Format             = kEfbDepthResourceFormat;
        drd.SampleDesc.Count   = 1;
        drd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE dcv{};
        dcv.Format               = kEfbDepthFormat;
        dcv.DepthStencil.Depth   = 1.0f;
        d3d_check(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &drd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv,
            IID_PPV_ARGS(&efb_depth_)), "EFB depth create");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = kEfbDepthFormat;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(
            efb_depth_.Get(), &dsv,
            dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    }

    // Add EFB RTV at slot kSwapChainBuffers of the backbuffer RTV heap.
    {
        const UINT stride = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE h =
            rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
        device_->CreateRenderTargetView(efb_color_.Get(), nullptr, h);
    }
    return true;
}

HRESULT create_gx_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept {
    if (device == nullptr || root_signature == nullptr) {
        return E_INVALIDARG;
    }
    *root_signature = nullptr;

    // GX root signature:
    //   [0] Root CBV b0,space0  VS+GS — GxVsConstants
    //   [1] Root CBV b1,space0  PS — GxPsConstants
    //   [2] Descriptor table   SRV t0..t7,space0  PS — textures
    //   [3] Descriptor table   Sampler s0..s7       PS
    //   [4] Root SRV  t0,space1 VS — matrix palette

    D3D12_DESCRIPTOR_RANGE tex_range{};
    tex_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_range.NumDescriptors     = 8;
    tex_range.BaseShaderRegister = 0;
    tex_range.RegisterSpace      = 0;
    tex_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE samp_range{};
    samp_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samp_range.NumDescriptors     = 8;
    samp_range.BaseShaderRegister = 0;
    samp_range.RegisterSpace      = 0;
    samp_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &tex_range;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges   = &samp_range;
    params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    params[4].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor.ShaderRegister = 0;
    params[4].Descriptor.RegisterSpace  = 1;
    params[4].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 5;
    rsd.pParameters   = params;
    rsd.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> blob, err;
    const HRESULT serialize_hr = D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(serialize_hr)) {
        return serialize_hr;
    }
    return device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(root_signature));
}

EfbClearValues make_efb_clear_values(std::uint32_t ar, std::uint32_t gb,
    std::uint32_t z24, std::uint8_t pixel_format) noexcept {
    std::uint32_t r = ar & 255u, g = (gb >> 8u) & 255u;
    std::uint32_t b = gb & 255u, a = (ar >> 8u) & 255u;
    const auto expand6 = [](std::uint32_t v) { return (v & 0xfcu) | (v >> 6u); };
    const auto expand5 = [](std::uint32_t v) { return (v & 0xf8u) | (v >> 5u); };
    if (pixel_format == 1u) {
        r = expand6(r); g = expand6(g); b = expand6(b); a = expand6(a);
    } else {
        if (pixel_format == 2u) { r = expand5(r); g = expand6(g); b = expand5(b); }
        a = 255u;
    }
    z24 &= 0xffffffu;
    if (pixel_format == 2u) z24 = (z24 & 0xffff00u) | (z24 >> 16u);
    return {{{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
              static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f}},
            1.0f - static_cast<float>(z24) / 16777216.0f};
}

bool RendererD3D12::create_root_signature() {
    d3d_check(
        create_gx_root_signature(
            device_.Get(), root_signature_.ReleaseAndGetAddressOf()),
        "Create GX root signature");
    return true;
}

// Each frame slot owns a sampler heap. Slots 0-7 hold its default table;
// dynamic tables are reclaimed only after that slot's submission fence.

D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    std::uint32_t key,
    const RenderConfig& cfg) noexcept {
    auto to_d3d_wrap = [](std::uint32_t gx) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (gx) {
        case 0:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case 1:  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case 2:  return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }
    };

    const float cfg_bias =
        static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy =
        std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    const std::uint32_t wrap_s = key & 0x3u;
    const std::uint32_t wrap_t = (key >> 2) & 0x3u;
    const bool mag_linear = ((key >> 4) & 0x1u) != 0u;
    const std::uint32_t minf = (key >> 5) & 0x7u;
    const float lod_bias = static_cast<float>(
        static_cast<std::int8_t>((key >> 8) & 0xFFu)) / 32.0f;
    const float min_lod =
        static_cast<float>((key >> 16) & 0xFFu) / 16.0f;
    const float max_lod =
        static_cast<float>((key >> 24) & 0xFFu) / 16.0f;

    // TexMinFilter: 0 Near, 1 NearMipNear, 2 NearMipLinear,
    //               4 Linear, 5 LinearMipNear, 6 LinearMipLinear.
    const bool min_linear = (minf >= 4u);
    const unsigned mip_mode = minf & 0x3u;  // 0 none, 1 point, 2 linear
    const D3D12_FILTER_TYPE ft_min = min_linear
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const D3D12_FILTER_TYPE ft_mag = mag_linear
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const D3D12_FILTER_TYPE ft_mip = (mip_mode == 2u)
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;

    D3D12_SAMPLER_DESC desc{};
    const bool use_anisotropic =
        anisotropy > 1u && mip_mode != 0u && min_linear && mag_linear;
    desc.Filter = use_anisotropic
        ? D3D12_FILTER_ANISOTROPIC
        : D3D12_ENCODE_BASIC_FILTER(
              ft_min, ft_mag, ft_mip, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.AddressU = to_d3d_wrap(wrap_s);
    desc.AddressV = to_d3d_wrap(wrap_t);
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MipLODBias = lod_bias + cfg_bias;
    desc.MaxAnisotropy = use_anisotropic ? anisotropy : 1u;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    if (mip_mode == 0u) {
        desc.MinLOD = 0.0f;
        desc.MaxLOD = 0.0f;
    } else {
        desc.MinLOD = min_lod;
        desc.MaxLOD = (max_lod > min_lod) ? max_lod : min_lod;
    }
    return desc;
}

std::size_t SamplerTableKeyHasher::operator()(const SamplerTableKey& key) const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](std::uint32_t value) {
        for (unsigned byte = 0u; byte < 4u; ++byte) {
            hash ^= (value >> (byte * 8u)) & 0xffu;
            hash *= 0x100000001B3ull;
        }
    };
    for (const auto sampler : key.samplers) mix(sampler);
    mix(key.config);
    return static_cast<std::size_t>(hash);
}

SamplerTableKey make_sampler_table_key(const std::uint32_t keys[8], const RenderConfig& cfg) noexcept {
    SamplerTableKey key{};
    std::copy_n(keys, key.samplers.size(), key.samplers.begin());
    key.config = ((std::clamp(cfg.anisotropic_filtering, 1u, 16u) & 0x1fu) << 8u) |
        (std::clamp(cfg.texture_lod_bias, 0u, 8u) & 0xfu);
    return key;
}

SamplerTableCache::Allocation SamplerTableCache::get_or_allocate(const SamplerTableKey& key) {
    if (const auto it = tables_.find(key); it != tables_.end()) return {it->second, false};
    if (cursor_ + 8u > kSamplerHeapSlots) {
        throw std::runtime_error("[Renderer] sampler heap exhausted within one frame");
    }
    const auto [it, inserted] = tables_.emplace(key, cursor_);
    if (inserted) cursor_ += 8u;
    return {it->second, inserted};
}

bool RendererD3D12::create_sampler_heap() {
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    dhd.NumDescriptors = kSamplerHeapSlots;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    for (auto& heap : sampler_heaps_) {
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)),
                  "CreateDescriptorHeap Sampler");
    }
    reset_sampler_frame();
    return true;
}

void RendererD3D12::reset_sampler_frame() {
    sampler_heap_ = sampler_heaps_[frame_slot_];
    sampler_tables_.reset();

    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE base =
        sampler_heap_->GetCPUDescriptorHandleForHeapStart();

    const RenderConfig cfg = get_render_config();
    const float lod_bias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy =
        std::clamp(cfg.anisotropic_filtering, 1u, 16u);

    // Default s0-s7 fallback table at slots 0-7.
    D3D12_SAMPLER_DESC sd{};
    sd.Filter         = anisotropy > 1u
        ? D3D12_FILTER_ANISOTROPIC
        : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sd.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MipLODBias     = lod_bias;
    sd.MaxAnisotropy  = anisotropy;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd.MinLOD         = 0.0f;
    sd.MaxLOD         = D3D12_FLOAT32_MAX;
    for (unsigned i = 0; i < 8; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = base;
        h.ptr += static_cast<SIZE_T>(i) * stride;
        device_->CreateSampler(&sd, h);
    }
    default_sampler_table_ =
        sampler_heap_->GetGPUDescriptorHandleForHeapStart();
}

// Packed sampler key layout (must match pack_sampler_key in gx_backend.cpp):
//   bits 0-1  wrap_s (GX TexWrap)        bits 2-3  wrap_t
//   bit  4    mag linear                 bits 5-7  min filter (TexMinFilter)
//   bits 8-15  lod_bias  (s8, 1/32 LOD units)
//   bits 16-23 min_lod_x16
//   bits 24-31 max_lod_x16
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::sampler_table_for(
    const std::uint32_t keys[8]) {
    const RenderConfig cfg = get_render_config();
    const auto allocation = sampler_tables_.get_or_allocate(make_sampler_table_key(keys, cfg));
    const unsigned table_base = allocation.index;
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (unsigned i = 0; allocation.created && i < 8; ++i) {
        const D3D12_SAMPLER_DESC dyn =
            build_sampler_desc_from_key(keys[i], cfg);

        D3D12_CPU_DESCRIPTOR_HANDLE h =
            sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(table_base + i) * stride;
        device_->CreateSampler(&dyn, h);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(table_base) * stride;

    return gpu;
}

// ---------------------------------------------------------------------------
// EFB-copy conversion pipeline: root signature (16 root constants + one SRV
// table + static linear/point clamp samplers) and the blit PSO.
// ---------------------------------------------------------------------------

bool RendererD3D12::create_conversion_pipeline() {
    // Root signature.
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors     = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.Num32BitValues = 16;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ss[2]{};
        ss[0].Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        ss[0].AddressU = ss[0].AddressV = ss[0].AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[0].ShaderRegister   = 0;
        ss[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ss[1] = ss[0];
        ss[1].Filter         = D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[1].ShaderRegister = 1;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 2;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 2;
        rsd.pStaticSamplers   = ss;

        ComPtr<ID3DBlob> blob, err;
        d3d_check(D3D12SerializeRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
            "SerializeConvRS");
        d3d_check(device_->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&s_conv_root_sig)),
            "CreateConvRS");
    }

    // PSO (fullscreen triangle, no depth, RGBA8 target).
    {
        const ComPtr<ID3DBlob> vs =
            compile_hlsl(kBlitVs, sizeof(kBlitVs) - 1, "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(kConvPs, sizeof(kConvPs) - 1, "ps_5_1", "main");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = s_conv_root_sig.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable   = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kEfbColorFormat;
        pd.SampleDesc.Count      = 1;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&conv_pipeline_)), "CreateConvPSO");
    }

    // CPU-only RTV slot reused after each conversion bind is recorded.
    {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1u;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(
            &dhd, IID_PPV_ARGS(&conv_rtv_heap_)), "ConvRTV heap");
        conv_scratch_rtv_ =
            conv_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    }

    // CPU-only cached EFB source SRVs.  Each conversion copy copies one of
    // these into the frame's shader-visible SRV ring.
    {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 2;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d3d_check(device_->CreateDescriptorHeap(
            &dhd, IID_PPV_ARGS(&conv_srv_heap_)), "ConvSRV heap");
        const UINT stride =
            device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        conv_efb_color_srv_ =
            conv_srv_heap_->GetCPUDescriptorHandleForHeapStart();
        conv_efb_depth_srv_ = conv_efb_color_srv_;
        conv_efb_depth_srv_.ptr += stride;

        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels     = 1;
        sd.Format                  = kEfbColorFormat;
        device_->CreateShaderResourceView(
            efb_color_.Get(), &sd, conv_efb_color_srv_);
        sd.Format = kEfbDepthSrvFormat;
        device_->CreateShaderResourceView(
            efb_depth_.Get(), &sd, conv_efb_depth_srv_);
    }
    return true;
}

bool RendererD3D12::create_clear_pipeline() {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = 4;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 1;
    rsd.pParameters = &param;

    ComPtr<ID3DBlob> blob, err;
    d3d_check(D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
        "SerializeClearRS");
    d3d_check(device_->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&s_clear_root_sig)), "CreateClearRS");

    const ComPtr<ID3DBlob> vs =
        compile_hlsl(kBlitVs, sizeof(kBlitVs) - 1, "vs_5_1", "main");
    const ComPtr<ID3DBlob> ps =
        compile_hlsl(kClearPs, sizeof(kClearPs) - 1, "ps_5_1", "main");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = s_clear_root_sig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kEfbColorFormat;
    pd.SampleDesc.Count = 1;

    pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_RED |
        D3D12_COLOR_WRITE_ENABLE_GREEN |
        D3D12_COLOR_WRITE_ENABLE_BLUE;
    d3d_check(device_->CreateGraphicsPipelineState(
        &pd, IID_PPV_ARGS(&clear_rgb_pipeline_)), "CreateClearRgbPSO");

    pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALPHA;
    d3d_check(device_->CreateGraphicsPipelineState(
        &pd, IID_PPV_ARGS(&clear_alpha_pipeline_)), "CreateClearAlphaPSO");
    return true;
}

// ---------------------------------------------------------------------------
// initialize / shutdown / pump_messages
// ---------------------------------------------------------------------------

bool RendererD3D12::initialize(int window_width, int window_height, unsigned efb_scale) {
    efb_scale_   = std::clamp(efb_scale, 1u, kMaxEfbScale);
    frame_slot_ = 0;
    frame_index_ = 0;
    next_fence_value_ = 0;
    fence_values_.fill(0);
    timestamp_pending_.fill(false);
    has_presented_swap_chain_ = false;
    window_shown_ = false;
    s_quit_requested.store(false, std::memory_order_release);
    const bool hidden_window = hidden_window_mode_enabled();
    const bool fullscreen_window =
        fullscreen_window_mode_enabled() && !hidden_window;
    int actual_window_width = window_width;
    int actual_window_height = window_height;
    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    DWORD window_style = kWindowStyle;

    if (fullscreen_window) {
        POINT origin{0, 0};
        HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfoW(monitor, &monitor_info)) {
            window_x = monitor_info.rcMonitor.left;
            window_y = monitor_info.rcMonitor.top;
            const std::int64_t monitor_width =
                static_cast<std::int64_t>(monitor_info.rcMonitor.right) -
                monitor_info.rcMonitor.left;
            const std::int64_t monitor_height =
                static_cast<std::int64_t>(monitor_info.rcMonitor.bottom) -
                monitor_info.rcMonitor.top;
            if (monitor_width <= 0 || monitor_height <= 0 ||
                monitor_width > std::numeric_limits<int>::max() ||
                monitor_height > std::numeric_limits<int>::max()) {
                std::cerr << "[Renderer] invalid monitor dimensions\n";
                return false;
            }
            actual_window_width = static_cast<int>(monitor_width);
            actual_window_height = static_cast<int>(monitor_height);
        }
        window_style = WS_POPUP;
    }

    if (actual_window_width <= 0 || actual_window_height <= 0) {
        std::cerr << "[Renderer] invalid client dimensions\n";
        return false;
    }

    s_bb_width   = static_cast<UINT>(actual_window_width);
    s_bb_height  = static_cast<UINT>(actual_window_height);

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
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        RECT frame{0, 0, 0, 0};
        if (!fullscreen_window) {
            if (!AdjustWindowRectExForDpi(&frame, window_style, FALSE, WS_EX_APPWINDOW, GetDpiForSystem())) {
                std::cerr << "[Renderer] AdjustWindowRect failed\n";
                return false;
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
            return false;
        }
        const DWORD ex_style =
            hidden_window ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
                          : WS_EX_APPWINDOW;
        window_ = window_owner_.start({
            kWindowClass, L"Nebula", window_style, ex_style,
            window_x, window_y, static_cast<int>(outer_width),
            static_cast<int>(outer_height), !hidden_window});
        if (!window_) { std::cerr << "[Renderer] CreateWindow failed\n"; return false; }
        // A centered geometry seed is not evidence that the OS cursor is in
        // the client. The first real message or authoritative cursor poll
        // acquires visibility.
        // The owner publishes real geometry/input through WM_SIZE/WM_SETFOCUS.
        // Do not reseed from this thread after it has begun processing messages.
        window_shown_ = !hidden_window;
    }

    // D3D12 debug layer + GPU-based validation: OPT-IN via the
    // GALAXY_D3D_DEBUG env var (1 = debug layer, 2 = + GPU validation).
    // GPU-based validation alone is a 5-20x GPU-side slowdown and the debug
    // layer adds large CPU overhead per call — leaving them unconditionally
    // enabled in _DEBUG builds was a primary cause of the low frame rate
    // (the documented run recipe builds Debug).
    unsigned d3d_debug_level = 0;
    {
        char buf[16]{};
        std::size_t len = 0;
        if (getenv_s(&len, buf, sizeof(buf), "GALAXY_D3D_DEBUG") == 0 &&
            len > 0) {
            d3d_debug_level =
                static_cast<unsigned>(std::strtoul(buf, nullptr, 10));
        }
    }
    try {
        if (d3d_debug_level >= 1) {
            ComPtr<ID3D12Debug> d;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) {
                d->EnableDebugLayer();
                if (d3d_debug_level >= 2) {
                    ComPtr<ID3D12Debug1> d1;
                    if (SUCCEEDED(d.As(&d1))) {
                        d1->SetEnableGPUBasedValidation(TRUE);
                    }
                }
            }
        }
        // DRED: auto breadcrumbs + page-fault reporting for device-removal
        // triage. Keep it opt-in; forcing it on during normal play adds
        // diagnostic driver work to every launch/frame.
        if (dred_diagnostics_enabled()) {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }
        UINT flags = 0;
        if (d3d_debug_level >= 1) {
            flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        ComPtr<IDXGIFactory4> factory;
        d3d_check(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

        // DXGI's high-performance order is a preference, not a guarantee
        // that entry zero can create the device. Check every hardware adapter
        // in that order and retain the first usable D3D12 device. A legacy
        // enumeration covers systems without IDXGIFactory6 or a failed
        // preferred enumeration. Do not silently select a software adapter.
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 adapter_desc{};
        auto try_adapter = [&](IDXGIAdapter1* candidate) {
            DXGI_ADAPTER_DESC1 description{};
            if (candidate == nullptr ||
                FAILED(candidate->GetDesc1(&description)) ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) {
                return false;
            }
            ComPtr<ID3D12Device> candidate_device;
            if (FAILED(D3D12CreateDevice(
                    candidate, D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&candidate_device)))) {
                return false;
            }
            adapter = candidate;
            adapter_desc = description;
            device_ = std::move(candidate_device);
            return true;
        };
        ComPtr<IDXGIFactory6> preferred_factory;
        if (SUCCEEDED(factory.As(&preferred_factory))) {
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = preferred_factory->EnumAdapterByGpuPreference(
                    index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate));
                if (result == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (FAILED(result)) {
                    break;
                }
                if (try_adapter(candidate.Get())) {
                    break;
                }
            }
        }
        if (!adapter) {
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = factory->EnumAdapters1(index, &candidate);
                if (result == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (FAILED(result)) {
                    break;
                }
                if (try_adapter(candidate.Get())) {
                    break;
                }
            }
        }
        if (!adapter || !device_) {
            throw std::runtime_error(
                "No hardware D3D12 adapter supports feature level 11_0");
        }
        {
            char narrow[128]{};
            WideCharToMultiByte(
                CP_UTF8, 0, adapter_desc.Description, -1, narrow,
                static_cast<int>(sizeof(narrow) - 1), nullptr, nullptr);
            const bool is_software =
                (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            std::cout << "[gpu] adapter=\"" << narrow << "\""
                      << " vram-mb="
                      << (adapter_desc.DedicatedVideoMemory / (1024 * 1024))
                      << " software=" << (is_software ? 1 : 0)
                      << " vendor=0x" << std::hex << adapter_desc.VendorId
                      << " device=0x" << adapter_desc.DeviceId << std::dec
                      << '\n';
        }

        // Live debug-layer message callback (opt-in, same GALAXY_D3D_DEBUG
        // gate as EnableDebugLayer above): flushes ERROR/CORRUPTION messages
        // to stderr the instant they're produced, not just at
        // device-removal time. A driver-side hard fail-fast (e.g. a
        // STATUS_STACK_BUFFER_OVERRUN inside the UMD) tears the whole
        // process down before any later "dump stored messages on removal"
        // code can run, so only an immediate callback has a chance of
        // catching the validation error -- if any -- for the API call that
        // triggered it.
        if (d3d_debug_level >= 1) {
            ComPtr<ID3D12InfoQueue1> iq1;
            if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&iq1)))) {
                DWORD cookie = 0;
                iq1->RegisterMessageCallback(
                    [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                       D3D12_MESSAGE_ID, LPCSTR description, void*) {
                        if (severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                            severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                            std::cerr << "[D3D12-LIVE-ERR] " << description
                                      << '\n';
                            std::cerr.flush();
                        }
                    },
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                    nullptr,
                    &cookie);
            }
        }

        D3D_FEATURE_LEVEL requested_feature_levels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels{};
        feature_levels.NumFeatureLevels =
            static_cast<UINT>(std::size(requested_feature_levels));
        feature_levels.pFeatureLevelsRequested = requested_feature_levels;
        feature_levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(device_->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS,
                &feature_levels,
                sizeof(feature_levels)))) {
            feature_levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        }
        ComPtr<ID3D12Device5> device5;
        const bool dxr_device5_supported = SUCCEEDED(device_.As(&device5));
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        const bool dxr_runtime_supported =
            dxr_device5_supported &&
            SUCCEEDED(device_->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5))) &&
            options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        RenderFeatureCaps caps{};
        caps.d3d12_device_created = true;
        caps.d3d_feature_level =
            static_cast<unsigned>(feature_levels.MaxSupportedFeatureLevel);
        caps.adapter_vendor_id = adapter_desc.VendorId;
        caps.adapter_device_id = adapter_desc.DeviceId;
        caps.adapter_subsys_id = adapter_desc.SubSysId;
        caps.adapter_revision = adapter_desc.Revision;
        caps.adapter_luid_low =
            static_cast<std::uint32_t>(adapter_desc.AdapterLuid.LowPart);
        caps.adapter_luid_high =
            static_cast<std::uint32_t>(adapter_desc.AdapterLuid.HighPart);
        copy_adapter_description(adapter_desc, caps.adapter_description);
        caps.dxr_device5_supported = dxr_device5_supported;
        caps.dxr_raytracing_tier =
            raytracing_tier_code(options5.RaytracingTier);
        caps.streamline_bridge_built = GALAXY_WITH_STREAMLINE_BRIDGE != 0;
        caps.msaa = make_render_feature_status(
            false,
            false,
            false,
            false,
            "EFB/depth targets and GX PSOs are single-sample");
        caps.taa = make_render_feature_status(
            false,
            false,
            false,
            false,
            "motion vectors, jitter, and history resources are not built");
        caps.dxr = make_render_feature_status(
            false,
            dxr_runtime_supported,
            false,
            false,
            dxr_runtime_supported
                ? "DXR runtime is available; native RT effects are not built"
                : "D3D12 raytracing tier is unsupported");
        caps.streamline = make_render_feature_status(
            caps.streamline_bridge_built,
            false,
            false,
            false,
            caps.streamline_bridge_built
                ? "NVIDIA Streamline runtime support is not initialized"
                : "NVIDIA Streamline bridge is not built into this runtime");
        caps.reflex = make_render_feature_status(
            false,
            false,
            false,
            false,
            "Streamline Reflex integration is not built");
        caps.dlss_super_resolution = make_render_feature_status(
            false,
            false,
            false,
            false,
            "DLSS SDK plus depth and motion vectors are required");
        caps.dlss_frame_generation = make_render_feature_status(
            false,
            false,
            false,
            false,
            "DLFG requires Streamline, Reflex, motion vectors, and UI tagging");
        set_render_feature_caps(caps);

        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        d3d_check(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        d3d_check(
            queue_->GetTimestampFrequency(&timestamp_frequency_),
            "GetTimestampFrequency");
        QueryPerformanceFrequency(&qpc_frequency_);

        if (gpu_timestamps_enabled()) {
            D3D12_QUERY_HEAP_DESC qhd{};
            qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            qhd.Count = kFramesInFlight * 2u;
            d3d_check(
                device_->CreateQueryHeap(
                    &qhd, IID_PPV_ARGS(&timestamp_query_heap_)),
                "Create timestamp query heap");

            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width =
                kFramesInFlight * 2u * sizeof(std::uint64_t);
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            d3d_check(
                device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&timestamp_readback_)),
                "Create timestamp readback");
        }

        // Swap chain.
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.BufferCount  = kSwapChainBuffers;
        scd.Width        = static_cast<UINT>(actual_window_width);
        scd.Height       = static_cast<UINT>(actual_window_height);
        scd.Format       = kBackbufferFormat;
        scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect   = flip_discard_enabled()
            ? DXGI_SWAP_EFFECT_FLIP_DISCARD
            : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.SampleDesc.Count = 1;
        scd.Scaling      = DXGI_SCALING_STRETCH;
        scd.AlphaMode    = DXGI_ALPHA_MODE_IGNORE;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (allow_tearing_enabled()) {
            ComPtr<IDXGIFactory5> factory5;
            BOOL supported = FALSE;
            if (SUCCEEDED(factory.As(&factory5)) &&
                SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &supported, sizeof(supported))) &&
                supported) {
                tearing_supported_ = true;
                scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            }
        }
        ComPtr<IDXGISwapChain1> sc1;
        d3d_check(factory->CreateSwapChainForHwnd(queue_.Get(), window_, &scd, nullptr, nullptr, &sc1), "CreateSwapChain");
        d3d_check(sc1.As(&swap_chain_), "SwapChain As");
        {
            ComPtr<IDXGISwapChain2> swap_chain2;
            d3d_check(swap_chain_.As(&swap_chain2), "SwapChain2 As");
            d3d_check(swap_chain2->SetMaximumFrameLatency(
                          swap_chain_frame_latency()),
                      "SetMaximumFrameLatency");
            frame_latency_waitable_ =
                swap_chain2->GetFrameLatencyWaitableObject();
            if (frame_latency_waitable_ == nullptr) {
                throw std::runtime_error(
                    "GetFrameLatencyWaitableObject failed");
            }
        }
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        if (fullscreen_window && exclusive_fullscreen_enabled()) {
            const HRESULT fs_hr = swap_chain_->SetFullscreenState(TRUE, nullptr);
            if (SUCCEEDED(fs_hr)) {
                exclusive_fullscreen_active_ = true;
                // Flip-model fullscreen transitions require ResizeBuffers,
                // even when the client dimensions did not change.
                d3d_check(swap_chain_->ResizeBuffers(kSwapChainBuffers,
                    scd.Width, scd.Height, scd.Format, scd.Flags),
                    "exclusive fullscreen ResizeBuffers");
            } else {
                std::cerr << "[Renderer] exclusive fullscreen unavailable hr=0x"
                          << std::hex << std::uppercase
                          << std::bit_cast<std::uint32_t>(
                                 static_cast<std::int32_t>(fs_hr))
                          << std::dec
                          << "; continuing in borderless fullscreen\n";
            }
        }

        // RTV heap: kSwapChainBuffers backbuffers + 1 EFB slot.
        {
            D3D12_DESCRIPTOR_HEAP_DESC dhd{};
            dhd.NumDescriptors = kSwapChainBuffers + 1;
            dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtv_heap_)), "RTV heap");
            const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
            for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
                d3d_check(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "GetBuffer");
                device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
                h.ptr += stride;
            }
        }

        // Command allocators.
        for (unsigned i = 0; i < kFramesInFlight; ++i)
            d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i])), "CreateCommandAllocator");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list_)), "CreateCommandList");
        command_list_->SetName(L"GxFrameList");
        d3d_check(command_list_->Close(), "CommandList Close init");

        // Fence.
        d3d_check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_) throw std::runtime_error("CreateEvent failed");

        // EFB render targets.
        if (!create_efb_targets(efb_scale_)) return false;

        // Root signatures.
        if (!create_root_signature()) return false;

        // Blit root signature: one SRV table + static linear-clamp sampler.
        {
            D3D12_STATIC_SAMPLER_DESC ss{};
            ss.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss.ShaderRegister   = 0;
            ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors     = 1;
            range.BaseShaderRegister = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER param{};
            param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges   = &range;
            param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsd{};
            rsd.NumParameters     = 1;
            rsd.pParameters       = &param;
            rsd.NumStaticSamplers = 1;
            rsd.pStaticSamplers   = &ss;

            ComPtr<ID3DBlob> blob, err;
            d3d_check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "SerializeBlitRS");
            d3d_check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s_blit_root_sig)), "CreateBlitRS");
        }

        // Blit pipeline.
        {
            const ComPtr<ID3DBlob> vs = compile_hlsl(kBlitVs, sizeof(kBlitVs) - 1, "vs_5_1", "main");
            const ComPtr<ID3DBlob> ps = compile_hlsl(kBlitPs, sizeof(kBlitPs) - 1, "ps_5_1", "main");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = s_blit_root_sig.Get();
            pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
            pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pd.SampleMask = UINT_MAX;
            pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
            pd.RasterizerState.DepthClipEnable = TRUE;
            pd.DepthStencilState.DepthEnable   = FALSE;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets      = 1;
            pd.RTVFormats[0]         = kBackbufferFormat;
            pd.SampleDesc.Count      = 1;
            d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&blit_pipeline_)), "CreateBlitPSO");
        }

        // Sampler heap.
        if (!create_sampler_heap()) return false;

        // EFB-copy conversion blit pipeline.
        if (!create_conversion_pipeline()) return false;
        if (!create_clear_pipeline()) return false;

        // Upload + descriptor rings.
        if (!vertex_ring_  .initialize(device_.Get(), "vertex",   kVbPerFrame,  kFramesInFlight)) return false;
        if (!index_ring_   .initialize(device_.Get(), "index",    kIbPerFrame,  kFramesInFlight)) return false;
        if (!constant_ring_.initialize(device_.Get(), "constant", kCbPerFrame,  kFramesInFlight)) return false;
        if (!matrix_ring_  .initialize(device_.Get(), "matrix",   kMtxPerFrame, kFramesInFlight)) return false;
        if (!srv_ring_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvPerFrame, kFramesInFlight, kPersistentSrvDescriptors)) return false;

        if (trace_renderer_startup_enabled()) {
            std::cout << "[Renderer] OK  EFB=" << kEfbWidth * efb_scale_
                      << "x" << kEfbHeight * efb_scale_ << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[Renderer] initialize failed: " << e.what() << "\n";
        return false;
    }
    return true;
}

void RendererD3D12::shutdown() {
    std::exception_ptr shutdown_failure;
    const auto retain_shutdown_failure =
        [&](const char* stage, auto&& action) {
            try {
                action();
            } catch (const std::exception& error) {
                std::cerr << "[Renderer] shutdown " << stage
                          << " failed: " << error.what() << "\n";
                if (shutdown_failure == nullptr) {
                    shutdown_failure = std::current_exception();
                }
            } catch (...) {
                std::cerr << "[Renderer] shutdown " << stage
                          << " failed with an unknown exception\n";
                if (shutdown_failure == nullptr) {
                    shutdown_failure = std::current_exception();
                }
            }
        };

    retain_shutdown_failure("settings flush", [&] {
        flush_native_settings_if_dirty();
    });
    if (queue_ && fence_) {
        retain_shutdown_failure("GPU drain", [&] { wait_for_gpu(); });
    }
    // A diagnostic copy can be the final submitted command list. Drain only
    // here, after benchmark execution has ended, then hand its CPU data to the
    // bounded background PPM writers before any D3D resources are released.
    retain_shutdown_failure("EFB diagnostic readback", [&] {
        debug_log_readback();
    });
    retain_shutdown_failure("backbuffer diagnostic readback", [&] {
        debug_log_backbuffer_readback();
    });
    retain_shutdown_failure("selected XFB diagnostic readback", [&] {
        debug_log_selected_xfb_readback();
    });
    retain_shutdown_failure("diagnostic writer drain", [&] {
        reap_debug_capture_writers(true);
    });
    if (fence_event_) { CloseHandle(fence_event_); fence_event_ = nullptr; }
    ui_texture_pipeline_.Reset();
    ui_solid_pipeline_.Reset();
    ui_solid_root_sig_.Reset();
    ui_solid_pipeline_ready_ = false;
    ui_solid_pipeline_load_attempted_ = false;
    ui_texture_root_sig_.Reset();
    ui_texture_srv_heap_.Reset();
    ui_corner_texture_.Reset();
    ui_corner_upload_.Reset();
    ui_corner_texture_ready_ = false;
    ui_corner_texture_load_attempted_ = false;
    s_pending_resize_extent.store(0u, std::memory_order_relaxed);
    if (frame_latency_waitable_) {
        CloseHandle(frame_latency_waitable_);
        frame_latency_waitable_ = nullptr;
    }
    if (swap_chain_ && exclusive_fullscreen_active_) {
        swap_chain_->SetFullscreenState(FALSE, nullptr);
        exclusive_fullscreen_active_ = false;
    }
    s_blit_root_sig.Reset();
    blit_pipeline_.Reset();
    s_conv_root_sig.Reset();
    conv_pipeline_.Reset();
    conv_rtv_heap_.Reset();
    conv_srv_heap_.Reset();
    conv_scratch_rtv_ = {};
    conv_efb_color_srv_ = {};
    conv_efb_depth_srv_ = {};
    next_fence_value_ = 0;
    peek_command_list_.Reset();
    peek_allocator_.Reset();
    peek_target_.Reset();
    pointer_depth_target_.Reset();
    pointer_depth_readback_.Reset();
    peek_readback_.Reset();
    peek_rtv_heap_.Reset();
    peek_srv_heap_.Reset();
    peek_rtv_ = {};
    peek_srv_ = {};
    peek_srv_gpu_ = {};
    peek_readback_row_pitch_ = 0;
    s_clear_root_sig.Reset();
    clear_rgb_pipeline_.Reset();
    clear_alpha_pipeline_.Reset();
    timestamp_query_heap_.Reset();
    timestamp_readback_.Reset();
    readback_buf_.Reset();
    backbuffer_readback_buf_.Reset();
    efb_copy_dests_.clear();
    for (auto& retired : retired_efb_copy_textures_) {
        retired.clear();
    }
    efb_color_.Reset();
    efb_depth_.Reset();
    for (auto& b : backbuffers_) b.Reset();
    rtv_heap_.Reset(); dsv_heap_.Reset(); sampler_heap_.Reset(); root_signature_.Reset();
    for (auto& heap : sampler_heaps_) heap.Reset();
    sampler_tables_.reset();
    default_sampler_table_ = {};
    swap_chain_.Reset(); command_list_.Reset();
    for (auto& a : allocators_) a.Reset();
    fence_.Reset(); queue_.Reset(); device_.Reset();
    window_owner_.stop();
    s_pointer_hwnd.store(0u, std::memory_order_release);
    window_ = nullptr;
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    if (shutdown_failure != nullptr) {
        std::rethrow_exception(shutdown_failure);
    }
}

void RendererD3D12::drain_gpu() {
    if (queue_ && fence_) {
        wait_for_gpu();
    }
}

void RendererD3D12::apply_pending_resize() {
    const auto extent = s_pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent == 0u) return;
    if (!swap_chain_ || !rtv_heap_) {
        return;
    }
    const UINT new_width = static_cast<UINT>(extent >> 32u);
    const UINT new_height = static_cast<UINT>(extent);
    if (new_width == s_bb_width && new_height == s_bb_height) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 current_desc{};
    if (FAILED(swap_chain_->GetDesc1(&current_desc))) {
        std::cerr << "[Renderer] resize: GetDesc1 failed; keeping prior "
                     "backbuffer size\n";
        return;
    }

    // ResizeBuffers requires every reference to the existing backbuffers to
    // be released first, and requires the GPU to be done with them. The EFB
    // color/depth targets, blit pipeline, and root signature are unaffected
    // -- present() always blits EFB/XFB content into whatever backbuffer
    // size is current, so only the swap chain's own resources change here.
    wait_for_gpu();
    for (auto& buffer : backbuffers_) {
        buffer.Reset();
    }

    const HRESULT resize_hr = swap_chain_->ResizeBuffers(
        kSwapChainBuffers,
        new_width,
        new_height,
        current_desc.Format,
        current_desc.Flags);
    if (FAILED(resize_hr)) {
        dump_device_removal(device_.Get());
        std::cerr << "[Renderer] resize: ResizeBuffers failed hr=0x"
                  << std::hex << static_cast<unsigned long>(resize_hr)
                  << std::dec << "; swap chain left at prior size\n";
        // The swap chain no longer owns valid buffers at this point (they
        // were released above); a failed ResizeBuffers is unrecoverable
        // in-place, so surface it the same way other fatal device errors
        // are surfaced rather than continuing to present into empty slots.
        d3d_check(resize_hr, "ResizeBuffers");
        return;
    }

    const UINT stride =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
        d3d_check(
            swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])),
            "resize GetBuffer");
        device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
        h.ptr += stride;
    }

    s_bb_width = new_width;
    s_bb_height = new_height;
    // The swap chain was fully drained and recreated at the new size; the
    // frame-latency waitable object and has_presented_swap_chain_ gate on
    // the OLD swap chain's present history, which no longer applies.
    has_presented_swap_chain_ = false;
    if (trace_present_stats_enabled()) {
        std::cerr << "[Renderer] resized backbuffer to " << new_width << "x"
                  << new_height << '\n';
    }
}

void RendererD3D12::ensure_native_settings_corner_texture_loaded() {
    if (ui_corner_texture_load_attempted_) {
        return;
    }
    ui_corner_texture_load_attempted_ = true;

    // Diagnostic-only isolation switch (never set automatically): lets a
    // crash repro distinguish "the corner-texture SRV/root-sig/PSO creation
    // itself is the trigger" from "clear_ui_rects is the trigger" without
    // guessing. See the F1+real-audio nvwgf2umx.dll STATUS_STACK_BUFFER_OVERRUN
    // investigation.
    {
        char disable_value[16]{};
        std::size_t disable_length = 0;
        const bool disabled =
            getenv_s(
                &disable_length, disable_value, sizeof(disable_value),
                "GALAXY_DISABLE_SETTINGS_CORNER_TEXTURE") == 0 &&
            disable_length > 1 && disable_value[0] != '0' &&
            disable_value[0] != 'n' && disable_value[0] != 'N';
        if (disabled) {
            return;
        }
    }

    // Read-only extraction from the user's own game dump -- never fatal if
    // it fails (a different game revision, a missing/renamed asset, or a
    // decode format this reader doesn't cover): the overlay already has a
    // working solid-color fallback, so this is purely cosmetic upside.
    if (s_native_settings_content_root.empty()) {
        return;
    }
    const auto tex = galaxy::gx::load_game_texture(
        s_native_settings_content_root, "files\\LayoutData\\FileSelect.arc",
        "myfileselwinframe.tpl");
    if (!tex.has_value() || tex->width == 0 || tex->height == 0) {
        return;
    }

    try {
        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC tex_desc{};
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Width = tex->width;
        tex_desc.Height = tex->height;
        tex_desc.DepthOrArraySize = 1;
        tex_desc.MipLevels = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        d3d_check(
            device_->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&ui_corner_texture_)),
            "create UI corner texture");

        const UINT row_pitch = static_cast<UINT>(
            (tex->width * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u));
        const UINT64 upload_size =
            static_cast<UINT64>(row_pitch) * tex->height;

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buf_desc{};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = upload_size;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.Format = DXGI_FORMAT_UNKNOWN;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // This renderer owns staging until its GPU drain during shutdown.
        d3d_check(
            device_->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&ui_corner_upload_)),
            "create UI corner texture upload buffer");

        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        d3d_check(
            ui_corner_upload_->Map(0, &no_read, &mapped),
            "map UI corner texture upload buffer");
        auto* dst_base = static_cast<std::uint8_t*>(mapped);
        for (unsigned y = 0; y < tex->height; ++y) {
            std::memcpy(
                dst_base + static_cast<std::size_t>(y) * row_pitch,
                tex->rgba8.data() + static_cast<std::size_t>(y) * tex->width * 4u,
                static_cast<std::size_t>(tex->width) * 4u);
        }
        ui_corner_upload_->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = ui_corner_texture_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = ui_corner_upload_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = tex->width;
        src.PlacedFootprint.Footprint.Height = tex->height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = row_pitch;
        // Recorded into the frame's already-open command list (present()
        // calls this from inside its own recording scope): the copy and
        // the state transition both land in the same submission as the
        // rest of this frame's work, so no extra synchronization is
        // needed beyond the normal per-frame fence.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = ui_corner_texture_.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        // SRV heap (one descriptor) + root signature + pipeline, created
        // once alongside the texture itself.
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.NumDescriptors = 1;
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        d3d_check(
            device_->CreateDescriptorHeap(
                &srv_heap_desc, IID_PPV_ARGS(&ui_texture_srv_heap_)),
            "create UI texture SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(
            ui_corner_texture_.Get(), &srv_desc,
            ui_texture_srv_heap_->GetCPUDescriptorHandleForHeapStart());

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 14;  // rect+uv+tint+screen
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srv_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &sampler;

        ComPtr<ID3DBlob> rs_blob, rs_err;
        d3d_check(
            D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err),
            "serialize UI texture root signature");
        d3d_check(
            device_->CreateRootSignature(
                0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                IID_PPV_ARGS(&ui_texture_root_sig_)),
            "create UI texture root signature");

        const ComPtr<ID3DBlob> vs = compile_hlsl(
            kUiTexturedQuadVs, sizeof(kUiTexturedQuadVs) - 1, "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps = compile_hlsl(
            kUiTexturedQuadPs, sizeof(kUiTexturedQuadPs) - 1, "ps_5_1", "main");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = ui_texture_root_sig_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kBackbufferFormat;
        pd.SampleDesc.Count = 1;
        d3d_check(
            device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&ui_texture_pipeline_)),
            "create UI texture PSO");

        // All fallible preparation is complete. From here to publication no
        // resource can be released by the cosmetic fallback after recording.
        command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        command_list_->ResourceBarrier(1, &barrier);

        ui_corner_texture_width_ = tex->width;
        ui_corner_texture_height_ = tex->height;
        ui_corner_texture_ready_ = true;
    } catch (const std::exception& e) {
        // Cosmetic-only path: log and keep the existing solid-color
        // fallback rather than propagating a fatal error for a UI asset.
        std::cerr << "[Renderer] native settings corner texture unavailable: "
                  << e.what() << '\n';
        ui_corner_texture_.Reset();
        ui_corner_upload_.Reset();
        ui_texture_srv_heap_.Reset();
        ui_texture_root_sig_.Reset();
        ui_texture_pipeline_.Reset();
        ui_corner_texture_ready_ = false;
    }
}

void RendererD3D12::draw_ui_textured_quad(
    int x, int y, int w, int h,
    float u0, float v0, float u1, float v1,
    const std::array<float, 4>& tint) {
    if (!ui_corner_texture_ready_) {
        return;
    }
    const float constants[14] = {
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(w), static_cast<float>(h),
        u0, v0, u1, v1,
        tint[0], tint[1], tint[2], tint[3],
        static_cast<float>(s_bb_width), static_cast<float>(s_bb_height),
    };
    ID3D12DescriptorHeap* heaps[] = {ui_texture_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootSignature(ui_texture_root_sig_.Get());
    command_list_->SetPipelineState(ui_texture_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 14, constants, 0);
    command_list_->SetGraphicsRootDescriptorTable(
        1, ui_texture_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list_->DrawInstanced(4, 1, 0, 0);
    invalidate_gx_bindings();
}

void RendererD3D12::ensure_native_settings_solid_pipeline_loaded() {
    if (ui_solid_pipeline_load_attempted_) {
        return;
    }
    ui_solid_pipeline_load_attempted_ = true;

    try {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.ShaderRegister = 0;
        param.Constants.Num32BitValues = 10;  // rect(4) + color(4) + screen(2)
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 1;
        rsd.pParameters = &param;

        ComPtr<ID3DBlob> rs_blob, rs_err;
        d3d_check(
            D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err),
            "serialize UI solid root signature");
        d3d_check(
            device_->CreateRootSignature(
                0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                IID_PPV_ARGS(&ui_solid_root_sig_)),
            "create UI solid root signature");

        const ComPtr<ID3DBlob> vs = compile_hlsl(
            kUiSolidQuadVs, sizeof(kUiSolidQuadVs) - 1, "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps = compile_hlsl(
            kUiSolidQuadPs, sizeof(kUiSolidQuadPs) - 1, "ps_5_1", "main");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = ui_solid_root_sig_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kBackbufferFormat;
        pd.SampleDesc.Count = 1;
        d3d_check(
            device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&ui_solid_pipeline_)),
            "create UI solid PSO");

        ui_solid_pipeline_ready_ = true;
    } catch (const std::exception& e) {
        std::cerr << "[Renderer] native settings solid UI pipeline unavailable: "
                  << e.what() << '\n';
        ui_solid_root_sig_.Reset();
        ui_solid_pipeline_.Reset();
        ui_solid_pipeline_ready_ = false;
    }
}

void RendererD3D12::draw_ui_solid_quad(
    int x, int y, int w, int h,
    const std::array<float, 4>& color) {
    ensure_native_settings_solid_pipeline_loaded();
    if (!ui_solid_pipeline_ready_ || w <= 0 || h <= 0) {
        return;
    }
    const int left = std::clamp(x, 0, static_cast<int>(s_bb_width));
    const int top = std::clamp(y, 0, static_cast<int>(s_bb_height));
    const int right = std::clamp(x + w, 0, static_cast<int>(s_bb_width));
    const int bottom = std::clamp(y + h, 0, static_cast<int>(s_bb_height));
    if (right <= left || bottom <= top) {
        return;
    }
    const float constants[10] = {
        static_cast<float>(left), static_cast<float>(top),
        static_cast<float>(right - left), static_cast<float>(bottom - top),
        color[0], color[1], color[2], color[3],
        static_cast<float>(s_bb_width), static_cast<float>(s_bb_height),
    };
    command_list_->SetGraphicsRootSignature(ui_solid_root_sig_.Get());
    command_list_->SetPipelineState(ui_solid_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 10, constants, 0);
    command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list_->DrawInstanced(4, 1, 0, 0);
    invalidate_gx_bindings();
}

void RendererD3D12::pump_messages() {
    // HWND ownership and all native modal loops stay on window_owner_.
    // Guest checkpoints must never dispatch a Win32 modal loop themselves.
}

bool RendererD3D12::quit_requested() const noexcept {
    return s_quit_requested.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void RendererD3D12::invalidate_gx_bindings() {
    bound_pipeline_ = nullptr;
    bound_vs_constants_ = 0;
    bound_ps_constants_ = 0;
    bound_texture_table_ = {};
    bound_sampler_table_ = {};
    bound_matrix_palette_ = 0;
    bound_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bound_scissor_valid_ = false;
    bound_viewport_valid_ = false;
}

void RendererD3D12::begin_frame(bool present_swap_chain) {
    // TEMPORARY diagnostic (unconditional, cheap: two steady_clock reads):
    // isolate which phase of the render loop produces a host-thread stall
    // long enough to desynchronize the exact-cadence VI consumer. Prints
    // immediately (not gated behind the 2s [present-stats] window, which a
    // stall this size never survives to see) whenever a single call takes
    // longer than one VI period.
    const auto stall_watch_begin_frame_start =
        std::chrono::steady_clock::now();
    // Live resize lands here: the one point guaranteed to be between frames
    // with no in-flight command-list recording on this renderer. Contains
    // its own full GPU-idle wait, so it belongs before any per-slot state
    // below is touched.
    apply_pending_resize();
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    last_latency_wait_us_ = 0;
    last_frame_slot_wait_us_ = 0;
    last_timestamp_read_us_ = 0;
    last_begin_reset_us_ = 0;
    const bool collect_begin_timing = trace_present_stats_enabled();
    if (present_swap_chain && has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(collect_begin_timing);
    }

    // Wait only on the slot we're about to reuse.
    const std::uint64_t wait_val = fence_values_[frame_slot_];
    if (fence_->GetCompletedValue() < wait_val) {
        const auto fence_wait_start = collect_begin_timing
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const HRESULT event_hr =
            fence_->SetEventOnCompletion(wait_val, fence_event_);
        if (FAILED(event_hr)) {
            dump_device_removal(device_.Get());
            d3d_check(event_hr, "SetEventOnCompletion");
        }
        const DWORD wait_result =
            WaitForSingleObjectEx(fence_event_, 30000, FALSE);
        if (wait_result != WAIT_OBJECT_0) {
            dump_device_removal(device_.Get());
            throw std::runtime_error("GPU frame-slot fence wait timed out");
        }
        if (collect_begin_timing) {
            last_frame_slot_wait_us_ = elapsed_us(
                fence_wait_start,
                std::chrono::steady_clock::now());
        }
    }

    if (timestamp_pending_[frame_slot_] && timestamp_readback_ &&
        timestamp_frequency_ != 0) {
        const auto timestamp_read_start = collect_begin_timing
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const SIZE_T offset =
            static_cast<SIZE_T>(frame_slot_) * 2u * sizeof(std::uint64_t);
        const D3D12_RANGE read_range{
            offset, offset + 2u * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(timestamp_readback_->Map(0, &read_range, &mapped))) {
            const auto* timestamps =
                reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::byte*>(mapped) + offset);
            if (timestamps[1] >= timestamps[0]) {
                const double gpu_ms =
                    static_cast<double>(timestamps[1] - timestamps[0]) *
                    1000.0 / static_cast<double>(timestamp_frequency_);
                telemetry_gpu_ms_ += gpu_ms;
                ++telemetry_gpu_samples_;
            }
            const D3D12_RANGE no_write{0, 0};
            timestamp_readback_->Unmap(0, &no_write);
        }
        timestamp_pending_[frame_slot_] = false;
        if (collect_begin_timing) {
            last_timestamp_read_us_ = elapsed_us(
                timestamp_read_start,
                std::chrono::steady_clock::now());
        }
    }

    const auto reset_start = collect_begin_timing
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const HRESULT allocator_reset_hr = allocators_[frame_slot_]->Reset();
    if (FAILED(allocator_reset_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(allocator_reset_hr, "CommandAllocator Reset");
    }
    const HRESULT command_reset_hr =
        command_list_->Reset(allocators_[frame_slot_].Get(), nullptr);
    if (FAILED(command_reset_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(command_reset_hr, "CommandList Reset");
    }
    if (timestamp_query_heap_) {
        command_list_->EndQuery(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u);
    }

    vertex_ring_  .begin_frame(frame_slot_);
    index_ring_   .begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    matrix_ring_  .begin_frame(frame_slot_);
    srv_ring_     .begin_frame(frame_slot_);
    reset_sampler_frame();
    retired_efb_copy_textures_[frame_slot_].clear();

    // Bind both heaps for the entire frame.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    if (collect_begin_timing) {
        last_begin_reset_us_ = elapsed_us(
            reset_start,
            std::chrono::steady_clock::now());
    }
    {
        const double stall_watch_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() -
            stall_watch_begin_frame_start).count();
        if (trace_stall_watch_enabled() && stall_watch_ms > 12.0) {
            std::ostringstream message;
            message << "[stall-watch] begin_frame took " << stall_watch_ms
                      << "ms latency-wait-us=" << last_latency_wait_us_
                      << " frame-slot-wait-us=" << last_frame_slot_wait_us_
                      << " timestamp-read-us=" << last_timestamp_read_us_
                      << " begin-reset-us=" << last_begin_reset_us_ << '\n';
            std::cerr << message.str();
        }
    }
}

void RendererD3D12::wait_for_frame_latency_if_needed(
    bool collect_present_telemetry) {
    LARGE_INTEGER latency_wait_start{};
    LARGE_INTEGER latency_wait_end{};
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&latency_wait_start);
    }
    if (frame_latency_wait_enabled() && frame_latency_waitable_ != nullptr) {
        const auto latency_wait_start_steady = collect_present_telemetry
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        constexpr DWORD kFrameLatencyWaitTimeoutMs = 17;
        const DWORD latency_result =
            WaitForSingleObjectEx(
                frame_latency_waitable_,
                kFrameLatencyWaitTimeoutMs,
                FALSE);
        if (latency_result != WAIT_OBJECT_0 &&
            latency_result != WAIT_TIMEOUT) {
            throw std::runtime_error("frame latency wait failed");
        }
        if (collect_present_telemetry) {
            last_latency_wait_us_ = elapsed_us(
                latency_wait_start_steady,
                std::chrono::steady_clock::now());
        }
    }
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&latency_wait_end);
    }
    if (collect_present_telemetry && qpc_frequency_.QuadPart != 0) {
        const double wait_ms =
            static_cast<double>(
                latency_wait_end.QuadPart - latency_wait_start.QuadPart) *
            1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
        telemetry_latency_wait_ms_ += wait_ms;
        telemetry_latency_wait_max_ms_ =
            std::max(telemetry_latency_wait_max_ms_, wait_ms);
    }
}

void RendererD3D12::pace_present_if_needed(
    unsigned vsync_interval,
    float max_fps) {
    // Runtime gameplay is paced by the native VI scheduler. A tiny bounded
    // render-thread delay can still smooth the handoff to DXGI when the VI edge
    // arrives a little early; keep it capped so it cannot become a producer
    // throttle or hide missing XFBs with duplicated presents.
    const bool realtime_paced = realtime_vi_paces_present();
    const std::uint64_t realtime_wait_cap_us =
        realtime_paced && smooth_realtime_present_enabled()
            ? smooth_realtime_present_max_wait_us()
            : 0u;
    if ((realtime_paced && realtime_wait_cap_us == 0u) ||
        vsync_interval != 0u ||
        max_fps <= 0.0f ||
        qpc_frequency_.QuadPart <= 0) {
        last_paced_present_qpc_ = 0;
        return;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (last_paced_present_qpc_ == 0) {
        last_paced_present_qpc_ = now.QuadPart;
        return;
    }

    const auto wait_start_qpc = now.QuadPart;
    const double ticks_per_frame =
        static_cast<double>(qpc_frequency_.QuadPart) /
        static_cast<double>(max_fps);
    const auto target =
        last_paced_present_qpc_ +
        static_cast<std::int64_t>(ticks_per_frame + 0.5);
    const std::int64_t wait_cap_ticks =
        realtime_paced
            ? static_cast<std::int64_t>(
                  (static_cast<double>(qpc_frequency_.QuadPart) *
                   static_cast<double>(realtime_wait_cap_us)) /
                  1'000'000.0)
            : std::numeric_limits<std::int64_t>::max();
    const std::int64_t wait_limit =
        realtime_paced ? now.QuadPart + wait_cap_ticks
                       : std::numeric_limits<std::int64_t>::max();
    while (now.QuadPart < target) {
        if (realtime_paced && now.QuadPart >= wait_limit) {
            break;
        }
        const std::int64_t capped_target =
            realtime_paced ? std::min<std::int64_t>(target, wait_limit)
                           : target;
        const double remaining_ms =
            static_cast<double>(capped_target - now.QuadPart) * 1000.0 /
            static_cast<double>(qpc_frequency_.QuadPart);
        if (remaining_ms >= 2.0) {
            Sleep(static_cast<DWORD>(remaining_ms - 1.0));
        } else {
            SwitchToThread();
        }
        QueryPerformanceCounter(&now);
    }

    if (trace_present_stats_enabled()) {
        const double wait_ms =
            static_cast<double>(now.QuadPart - wait_start_qpc) * 1000.0 /
            static_cast<double>(qpc_frequency_.QuadPart);
        telemetry_pace_wait_ms_ += wait_ms;
        telemetry_pace_wait_max_ms_ =
            std::max(telemetry_pace_wait_max_ms_, wait_ms);
    }

    const auto max_lag =
        static_cast<std::int64_t>(ticks_per_frame * 2.0);
    last_paced_present_qpc_ =
        (now.QuadPart < target && realtime_paced)
            ? now.QuadPart
            : ((now.QuadPart - target > max_lag) ? now.QuadPart : target);
}

std::uint64_t RendererD3D12::end_frame(bool present_swap_chain) {
    if (timestamp_query_heap_ && timestamp_readback_) {
        command_list_->EndQuery(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u + 1u);
        command_list_->ResolveQueryData(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u, 2u, timestamp_readback_.Get(),
            static_cast<UINT64>(frame_slot_) * 2u * sizeof(std::uint64_t));
    }
    const HRESULT close_hr = command_list_->Close();
    if (FAILED(close_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(close_hr, "CommandList Close");
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    timestamp_pending_[frame_slot_] =
        timestamp_query_heap_ != nullptr && timestamp_readback_ != nullptr;

    const std::uint64_t submitted_frame = frame_index_ + 1u;
    const std::uint64_t submitted_fence = ++next_fence_value_;
    fence_values_[frame_slot_] = submitted_fence;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), submitted_fence);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    // Readbacks are recorded on this command list, so this exact signal is the
    // only completion authority they need. Do not make their optional capture
    // diagnostics a synchronous GPU drain on the render thread.
    if (readback_pending_ && readback_fence_value_ == 0u) {
        readback_fence_value_ = submitted_fence;
    }
    if (backbuffer_readback_pending_ &&
        backbuffer_readback_fence_value_ == 0u) {
        backbuffer_readback_fence_value_ = submitted_fence;
    }
    if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ == 0u) {
        selected_xfb_readback_fence_ = submitted_fence;
    }

    if (present_swap_chain) {
        const RenderConfig cfg = get_render_config();
        const UINT vsync = std::min(2u, cfg.vsync_interval);
        const UINT present_flags =
            (vsync == 0 && tearing_supported_ && !exclusive_fullscreen_active_) ? DXGI_PRESENT_ALLOW_TEARING
                                               : 0u;
        {
            const auto stall_watch_pace_start = std::chrono::steady_clock::now();
            pace_present_if_needed(cfg.vsync_interval, cfg.max_fps);
            const double stall_watch_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() -
                    stall_watch_pace_start).count();
            if (trace_stall_watch_enabled() && stall_watch_ms > 12.0) {
                std::ostringstream message;
                message << "[stall-watch] pace_present_if_needed took "
                          << stall_watch_ms
                          << "ms vsync=" << cfg.vsync_interval
                          << " max-fps=" << cfg.max_fps << '\n';
                std::cerr << message.str();
            }
        }
        const bool collect_present_telemetry = trace_present_stats_enabled();
        LARGE_INTEGER present_start{};
        LARGE_INTEGER present_end{};
        if (collect_present_telemetry) {
            QueryPerformanceCounter(&present_start);
        }
        const auto stall_watch_present_start = std::chrono::steady_clock::now();
        const HRESULT present_hr = swap_chain_->Present(vsync, present_flags);
        {
            const double stall_watch_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() -
                    stall_watch_present_start).count();
            if (trace_stall_watch_enabled() && stall_watch_ms > 12.0) {
                std::ostringstream message;
                message << "[stall-watch] IDXGISwapChain::Present took "
                          << stall_watch_ms << "ms vsync=" << vsync
                          << " flags=" << present_flags << '\n';
                std::cerr << message.str();
            }
        }
        if (FAILED(present_hr)) {
            dump_device_removal(device_.Get());
            d3d_check(present_hr, "Present");
        }
        has_presented_swap_chain_ = true;
        if (collect_present_telemetry) {
            QueryPerformanceCounter(&present_end);
        }

        if (collect_present_telemetry && qpc_frequency_.QuadPart != 0) {
            const double present_ms =
                static_cast<double>(
                    present_end.QuadPart - present_start.QuadPart) *
                1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
            telemetry_present_ms_ += present_ms;
            telemetry_present_max_ms_ =
                std::max(telemetry_present_max_ms_, present_ms);

            if (telemetry_window_start_qpc_ == 0) {
                telemetry_window_start_qpc_ = present_end.QuadPart;
                telemetry_first_serial_start_ = xfb_present_stats().first_serial_presentations;
                telemetry_xfb_copy_start_ = xfb_copy_count();
            }
            if (last_present_qpc_ != 0) {
                const double interval_ms =
                    static_cast<double>(
                        present_end.QuadPart - last_present_qpc_) *
                    1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
                telemetry_interval_ms_ += interval_ms;
                telemetry_interval_max_ms_ =
                    std::max(telemetry_interval_max_ms_, interval_ms);
            }
            last_present_qpc_ = present_end.QuadPart;
            ++telemetry_present_count_;

            const double elapsed_seconds =
                static_cast<double>(
                    present_end.QuadPart - telemetry_window_start_qpc_) /
                static_cast<double>(qpc_frequency_.QuadPart);
            if (elapsed_seconds >= 2.0) {
                const auto progress = xfb_present_stats();
                const auto produced_copies = xfb_copy_count();
                const double present_fps =
                    static_cast<double>(telemetry_present_count_) /
                    elapsed_seconds;
                const double adjacent_serial_transition_hz =
                    static_cast<double>(
                        telemetry_adjacent_serial_transition_count_) /
                    elapsed_seconds;
                const double interval_avg =
                    telemetry_present_count_ > 1
                        ? telemetry_interval_ms_ /
                              static_cast<double>(
                                  telemetry_present_count_ - 1u)
                        : 0.0;
                const double present_avg =
                    telemetry_present_ms_ /
                    static_cast<double>(telemetry_present_count_);
                const double pace_wait_avg =
                    telemetry_pace_wait_ms_ /
                    static_cast<double>(telemetry_present_count_);
                const double latency_avg =
                    telemetry_latency_wait_ms_ /
                    static_cast<double>(telemetry_present_count_);
                const double gpu_avg =
                    telemetry_gpu_samples_ != 0
                        ? telemetry_gpu_ms_ /
                              static_cast<double>(telemetry_gpu_samples_)
                        : 0.0;
                const double same_serial_or_replay_hz =
                    std::max(
                        0.0, present_fps - adjacent_serial_transition_hz);
                const double same_serial_or_replay_percent =
                    present_fps > 0.0
                        ? same_serial_or_replay_hz * 100.0 / present_fps
                        : 0.0;

                // cerr is unit-buffered. Assemble one record before touching
                // the redirected stream so field formatting cannot turn a
                // single report into dozens of synchronous redirected writes while
                // the simulation waits for this render submission's token.
                std::ostringstream message;
                message
                    << "[present-stats] fps=" << present_fps
                    << " new-serial-hz="
                    << static_cast<double>(progress.first_serial_presentations - telemetry_first_serial_start_) / elapsed_seconds
                    << " xfb-production-hz="
                    << static_cast<double>(produced_copies - telemetry_xfb_copy_start_) / elapsed_seconds
                    << " first-serial-total=" << progress.first_serial_presentations
                    << " xfb-copy-total=" << produced_copies
                    << " steady-ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
                    << " adjacent-serial-transition-hz="
                    << adjacent_serial_transition_hz
                    << " same-serial-or-replay-hz="
                    << same_serial_or_replay_hz
                    << " same-serial-or-replay-pct="
                    << same_serial_or_replay_percent
                    << " frame-ms(avg/max)=" << interval_avg << "/"
                    << telemetry_interval_max_ms_
                    << " latency-wait-ms(avg/max)=" << latency_avg << "/"
                    << telemetry_latency_wait_max_ms_
                    << " pace-wait-ms(avg/max)=" << pace_wait_avg << "/"
                    << telemetry_pace_wait_max_ms_
                    << " smooth-present="
                    << (smooth_realtime_present_enabled() ? 1 : 0)
                    << " smooth-max-wait-us="
                    << smooth_realtime_present_max_wait_us()
                    << " present-ms(avg/max)=" << present_avg << "/"
                    << telemetry_present_max_ms_
                    << " vsync=" << vsync
                    << " present-flags=" << present_flags
                    << " tearing-enabled=" << (tearing_supported_ ? 1 : 0)
                    << " remote-session="
                    << (GetSystemMetrics(SM_REMOTESESSION) != 0 ? 1 : 0)
                    << " gpu-ms(avg)=" << gpu_avg
                    << " gpu-samples=" << telemetry_gpu_samples_
                    << " gpu-total-ms=" << telemetry_gpu_ms_
                    << " gpu-scope=completed-renderer-command-lists"
                    << " no-xfb-presents="
                    << telemetry_no_xfb_present_count_
                    << " backbuffer-clears="
                    << telemetry_backbuffer_clear_count_
                    << '\n';
                std::cerr << message.str();

                telemetry_window_start_qpc_ = present_end.QuadPart;
                telemetry_first_serial_start_ = progress.first_serial_presentations;
                telemetry_xfb_copy_start_ = produced_copies;
                telemetry_present_count_ = 0;
                telemetry_adjacent_serial_transition_count_ = 0;
                telemetry_interval_ms_ = 0.0;
                telemetry_interval_max_ms_ = 0.0;
                telemetry_present_ms_ = 0.0;
                telemetry_present_max_ms_ = 0.0;
                telemetry_pace_wait_ms_ = 0.0;
                telemetry_pace_wait_max_ms_ = 0.0;
                telemetry_latency_wait_ms_ = 0.0;
                telemetry_latency_wait_max_ms_ = 0.0;
                telemetry_gpu_ms_ = 0.0;
                telemetry_gpu_samples_ = 0;
                telemetry_no_xfb_present_count_ = 0;
                telemetry_backbuffer_clear_count_ = 0;
            }
        }

        if (!window_shown_ && window_ != nullptr &&
            !hidden_window_mode_enabled()) {
            window_shown_ = true;
            ShowWindowAsync(window_, SW_SHOWNORMAL);
        }
    }

    frame_index_ = submitted_frame;
    return submitted_fence;
}

std::uint64_t RendererD3D12::completed_fence_value() const {
    if (!fence_) {
        return 0u;
    }
    const std::uint64_t completed = fence_->GetCompletedValue();
    if (completed == ~std::uint64_t{0}) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "device removed while polling frame PE completion");
    }
    return completed;
}

void RendererD3D12::wait_for_fence_value(std::uint64_t fence_value) {
    if (!fence_ || fence_event_ == nullptr || fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    const std::uint64_t completed_before = fence_->GetCompletedValue();
    if (completed_before == ~std::uint64_t{0}) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "device removed before frame-specific GPU PE completion");
    }
    if (completed_before >= fence_value) {
        return;
    }
    const HRESULT event_hr =
        fence_->SetEventOnCompletion(fence_value, fence_event_);
    if (FAILED(event_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(event_hr, "frame PE SetEventOnCompletion");
    }
    const DWORD wait_result = WaitForSingleObjectEx(
        fence_event_, kFramePeCompletionTimeoutMs, FALSE);
    if (wait_result == WAIT_TIMEOUT) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "frame-specific GPU PE completion wait timed out");
    }
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD wait_error = GetLastError();
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "frame-specific GPU PE completion wait failed: result=" +
            std::to_string(wait_result) +
            " win32=" + std::to_string(wait_error));
    }
    const std::uint64_t completed_after = fence_->GetCompletedValue();
    if (completed_after == ~std::uint64_t{0}) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "device removed during frame-specific GPU PE completion");
    }
    if (completed_after < fence_value) {
        throw std::runtime_error(
            "frame-specific GPU PE completion event fired before its fence");
    }
}

// ---------------------------------------------------------------------------
// EFB binding and clearing
// ---------------------------------------------------------------------------

void RendererD3D12::bind_efb(const float xf_vp[6]) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    float vp_x = 0.0f, vp_y = 0.0f;
    float vp_w = static_cast<float>(kEfbWidth  * efb_scale_);
    float vp_h = static_cast<float>(kEfbHeight * efb_scale_);

    if (xf_vp) {
        // GX XF viewport: [xs, xo, ys, yo, zs, zo]
        // x_center = xo - 342,  half_w = |xs|
        // y_center = yo - 342,  half_h = |ys|
        const float xs = xf_vp[0], xo = xf_vp[1];
        const float ys = xf_vp[2], yo = xf_vp[3];
        const float scale = static_cast<float>(efb_scale_);
        const float hw = std::abs(xs) * scale;
        const float hh = std::abs(ys) * scale;
        const float cx = (xo - 342.0f) * scale;
        const float cy = (yo - 342.0f) * scale;
        vp_x = cx - hw; vp_y = cy - hh;
        vp_w = hw * 2.0f; vp_h = hh * 2.0f;
    }

    D3D12_VIEWPORT vp{vp_x, vp_y, vp_w, vp_h, 0.0f, 1.0f};
    D3D12_RECT sci{0, 0, static_cast<LONG>(kEfbWidth * efb_scale_), static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    bound_viewport_ = vp;
    bound_scissor_ = sci;
    bound_viewport_valid_ = true;
    bound_scissor_valid_ = true;
}

void RendererD3D12::clear_efb(
    std::uint32_t clear_ar, std::uint32_t clear_gb, std::uint32_t clear_z24,
    bool color_enable, bool alpha_enable, bool depth_enable,
    unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h,
    std::uint8_t pixel_format) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();

    // Scope the clear to the copy's src rect (scaled), per hardware.
    const LONG efb_w = static_cast<LONG>(kEfbWidth * efb_scale_);
    const LONG efb_h = static_cast<LONG>(kEfbHeight * efb_scale_);
    D3D12_RECT rect{0, 0, efb_w, efb_h};
    UINT num_rects = 0;
    const D3D12_RECT* rects = nullptr;
    if (rect_w != 0 && rect_h != 0) {
        const ScaledEfbRect scaled = compute_scaled_efb_rect(
            static_cast<std::int32_t>(rect_x),
            static_cast<std::int32_t>(rect_y),
            static_cast<std::int32_t>(rect_x + rect_w),
            static_cast<std::int32_t>(rect_y + rect_h),
            kEfbWidth,
            kEfbHeight,
            efb_scale_);
        rect = D3D12_RECT{
            scaled.left, scaled.top, scaled.right, scaled.bottom};
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        num_rects = 1;
        rects = &rect;
    }

    const auto clear = make_efb_clear_values(clear_ar, clear_gb, clear_z24, pixel_format);
    if (color_enable || alpha_enable) {
        if (pixel_format != 1u) alpha_enable = true;
        const float* col = clear.color.data();
        if (color_enable && alpha_enable) {
            command_list_->ClearRenderTargetView(rtv, col, num_rects, rects);
        } else {
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const D3D12_VIEWPORT vp{
                0.0f, 0.0f,
                static_cast<float>(efb_w), static_cast<float>(efb_h),
                0.0f, 1.0f};
            command_list_->RSSetViewports(1, &vp);
            command_list_->RSSetScissorRects(1, &rect);
            command_list_->SetGraphicsRootSignature(s_clear_root_sig.Get());
            command_list_->SetPipelineState(
                color_enable
                    ? clear_rgb_pipeline_.Get()
                    : clear_alpha_pipeline_.Get());
            command_list_->SetGraphicsRoot32BitConstants(0, 4, col, 0);
            command_list_->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->DrawInstanced(3, 1, 0, 0);

            command_list_->SetGraphicsRootSignature(root_signature_.Get());
            invalidate_gx_bindings();
            bind_efb(nullptr);
        }
    }
    if (depth_enable) {
        // REVERSED-Z buffer: GX z24 (near=0, far=2^24) inverts into the
        // D3D depth value.  GX_MAX_Z24 (0xFFFFFF = "clear to farthest")
        // becomes ≈0.0 here, which the flipped GEQUAL-style funcs pass.
        command_list_->ClearDepthStencilView(
            dsv, D3D12_CLEAR_FLAG_DEPTH, clear.reversed_depth, 0, num_rects, rects);
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void RendererD3D12::draw(const DrawCall& call) {
    if (call.pipeline != bound_pipeline_) {
        command_list_->SetPipelineState(call.pipeline);
        bound_pipeline_ = call.pipeline;
    }
    if (call.vs_constants &&
        call.vs_constants != bound_vs_constants_) {
        command_list_->SetGraphicsRootConstantBufferView(0, call.vs_constants);
        bound_vs_constants_ = call.vs_constants;
    }
    if (call.ps_constants &&
        call.ps_constants != bound_ps_constants_) {
        command_list_->SetGraphicsRootConstantBufferView(1, call.ps_constants);
        bound_ps_constants_ = call.ps_constants;
    }
    if (call.texture_table.ptr &&
        call.texture_table.ptr != bound_texture_table_.ptr) {
        command_list_->SetGraphicsRootDescriptorTable(2, call.texture_table);
        bound_texture_table_ = call.texture_table;
    }
    if (call.sampler_table.ptr &&
        call.sampler_table.ptr != bound_sampler_table_.ptr) {
        command_list_->SetGraphicsRootDescriptorTable(3, call.sampler_table);
        bound_sampler_table_ = call.sampler_table;
    }
    if (call.matrix_palette &&
        call.matrix_palette != bound_matrix_palette_) {
        command_list_->SetGraphicsRootShaderResourceView(4, call.matrix_palette);
        bound_matrix_palette_ = call.matrix_palette;
    }

    // Per-draw GX scissor, scaled to the EFB and clamped.  GX titles rely on
    // the scissor for UI clipping and split renders; an empty scissor is a
    // faithful "rasterize nothing" (also how CullMode::All is expressed).
    {
        const ScaledEfbRect scaled = compute_scaled_efb_rect(
            call.scissor.left,
            call.scissor.top,
            call.scissor.right,
            call.scissor.bottom,
            kEfbWidth,
            kEfbHeight,
            efb_scale_);
        if (scaled.empty()) {
            return;  // nothing can rasterize
        }
        const D3D12_RECT sci{
            scaled.left, scaled.top, scaled.right, scaled.bottom};
        if (!bound_scissor_valid_ ||
            sci.left != bound_scissor_.left ||
            sci.top != bound_scissor_.top ||
            sci.right != bound_scissor_.right ||
            sci.bottom != bound_scissor_.bottom) {
            command_list_->RSSetScissorRects(1, &sci);
            bound_scissor_ = sci;
            bound_scissor_valid_ = true;
        }
    }

    // Per-draw XF viewport (O8 closed — it was NOT benign).  SMG never uses
    // EFB copy-clear (boot 133: zero PE_COPY_EXECUTE with bit 11 across
    // 10k+ frames); it clears depth by drawing fullscreen quads with
    // GXSetViewport near==far==1.0 so the quad writes z=FAR regardless of
    // its geometry z.  Without the depth-range plumbing those clear quads
    // wrote z=NEAR, permanently poisoning the depth buffer → every LEQUAL
    // 3D draw failed (the boots 128-133 zero-fragment blocker O1).
    // XF layout (hardware/Dolphin xfmem): wd, ht, zRange, xOrig, yOrig, farZ.
    // Depth range — Dolphin BPFunctions::SetScissorAndViewport, the
    // REVERSED-Z form for backends without reversed-range support:
    //   gx_min = (farZ - zRange) / 2^24,  gx_max = farZ / 2^24
    //   MinDepth = 1 - gx_max,  MaxDepth = 1 - gx_min
    // Composes with the negated projection z (fill_projection): GX near →
    // depth MaxDepth side, GX far → MinDepth side; depth funcs are flipped
    // in pipeline_cache to match.
    if (call.has_viewport) {
        const ScaledGxViewport scaled =
            compute_scaled_gx_viewport(call.xf_viewport, efb_scale_);
        D3D12_VIEWPORT vp{
            scaled.x,
            scaled.y,
            scaled.width,
            scaled.height,
            scaled.min_depth,
            scaled.max_depth};
        if (scaled.valid()) {
            if (!bound_viewport_valid_ ||
                vp.TopLeftX != bound_viewport_.TopLeftX ||
                vp.TopLeftY != bound_viewport_.TopLeftY ||
                vp.Width != bound_viewport_.Width ||
                vp.Height != bound_viewport_.Height ||
                vp.MinDepth != bound_viewport_.MinDepth ||
                vp.MaxDepth != bound_viewport_.MaxDepth) {
                command_list_->RSSetViewports(1, &vp);
                bound_viewport_ = vp;
                bound_viewport_valid_ = true;
            }
        }
    }
    if (call.topology != bound_topology_) {
        command_list_->IASetPrimitiveTopology(call.topology);
        bound_topology_ = call.topology;
    }
    command_list_->IASetVertexBuffers(0, 1, &call.vertex_view);
    if (call.index_count > 0) {
        if (call.index_view.BufferLocation) {
            command_list_->IASetIndexBuffer(&call.index_view);
            command_list_->DrawIndexedInstanced(call.index_count, 1, 0, 0, 0);
        } else {
            command_list_->DrawInstanced(call.index_count, 1, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// EFB copy paths
// ---------------------------------------------------------------------------

D3D12_CPU_DESCRIPTOR_HANDLE RendererD3D12::allocate_conversion_rtv(
    ID3D12Resource* resource) {
    if (resource == nullptr || conv_rtv_heap_ == nullptr || conv_scratch_rtv_.ptr == 0u) {
        throw std::runtime_error(
            "[RendererD3D12] conversion RTV descriptor unavailable");
    }
    // RTV descriptors are consumed by OMSetRenderTargets at recording time.
    // Rebind this CPU-only slot each copy; frame fences own resource lifetime.
    device_->CreateRenderTargetView(resource, nullptr, conv_scratch_rtv_);
    return conv_scratch_rtv_;
}

// Records the conversion blit (see kConvPs).  Handles the EFB source
// transitions; the caller transitions `dest` to RENDER_TARGET first and back
// after.  Restores the GX root signature + EFB binding before returning.
void RendererD3D12::record_conversion_blit(
    ID3D12Resource* dest,
    unsigned dest_w,
    unsigned dest_h,
    D3D12_CPU_DESCRIPTOR_HANDLE dest_rtv,
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset) {
    ID3D12Resource* src =
        is_depth_copy ? efb_depth_.Get() : efb_color_.Get();

    // EFB -> shader resource.
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = src;
    b.Transition.StateBefore = is_depth_copy
        ? D3D12_RESOURCE_STATE_DEPTH_WRITE
        : D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    // Frame-ring SRV for the EFB source.  Depth reads use the R24/X8 view of
    // the typeless depth resource; the persistent CPU descriptor is copied
    // into the active shader-visible frame ring.
    const DescriptorRing::Table tbl = srv_ring_.allocate(1);
    const D3D12_CPU_DESCRIPTOR_HANDLE src_srv =
        is_depth_copy ? conv_efb_depth_srv_ : conv_efb_color_srv_;
    device_->CopyDescriptorsSimple(
        1,
        tbl.cpu,
        src_srv,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Scratch or persistent RTV (consumed by OMSetRenderTargets at record
    // time).  XFB copies pass an empty handle and use the scratch slot;
    // EFB-copy texture destinations pass their resource-owned cached RTV.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dest_rtv;
    if (rtv.ptr == 0u) {
        rtv = conv_scratch_rtv_;
        device_->CreateRenderTargetView(dest, nullptr, rtv);
    }

    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{
        0.0f, 0.0f,
        static_cast<float>(dest_w), static_cast<float>(dest_h),
        0.0f, 1.0f};
    const D3D12_RECT sci{
        0, 0, static_cast<LONG>(dest_w), static_cast<LONG>(dest_h)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    command_list_->SetPipelineState(conv_pipeline_.Get());

    const EfbConversionShaderConstants constants =
        make_efb_conversion_constants(
            params,
            is_depth_copy,
            intensity,
            keep_alpha,
            copy_format,
            src_x,
            src_y,
            step_x,
            step_y,
            filter_row_offset,
            efb_scale_,
            kEfbHeight);
    command_list_->SetGraphicsRoot32BitConstants(
        0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, tbl.gpu);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // EFB back to its steady state.
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Restore the GX drawing state (root signature changed; EFB RTV/DSV and
    // full viewport/scissor re-bound — per-draw viewport/scissor follow).
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
}

void RendererD3D12::copy_efb_to_xfb(const EfbCopyParams& params, XfbTexture& dest) {
    // The caller publishes a new XFB serial only after this returns. A
    // missing target must fail closed rather than masquerade as a new frame.
    if (!dest.texture) {
        throw std::runtime_error("XFB copy has no destination texture");
    }
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());
    dest.rtv_valid = true;

    // Dest XFB texture -> render target for the stretch blit (the y-scale
    // height difference between src rect and dest texture IS the stretch —
    // the old CopyTextureRegion could not express it).
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const EfbCopySamplingGeometry sampling =
        compute_xfb_sampling_geometry(
            params, dest.width, dest.height, efb_scale_);

    // Display copies never convert to intensity (Dolphin passes false on the
    // XFB path); gamma + copy filter (deflicker) do apply.
    record_conversion_blit(
        dest.texture.Get(), dest.width, dest.height, dest.rtv, params,
        /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
        /*copy_format=*/15u,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

}

bool RendererD3D12::copy_efb_to_texture(
    const EfbCopyParams& params, TextureCache& textures, bool is_depth_copy) {
    // SMG composites its visible frame from EFB->texture captures (the scene
    // is drawn, copied out at 640x456, and the final pass samples the
    // capture), so this path is required for any 3D pixel to reach the
    // screen.  The conversion blit applies intensity / gamma / copy filter /
    // half-scale and serves depth-source copies from efb_depth_ (SMG uses
    // depth copies for its focus/DOF effects).
    //
    // half_scale halves BOTH dimensions of the destination (Dolphin
    // TextureCacheBase scaleByHalf); the 2x step through the linear-filtered
    // source is the box filter.
    const UINT dw_unscaled = params.half_scale
        ? std::max<UINT>(1u, params.src_width / 2u)
        : params.src_width;
    const UINT dh_unscaled = params.half_scale
        ? std::max<UINT>(1u, params.src_height / 2u)
        : params.src_height;
    // Experimental until image/motion and route validation. Inspired by
    // Dolphin's Super Mario Galaxy Native Bloom graphics mod (iwubcode,
    // GPL-2.0-or-later): preserve native bloom surfaces while sampling the
    // actual high-resolution EFB. No scene/depth/XFB resolution reduction.
    static const bool native_bloom_workspace = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
            "GALAXY_NATIVE_BLOOM_WORKSPACE") == 0 &&
            length == 2u && value[0] == '1';
    }();
    // Diagnostic A/B control only: permits stationary-camera comparison in
    // one process. No file access or clock sampling in normal play. The
    // render worker owns this state; missing/malformed input preserves it.
    static const std::string bloom_comparison_file = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
            "GALAXY_NATIVE_BLOOM_COMPARISON_FILE") == 0 && length > 1u
            ? std::string(value) : std::string{};
    }();
    static bool comparison_enabled = native_bloom_workspace;
    static ULONGLONG comparison_last_poll = 0u;
    const bool bloom_copy = (native_bloom_workspace || !bloom_comparison_file.empty()) &&
        efb_scale_ > 1u &&
        is_galaxy_bloom_workspace_copy(params, is_depth_copy);
    if (bloom_copy && !bloom_comparison_file.empty()) {
        const auto now = GetTickCount64();
        if (comparison_last_poll == 0u || now - comparison_last_poll >= 250u) {
            comparison_last_poll = now;
            const HANDLE file = CreateFileA(bloom_comparison_file.c_str(),
                GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                char token[4]{};
                DWORD count = 0u;
                const BOOL read = ReadFile(file, token, sizeof(token), &count, nullptr);
                CloseHandle(file);
                const bool valid = read && count >= 1u && count <= 3u &&
                    (token[0] == '0' || token[0] == '1') &&
                    (count == 1u || (count == 2u && token[1] == '\n') ||
                     (count == 3u && token[1] == '\r' && token[2] == '\n'));
                if (valid && comparison_enabled != (token[0] == '1')) {
                    comparison_enabled = token[0] == '1';
                    std::cerr << "[bloom-comparison] native=" << comparison_enabled
                              << " scope=diagnostic-not-performance\n";
                }
            }
        }
    }
    const bool native_bloom = bloom_copy && comparison_enabled;
    const ScaledEfbCopyExtent extent = compute_scaled_efb_copy_extent(
        static_cast<std::uint16_t>(dw_unscaled),
        static_cast<std::uint16_t>(dh_unscaled),
        native_bloom ? 1u : efb_scale_);
    const UINT w = extent.width;
    const UINT h = extent.height;
    if (w == 0 || h == 0) {
        return false;
    }

    // Keyed by (addr, format, depth-source): SMG reuses one address with
    // different copy formats and each needs its own converted texture.
    const std::uint64_t dest_key =
        static_cast<std::uint64_t>(params.dest_addr) |
        (static_cast<std::uint64_t>(params.target_format) << 32) |
        (is_depth_copy ? (1ull << 40) : 0ull) |
        // One guest address alternates between early scaled masks and the
        // final native bloom composite. Cache both resources to avoid
        // reallocating on every pass/frame; the latest alias still wins.
        (native_bloom ? (1ull << 41) : 0ull);

    EfbCopyDest& dest = efb_copy_dests_[dest_key];
    const bool created =
        !dest.texture || dest.width != w || dest.height != h;
    if (created) {
        if (dest.texture) {
            retired_efb_copy_textures_[frame_slot_].push_back(
                std::move(dest.texture));
        }
        dest.rtv = {};
        dest.rtv_valid = false;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = kEfbColorFormat;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const HRESULT create_hr = device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr, IID_PPV_ARGS(&dest.texture));
        if (FAILED(create_hr)) {
            dump_device_removal(device_.Get());
            efb_copy_dests_.erase(dest_key);
            std::ostringstream message;
            message << "[efb-copy] dest texture allocation failed"
                    << " hr=0x" << std::hex
                    << static_cast<unsigned long>(create_hr)
                    << " addr=0x" << std::hex << params.dest_addr
                    << " key=0x" << dest_key
                    << " size=" << std::dec << w << 'x' << h;
            throw std::runtime_error(message.str());
        }
        dest.width  = w;
        dest.height = h;
    }
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());
    dest.rtv_valid = true;

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const EfbCopySamplingGeometry sampling =
        compute_efb_copy_sampling_geometry(params, w, h, efb_scale_);

    // Intensity copies to the RA4/RA8 (-> IA4/IA8) formats keep the source
    // alpha; pure-I formats replicate Y into alpha, matching the texture
    // decoder's I-format convention.
    const bool keep_alpha =
        params.target_format == 2u || params.target_format == 3u;

    record_conversion_blit(
        dest.texture.Get(), w, h, dest.rtv, params,
        is_depth_copy, params.intensity, keep_alpha,
        params.target_format,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Refresh the guest-address alias every copy: with format-keyed dest
    // textures one address can alternate between resources, and the alias
    // must always point at the most recent copy (register_efb_copy is a
    // cheap no-op when the resource is unchanged).
    return textures.register_efb_copy(params.dest_addr, params, dest.texture);
}

bool RendererD3D12::ensure_peek_resources() {
    if (peek_target_ && peek_readback_ && peek_rtv_heap_ &&
        peek_srv_heap_ && peek_allocator_ && peek_command_list_) {
        return true;
    }
    if (!device_ || !queue_ || !fence_ || !efb_color_ || !efb_depth_ ||
        !conv_pipeline_ || !s_conv_root_sig) {
        return false;
    }

    d3d_check(
        device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&peek_allocator_)),
        "GXPeek command allocator");
    d3d_check(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            peek_allocator_.Get(),
            nullptr,
            IID_PPV_ARGS(&peek_command_list_)),
        "GXPeek command list");
    d3d_check(peek_command_list_->Close(), "GXPeek command list close");
    peek_command_list_->SetName(L"GxPeekList");

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = 1;
    target_desc.Height = 1;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = kEfbColorFormat;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE target_clear{};
    target_clear.Format = kEfbColorFormat;
    target_clear.Color[3] = 1.0f;
    d3d_check(
        device_->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &target_desc,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            &target_clear,
            IID_PPV_ARGS(&peek_target_)),
        "GXPeek target");

    peek_readback_row_pitch_ = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = peek_readback_row_pitch_;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d3d_check(
        device_->CreateCommittedResource(
            &readback_heap,
            D3D12_HEAP_FLAG_NONE,
            &readback_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&peek_readback_)),
        "GXPeek readback");

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    d3d_check(
        device_->CreateDescriptorHeap(
            &rtv_heap_desc,
            IID_PPV_ARGS(&peek_rtv_heap_)),
        "GXPeek RTV heap");
    peek_rtv_ = peek_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(peek_target_.Get(), nullptr, peek_rtv_);

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    d3d_check(
        device_->CreateDescriptorHeap(
            &srv_heap_desc,
            IID_PPV_ARGS(&peek_srv_heap_)),
        "GXPeek SRV heap");
    peek_srv_ = peek_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    peek_srv_gpu_ = peek_srv_heap_->GetGPUDescriptorHandleForHeapStart();
    return true;
}

bool RendererD3D12::peek_efb(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value) {
    return read_efb_pixels(x, y, kind, value, {});
}

bool RendererD3D12::capture_pointer_depth(std::span<std::uint32_t> pixels) {
    if (pixels.size() != kEfbWidth * kEfbHeight) {
        throw std::invalid_argument("pointer depth field requires 640x528 pixels");
    }
    std::uint32_t unused{};
    return read_efb_pixels(0u, 0u, EfbPeekKind::Depth, unused, pixels);
}

bool RendererD3D12::read_efb_pixels(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
    std::uint32_t& value, std::span<std::uint32_t> depth_pixels) {
    value = 0;
    if (x >= kEfbWidth || y >= kEfbHeight || !ensure_peek_resources()) {
        return false;
    }

    const bool field = !depth_pixels.empty();
    const UINT width = field ? kEfbWidth : 1u;
    const UINT height = field ? kEfbHeight : 1u;
    const UINT row_pitch = field ? kEfbWidth * 4u : peek_readback_row_pitch_;
    if (field && !pointer_depth_target_) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = peek_target_->GetDesc();
        td.Width = width; td.Height = height;
        d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
            &td, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
            IID_PPV_ARGS(&pointer_depth_target_)), "pointer depth target");
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = peek_readback_->GetDesc();
        rd.Width = static_cast<UINT64>(row_pitch) * height;
        d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&pointer_depth_readback_)), "pointer depth readback");
    }
    ID3D12Resource* target = field ? pointer_depth_target_.Get() : peek_target_.Get();
    ID3D12Resource* readback = field ? pointer_depth_readback_.Get() : peek_readback_.Get();
    // All preceding uses of these private utility descriptors have completed
    // their exact fence before the next utility can enter on the render owner.
    device_->CreateRenderTargetView(target, nullptr, peek_rtv_);
    const bool is_depth = kind == EfbPeekKind::Depth;
    ID3D12Resource* src = is_depth ? efb_depth_.Get() : efb_color_.Get();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Format = is_depth ? kEfbDepthSrvFormat : kEfbColorFormat;
    device_->CreateShaderResourceView(src, &srv_desc, peek_srv_);

    d3d_check(peek_allocator_->Reset(), "GXPeek allocator reset");
    d3d_check(
        peek_command_list_->Reset(peek_allocator_.Get(), nullptr),
        "GXPeek command list reset");

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore =
        is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                 : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    peek_command_list_->ResourceBarrier(2, barriers);

    ID3D12DescriptorHeap* heaps[] = {peek_srv_heap_.Get()};
    peek_command_list_->SetDescriptorHeaps(1, heaps);
    peek_command_list_->OMSetRenderTargets(1, &peek_rtv_, FALSE, nullptr);
    const D3D12_VIEWPORT kViewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT kScissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    peek_command_list_->RSSetViewports(1, &kViewport);
    peek_command_list_->RSSetScissorRects(1, &kScissor);
    peek_command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    peek_command_list_->SetPipelineState(conv_pipeline_.Get());

    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(x, y, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, kind);
    if (field) constants.values[15] = -1.0f;
    peek_command_list_->SetGraphicsRoot32BitConstants(
        0, 16, constants.values.data(), 0);
    peek_command_list_->SetGraphicsRootDescriptorTable(1, peek_srv_gpu_);
    peek_command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    peek_command_list_->DrawInstanced(3, 1, 0, 0);

    std::swap(
        barriers[0].Transition.StateBefore,
        barriers[0].Transition.StateAfter);
    std::swap(
        barriers[1].Transition.StateBefore,
        barriers[1].Transition.StateAfter);
    peek_command_list_->ResourceBarrier(2, barriers);

    D3D12_TEXTURE_COPY_LOCATION src_copy{};
    src_copy.pResource = target;
    src_copy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst_copy{};
    dst_copy.pResource = readback;
    dst_copy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_copy.PlacedFootprint.Footprint.Format = kEfbColorFormat;
    dst_copy.PlacedFootprint.Footprint.Width = width;
    dst_copy.PlacedFootprint.Footprint.Height = height;
    dst_copy.PlacedFootprint.Footprint.Depth = 1;
    dst_copy.PlacedFootprint.Footprint.RowPitch = row_pitch;
    peek_command_list_->CopyTextureRegion(
        &dst_copy, 0, 0, 0, &src_copy, nullptr);

    d3d_check(peek_command_list_->Close(), "GXPeek command list close");
    ID3D12CommandList* lists[] = {peek_command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    const std::uint64_t fence_value = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "GXPeek queue signal");
    }
    if (fence_->GetCompletedValue() < fence_value) {
        const HRESULT event_hr =
            fence_->SetEventOnCompletion(fence_value, fence_event_);
        if (FAILED(event_hr)) {
            dump_device_removal(device_.Get());
            d3d_check(event_hr, "GXPeek SetEventOnCompletion");
        }
        const DWORD wait_result =
            WaitForSingleObjectEx(fence_event_, 30000, FALSE);
        if (wait_result != WAIT_OBJECT_0) {
            dump_device_removal(device_.Get());
            throw std::runtime_error("GXPeek GPU fence wait timed out");
        }
    }

    void* mapped = nullptr;
    const D3D12_RANGE read_range{
        0, static_cast<SIZE_T>(row_pitch) * height};
    d3d_check(
        readback->Map(0, &read_range, &mapped),
        "GXPeek readback map");
    const auto* rgba = static_cast<const std::uint8_t*>(mapped);
    if (field) {
        for (std::size_t row = 0; row < height; ++row) {
            const auto* pixel = rgba + row * row_pitch;
            for (std::size_t col = 0; col < width; ++col, pixel += 4) {
                depth_pixels[row * width + col] =
                    (static_cast<std::uint32_t>(pixel[0]) << 16u) |
                    (static_cast<std::uint32_t>(pixel[1]) << 8u) | pixel[2];
            }
        }
    }
    const std::uint32_t r = rgba[0];
    const std::uint32_t g = rgba[1];
    const std::uint32_t b = rgba[2];
    const std::uint32_t a = rgba[3];
    if (is_depth) {
        value = (r << 16) | (g << 8) | b;
    } else {
        value = (a << 24) | (r << 16) | (g << 8) | b;
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
    return true;
}

// ---------------------------------------------------------------------------
// Bring-up EFB readback diagnostics
// ---------------------------------------------------------------------------

void RendererD3D12::set_debug_capture_paths(
    std::string efb_ppm_path,
    std::string backbuffer_ppm_path) {
    next_efb_capture_path_ = std::move(efb_ppm_path);
    next_backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

void RendererD3D12::reap_debug_capture_writers(bool wait_for_all) {
    for (auto it = debug_ppm_writers_.begin();
         it != debug_ppm_writers_.end();) {
        if (!wait_for_all &&
            it->completion.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            ++it;
            continue;
        }
        try {
            it->completion.get();
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "asynchronous " + it->tag + " capture failed for " +
                it->path + ": " + error.what());
        }
        std::cerr << '[' << it->tag << "] wrote " << it->path << '\n';
        it = debug_ppm_writers_.erase(it);
    }
}

void RendererD3D12::queue_debug_ppm_capture(
    const char* tag,
    const std::string& path,
    const std::uint8_t* source,
    unsigned width,
    unsigned height,
    unsigned row_pitch) {
    constexpr std::size_t kMaximumPendingDebugCaptures = 8u;
    reap_debug_capture_writers(false);
    if (debug_ppm_writers_.size() >= kMaximumPendingDebugCaptures) {
        throw std::runtime_error(
            "debug PPM capture writer queue saturated; refusing to drop a capture");
    }
    if (source == nullptr || width == 0u || height == 0u ||
        row_pitch < width * 4u) {
        throw std::runtime_error("invalid mapped debug PPM capture");
    }
    const std::size_t source_row_bytes =
        static_cast<std::size_t>(width) * 4u;
    if (height > std::numeric_limits<std::size_t>::max() / source_row_bytes) {
        throw std::runtime_error("debug PPM capture size overflow");
    }
    std::vector<std::uint8_t> rgba(
        source_row_bytes * static_cast<std::size_t>(height));
    for (unsigned y = 0u; y < height; ++y) {
        std::memcpy(
            rgba.data() + static_cast<std::size_t>(y) * source_row_bytes,
            source + static_cast<std::size_t>(y) * row_pitch,
            source_row_bytes);
    }

    DebugPpmWriter writer{};
    writer.tag = tag;
    writer.path = path;
    writer.completion = std::async(
        std::launch::async,
        [path, width, height, rgba = std::move(rgba)]() {
            FILE* capture = nullptr;
            if (fopen_s(&capture, path.c_str(), "wb") != 0 ||
                capture == nullptr) {
                throw std::runtime_error("could not open output file");
            }
            const auto close_capture = [&capture]() noexcept {
                if (capture != nullptr) {
                    std::fclose(capture);
                    capture = nullptr;
                }
            };
            try {
                if (std::fprintf(capture, "P6\n%u %u\n255\n", width, height) <
                    0) {
                    throw std::runtime_error("could not write PPM header");
                }
                std::vector<std::uint8_t> rgb(
                    static_cast<std::size_t>(width) * 3u);
                for (unsigned y = 0u; y < height; ++y) {
                    const auto* source_row = rgba.data() +
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(width) * 4u;
                    for (unsigned x = 0u; x < width; ++x) {
                        rgb[x * 3u + 0u] = source_row[x * 4u + 0u];
                        rgb[x * 3u + 1u] = source_row[x * 4u + 1u];
                        rgb[x * 3u + 2u] = source_row[x * 4u + 2u];
                    }
                    if (std::fwrite(rgb.data(), 1u, rgb.size(), capture) !=
                        rgb.size()) {
                        throw std::runtime_error("could not write PPM pixels");
                    }
                }
                if (std::fclose(capture) != 0) {
                    capture = nullptr;
                    throw std::runtime_error("could not finalize output file");
                }
                capture = nullptr;
            } catch (...) {
                close_capture();
                throw;
            }
        });
    debug_ppm_writers_.push_back(std::move(writer));
}

void RendererD3D12::debug_copy_efb_to_readback() {
    if (readback_pending_) {
        throw std::runtime_error(
            "EFB capture requested while a prior capture readback is pending");
    }
    const UINT w = kEfbWidth * efb_scale_;
    const UINT h = kEfbHeight * efb_scale_;
    const UINT row_pitch =
        (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    if (!readback_buf_) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = static_cast<UINT64>(row_pitch) * h;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&readback_buf_)))) {
            return;
        }
    }

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = efb_color_.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = efb_color_.Get();
    src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback_buf_.Get();
    dst.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format   = kEfbColorFormat;
    dst.PlacedFootprint.Footprint.Width    = w;
    dst.PlacedFootprint.Footprint.Height   = h;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
    readback_pending_ = true;
    readback_fence_value_ = 0u;
    readback_capture_path_ = next_efb_capture_path_;
}

void RendererD3D12::debug_log_readback() {
    reap_debug_capture_writers(false);
    if (!readback_pending_ || !readback_buf_) {
        return;
    }
    if (readback_fence_value_ == 0u ||
        completed_fence_value() < readback_fence_value_) {
        return;
    }
    readback_pending_ = false;
    readback_fence_value_ = 0u;

    const UINT w = kEfbWidth * efb_scale_;
    const UINT h = kEfbHeight * efb_scale_;
    const UINT row_pitch =
        (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(row_pitch) * h};
    if (FAILED(readback_buf_->Map(0, &range, &mapped))) {
        readback_capture_path_.clear();
        return;
    }
    std::uint64_t nonblack = 0, alpha_hits = 0, rs = 0, gs = 0, bs = 0;
    std::uint8_t rmax = 0, gmax = 0, bmax = 0, amax = 0;
    const auto* base = static_cast<const std::uint8_t*>(mapped);
    for (UINT y = 0; y < h; y += 4) {           // sample every 4th row/col
        const std::uint8_t* row = base + static_cast<std::size_t>(y) * row_pitch;
        for (UINT x = 0; x < w; x += 4) {
            const std::uint8_t r = row[x * 4 + 0];
            const std::uint8_t g = row[x * 4 + 1];
            const std::uint8_t bl = row[x * 4 + 2];
            const std::uint8_t a = row[x * 4 + 3];
            rs += r; gs += g; bs += bl;
            rmax = std::max(rmax, r); gmax = std::max(gmax, g);
            bmax = std::max(bmax, bl); amax = std::max(amax, a);
            if (r > 8 || g > 8 || bl > 8) ++nonblack;
            if (a > 8) ++alpha_hits;
        }
    }
    const std::uint64_t samples =
        (static_cast<std::uint64_t>((h + 3) / 4)) * ((w + 3) / 4);

    if (!readback_capture_path_.empty()) {
        queue_debug_ppm_capture(
            "efb-dump",
            readback_capture_path_,
            base,
            w,
            h,
            row_pitch);
    }

    D3D12_RANGE no_write{0, 0};
    readback_buf_->Unmap(0, &no_write);
    readback_capture_path_.clear();
    std::cerr << "[efb-dump] nonblack=" << nonblack << "/" << samples
              << " alpha=" << alpha_hits
              << " mean=(" << rs / samples << "," << gs / samples << ","
              << bs / samples << ") max=("
              << static_cast<unsigned>(rmax) << ","
              << static_cast<unsigned>(gmax) << ","
              << static_cast<unsigned>(bmax) << ","
              << static_cast<unsigned>(amax) << ")\n";
}

// ---------------------------------------------------------------------------
// present
// ---------------------------------------------------------------------------

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
    case '/': return {0x01u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x10u};
    case '%': return {0x19u, 0x19u, 0x02u, 0x04u, 0x08u, 0x13u, 0x13u};
    case '<': return {0x02u, 0x04u, 0x08u, 0x10u, 0x08u, 0x04u, 0x02u};
    case '>': return {0x08u, 0x04u, 0x02u, 0x01u, 0x02u, 0x04u, 0x08u};
    default: return {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    }
}

// Draw-call based (see ensure_native_settings_solid_pipeline_loaded): a
// real repro on an RTX 5090 (driver 32.0.15.9597) showed the previous
// ClearRenderTargetView(rtv, color, NumRects>0, pRects)-based
// implementation of these two helpers -- used to draw the native settings
// overlay's panel background and per-glyph text pixels -- reliably fault
// inside nvwgf2umx.dll itself with STATUS_STACK_BUFFER_OVERRUN, a fail-fast
// that bypasses this process's own SEH/terminate handlers entirely
// (confirmed via Application-log Event 1000: "Faulting module name:
// nvwgf2umx.dll"). This reproduced identically (same fault offset) with the
// rect batch size cut from 1024 to 48 and with the corner texture draw path
// disabled entirely, isolating the trigger to the rect-limited
// ClearRenderTargetView call itself rather than to batch size or to the
// corner-texture code. Full-target clears (NumRects == 0, pRects ==
// nullptr) are unaffected and still used elsewhere in this file.
void ui_rect(
    RendererD3D12& renderer,
    int x,
    int y,
    int w,
    int h,
    const std::array<float, 4>& color) {
    renderer.draw_ui_solid_quad(x, y, w, h, color);
}

void ui_text(
    RendererD3D12& renderer,
    int x,
    int y,
    int scale,
    const std::string& text,
    const std::array<float, 4>& color) {
    int cursor_x = x;
    for (const char ch : text) {
        if (ch == ' ') {
            cursor_x += 4 * scale;
            continue;
        }
        const std::array<std::uint8_t, 7> rows = ui_glyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) != 0u) {
                    renderer.draw_ui_solid_quad(
                        cursor_x + col * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color);
                }
            }
        }
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

const char* exposed_text(const RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

std::string native_settings_row_text(unsigned tab, unsigned row) {
    const RenderConfig render = get_render_config();
    const RuntimeSettings runtime = get_runtime_settings();
    const RenderFeatureCaps caps = get_render_feature_caps();
    char text[96]{};
    switch (tab % kNativeSettingsTabCount) {
    case 0:
        switch (row) {
        case 0:
            std::snprintf(
                text,
                sizeof(text),
                "INTERNAL RES: %uX -> %uX RESTART",
                std::clamp(render.efb_scale, 1u, kMaxEfbScale),
                get_next_start_efb_scale());
            return text;
        case 1:
            if (render.max_fps <= 0.0f) {
                return std::string("PRESENT: VSYNC ") +
                       (render.vsync_interval == 0u ? "OFF" : "ON") +
                       " CAP INF RO";
            }
            std::snprintf(
                text,
                sizeof(text),
                "PRESENT: VSYNC %s CAP %.0f RO",
                render.vsync_interval == 0u ? "OFF" : "ON",
                static_cast<double>(render.max_fps));
            return text;
        default:
            return {};
        }
    case 1:
        return std::string("ACTIVE SOURCE (LAUNCHER): ") +
               runtime_input_mode_name(runtime.input_mode);
    case 2:
        switch (row) {
        case 0: return std::string("MASTER: ") + percent_text(runtime.audio.master);
        case 1: return std::string("MUSIC: ") + percent_text(runtime.audio.music);
        case 2: return std::string("SFX: ") + percent_text(runtime.audio.sfx);
        case 3: return std::string("VOICE: ") + percent_text(runtime.audio.voice);
        case 4: return std::string("AMBIENCE: ") + percent_text(runtime.audio.ambience);
        default: return {};
        }
    case 3:
        switch (row) {
        case 0:
            std::snprintf(
                text,
                sizeof(text),
                "DXR: TIER %u %s",
                caps.dxr_raytracing_tier,
                exposed_text(caps.dxr));
            return text;
        case 1:
            return std::string("STREAMLINE: ") +
                   exposed_text(caps.streamline);
        case 2:
            return std::string("REFLEX: ") + exposed_text(caps.reflex);
        case 3:
            return std::string("DLSS: ") +
                   exposed_text(caps.dlss_super_resolution);
        case 4:
            return std::string("DLFG: ") +
                   exposed_text(caps.dlss_frame_generation);
        default:
            return {};
        }
    default:
        return {};
    }
}

// A small clickable "PC SETTINGS" button, drawn only while
// galaxy::host::file_select_scene_active() says the guest is on the
// save-file-select screen -- so it reads as part of that screen instead of
// a keyboard-only developer overlay. Left-click inside it opens the same
// native settings panel F1 does (F1 remains a working accessibility
// fallback; this is the primary, discoverable entry point). Deliberately
// mouse-driven and drawn with the file-select screen's own extracted
// window-frame texture rather than patching the guest's own
// FileSelectButton/ButtonPaneController code: locating and hooking that
// safely would need a real interactive decompiler (see the session's
// FileSelectButton xref investigation), which isn't available here, and
// guessing an address risks guest memory corruption. This is the closest
// safe equivalent: a native button that looks native to this screen and
// reuses the game's own UI art, without touching a single guest
// instruction.
void draw_and_handle_file_select_settings_button(RendererD3D12& renderer) {
    if (!native_settings_ui_enabled()) {
        return;
    }
    if (!galaxy::host::file_select_scene_active()) {
        return;
    }
    if (s_native_settings_visible.load(std::memory_order_relaxed)) {
        return;  // the full panel is already open; nothing else to draw.
    }
    if (s_bb_width < 320u || s_bb_height < 240u) {
        return;
    }
    const bool compact = s_bb_width < 854u || s_bb_height < 480u;
    const int scale = compact ? 1 : (s_bb_width >= 1280u ? 2 : 1);
    const int margin = compact ? 8 : 16;
    const int button_w = compact ? 118 : 176;
    const int button_h = compact ? 20 : 32;
    const int button_x = margin;
    const int button_y = margin;

    const std::array<float, 4> button_bg{0.02f, 0.025f, 0.035f, 0.82f};
    const std::array<float, 4> button_rail{0.15f, 0.72f, 0.85f, 0.9f};
    const std::array<float, 4> button_text{0.88f, 0.94f, 0.96f, 1.0f};
    renderer.draw_ui_solid_quad(button_x, button_y, button_w, button_h, button_bg);
    renderer.draw_ui_solid_quad(button_x, button_y, 4, button_h, button_rail);

    renderer.ensure_native_settings_corner_texture_loaded();
    {
        constexpr int kCornerPx = 12;
        const std::array<float, 4> corner_tint{1.0f, 1.0f, 1.0f, 0.85f};
        renderer.draw_ui_textured_quad(
            button_x + button_w - kCornerPx, button_y, kCornerPx, kCornerPx,
            1.0f, 0.0f, 0.0f, 1.0f, corner_tint);
    }
    ui_text(
        renderer,
        button_x + 12,
        button_y + (compact ? 6 : 10),
        scale,
        "PC SETTINGS",
        button_text);

    // Edge-detected left click, hit-tested in client-pixel coordinates
    // (which match backbuffer pixel coordinates 1:1 -- see
    // poll_host_pointer_state()). A local static is enough here: exactly
    // one native settings overlay/button exists per process, matching the
    // convention already used for s_native_settings_visible etc. above.
    static bool s_button_left_was_down = false;
    const HostPointerState pointer = poll_host_pointer_state();
    const bool inside_button =
        pointer.inside_client &&
        pointer.client_x >= button_x &&
        pointer.client_x < button_x + button_w &&
        pointer.client_y >= button_y &&
        pointer.client_y < button_y + button_h;
    if (pointer.left_button && inside_button && !s_button_left_was_down) {
        set_native_settings_visible(true);
    }
    s_button_left_was_down = pointer.left_button;
}

void draw_native_settings_overlay(RendererD3D12& renderer) {
    if (!s_native_settings_visible.load(std::memory_order_relaxed)) {
        return;
    }

    const unsigned tab =
        s_native_settings_tab.load(std::memory_order_relaxed) %
        kNativeSettingsTabCount;
    const NativeSettingsLayout layout =
        compute_native_settings_layout(s_bb_width, s_bb_height, tab);
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
        "NATIVE SETTINGS",
        text);

    const char* tabs[] = {"VIDEO", "INPUT", "AUDIO", "CAPS"};
    int tab_x = panel_x + 28;
    const int tab_step = std::max(100, (panel_w - 56) / 4);
    for (unsigned i = 0; i < kNativeSettingsTabCount; ++i) {
        ui_text(
            renderer,
            tab_x,
            panel_y + (scale == 1 ? 30 : 70),
            scale,
            tabs[i],
            i == tab ? accent : muted);
        tab_x += tab_step;
    }

    const unsigned row_count = native_settings_row_count(tab);
    const unsigned selected_row =
        std::min(s_native_settings_row.load(std::memory_order_relaxed),
                 row_count == 0u ? 0u : row_count - 1u);
    for (unsigned row = 0; row < row_count; ++row) {
        const int y =
            layout.rows_y + static_cast<int>(row) * layout.row_step;
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
            native_settings_row_text(tab, row),
            text);
    }

    const int footer_y = layout.footer_y;
    const int footer_scale = std::min(2, scale);
    const int footer_step = footer_scale == 1 ? 18 : 28;
    ui_text(
        renderer,
        panel_x + 28,
        footer_y,
        footer_scale,
        s_native_settings_save_error.load(std::memory_order_acquire)
            ? "SAVE FAILED - RETRY ON NEXT CLOSE"
            : "INTERNAL RES APPLIES AFTER RESTART",
        s_native_settings_save_error.load(std::memory_order_relaxed)
            ? error
            : muted);
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step,
        footer_scale,
        "F1 TOGGLE  TAB PAGE",
        muted);
    ui_text(
        renderer,
        panel_x + 28,
        footer_y + footer_step * 2,
        footer_scale,
        "ARROWS MOVE/ADJUST  ESC CLOSE",
        muted);
}

void RendererD3D12::present(
    const XfbTexture* source, bool capture_backbuffer) {
    // Optional diagnostic: measure the wall-clock gap
    // between successive present() *entries*, independent of the
    // trace_present_stats_enabled()-gated telemetry below. Everything that
    // runs between two present() calls -- the OS message pump, checkpoint
    // servicing, guest simulation -- lands in this gap, so it catches a
    // stall the begin_frame/Present/overlay-draw-local stall-watch probes
    // miss entirely because it happens *outside* all of them.
    if (trace_stall_watch_enabled()) {
        static std::chrono::steady_clock::time_point
            stall_watch_last_present_entry{};
        const auto stall_watch_now = std::chrono::steady_clock::now();
        if (stall_watch_last_present_entry.time_since_epoch().count() != 0) {
            const double stall_watch_gap_ms =
                std::chrono::duration<double, std::milli>(
                    stall_watch_now - stall_watch_last_present_entry)
                    .count();
            if (stall_watch_gap_ms > 20.0) {
                std::ostringstream message;
                message << "[stall-watch] gap between present() entries: "
                          << stall_watch_gap_ms << "ms\n";
                std::cerr << message.str();
            }
        }
        stall_watch_last_present_entry = stall_watch_now;
    }
    const bool can_blit =
        source != nullptr && source->texture && blit_pipeline_ &&
        s_blit_root_sig;
    if (!can_blit) {
        ++telemetry_no_xfb_present_count_;
        throw std::runtime_error(
            "present requested without a valid XFB blit source");
    }
    if (capture_backbuffer) {
        debug_log_selected_xfb_readback();
        debug_copy_selected_xfb_to_readback(*source);
    }

    const auto ultrawide =
        galaxy::experimental_effective_host_aspect();
    if (ultrawide &&
        (source->width != kEfbWidth * efb_scale_ ||
         source->height == 0u ||
         source->height > kEfbHeight * efb_scale_)) {
        throw std::runtime_error(
            "RMGE01 ultrawide aspect requires a full-width Wii XFB copy");
    }

    const std::uint64_t present_frame = frame_index_ + 1u;
    const bool trace_luma = trace_present_luma_frame(present_frame);
    const bool record_backbuffer = capture_backbuffer || trace_luma;

    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    const UINT rtv_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * rtv_stride;

    // Transition backbuffer → RENDER_TARGET.
    D3D12_RESOURCE_BARRIER bar{};
    bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource   = backbuffers_[buf_idx].Get();
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &bar);

    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
    const auto content_aspect=galaxy::presentation_content_aspect(
        ultrawide ? ultrawide->content_aspect : galaxy::kWiiContentAspect,source->prerecorded_movie);
    galaxy::ContentViewport content_viewport = galaxy::fit_content_viewport(
        static_cast<int>(s_bb_width),static_cast<int>(s_bb_height),content_aspect);
    if (content_viewport.width <= 0 || content_viewport.height <= 0) {
        // Resize processing clamps the swap-chain extent to at least 1x1.
        // fit_content_viewport intentionally rejects degenerate input/pointer
        // surfaces, but presentation must still have a valid fallback while a
        // window is transitioning through such an extent.
        content_viewport = galaxy::ContentViewport{
            0, 0, static_cast<int>(s_bb_width),
            static_cast<int>(s_bb_height)};
    }
    (void)galaxy::g_presentation_content.publish(
        static_cast<int>(s_bb_width),static_cast<int>(s_bb_height),content_viewport);
    static const bool trace_layout=[] {
        char value[8]{};std::size_t size{};
        return getenv_s(&size,value,sizeof(value),"GALAXY_MONITOR_DISPLAY_LAYOUT")==0 && value[0]=='1';
    }();
    if (trace_layout) {
        const std::array<int,7> current{static_cast<int>(s_bb_width),static_cast<int>(s_bb_height),
            content_viewport.left,content_viewport.top,content_viewport.width,content_viewport.height,
            static_cast<int>(source->prerecorded_movie)};
        static std::array<int,7> previous{};
        static unsigned changes{};
        if (current!=previous && changes<16u) {
            previous=current;++changes;
            std::cerr<<"[display-present-layout] generation="<<changes<<" physical-output="<<s_bb_width<<'x'<<s_bb_height
                <<" content-rect="<<content_viewport.left<<','<<content_viewport.top<<','<<content_viewport.width<<','<<content_viewport.height
                <<" selected-aspect="<<(ultrawide?ultrawide->content_aspect:galaxy::kWiiContentAspect)
                <<" presentation-aspect="<<content_aspect<<" prerecorded-movie="<<source->prerecorded_movie
                <<" dpi="<<GetDpiForWindow(window_)<<" internal-scale="<<efb_scale_<<'\n';
        }
    }
    const bool content_has_borders =
        content_viewport.left != 0 || content_viewport.top != 0 ||
        content_viewport.width != static_cast<int>(s_bb_width) ||
        content_viewport.height != static_cast<int>(s_bb_height);

    const BackbufferPreBlitClearMode clear_mode =
        backbuffer_pre_blit_clear_mode();
    if (content_has_borders ||
        clear_mode != BackbufferPreBlitClearMode::Disabled) {
        constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        constexpr float kDiagnostic[4] = {1.0f, 0.0f, 1.0f, 1.0f};
        const float kSafety[4] = {source->safety_surround, source->safety_surround,
                                 source->safety_surround, 1.0f};
        const float* clear_color =
            clear_mode == BackbufferPreBlitClearMode::Diagnostic
                ? kDiagnostic
                : source->safety_surround > 0.0f ? kSafety : kBlack;
        command_list_->ClearRenderTargetView(
            back_rtv, clear_color, 0, nullptr);
        ++telemetry_backbuffer_clear_count_;
    }

    if (source->copy_serial != last_presented_xfb_serial_) {
        last_presented_xfb_serial_ = source->copy_serial;
        ++telemetry_adjacent_serial_transition_count_;
    }

    // Allocate a one-slot SRV in the frame's ring for the XFB texture.
    const DescriptorRing::Table tbl = srv_ring_.allocate(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels       = 1;
    device_->CreateShaderResourceView(source->texture.Get(), &sd, tbl.cpu);

    const D3D12_VIEWPORT vp{
        static_cast<float>(content_viewport.left),
        static_cast<float>(content_viewport.top),
        static_cast<float>(content_viewport.width),
        static_cast<float>(content_viewport.height),
        0.0f,
        1.0f};
    const D3D12_RECT sci{
        static_cast<LONG>(content_viewport.left),
        static_cast<LONG>(content_viewport.top),
        static_cast<LONG>(content_viewport.left + content_viewport.width),
        static_cast<LONG>(content_viewport.top + content_viewport.height)};

    // Switch to blit root signature + pipeline while preserving both shader
    // visible heaps. The blit path currently uses static samplers, but keeping
    // the sampler heap bound avoids a D3D12 state footgun for overlays/future
    // present effects and matches normal GX draw recording.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
    command_list_->SetPipelineState(blit_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);
    invalidate_gx_bindings();

    // Native overlays are authored in full client/backbuffer pixels, not Wii
    // content coordinates. Restore the full-surface raster state after the XFB
    // blit so overlays stay correctly positioned and captures below include
    // the complete composited surface (black borders, game, then overlays).
    const D3D12_VIEWPORT overlay_vp{
        0.0f, 0.0f, static_cast<float>(s_bb_width),
        static_cast<float>(s_bb_height), 0.0f, 1.0f};
    const D3D12_RECT overlay_sci{
        0, 0, static_cast<LONG>(s_bb_width),
        static_cast<LONG>(s_bb_height)};
    command_list_->RSSetViewports(1, &overlay_vp);
    command_list_->RSSetScissorRects(1, &overlay_sci);

    {
        const auto stall_watch_overlay_start = std::chrono::steady_clock::now();
        draw_and_handle_file_select_settings_button(*this);
        draw_native_settings_overlay(*this);
        const double stall_watch_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() -
            stall_watch_overlay_start).count();
        if (trace_stall_watch_enabled() && stall_watch_ms > 4.0) {
            std::ostringstream message;
            message << "[stall-watch] draw_native_settings_overlay took "
                      << stall_watch_ms << "ms\n";
            std::cerr << message.str();
        }
    }

    // Transition backbuffer → PRESENT.
    if (record_backbuffer) {
        if (backbuffer_readback_pending_) {
            throw std::runtime_error(
                "backbuffer capture requested while a prior readback is pending");
        }
        const UINT w = s_bb_width;
        const UINT h = s_bb_height;
        const UINT row_pitch =
            (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        const UINT64 required_bytes = static_cast<UINT64>(row_pitch) * h;
        if (!backbuffer_readback_buf_ ||
            backbuffer_readback_buf_->GetDesc().Width < required_bytes) {
            backbuffer_readback_buf_.Reset();
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = required_bytes;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            d3d_check(
                device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&backbuffer_readback_buf_)),
                "Create backbuffer readback");
        }

        D3D12_RESOURCE_BARRIER capture_barrier{};
        capture_barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        capture_barrier.Transition.pResource =
            backbuffers_[buf_idx].Get();
        capture_barrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_RENDER_TARGET;
        capture_barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_COPY_SOURCE;
        capture_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &capture_barrier);

        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = backbuffers_[buf_idx].Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = backbuffer_readback_buf_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = kBackbufferFormat;
        dst.PlacedFootprint.Footprint.Width = w;
        dst.PlacedFootprint.Footprint.Height = h;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
        command_list_->CopyTextureRegion(
            &dst, 0, 0, 0, &src, nullptr);

        std::swap(
            capture_barrier.Transition.StateBefore,
            capture_barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &capture_barrier);
        backbuffer_readback_pending_ = true;
        backbuffer_readback_fence_value_ = 0u;
        backbuffer_readback_capture_ = capture_backbuffer;
        backbuffer_readback_luma_trace_ = trace_luma;
        backbuffer_readback_width_ = w;
        backbuffer_readback_height_ = h;
        backbuffer_readback_row_pitch_ = row_pitch;
        backbuffer_readback_capture_path_ = capture_backbuffer
            ? next_backbuffer_capture_path_
            : std::string{};
        backbuffer_readback_frame_ = present_frame;
        backbuffer_readback_xfb_addr_ = source->guest_addr;
        backbuffer_readback_xfb_stamp_ = source->frame_stamp;
    }

    std::swap(bar.Transition.StateBefore, bar.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &bar);
}

// Extend the existing bounded capture with the actual selected source pixels.
// The copy and final blit share a command list and exact completion fence.
// Retain the source until completion; never wait here or change VI selection.
void RendererD3D12::debug_copy_selected_xfb_to_readback(const XfbTexture& source) {
    if (selected_xfb_readback_pending_ || next_backbuffer_capture_path_.empty()) {
        throw std::runtime_error("selected XFB capture has pending data or no path");
    }
    const auto desc = source.texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.Width != source.width || desc.Height != source.height ||
        source.width == 0u || source.height == 0u) {
        throw std::runtime_error("selected XFB capture has inconsistent extent/format");
    }
    const UINT pitch = (source.width * 4u + 255u) & ~255u;
    const UINT64 bytes = static_cast<UINT64>(pitch) * source.height;
    if (!selected_xfb_readback_buf_ || selected_xfb_readback_buf_->GetDesc().Width < bytes) {
        selected_xfb_readback_buf_.Reset();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&selected_xfb_readback_buf_)), "Create selected XFB readback");
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = source.texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source.texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = selected_xfb_readback_buf_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = source.width;
    dst.PlacedFootprint.Footprint.Height = source.height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    selected_xfb_readback_source_ = source.texture;
    selected_xfb_readback_width_ = source.width;
    selected_xfb_readback_height_ = source.height;
    selected_xfb_readback_pitch_ = pitch;
    selected_xfb_readback_path_ = next_backbuffer_capture_path_ + ".xfb.ppm";
    selected_xfb_readback_fence_ = 0u;
    selected_xfb_readback_pending_ = true;
    std::cerr << "[selected-xfb-capture] addr=0x" << std::hex << source.guest_addr
              << " resource=0x" << reinterpret_cast<std::uintptr_t>(source.texture.Get())
              << std::dec << " serial=" << source.copy_serial << " stamp=" << source.frame_stamp
              << " extent=" << source.width << 'x' << source.height
              << " path=" << selected_xfb_readback_path_ << '\n';
}

void RendererD3D12::debug_log_selected_xfb_readback() {
    if (!selected_xfb_readback_pending_ || selected_xfb_readback_fence_ == 0u ||
        completed_fence_value() < selected_xfb_readback_fence_) return;
    const SIZE_T bytes = static_cast<SIZE_T>(selected_xfb_readback_pitch_) *
        selected_xfb_readback_height_;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, bytes};
    d3d_check(selected_xfb_readback_buf_->Map(0, &range, &mapped), "Map selected XFB readback");
    try {
        queue_debug_ppm_capture("selected-xfb-dump", selected_xfb_readback_path_,
            static_cast<const std::uint8_t*>(mapped), selected_xfb_readback_width_,
            selected_xfb_readback_height_, selected_xfb_readback_pitch_);
    } catch (...) {
        const D3D12_RANGE no_write{0, 0};
        selected_xfb_readback_buf_->Unmap(0, &no_write);
        throw;
    }
    const D3D12_RANGE no_write{0, 0};
    selected_xfb_readback_buf_->Unmap(0, &no_write);
    selected_xfb_readback_pending_ = false;
    selected_xfb_readback_fence_ = 0u;
    selected_xfb_readback_source_.Reset();
    selected_xfb_readback_path_.clear();
}

void RendererD3D12::debug_log_backbuffer_readback() {
    reap_debug_capture_writers(false);
    if (!backbuffer_readback_pending_ || !backbuffer_readback_buf_) {
        return;
    }
    if (backbuffer_readback_fence_value_ == 0u ||
        completed_fence_value() < backbuffer_readback_fence_value_) {
        return;
    }
    backbuffer_readback_pending_ = false;
    backbuffer_readback_fence_value_ = 0u;

    // A resize may have completed after this copy was recorded. Decode using
    // the captured footprint rather than the current swap-chain extent.
    const UINT w = backbuffer_readback_width_;
    const UINT h = backbuffer_readback_height_;
    const UINT row_pitch = backbuffer_readback_row_pitch_;
    if (w == 0u || h == 0u || row_pitch == 0u) {
        throw std::runtime_error(
            "pending backbuffer readback has no captured footprint");
    }
    const SIZE_T byte_count = static_cast<SIZE_T>(row_pitch) * h;
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, byte_count};
    if (FAILED(
            backbuffer_readback_buf_->Map(
                0, &read_range, &mapped))) {
        backbuffer_readback_capture_ = false;
        backbuffer_readback_luma_trace_ = false;
        backbuffer_readback_capture_path_.clear();
        backbuffer_readback_frame_ = 0;
        backbuffer_readback_xfb_addr_ = 0;
        backbuffer_readback_xfb_stamp_ = 0;
        backbuffer_readback_width_ = 0;
        backbuffer_readback_height_ = 0;
        backbuffer_readback_row_pitch_ = 0;
        return;
    }

    const auto* base = static_cast<const std::uint8_t*>(mapped);
    if (!backbuffer_readback_capture_path_.empty()) {
        queue_debug_ppm_capture(
            "backbuffer-dump",
            backbuffer_readback_capture_path_,
            base,
            w,
            h,
            row_pitch);
    }

    if (backbuffer_readback_luma_trace_) {
        const std::uint64_t pixel_count =
            static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
        std::uint64_t luma_sum = 0;
        std::uint64_t near_black = 0;
        std::uint8_t min_luma = 255;
        std::uint8_t max_luma = 0;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (UINT y = 0; y < h; ++y) {
            const std::uint8_t* row =
                base + static_cast<std::size_t>(y) * row_pitch;
            crc = crc32_update_raw(
                crc,
                row,
                static_cast<std::size_t>(w) * 4u);
            for (UINT x = 0; x < w; ++x) {
                const std::uint8_t r = row[x * 4u + 0u];
                const std::uint8_t g = row[x * 4u + 1u];
                const std::uint8_t b = row[x * 4u + 2u];
                const std::uint8_t luma = static_cast<std::uint8_t>(
                    (54u * r + 183u * g + 19u * b + 128u) >> 8u);
                luma_sum += luma;
                near_black += luma <= 8u ? 1u : 0u;
                min_luma = std::min(min_luma, luma);
                max_luma = std::max(max_luma, luma);
            }
        }
        crc ^= 0xFFFFFFFFu;
        const double mean_luma =
            pixel_count != 0u
                ? static_cast<double>(luma_sum) /
                      static_cast<double>(pixel_count)
                : 0.0;
        const double near_black_pct =
            pixel_count != 0u
                ? static_cast<double>(near_black) * 100.0 /
                      static_cast<double>(pixel_count)
                : 0.0;
        std::cerr << "[present-luma] frame=" << backbuffer_readback_frame_
                  << " xfb=0x" << std::hex << backbuffer_readback_xfb_addr_
                  << " crc32=0x" << crc << std::dec
                  << " stamp=" << backbuffer_readback_xfb_stamp_
                  << " mean=" << mean_luma
                  << " min=" << static_cast<unsigned>(min_luma)
                  << " max=" << static_cast<unsigned>(max_luma)
                  << " near-black=" << near_black << "/"
                  << pixel_count
                  << " near-black-pct=" << near_black_pct
                  << '\n';
    }

    backbuffer_readback_capture_ = false;
    backbuffer_readback_luma_trace_ = false;
    backbuffer_readback_capture_path_.clear();
    backbuffer_readback_frame_ = 0;
    backbuffer_readback_xfb_addr_ = 0;
    backbuffer_readback_xfb_stamp_ = 0;
    backbuffer_readback_width_ = 0;
    backbuffer_readback_height_ = 0;
    backbuffer_readback_row_pitch_ = 0;

    const D3D12_RANGE no_write{0, 0};
    backbuffer_readback_buf_->Unmap(0, &no_write);
}

HostClientExtent current_host_client_extent() noexcept {
    const HWND hwnd = reinterpret_cast<HWND>(
        s_pointer_hwnd.load(std::memory_order_acquire));
    RECT client{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &client)) {
        return {};
    }
    return {client.right - client.left, client.bottom - client.top};
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
        reinterpret_cast<HWND>(s_pointer_hwnd.load(std::memory_order_relaxed));
    const galaxy::host::HostPointerSnapshot cached_snapshot =
        s_pointer_snapshot.snapshot();
    HostPointerState state{};
    state.debug_window = reinterpret_cast<std::uintptr_t>(hwnd);
    state.left_button = message_left_button;
    state.right_button = message_right_button;
    state.absolute_acquired_ms = cached_snapshot.absolute_acquired_ms;
    if (s_system_menu_active.load(std::memory_order_acquire) ||
        s_move_size_active.load(std::memory_order_acquire)) {
        // Native modal interaction has released guest button ownership.
        // Do not let the authoritative cursor poll reacquire focus inside it.
        state.left_button = false;
        state.right_button = false;
        return state;
    }

    // Prefer real client-area mouse messages over frame-time cursor polling.
    // On Remote Desktop and some cursor-hidden paths, GetCursorPos can report a
    // stale centered position for long stretches while WM_MOUSEMOVE still
    // carries the live client coordinates. The seed path deliberately does not
    // increment the event sequence; startup centering is inactive until a real
    // cursor acquisition succeeds.
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
        s_system_menu_active.load(std::memory_order_acquire) ||
        s_move_size_active.load(std::memory_order_acquire);
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
    const int desired_height = compact
        ? 224
        : (layout.text_scale >= 4 ? 480 : 420);
    layout.panel_height = std::min(desired_height, height - margin * 2);
    layout.rows_y = compact
        ? layout.panel_y + 54
        : layout.panel_y + 110 + (layout.text_scale - 2) * 8;
    layout.row_step = compact
        ? 18
        : std::max(32, layout.text_scale * 9 + 8);
    layout.footer_y =
        layout.panel_y + layout.panel_height - (compact ? 62 : 82);

    const unsigned row_count = native_settings_row_count(tab);
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

void native_settings_reset_overlay_state_for_tests() noexcept {
    s_native_settings_visible.store(false, std::memory_order_relaxed);
    s_native_settings_dirty.store(false, std::memory_order_relaxed);
    s_native_settings_save_error.store(false, std::memory_order_relaxed);
    s_native_settings_tab.store(0u, std::memory_order_relaxed);
    s_native_settings_row.store(0u, std::memory_order_relaxed);
}

}  // namespace galaxy::gx
