#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/display_modes.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/game_asset_extract.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/streamline_bridge.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"
#include "galaxy/native_host.h"
#include "galaxy/frame_timeline.h"

#pragma warning(push, 0)
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <immintrin.h>
#pragma warning(pop)

#include <algorithm>
#include <cstdarg>
#include <cwchar>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cerrno>
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

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps);

bool detail::ConversionRtvSlot::bind(
    ID3D12Device* device, ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    if (device == nullptr || resource == nullptr || descriptor.ptr == 0u) {
        throw std::runtime_error("conversion RTV slot has no device/resource/descriptor");
    }
    if (resource_.Get() == resource && descriptor_.ptr == descriptor.ptr) {
        return false;
    }
    device->CreateRenderTargetView(resource, nullptr, descriptor);
    resource_ = resource;
    descriptor_ = descriptor;
    return true;
}

// ---------------------------------------------------------------------------
// Module-level state for things the frozen header can't hold
// ---------------------------------------------------------------------------

namespace {

// Blit root signature. Lifetime matches RendererD3D12; stored here because the frozen
// header has no slot for it.
static ComPtr<ID3D12RootSignature> s_blit_root_sig;
// EFB-copy conversion blit root signature (same lifetime pattern).
static ComPtr<ID3D12RootSignature> s_conv_root_sig;
static ComPtr<ID3D12RootSignature> s_clear_root_sig;
// Cached backbuffer dimensions set during initialize.
static UINT s_bb_width  = 1280;
// Swap-chain presents per second (any source), for the FRAME RATE row.
static std::atomic<double> s_measured_present_hz{0.0};
// RendererD3D12::suspend_temporal_interpolation (the FRAME RATE row says so).
static std::atomic<bool> s_temporal_interp_suspended{false};
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
// Window-thread requests from the native settings overlay: a client size in
// (wParam, lParam) and a display mode (wParam != 0 selects borderless).
static constexpr UINT kClientSizeMessage = WM_USER + 0x41;
static constexpr UINT kDisplayModeMessage = WM_USER + 0x42;
// An overlay key from the controller (wParam: virtual-key code).
static constexpr UINT kSettingsKeyMessage = WM_USER + 0x43;
// Render-thread display change (lParam: const WindowPlacement*), sent and
// completed on the window thread.
static constexpr UINT kPlaceWindowMessage = WM_USER + 0x44;
struct WindowPlacement {
    DWORD style = 0;
    RECT rect{};
    bool borderless = false;
};
RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width,
                              unsigned height);
static std::atomic<std::uintptr_t> s_native_settings_window{0};
static std::atomic<bool> s_borderless_active{false};
static std::atomic<bool> s_exclusive_fullscreen_active{false};
static RECT s_windowed_rect{};
// The last internal resolution refused for the video-memory budget (0: none).
static std::atomic<unsigned> s_efb_scale_rejected{0u};

// The internal resolution that fills the display: the smallest scale whose
// 640x456 game picture covers the monitor's current mode, or the swap chain
// in exclusive fullscreen. Higher scales cost GPU time and memory without
// adding pixels the display can show.
static unsigned display_efb_scale_cap() {
    UINT width = 0u;
    UINT height = 0u;
    if (s_exclusive_fullscreen_active.load(std::memory_order_acquire)) {
        width = s_bb_width;
        height = s_bb_height;
    } else {
        const HWND window = reinterpret_cast<HWND>(
            s_native_settings_window.load(std::memory_order_acquire));
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

#if defined(GALAXY_WITH_STREAMLINE)
constexpr bool kStreamlineBuilt = true;
#else
constexpr bool kStreamlineBuilt = false;
#endif

// DLSS before it is installed (defined after the adapter selection below).
bool dlss_offered();
// DXR 1.1 with shader model 6.5 on an adapter with dedicated memory (the RAY TRACING row).
std::atomic<bool> s_ray_tracing_offered{false};
bool dlss_installed_now();
bool dlss_hold_back(RenderConfig& applied, const RenderConfig& live);
bool dlss_prompt_key(WPARAM key, bool was_down);
void dlss_dismiss_prompt();
void poll_dlss_download();
std::string dlss_footer(std::uint64_t now_ms, bool& error);

bool temporal_interpolation_allowed() {
    // A temporal upscaler upscales every presented frame at rates that are
    // multiples of 60. GALAXY_TEMPORAL_INTERPOLATION=0 keeps it at 60
    // (diagnostics).
    static const bool allowed = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value), "GALAXY_TEMPORAL_INTERPOLATION") == 0 &&
                 length > 1u && value[0] == '0');
    }();
    return allowed;
}

// The temporal upscaler this configuration runs (2 FSR 3, 3 DLSS), 0 none.
// Without temporal interpolation it limits the frame rate to 60.
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

// The quality row serves every upscaler: it sets the render resolution relative to the output
// (QUALITY 1.5x, BALANCED 1.7x, PERFORMANCE 2x, ULTRA PERFORMANCE 3x fewer pixels per axis).
// Its first mode is the INTERNAL RES row for bilinear and FSR 1, native resolution for FSR 3
// and DLSS.
bool temporal_quality_row_shown() {
    return true;
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
std::atomic<bool> s_quit_requested{false};

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

constexpr unsigned kNativeSettingsTabCount = 6;
// Rows the panel shows at once; longer pages scroll.
constexpr unsigned kNativeSettingsVisibleRows = 9u;

unsigned native_settings_first_row(unsigned first, unsigned selected, unsigned count) {
    if (count <= kNativeSettingsVisibleRows) return 0u;
    if (selected < first) first = selected;
    if (selected >= first + kNativeSettingsVisibleRows) first = selected + 1u - kNativeSettingsVisibleRows;
    return std::min(first, count - kNativeSettingsVisibleRows);
}
// Set by the overlay when "resolution follows window" is switched on, so the
// render thread re-derives the scale for the current window right away.
std::atomic<bool> s_follow_window_rearm{false};

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

PostProcessSettings post_process_settings_from(const RenderConfig& cfg) noexcept {
    PostProcessSettings post{};
    post.aa = static_cast<PostProcessSettings::Aa>(std::min(cfg.aa_mode, 3u));
    post.smaa_quality = static_cast<std::uint8_t>(std::min(cfg.smaa_quality, 3u));
    post.upscaler = static_cast<PostProcessSettings::Upscaler>(std::min(cfg.upscaler, 1u));
    post.sharpness = static_cast<std::uint8_t>(std::min(cfg.sharpness, 100u));
    return post;
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
// GALAXY_DISPLAY_REFRESH_HZ: the refresh rate the menu and the pacing assume
// (hidden test runs; a remote desktop session reports its virtual display's).
unsigned refresh_override_hz() {
    static const unsigned hz = [] {
        char value[16]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_DISPLAY_REFRESH_HZ") == 0 && length > 1u
            ? static_cast<unsigned>(std::strtoul(value, nullptr, 10)) : 0u;
    }();
    return hz;
}

unsigned settings_refresh_hz(const RenderConfig& render) {
    if (const unsigned hz = refresh_override_hz(); hz != 0u) {
        return hz;
    }
    const MonitorInfo* monitor = settings_monitor(render);
    if (render.display_mode == kDisplayExclusive && render.fullscreen_refresh_hz != 0u) {
        return render.fullscreen_refresh_hz;
    }
    return monitor != nullptr ? monitor->current.refresh_hz : 0u;
}

// FRAME RATE choices while a temporal upscaler runs: 30 and 60, and with
// temporal interpolation the multiples of 60.
std::vector<unsigned> frame_rate_choices(const RenderConfig& render) {
    std::vector<unsigned> rates = frame_rate_options(settings_refresh_hz(render));
    if (temporal_upscaler(render) != 0u) {
        const bool multiples = temporal_interpolation_allowed();
        std::erase_if(rates, [multiples](unsigned rate) {
            return rate == 0u || (rate > 60u && (!multiples || rate % 60u != 0u));
        });
    }
    return rates;
}

const MonitorInfo* settings_monitor(const RenderConfig& render) {
    return find_monitor(s_settings_monitors, widen_utf8(render.monitor.data()));
}

// The picture size the settings target: the fullscreen mode, the monitor
// (borderless) or the window.
std::pair<unsigned, unsigned> settings_output_size(const RenderConfig& render) {
    const MonitorInfo* monitor = settings_monitor(render);
    const unsigned desktop_w = monitor ? monitor->current.width : s_bb_width;
    const unsigned desktop_h = monitor ? monitor->current.height : s_bb_height;
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
    unsigned width = monitor ? monitor->current.width : s_bb_width;
    unsigned height = monitor ? monitor->current.height : s_bb_height;
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
    changes += d.spatial_quality != live.spatial_quality;
    changes += d.sharpness != live.sharpness;
    changes += d.anisotropic_filtering != live.anisotropic_filtering;
    changes += d.enhanced_mipmaps != live.enhanced_mipmaps;
    changes += d.per_pixel_lighting != live.per_pixel_lighting;
    changes += d.hud_output_resolution != live.hud_output_resolution;
    changes += d.ray_tracing_enabled != live.ray_tracing_enabled;
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
    return !temporal_quality_row_shown() && row >= 3u ? row + 1u : row;
}

unsigned native_settings_row_count(unsigned tab) {
    switch (tab % kNativeSettingsTabCount) {
    case 0: return 8u;
    case 1:
        return (temporal_quality_row_shown() ? 9u : 8u) +
               (s_ray_tracing_offered.load(std::memory_order_relaxed) ? 1u : 0u);
    case 2: return controls_row_count();
    case 3: return 7u;
    case 4: return 1u;
    case 5: return 6u;
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
    const bool dlss_asked = dlss_hold_back(applied, live);
    applied.temporal_quality = next.temporal_quality;
    applied.spatial_quality = next.spatial_quality;
    applied.sharpness = next.sharpness;
    applied.anisotropic_filtering = next.anisotropic_filtering;
    applied.enhanced_mipmaps = next.enhanced_mipmaps;
    applied.per_pixel_lighting = next.per_pixel_lighting;
    applied.hud_output_resolution = next.hud_output_resolution;
    applied.ray_tracing_enabled = next.ray_tracing_enabled;
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
        s_follow_window_rearm.store(true, std::memory_order_release);
    }
    s_settings_draft = get_render_config();
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
                                        get_runtime_settings(),
                                        get_render_feature_caps());
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
            const auto rates = frame_rate_choices(draft);
            const auto current = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
            std::size_t index = rates.size() - 1u;  // unlimited, or the highest offered
            for (std::size_t i = 0; i < rates.size(); ++i) {
                if (rates[i] != 0u && current != 0u && rates[i] <= current) index = i;
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
            const std::vector<unsigned> choices = available_upscalers();
            std::size_t index = 0u;
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (choices[i] == draft.upscaler) index = i;
            }
            draft.upscaler = choices[step(static_cast<unsigned>(index), static_cast<unsigned>(choices.size()))];
            if (temporal_upscaler(draft) != 0u) {
                // The highest rate still offered at or below the chosen one.
                const auto rates = frame_rate_choices(draft);
                const auto wanted = static_cast<unsigned>(std::lround(std::max(0.0f, draft.max_fps)));
                unsigned best = 60u;
                for (const unsigned rate : rates) {
                    if (rate != 0u && (wanted == 0u || rate <= wanted)) best = std::max(best, rate);
                }
                draft.max_fps = static_cast<float>(best);
            }
            break;
        }
        case 3:
            if (draft.upscaler >= 2u) {
                draft.temporal_quality = step(std::min(draft.temporal_quality, 4u), 5u);
            } else {
                draft.spatial_quality = step(std::min(draft.spatial_quality, 4u), 5u);
            }
            break;
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
        case 9: draft.ray_tracing_enabled = !draft.ray_tracing_enabled; break;
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
                                &settings.audio.ambience, &settings.audio.cutscene,
                                &settings.audio.wiimote_speaker};
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
        // Closing discards an unapplied draft, an unconfirmed display and an
        // unanswered DLSS question.
        dlss_dismiss_prompt();
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
    const std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex);
    if (!native_settings_ui_enabled()) {
        return false;
    }
    if (key == galaxy::host::kBindingCaptureKey && !s_native_settings_visible.load(std::memory_order_relaxed)) {
        (void)galaxy::host::take_controller_binding_capture();
        return true;
    }
    if (key != VK_F1 && s_native_settings_visible.load(std::memory_order_relaxed) &&
        dlss_prompt_key(key, was_down)) {
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

// GALAXY_GPU_PREFERENCE=low selects the minimum-power (integrated) adapter
// first, e.g. to measure on a hybrid machine's iGPU.
DXGI_GPU_PREFERENCE adapter_preference() {
    char value[16]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), "GALAXY_GPU_PREFERENCE") == 0 &&
        length > 1 && (value[0] == 'l' || value[0] == 'L' || value[0] == 'm' || value[0] == 'M')) {
        return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    }
    return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
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

// With GALAXY_D3D_DRED: a breadcrumb context (PIX unicode marker) that the
// device-removal dump prints next to the operation where the GPU stopped.
void dred_marker(ID3D12GraphicsCommandList* list, const wchar_t* format, ...) {
    if (!dred_diagnostics_enabled() || list == nullptr) {
        return;
    }
    wchar_t text[96]{};
    va_list args;
    va_start(args, format);
    const int length = std::vswprintf(text, sizeof(text) / sizeof(text[0]), format, args);
    va_end(args);
    if (length > 0) {
        list->SetMarker(0u, text, static_cast<UINT>((std::wcslen(text) + 1u) * sizeof(wchar_t)));
    }
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

// Installed physical memory, in MiB, or 0 if it cannot be queried.
//
// Reported alongside the adapter's dedicated/shared figures because on a unified
// memory adapter (integrated graphics) the usable graphics budget is the OS cap of
// one-half of system memory, so neither number is interpretable without it. On a
// discrete adapter this is context only. Queried once; the value cannot change
// during a session.
std::uint64_t host_ram_mb() {
    static const std::uint64_t megabytes = [] {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == FALSE) {
            return std::uint64_t{0};
        }
        return static_cast<std::uint64_t>(status.ullTotalPhys) / (1024ull * 1024ull);
    }();
    return megabytes;
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

static void apply_requested_display_mode(HWND hwnd, bool borderless) {
    if (borderless == s_borderless_active.load(std::memory_order_acquire)) {
        return;
    }
    if (borderless) {
        (void)GetWindowRect(hwnd, &s_windowed_rect);
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            return;
        }
        (void)SetWindowLongPtrW(
            hwnd, GWL_STYLE, static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE));
        (void)SetWindowPos(
            hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
            info.rcMonitor.right - info.rcMonitor.left,
            info.rcMonitor.bottom - info.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
        s_borderless_active.store(true, std::memory_order_release);
        return;
    }
    (void)SetWindowLongPtrW(
        hwnd, GWL_STYLE, static_cast<LONG_PTR>(kWindowStyle | WS_VISIBLE));
    const RECT restored = s_windowed_rect;
    int width = restored.right - restored.left;
    int height = restored.bottom - restored.top;
    int x = restored.left;
    int y = restored.top;
    if (width <= 0 || height <= 0) {
        RECT frame{0, 0, 1280, 720};
        (void)AdjustWindowRectExForDpi(
            &frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, GetDpiForWindow(hwnd));
        width = frame.right - frame.left;
        height = frame.bottom - frame.top;
        x = 64;
        y = 64;
    }
    (void)SetWindowPos(
        hwnd, nullptr, x, y, width, height,
        SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    s_borderless_active.store(false, std::memory_order_release);
}

static void apply_requested_client_size(HWND hwnd, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (s_borderless_active.load(std::memory_order_acquire)) {
        apply_requested_display_mode(hwnd, false);
    }
    RECT frame{0, 0, width, height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&frame, style, FALSE, ex_style, GetDpiForWindow(hwnd))) {
        return;
    }
    (void)SetWindowPos(
        hwnd, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kClientSizeMessage) {
        apply_requested_client_size(hwnd, static_cast<int>(wp), static_cast<int>(lp));
        return 0;
    }
    if (msg == kDisplayModeMessage) {
        apply_requested_display_mode(hwnd, wp != 0u);
        return 0;
    }
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
            s_borderless_active.store(placement->borderless, std::memory_order_release);
        }
        return 0;
    }
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
    if (msg == WM_EXITSIZEMOVE &&
        !s_borderless_active.load(std::memory_order_acquire) &&
        !s_exclusive_fullscreen_active.load(std::memory_order_acquire)) {
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
            }
        }
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
        s_quit_requested.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        flush_native_settings_if_dirty();
        s_quit_requested.store(true, std::memory_order_release);
        hold_display_on(false);
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
            // Every list still in flight; finished ones only up to 4.
            int node_idx = 0;
            int finished = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* n = bo.pHeadAutoBreadcrumbNode;
                 n != nullptr && node_idx < 256; n = n->pNext, ++node_idx) {
                const UINT32 last = n->pLastBreadcrumbValue
                    ? *n->pLastBreadcrumbValue : 0u;
                if (last >= n->BreadcrumbCount && ++finished > 4) {
                    continue;
                }
                char name[96] = "(unnamed)";
                if (n->pCommandListDebugNameW != nullptr) {
                    std::snprintf(name, sizeof(name), "%ls", n->pCommandListDebugNameW);
                } else if (n->pCommandListDebugNameA != nullptr) {
                    std::snprintf(name, sizeof(name), "%s", n->pCommandListDebugNameA);
                }
                std::cerr << "[DRED] node " << node_idx << " cmdlist " << name
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
        ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bo1{};
        if (SUCCEEDED(dred.As(&dred1)) && SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&bo1))) {
            int node_idx = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE1* n = bo1.pHeadAutoBreadcrumbNode;
                 n != nullptr && node_idx < 256; n = n->pNext, ++node_idx) {
                const UINT32 last = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0u;
                if (last >= n->BreadcrumbCount || n->BreadcrumbContextsCount == 0u) {
                    continue;
                }
                // The markers before the stop (the last three) and the first after it.
                std::vector<std::wstring> before;
                std::wstring after;
                for (UINT c = 0; c < n->BreadcrumbContextsCount; ++c) {
                    const D3D12_DRED_BREADCRUMB_CONTEXT& context = n->pBreadcrumbContexts[c];
                    const std::wstring value = context.pContextString != nullptr ? context.pContextString : L"";
                    if (context.BreadcrumbIndex <= last) {
                        before.push_back(std::to_wstring(context.BreadcrumbIndex) + L":" + value);
                        if (before.size() > 3u) before.erase(before.begin());
                    } else if (after.empty()) {
                        after = std::to_wstring(context.BreadcrumbIndex) + L":" + value;
                    }
                }
                std::string line;
                for (const std::wstring& item : before) {
                    char buffer[160]{};
                    std::snprintf(buffer, sizeof(buffer), " [%ls]", item.c_str());
                    line += buffer;
                }
                char next[160]{};
                std::snprintf(next, sizeof(next), " next [%ls]", after.c_str());
                std::cerr << "[DRED] node " << node_idx << " stopped at op " << last << " after" << line << next << '\n';
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
        // Print only ERROR/CORRUPTION severity. Warnings (e.g. clear-value mismatches) flood the
        // tail and bury the actual illegal call.
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

// Fullscreen triangle with UV. No vertex buffer needed.
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
    int4 rect;
    float4 color;
    uint2 screen;
    uint2 glyph_mask;
    uint glyph_mode;
};
struct V { float4 pos:SV_Position; };
V main(uint id : SV_VertexID, uint cell : SV_InstanceID) {
    V o;
    float2 corner = float2(float(id & 1u), float((id >> 1u) & 1u));
    float2 px = float2(rect.xy) + corner * float2(rect.zw);
    if (glyph_mode != 0u) {
        uint mask_word = cell < 32u ? glyph_mask.x : glyph_mask.y;
        uint bit = (mask_word >> (cell & 31u)) & 1u;
        // Match the old CPU integer cell placement/clipping before conversion.
        int2 cell_min = rect.xy + int2(cell % 5u, cell / 5u) * rect.zw;
        int2 cell_max = clamp(cell_min + rect.zw, int2(0, 0), int2(screen));
        cell_min = clamp(cell_min, int2(0, 0), int2(screen));
        px = float2(cell_min) + corner * float2(cell_max - cell_min) * float(bit);
    }
    float2 ndc = float2(px.x / screen.x * 2.0 - 1.0, 1.0 - px.y / screen.y * 2.0);
    o.pos = float4(ndc, 0.5, 1.0);
    return o;
}
)HLSL";

static constexpr char kUiSolidQuadPs[] = R"HLSL(
cbuffer C : register(b0) {
    int4 rect;
    float4 color;
    uint2 screen;
    uint2 glyph_mask;
    uint glyph_mode;
};
float4 main() : SV_Target { return color; }
)HLSL";

struct UiSolidQuadConstants {
    std::int32_t rect[4];
    float color[4];
    std::uint32_t screen[2];
    std::uint32_t glyph_mask[2];
    std::uint32_t glyph_mode;
};
static_assert(sizeof(UiSolidQuadConstants) == 13u * sizeof(std::uint32_t));

static constexpr char kClearPs[] =
    "cbuffer C:register(b0){float4 clear_color;};"
    "float4 main():SV_Target{return clear_color;}";

// EFB-copy conversion pixel shader. Operation order on hardware: quantize source samples to
// bytes -> integer 3-row filter -> gamma -> optional RGB-to-YUV conversion -> destination-format
// channel mapping. The filter applies to color and packed depth RGB alike; alpha is always the
// unfiltered center sample. The depth buffer is reversed, so guest z24 is reconstructed from
// 1 - sampled depth before filtering its three bytes.
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
    // A destination texel covering more than 2x2 source texels (a native-size
    // effect copy of a scaled EFB) averages the central half of its
    // footprint. At 1x each EFB pixel is one sample at its centre; when the
    // game minifies a texture 4:1 into the bloom workspace that sample is a
    // bilinear 2x2 of each 4x4 block, and the bloom threshold that follows is
    // tuned to that sparse coverage. The central half reproduces it (a full
    // footprint average dims small glows below the threshold) while still
    // averaging many scaled samples. Bilinear taps cover 2x2 texel groups.
    const float2 step = src_xform.zw;
    float4 tex_sample;
    [branch] if (max(step.x, step.y) > 2.0) {
        const float2 footprint = 0.5 * step;
        const uint2 taps = uint2(clamp(ceil(footprint * 0.5), 1.0, 16.0));
        const float2 origin = sample_pos - 0.5 * footprint;
        const float2 pitch = footprint / float2(taps);
        tex_sample = 0.0;
        [loop] for (uint ty = 0u; ty < taps.y; ++ty) {
            [loop] for (uint tx = 0u; tx < taps.x; ++tx) {
                tex_sample += srctex.SampleLevel(
                    s_lin, (origin + (float2(tx, ty) + 0.5) * pitch) * inv, 0);
            }
        }
        tex_sample /= float(taps.x * taps.y);
    } else {
        tex_sample = srctex.SampleLevel(s_lin, sample_pos * inv, 0);
    }
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
            [loop] for (uint py = 0u; py < reduction_width; ++py) {
                [loop] for (uint px = 0u; px < reduction_width; ++px) {
                    sum += srctex.Load(int3(origin + int2(px, py), 0));
                }
            }
            float4 reduced = sum / float(reduction_width * reduction_width);
            texcol_raw = uint4(round(saturate(reduced) * 255.0));
        }
    } else {
        float ylo = filt.w, yhi = filt2.x;
        float filter_offset = max(filt2.z, 1.0);
        uint3 coefficients = uint3(filt.xyz + 0.5);
        uint4 current_row = sample_efb(
            float2(sp.x, clamp(sp.y, ylo, yhi)), inv);
        uint3 combined_rows = current_row.rgb * coefficients.y;
        [branch] if (coefficients.x != 0u) {
            uint4 prev_row = sample_efb(
                float2(sp.x, clamp(sp.y - filter_offset, ylo, yhi)), inv);
            combined_rows += prev_row.rgb * coefficients.x;
        }
        [branch] if (coefficients.z != 0u) {
            uint4 next_row = sample_efb(
                float2(sp.x, clamp(sp.y + filter_offset, ylo, yhi)), inv);
            combined_rows += next_row.rgb * coefficients.z;
        }
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

namespace {
// Identities of ring buffers and of their segmentations (immutable-upload tokens).
std::atomic<std::uint64_t> g_upload_ring_identity{1};
}  // namespace

void UploadRing::reset() noexcept {
    if (buffer_ && mapped_ != nullptr) {
        buffer_->Unmap(0, nullptr);
    }
    *this = UploadRing{};
}

bool UploadRing::initialize(
    ID3D12Device* device, const char* name, std::size_t bytes_per_frame,
    unsigned frames_in_flight, std::size_t capacity) {
    if (device == nullptr || bytes_per_frame == 0u || frames_in_flight == 0u ||
        bytes_per_frame > std::numeric_limits<std::size_t>::max() / frames_in_flight) {
        return false;
    }
    // Prepare all fallible state before replacing the retained mapped buffer.
    const std::size_t total = std::max(bytes_per_frame * frames_in_flight, capacity);
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
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&buffer))))
        return false;
    std::vector<std::uint64_t> generations(frames_in_flight, 0u);
    D3D12_RANGE no_read{0, 0};
    void* ptr = nullptr;
    if (FAILED(buffer->Map(0, &no_read, &ptr))) return false;
    const auto gpu_base = buffer->GetGPUVirtualAddress();
    // GPU address ranges, so a DRED page-fault address can be attributed.
    std::cout << "[ring] name=" << (name != nullptr ? name : "") << " gpu=0x"
              << std::hex << gpu_base << "-0x" << (gpu_base + total) << std::dec
              << " bytes=" << total << '\n';
    buffer_ = std::move(buffer);
    mapped_ = static_cast<std::byte*>(ptr);
    gpu_base_ = gpu_base;
    name_ = name != nullptr ? name : "";
    bytes_per_frame_ = bytes_per_frame;
    frames_in_flight_ = frames_in_flight;
    capacity_ = total;
    resource_identity_ = g_upload_ring_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_ = std::move(generations);
    segment_base_ = cursor_ = 0u;
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    reused_bytes_ = copied_bytes_ = 0u;
    immutable_upload_calls_ = 0u;
    peak_bytes_per_frame_ = 0u;
    return true;
}

bool UploadRing::resegment(std::size_t bytes_per_frame, unsigned frames_in_flight) {
    if (!buffer_ || mapped_ == nullptr || bytes_per_frame == 0u || frames_in_flight == 0u ||
        bytes_per_frame > capacity_ / frames_in_flight) {
        return false;
    }
    bytes_per_frame_ = bytes_per_frame;
    frames_in_flight_ = frames_in_flight;
    // A new identity: no immutable-upload token of the old segmentation matches.
    resource_identity_ = g_upload_ring_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_.assign(frames_in_flight, 0u);
    segment_base_ = cursor_ = 0u;
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    reused_bytes_ = copied_bytes_ = 0u;
    immutable_upload_calls_ = 0u;
    peak_bytes_per_frame_ = 0u;
    return true;
}

void UploadRing::begin_frame(unsigned frame_slot) {
    if (mapped_ == nullptr || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("UploadRing frame slot is out of range");
    }
    active_slot_ = frame_slot;
    segment_allocation_pending_ = true;
    reused_bytes_ = 0;
    copied_bytes_ = 0;
    immutable_upload_calls_ = 0;
    segment_base_ = static_cast<std::size_t>(frame_slot) * bytes_per_frame_;
    cursor_       = segment_base_;
    frame_ready_ = true;
}

UploadRing::Allocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    if (!frame_ready_) {
        throw std::runtime_error("UploadRing allocation before begin_frame");
    }
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        throw std::runtime_error("UploadRing alignment is not a nonzero power of two");
    }
    // Unsigned negation gives the exact padding without cursor+alignment
    // overflow. Prove both padding and payload fit before forming addresses.
    const std::size_t padding = (std::size_t{0} - cursor_) & (alignment - 1u);
    const std::size_t remaining = bytes_per_frame_ - (cursor_ - segment_base_);
    if (padding > remaining || size > remaining - padding)
        throw std::runtime_error(
            std::string("UploadRing '") + name_ + "' exhausted: request " +
            std::to_string(size) + " B at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(bytes_per_frame_) + " B");
    const std::size_t aligned = cursor_ + padding;
    cursor_ = aligned + size;
    // Peak within the segment, i.e. the bytes of this frame's segment that were
    // actually handed out. This is the only sound floor for shrinking
    // `bytes_per_frame_`, and it is what separates the ring's reserved size from
    // the physical memory it really backs.
    const std::size_t frame_used = cursor_ - segment_base_;
    if (frame_used > peak_bytes_per_frame_) {
        peak_bytes_per_frame_ = frame_used;
    }
    // Fence-only chunks reset the frame slot but never touch vertex bytes.
    // Count only segment uses that can actually replace their contents.
    if (size != 0 && segment_allocation_pending_) {
        ++slot_generations_[active_slot_];
        segment_allocation_pending_ = false;
    }
    return Allocation{
        mapped_ + aligned,
        gpu_base_ + static_cast<UINT64>(aligned),
        aligned
    };
}

UploadRing::Allocation UploadRing::upload_immutable(
    const void* source, std::size_t size, std::size_t alignment,
    ImmutableUploadToken& token) {
    const Allocation result = allocate(size, alignment);
    // Counted before any early classification, so the field counts every entry into the
    // immutable path. The two byte counters below are read against it.
    ++immutable_upload_calls_;
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

void DescriptorRing::reset() noexcept {
    *this = DescriptorRing{};
}

bool DescriptorRing::initialize(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    unsigned descriptors_per_frame,
    unsigned frames_in_flight,
    unsigned persistent_descriptors) {
    const std::uint64_t total = static_cast<std::uint64_t>(persistent_descriptors) +
        static_cast<std::uint64_t>(descriptors_per_frame) * frames_in_flight;
    if (device == nullptr || frames_in_flight == 0u || total == 0u ||
        total > std::numeric_limits<UINT>::max()) {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type;
    dhd.NumDescriptors = static_cast<UINT>(total);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // Publish a complete heap/stride/base tuple only on success. A failed
    // initialization must not pair an old heap with new cursor limits.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)))) {
        return false;
    }
    const auto cpu_base = heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu_base = heap->GetGPUDescriptorHandleForHeapStart();
    const auto increment = device->GetDescriptorHandleIncrementSize(type);
    heap_ = std::move(heap);
    cpu_base_ = cpu_base;
    gpu_base_ = gpu_base;
    increment_ = increment;
    persistent_descriptors_ = persistent_descriptors;
    persistent_cursor_ = 0u;
    descriptors_per_frame_ = descriptors_per_frame;
    frames_in_flight_ = frames_in_flight;
    segment_base_ = cursor_ = 0u;
    frame_ready_ = false;
    return true;
}

void DescriptorRing::begin_frame(unsigned frame_slot) {
    if (!heap_ || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("DescriptorRing invalid frame slot");
    }
    segment_base_ = persistent_descriptors_ +
        frame_slot * descriptors_per_frame_;
    cursor_       = segment_base_;
    frame_ready_ = true;
}

DescriptorRing::Table DescriptorRing::allocate_persistent(unsigned count) {
    if (!heap_) {
        throw std::runtime_error("DescriptorRing not initialized");
    }
    if (count > persistent_descriptors_ - persistent_cursor_) {
        throw std::runtime_error(
            "DescriptorRing persistent region exhausted: request " +
            std::to_string(count) + " descriptors at used " +
            std::to_string(persistent_cursor_) + "/" +
            std::to_string(persistent_descriptors_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_base_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_base_;
    cpu.ptr += static_cast<SIZE_T>(persistent_cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>(persistent_cursor_) * increment_;
    persistent_cursor_ += count;
    return Table{cpu, gpu};
}

DescriptorRing::Table DescriptorRing::allocate(unsigned count) {
    if (!frame_ready_) {
        throw std::runtime_error("DescriptorRing allocation before begin_frame");
    }
    if (count > descriptors_per_frame_ - (cursor_ - segment_base_)) {
        throw std::runtime_error(
            "DescriptorRing exhausted: request " + std::to_string(count) +
            " descriptors at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(descriptors_per_frame_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_base_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_base_;
    cpu.ptr += static_cast<SIZE_T>(cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>  (cursor_) * increment_;
    cursor_ += count;
    return Table{cpu, gpu};
}

// ---------------------------------------------------------------------------
// RendererD3D12 helpers
// ---------------------------------------------------------------------------

// Upload rings are created with CreateCommittedResource on D3D12_HEAP_TYPE_UPLOAD and mapped
// for the life of the process, so bytes_per_frame * frames_in_flight is resident memory that
// is never released. The defaults total (64+16+8+32) MiB * 2 slots = 240 MiB, plus a
// shader-visible descriptor heap.
//
// On an integrated adapter that is a share of system memory, because there is no separate
// pool. DXGI's DedicatedVideoMemory there is a fixed compatibility value, not VRAM (Intel
// graphics-memory FAQ, support article 000020962: 128 MB "for compatibility with applications
// that don't correctly comprehend a fully unified memory architecture"). The graphics share is
// capped at half of installed RAM.
//
// Each ring can be overridden in MiB so the budget can be measured on the target. Exhaustion
// stays fatal: UploadRing::allocate throws, so a too-small value fails visibly instead of
// dropping geometry.
namespace {

// Keep malformed or overflowing settings from changing the intended budget.
[[nodiscard]] std::uint64_t read_megabyte_budget(
    const char* variable, std::uint64_t default_bytes,
    bool zero_uses_default,
    std::uint64_t maximum_bytes = std::numeric_limits<std::uint64_t>::max()) {
    char value[24]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), variable) != 0 || length <= 1u) {
        return default_bytes;
    }
    for (const char* digit = value; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return default_bytes;
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long megabytes = std::strtoull(value, &end, 10);
    constexpr std::uint64_t kMegabyte = UINT64_C(1) << 20;
    if (end == value || *end != '\0' || errno == ERANGE ||
        megabytes > maximum_bytes / kMegabyte ||
        (megabytes == 0u && zero_uses_default)) {
        return default_bytes;
    }
    return static_cast<std::uint64_t>(megabytes) * kMegabyte;
}

[[nodiscard]] std::size_t ring_bytes_per_frame(
    const char* variable,
    std::size_t default_bytes) {
    const auto bytes = read_megabyte_budget(
        variable, default_bytes, true, std::numeric_limits<std::size_t>::max());
    constexpr std::size_t kFloorBytes = 1u * 1024u * 1024u;
    return std::max(kFloorBytes, static_cast<std::size_t>(bytes));
}

// The adapter chosen for this session, published for the ring budgeting below.
// Set once during initialize() before any ring is created, read from the same
// thread on that path only; no other thread can observe the intermediate state
// because the render thread does not start until initialize() returns.
DXGI_ADAPTER_DESC1 g_selected_adapter_desc{};
bool g_selected_adapter_valid = false;

// True when the adapter has no meaningful pool of its own, i.e. graphics memory is system
// memory (integrated graphics).
//
// Detected structurally rather than by vendor ID, because the condition is about the memory
// architecture. DXGI's dedicated figure is negligible next to the shared figure, and on this
// class of part it is the fictitious compatibility value (see the ring-sizing comment above,
// and Intel FAQ 000020962). The threshold is loose (256 MiB dedicated, with shared an order of
// magnitude larger), so a low-end discrete part with a small real pool is not classed as unified.
// Byte budget for the EFB-copy destination cache. It is hoisted out of the eviction site so the
// value in use can be reported with the live bytes. The pool's allocation scales with
// efb_scale^2 while this budget stays fixed, so it is the one resolution-scaled GPU pool that is
// neither a render target nor visible in any recording.
[[nodiscard]] std::uint64_t efb_copy_dest_budget_bytes() {
    static const std::uint64_t bytes = read_megabyte_budget(
        "GALAXY_EFB_COPY_DEST_BUDGET_MB", UINT64_C(192) << 20, false);
    return bytes;
}

[[nodiscard]] bool adapter_is_unified_memory() {
    if (!g_selected_adapter_valid) {
        return false;
    }
    const std::uint64_t dedicated = g_selected_adapter_desc.DedicatedVideoMemory;
    const std::uint64_t shared = g_selected_adapter_desc.SharedSystemMemory;
    constexpr std::uint64_t kNegligibleDedicatedBytes = 256ull * 1024ull * 1024ull;
    return dedicated <= kNegligibleDedicatedBytes &&
        shared >= dedicated * 8ull;
}

// Upload-ring budget per frame, in bytes, for one ring.
//
// Scaling down the reservation on unified-memory adapters has no behavioural trade-off, so it is
// the default rather than an environment-variable choice:
//
//   * A ring is created with CreateCommittedResource, but on an UPLOAD heap the untouched part is
//     never written and so never backed by physical pages. Reserving less removes address space
//     and page-file commit the workload was not using. UploadRing::peak_bytes_per_frame() reports
//     how much is used, so the reservation can be checked against actual use.
//   * Exhaustion is loud: UploadRing::allocate throws with the ring name, the request size and
//     the frame usage. A too-small ring fails on the first frame that needs the space.
//
// The unified-memory defaults below are not smaller than the dedicated ones. The vertex ring
// peaks at 117,361 vertices in one frame; at sizeof(GxVertexOut) == 144 that is 16.1 MiB, which
// would not fit a 16 MiB ring. The recorded peak sizes these defaults for a real worst case, and
// shrinking them without that evidence fails at launch. [gpu-rings] reports unified-memory=0/1
// and the per-ring peak, so a run on the target gives the numbers needed to size the rings.
// Until then the reservation stays as it was.
[[nodiscard]] std::size_t default_ring_bytes_per_frame(
    std::size_t dedicated_default_bytes,
    std::size_t unified_default_bytes) {
    return adapter_is_unified_memory() ? unified_default_bytes
                                       : dedicated_default_bytes;
}

// DLSS before it is installed. On an NVIDIA RTX adapter the UPSCALER row lists
// DLSS; applying it asks to download it. The launcher next to this runtime
// (Nebula.exe --install-dlss) shows NVIDIA's license and downloads the files,
// and the setting is saved so Streamline loads at the next start (it must load
// before the device exists). Guarded by s_native_settings_mutex.
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
    static const bool offered = g_selected_adapter_valid && g_selected_adapter_desc.VendorId == 0x10DEu &&
                                std::wcsstr(g_selected_adapter_desc.Description, L"RTX") != nullptr &&
                                !streamline_files_present() && !launcher_path().empty();
    return offered;
}

bool dlss_installed_now() {
    return s_dlss_installed_now && !streamline::dlss_supported();
}

// DLSS chosen before it is installed is not applied: the menu asks to
// download it. Once downloaded this session it applies and runs after a
// restart (FSR 1 until then).
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
    // Ready: the launcher starts the game again once this process has exited.
    s_dlss_offer = DlssOffer::None;
    if (start_launcher(L"--restart-after " + std::to_wstring(GetCurrentProcessId()), nullptr)) {
        std::cerr << "[dlss] restarting\n";
        PostMessageW(reinterpret_cast<HWND>(s_native_settings_window.load(std::memory_order_acquire)),
                     WM_CLOSE, 0, 0);
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
        return "DOWNLOAD NVIDIA DLSS (31 MB)? ENTER/Y YES  ESC/B NO";
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

}  // namespace

// Out-of-line so the budget can be reported without exposing the anonymous-namespace
// helper, and so both the eviction site and the telemetry read the same value.
std::uint64_t RendererD3D12::efb_copy_dest_budget_bytes() {
    return galaxy::gx::efb_copy_dest_budget_bytes();
}

static std::size_t vb_per_frame_bytes() {
    // Worst observed frame: 117,361 vertices x 144 B = 16.1 MiB, so 64 MiB leaves about 4x headroom.
    return ring_bytes_per_frame(
        "GALAXY_GPU_VERTEX_RING_MB",
        default_ring_bytes_per_frame(64u * 1024u * 1024u, 64u * 1024u * 1024u));
}
static std::size_t ib_per_frame_bytes() {
    // No recorded counter bounds this ring's demand; the default is kept until a peak is measured.
    return ring_bytes_per_frame(
        "GALAXY_GPU_INDEX_RING_MB",
        default_ring_bytes_per_frame(16u * 1024u * 1024u, 16u * 1024u * 1024u));
}
static std::size_t cb_per_frame_bytes() {
    return ring_bytes_per_frame(
        "GALAXY_GPU_CONSTANT_RING_MB",
        default_ring_bytes_per_frame(8u * 1024u * 1024u, 8u * 1024u * 1024u));
}
static std::size_t mtx_per_frame_bytes() {
    // No recorded counter bounds this ring's demand; the default is kept until a peak is measured.
    return ring_bytes_per_frame(
        "GALAXY_GPU_MATRIX_RING_MB",
        default_ring_bytes_per_frame(32u * 1024u * 1024u, 32u * 1024u * 1024u));
}
// 32 MiB default. A busy frame re-loads the XF palette many times (per-object and skinned
// matrices via LOAD_INDX), so the headroom is needed. peak_bytes_per_frame() reports actual use,
// so this figure can be tightened against measurements.
static constexpr unsigned    kSrvPerFrame = 32768u;
// Color and depth EFB sources never change during a renderer lifetime. Keep
// their two views outside the rewound frame segments instead of copying a
// descriptor into the frame ring for every EFB conversion. TextureCache's own
// SRV indices are independent of this prefix.
static constexpr unsigned    kPersistentSrvDescriptors = 6u;  // EFB, output EFB and sub-frame EFB conversion sources

void RendererD3D12::wait_for_gpu() {
    const std::uint64_t val = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), val);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    wait_for_fence_value(val);
}

bool RendererD3D12::create_efb_targets(unsigned efb_scale) {
    // Aurora MIT lib/window.cpp and lib/gx/gx.cpp keep logical Wii size
    // separate from the physical target. Apply that rule to Galaxy's D3D12
    // resources without changing guest EFB/XFB addresses or dimensions.
    //
    // Both resources are created before anything is replaced: a failure
    // (out of video memory) leaves the current targets untouched. On success
    // the previous targets move to efb_carry_*, whose contents begin_frame
    // copies into the new ones.
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

    ComPtr<ID3D12Resource> color;
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
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&color));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] EFB color " << w << 'x' << efb_h
                      << " allocation failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
    }
    ComPtr<ID3D12Resource> depth;
    {
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
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &drd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&depth));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] EFB depth " << w << 'x' << efb_h
                      << " allocation failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
    }
    if (!dsv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap_)))) {
            return false;
        }
    }

    // Commit. Callers have drained the GPU, so the descriptor slots below can
    // be rewritten (OMSetRenderTargets copied the old views when recorded).
    efb_carry_color_ = std::move(efb_color_);
    efb_carry_depth_ = std::move(efb_depth_);
    efb_color_ = std::move(color);
    efb_depth_ = std::move(depth);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        efb_depth_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());

    // EFB RTV at slot kSwapChainBuffers of the backbuffer RTV heap.
    {
        const UINT stride = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE h =
            rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
        device_->CreateRenderTargetView(efb_color_.Get(), nullptr, h);
        efb_rtv_ = h;
    }
    efb_dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    return true;
}

// After an internal-resolution change the new EFB starts with the previous targets' contents,
// resampled. The game expects the EFB it left at the end of the last frame (normally the display
// copy's clear, whose alpha its shadow and glow passes read); zero-filled targets would show as a
// dark picture with stray geometry.
void RendererD3D12::carry_over_efb_contents() {
    if (!efb_carry_color_ || !efb_carry_depth_) {
        efb_carry_color_.Reset();
        efb_carry_depth_.Reset();
        return;
    }
    if (!efb_carry_depth_pso_) {
        static constexpr char kDepthCarryPs[] =
            "Texture2D<float> t:register(t0);"
            "SamplerState s:register(s0);"
            "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
            "float main(V v):SV_Depth{return t.SampleLevel(s,v.uv,0);}";
        const ComPtr<ID3DBlob> vs = compile_hlsl(kBlitVs, sizeof(kBlitVs) - 1, "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(kDepthCarryPs, sizeof(kDepthCarryPs) - 1, "ps_5_1", "main");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = s_blit_root_sig.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = FALSE;
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 0;
        pd.DSVFormat = kEfbDepthFormat;
        pd.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(
                &pd, IID_PPV_ARGS(&efb_carry_depth_pso_)))) {
            efb_carry_depth_pso_.Reset();
        }
    }

    const DescriptorRing::Table table = srv_ring_.allocate(2);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(efb_carry_color_.Get(), &view, table.cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    D3D12_GPU_DESCRIPTOR_HANDLE depth_gpu = table.gpu;
    depth_gpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(efb_carry_depth_.Get(), &view, depth_cpu);

    D3D12_RESOURCE_BARRIER to_read[2]{};
    for (auto& barrier : to_read) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    to_read[0].Transition.pResource = efb_carry_color_.Get();
    to_read[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_read[1].Transition.pResource = efb_carry_depth_.Get();
    to_read[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    command_list_->ResourceBarrier(2u, to_read);

    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(kEfbWidth * efb_scale_),
        static_cast<float>(kEfbHeight * efb_scale_), 0.0f, 1.0f};
    const D3D12_RECT scissor{
        0, 0, static_cast<LONG>(kEfbWidth * efb_scale_),
        static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    command_list_->SetPipelineState(blit_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, table.gpu);
    command_list_->DrawInstanced(3, 1, 0, 0);
    if (efb_carry_depth_pso_) {
        command_list_->OMSetRenderTargets(0, nullptr, FALSE, &efb_dsv_);
        bound_rt_aux_ = 0u;
        command_list_->SetPipelineState(efb_carry_depth_pso_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, depth_gpu);
        command_list_->DrawInstanced(3, 1, 0, 0);
    } else {
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        command_list_->ClearDepthStencilView(
            efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
    }
    // The old targets are read by this frame's command list.
    retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_color_));
    retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_depth_));
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
}

HRESULT create_gx_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept {
    if (device == nullptr || root_signature == nullptr) {
        return E_INVALIDARG;
    }
    *root_signature = nullptr;

    // GX root signature:
    //   [0] Root CBV b0,space0  VS+GS - GxVsConstants
    //   [1] Root CBV b1,space0  PS - GxPsConstants
    //   [2] Descriptor table   SRV t0..t7,space0  PS - textures
    //   [3] Descriptor table   Sampler s0..s7       PS
    //   [4] Root SRV  t0,space1 VS - matrix palette
    //   [5] Root SRV  t1,space1 VS - previous vertices (sub-frame blending)
    //   [6] Root constants b2   VS - vertex blend mode, t, verdict slot
    //   [7] Root SRV  t2,space1 VS - vertex blend verdicts

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

    D3D12_ROOT_PARAMETER params[8]{};
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
    // The matrix palette also holds the XF lights, which per-pixel lighting
    // reads in the pixel shader.
    params[4].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[5].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[5].Descriptor.ShaderRegister = 1;
    params[5].Descriptor.RegisterSpace  = 1;
    params[5].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    params[6].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[6].Constants.ShaderRegister = 2;
    params[6].Constants.RegisterSpace  = 0;
    params[6].Constants.Num32BitValues = 4;
    params[6].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    params[7].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[7].Descriptor.ShaderRegister = 2;
    params[7].Descriptor.RegisterSpace  = 1;
    params[7].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 8;
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

static std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept {
    return ((std::clamp(cfg.anisotropic_filtering, 1u, 16u) & 0x1fu) << 8u) |
        (std::clamp(cfg.texture_lod_bias, 0u, 8u) & 0xfu);
}

SamplerTableKey make_sampler_table_key(const std::uint32_t keys[8], const RenderConfig& cfg) noexcept {
    SamplerTableKey key{};
    std::copy_n(keys, key.samplers.size(), key.samplers.begin());
    key.config = sampler_config_key(cfg);
    return key;
}

SamplerTableCache::Allocation SamplerTableCache::get_or_allocate(const SamplerTableKey& key) {
    if (const auto it = tables_.find(key); it != tables_.end()) {
        slot_frames_[(it->second - 8u) / 8u] = frame_;
        return {it->second, false};
    }
    const bool fresh_slot = cursor_ + 8u <= kSamplerHeapSlots;
    unsigned slot = fresh_slot ? (cursor_ - 8u) / 8u : eviction_cursor_;
    if (!fresh_slot) {
        unsigned checked = 0u;
        while (checked < kDynamicTables && slot_frames_[slot] == frame_) {
            slot = (slot + 1u) % kDynamicTables;
            ++checked;
        }
        if (checked == kDynamicTables) {
            throw std::runtime_error(
                "[Renderer] sampler heap exhausted: more than 255 distinct "
                "sampler configurations used within one frame");
        }
    }
    const unsigned index = 8u + slot * 8u;
    // Insert before retiring the old mapping: an allocation failure leaves
    // all resident descriptors discoverable. Do not hold a victim iterator
    // across insertion, which may rehash the map.
    const auto [it, inserted] = tables_.emplace(key, index);
    if (inserted) {
        if (fresh_slot) cursor_ += 8u;
        else {
            tables_.erase(slot_keys_[slot]);
            eviction_cursor_ = (slot + 1u) % kDynamicTables;
        }
        slot_keys_[slot] = key;
        slot_frames_[slot] = frame_;
    }
    return {it->second, inserted};
}

bool RendererD3D12::create_sampler_heap() {
    default_sampler_valid_.fill(false);
    for (auto& tables : sampler_tables_) tables.reset();
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
    sampler_heap_ = sampler_heaps_[sampler_slot_];

    const RenderConfig cfg = get_render_config();
    const std::uint32_t config_key = sampler_config_key(cfg);
    // Retain tables only in the heap where they were written. A table found in
    // a different slot's cache has no initialized descriptors in this heap.
    // Each heap warms independently, retaining its eight-descriptor tables
    // until its sampler configuration changes.
    sampler_tables_[sampler_slot_].reset(config_key);

    default_sampler_table_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    if (default_sampler_valid_[sampler_slot_] &&
        default_sampler_config_keys_[sampler_slot_] == config_key) {
        return;
    }

    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE base =
        sampler_heap_->GetCPUDescriptorHandleForHeapStart();

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
    // Slots 0..7 are never allocated dynamically. Their descriptors survive
    // this slot's fence/reset, and need rewriting only for changed settings.
    default_sampler_config_keys_[sampler_slot_] = config_key;
    default_sampler_valid_[sampler_slot_] = true;
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
    const auto allocation = sampler_tables_[sampler_slot_].get_or_allocate(make_sampler_table_key(keys, cfg));
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
        conv_scratch_rtv_slot_.reset();
    }

    refresh_efb_conversion_source_views();
    return true;
}

void RendererD3D12::refresh_efb_conversion_source_views() {
    if (!efb_color_ || !efb_depth_) {
        throw std::runtime_error("EFB conversion source resource unavailable");
    }
    if (!conv_efb_source_table_) {
        conv_efb_source_table_ = srv_ring_.allocate_persistent(2u);
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(
        efb_color_.Get(), &view, conv_efb_source_table_->cpu);
    auto depth_cpu = conv_efb_source_table_->cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(efb_depth_.Get(), &view, depth_cpu);
    conv_efb_color_srv_ = conv_efb_source_table_->gpu;
    auto depth_gpu = conv_efb_source_table_->gpu;
    depth_gpu.ptr += stride;
    conv_efb_depth_srv_ = depth_gpu;
}

void RendererD3D12::follow_window_internal_resolution() {
    // Dynamic aspect latches a new window shape at a later VI boundary than
    // the swap-chain resize, so the content aspect is part of the key.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect =
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect;
    if (s_follow_window_rearm.load(std::memory_order_relaxed) &&
        s_follow_window_rearm.exchange(false, std::memory_order_acq_rel)) {
        follow_window_width_ = 0u;
    }
    if (follow_window_retry_ms_ != 0u && GetTickCount64() >= follow_window_retry_ms_) {
        follow_window_retry_ms_ = 0u;
        follow_window_width_ = 0u;
    }
    if (s_bb_width == follow_window_width_ && s_bb_height == follow_window_height_ &&
        content_aspect == follow_window_aspect_) {
        return;
    }
    // An edge drag resizes every frame; settle first, then re-create once.
    if (s_move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    follow_window_width_ = s_bb_width;
    follow_window_height_ = s_bb_height;
    follow_window_aspect_ = content_aspect;
    RenderConfig render = get_render_config();
    if (!render.internal_follow_window) {
        return;
    }
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(s_bb_width), static_cast<int>(s_bb_height), content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return;
    }
    // The launcher's Match Output rule: cover the picture on both axes.
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth),
                                   content.height / 456.0);
    const unsigned scale = std::clamp(
        static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
    if (scale != render.efb_scale) {
        render.efb_scale = scale;
        set_render_config(render);
        stage_next_start_efb_scale(scale);
    }
}

void RendererD3D12::sync_post_process_settings() {
    if (!post_process_.ready()) {
        return;
    }
    const PostProcessSettings next = post_process_settings_from(get_render_config());
    const PostProcessSettings& current = post_process_.settings();
    if (next.aa != current.aa || next.smaa_quality != current.smaa_quality ||
        next.upscaler != current.upscaler || next.sharpness != current.sharpness) {
        post_process_.apply_settings(next, frame_index_, kFramesInFlight);
    }
}

void RendererD3D12::apply_pending_efb_scale() {
    unsigned wanted = std::clamp(get_render_config().efb_scale, 1u, kMaxEfbScale);
    if (temporal_mode() || spatial_quality_mode(get_render_config())) {
        const unsigned output_scale = output_efb_scale_for_window();
        if (output_scale != 0u) {
            wanted = temporal_mode() ? temporal_render_scale(output_scale)
                                     : quality_render_scale(output_scale, get_render_config().spatial_quality);
        }
    }
    wanted = std::min(wanted, display_efb_scale_cap());
    if (wanted == efb_scale_ || !device_ || !efb_color_ || output_efb_active_) {
        return;
    }
    if (wanted > efb_scale_ && !efb_scale_fits_memory(wanted)) {
        // Keep the current targets; the setting returns to what is live.
        std::cerr << "[Renderer] internal resolution " << wanted
                  << "x exceeds the video-memory budget; staying at "
                  << efb_scale_ << "x\n";
        RenderConfig render = get_render_config();
        render.efb_scale = efb_scale_;
        set_render_config(render);
        stage_next_start_efb_scale(efb_scale_);
        s_efb_scale_rejected.store(wanted, std::memory_order_release);
        if (render.internal_follow_window) {
            follow_window_retry_ms_ = GetTickCount64() + 10'000u;
        }
        return;
    }
    // Between frames with nothing recording; the GPU must be done with the
    // old targets and every scaled copy destination before they go away.
    wait_for_gpu();
    efb_copy_dests_.clear();
    efb_copy_dests_bytes_ = 0u;
    efb_copy_dests_tick_ = 0u;
    for (auto& retired : retired_efb_copy_textures_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
        retired.clear();
    }
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0u;
    const unsigned previous = efb_scale_;
    if (!create_efb_targets(wanted)) {
        // The current targets are untouched; the setting returns to them.
        RenderConfig render = get_render_config();
        render.efb_scale = previous;
        set_render_config(render);
        stage_next_start_efb_scale(previous);
        s_efb_scale_rejected.store(wanted, std::memory_order_release);
        return;
    }
    const unsigned applied = wanted;
    efb_scale_ = applied;
    refresh_efb_conversion_source_views();
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
    std::cout << "[Renderer] internal resolution " << previous << "x -> "
              << applied << "x (" << kEfbWidth * applied << "x"
              << kEfbHeight * applied << ")\n";
}

bool RendererD3D12::efb_scale_fits_memory(unsigned scale) const {
    if (!adapter3_) {
        return true;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(
            0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0u) {
        return true;
    }
    // Scale-dependent residency: EFB color + depth (8 bytes per pixel) and,
    // at most, the XFB pair plus one full-picture copy (12 bytes per pixel of
    // the 640x456 picture). Everything else is independent of the scale.
    const auto scaled_bytes = [](unsigned s) {
        const double area = static_cast<double>(s) * s;
        return area * (static_cast<double>(kEfbWidth) * kEfbHeight * 8.0 +
                       static_cast<double>(kEfbWidth) * 456.0 * 12.0);
    };
    const double needed = scaled_bytes(scale) - scaled_bytes(efb_scale_);
    return static_cast<double>(info.CurrentUsage) + needed <=
           0.9 * static_cast<double>(info.Budget);
}

std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>
detail::create_efb_conversion_source_views(
    ID3D12Device* device, ID3D12Resource* color, ID3D12Resource* depth,
    DescriptorRing& ring) {
    if (device == nullptr || color == nullptr || depth == nullptr) {
        throw std::runtime_error("EFB conversion source resource unavailable");
    }
    // Reserve both views together: exhaustion must not leave a partial pair.
    const auto table = ring.allocate_persistent(2u);
    const UINT stride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    auto depth_gpu = table.gpu;
    depth_gpu.ptr += stride;

    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device->CreateShaderResourceView(color, &view, table.cpu);
    view.Format = kEfbDepthSrvFormat;
    device->CreateShaderResourceView(depth, &view, depth_cpu);
    return {table.gpu, depth_gpu};
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
            return false;
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
        s_native_settings_window.store(
            reinterpret_cast<std::uintptr_t>(window_), std::memory_order_release);
        s_borderless_active.store(fullscreen_window, std::memory_order_release);
        // A centered geometry seed is not evidence that the OS cursor is in
        // the client. The first real message or authoritative cursor poll
        // acquires visibility.
        // The owner publishes real geometry/input through WM_SIZE/WM_SETFOCUS.
        // Do not reseed from this thread after it has begun processing messages.
        window_shown_ = !hidden_window;
    }

    // D3D12 debug layer and GPU-based validation: opt-in via GALAXY_D3D_DEBUG (1 = debug layer,
    // 2 = plus GPU validation). GPU-based validation slows the GPU considerably and the debug layer
    // adds CPU overhead per call, so neither is enabled by default in _DEBUG builds.
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
        // DLSS: Streamline must be running before the swap chain exists. It
        // starts whenever the user installed it (GALAXY_STREAMLINE=0 skips
        // it), so DLSS can be chosen in the settings without a restart.
        {
            char value[8]{};
            std::size_t length = 0u;
            const bool skip = getenv_s(&length, value, sizeof(value), "GALAXY_STREAMLINE") == 0 &&
                              length > 1u && value[0] == '0';
            if (!skip) {
                (void)streamline::startup(streamline::default_directory());
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
                    index, adapter_preference(),
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
        // Publish the selected adapter for the ring budgeting below. Assigned only
        // here, after every failure path, so `adapter_is_unified_memory()` can
        // never read a partially-populated description: it is guarded by its own
        // validity flag as well.
        g_selected_adapter_desc = adapter_desc;
        g_selected_adapter_valid = true;
        (void)adapter.As(&adapter3_);
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
                      // vram-mb alone is misleading on integrated graphics, where it is a fixed compatibility value
                      // (Intel reports 128 MB for applications that do not comprehend a unified memory architecture;
                      // see the ring-sizing comment above). The real budget there is SharedSystemMemory, which the OS
                      // caps at one-half of installed RAM and which is a limit, not a reservation. Emitting all three
                      // fields together lets a reader tell the cases apart: a large dedicated figure means a separate
                      // pool exists; a dedicated figure far below shared means unified memory.
                      << " shared-mb="
                      << (adapter_desc.SharedSystemMemory / (1024 * 1024))
                      << " host-ram-mb=" << host_ram_mb()
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
        {
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
            const bool offered =
                dxr_runtime_supported && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
                SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model,
                                                       sizeof(shader_model))) &&
                shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_5 && !adapter_is_unified_memory();
            s_ray_tracing_offered.store(offered, std::memory_order_relaxed);
            record_render_adapter(adapter_desc.Description, adapter_desc.VendorId, adapter_desc.DeviceId,
                                  adapter_preference() == DXGI_GPU_PREFERENCE_MINIMUM_POWER, offered);
        }
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
        const bool dlss_ready = streamline::active() &&
                                streamline::attach_device(device_.Get(), adapter_desc.AdapterLuid);
        caps.streamline_bridge_built = kStreamlineBuilt;
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
            true,
            dxr_runtime_supported,
            s_ray_tracing_offered.load(std::memory_order_relaxed),
            s_ray_tracing_offered.load(std::memory_order_relaxed),
            s_ray_tracing_offered.load(std::memory_order_relaxed)
                ? "ray-traced ambient occlusion (GRAPHICS: RAY TRACING)"
            : dxr_runtime_supported ? "needs DXR 1.1, shader model 6.5 and dedicated video memory"
                                    : "D3D12 raytracing tier is unsupported");
        caps.streamline = make_render_feature_status(
            caps.streamline_bridge_built,
            streamline::active(),
            streamline::active(),
            false,
            !caps.streamline_bridge_built ? "NVIDIA Streamline bridge is not built into this runtime"
            : streamline::active()        ? "NVIDIA Streamline is running"
                                          : "NVIDIA Streamline is not installed or not signed");
        caps.reflex = make_render_feature_status(
            false,
            false,
            false,
            false,
            "Streamline Reflex integration is not built");
        caps.dlss_super_resolution = make_render_feature_status(
            caps.streamline_bridge_built,
            dlss_ready,
            dlss_ready,
            dlss_ready,
            dlss_ready ? "DLSS super resolution is available" : "DLSS needs Streamline and an NVIDIA RTX GPU");
        caps.dlss_frame_generation = make_render_feature_status(
            false,
            false,
            false,
            false,
            "DLFG requires Streamline, Reflex, motion vectors, and UI tagging");
        set_render_feature_caps(caps);

        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        // Streamline tracks queues through its device proxy.
        d3d_check(streamline::queue_device(device_.Get())->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)),
                  "CreateCommandQueue");
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
        // ALLOW_MODE_SWITCH lets exclusive fullscreen switch the monitor to
        // the selected resolution and refresh rate.
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
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
        // Through Streamline's factory when it runs: its presents go through it.
        d3d_check(streamline::swap_chain_factory(factory.Get())
                      ->CreateSwapChainForHwnd(queue_.Get(), window_, &scd, nullptr, nullptr, &sc1),
                  "CreateSwapChain");
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
        // Exclusive fullscreen is entered by the first frame's display check
        // (reconcile_display_state), once the swap chain's buffers exist.

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
        // A saved scale above what this display can show is clamped here.
        efb_scale_ = std::min(efb_scale_, display_efb_scale_cap());
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

        efb_copy_heap_.initialize(device_.Get());

        // Presentation post-processing (SMAA, FSR 1, RCAS, box resample).
        {
            const RenderConfig cfg = get_render_config();
            const PostProcessSettings post = post_process_settings_from(cfg);
            PostProcessStatus off{};
            off.live_aa = static_cast<std::uint8_t>(post.aa);
            off.live_quality = post.smaa_quality;
            if (!cfg.post_process) {
                std::cerr << "[post-process] off; presenting with the plain blit\n";
                std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
                set_post_process_status(off);
            } else if (!post_process_.initialize(device_.Get(), queue_.Get(), post)) {
                std::cerr << "[post-process] unavailable; presenting with the plain blit\n";
                std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "INIT FAILED");
                set_post_process_status(off);
            } else {
                set_post_process_status(post_process_.status());
                std::cerr << "[post-process] aa=" << cfg.aa_mode << " smaa-quality=" << cfg.smaa_quality
                          << " upscaler=" << cfg.upscaler << " sharpness=" << cfg.sharpness << '\n';
            }
        }

        // Sampler heap.
        if (!create_sampler_heap()) return false;

        // Upload + descriptor rings.
        // Each ring reserves its largest segmentation (interpolation, temporal), so a frame-rate or
        // upscaler change only re-segments it: no 300-400 MB of upload memory created mid-game.
        constexpr unsigned kMostSegments = std::max({kFramesInFlight, kInterpSegments, kRingSegments});
        if (!vertex_ring_  .initialize(device_.Get(), "vertex",   vb_per_frame_bytes(),  kFramesInFlight,
                                       vb_per_frame_bytes() * std::max(kFramesInFlight, kInterpSegments))) return false;
        if (!index_ring_   .initialize(device_.Get(), "index",    ib_per_frame_bytes(),  kFramesInFlight,
                                       ib_per_frame_bytes() * std::max(kFramesInFlight, kInterpSegments))) return false;
        if (!constant_ring_.initialize(device_.Get(), "constant", cb_per_frame_bytes(),  kFramesInFlight,
                                       cb_per_frame_bytes() * 4u * kInterpSegments)) return false;
        if (!matrix_ring_  .initialize(device_.Get(), "matrix",   mtx_per_frame_bytes(), kFramesInFlight,
                                       mtx_per_frame_bytes() * kMostSegments)) return false;
        if (!srv_ring_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvPerFrame, kInterpSegments, kPersistentSrvDescriptors)) return false;

        // Conversion sources occupy the descriptor ring's immutable prefix.
        if (!create_conversion_pipeline()) return false;
        if (!create_clear_pipeline()) return false;

        // Reserved upload-ring bytes, emitted with the `[gpu]` adapter line so the two are read together:
        // the reservation is only meaningful next to the adapter that has to back it. This is the
        // configuration, not the usage. A mapped D3D12_HEAP_TYPE_UPLOAD buffer only commits physical pages
        // for the ranges actually written, so the resident cost tracks the per-frame peak rather than
        // bytes_per_frame * frames_in_flight. The peak is reported per ring below and at shutdown; the
        // pairing is what makes the 240 MiB default checkable against a real workload.
        {
            const auto ring_reserved_mb = [](std::size_t bytes) {
                return (bytes * kFramesInFlight) / (1024u * 1024u);
            };
            std::cerr << "[gpu-rings]"
                      << " vertex-reserved-mb=" << ring_reserved_mb(vb_per_frame_bytes())
                      << " index-reserved-mb=" << ring_reserved_mb(ib_per_frame_bytes())
                      << " constant-reserved-mb=" << ring_reserved_mb(cb_per_frame_bytes())
                      << " matrix-reserved-mb=" << ring_reserved_mb(mtx_per_frame_bytes())
                      << " unified-memory=" << (adapter_is_unified_memory() ? 1 : 0)
                      << " frames-in-flight=" << kFramesInFlight
                      << " total-reserved-mb="
                      << ring_reserved_mb(vb_per_frame_bytes()) +
                             ring_reserved_mb(ib_per_frame_bytes()) +
                             ring_reserved_mb(cb_per_frame_bytes()) +
                             ring_reserved_mb(mtx_per_frame_bytes())
                      << " note=reservation-not-residency"
                      << '\n';
        }

        // Ray tracing (config ray_tracing_enabled, GALAXY_RAY_TRACING=1) where offered.
        reconcile_ray_tracing();

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
    // Streamline shuts down before the swap chain and device it wraps go.
    retain_shutdown_failure("Streamline", [&] {
        temporal_.reset();
        streamline::shutdown();
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
        s_exclusive_fullscreen_active.store(false, std::memory_order_release);
    }
    s_blit_root_sig.Reset();
    post_process_.shutdown();
    ray_tracer_.shutdown();
    {
        const PlacedTextureAllocator::Stats heap = efb_copy_heap_.stats();
        std::cerr << "[efb-copy-heap] heaps=" << heap.heaps << " heap-bytes=" << heap.heap_bytes
                  << " used-bytes=" << heap.used_bytes << " placed=" << heap.placed
                  << " committed-fallbacks=" << heap.committed_fallbacks << '\n';
    }
    efb_copy_heap_.shutdown();
    blit_pipeline_.Reset();
    s_conv_root_sig.Reset();
    conv_pipeline_.Reset();
    conv_scratch_rtv_slot_.reset();
    conv_rtv_heap_.Reset();
    conv_scratch_rtv_ = {};
    conv_efb_color_srv_ = {};
    conv_efb_depth_srv_ = {};
    conv_efb_source_table_.reset();
    next_fence_value_ = 0;
    peek_command_list_.Reset();
    peek_allocator_.Reset();
    pointer_depth_capture_fence_ = 0;
    peek_target_.Reset();
    pointer_depth_target_.Reset();
    pointer_depth_readback_.Reset();
    for (auto& readback : pointer_depth_ring_) readback.Reset();
    pointer_depth_ring_rgba_.fill(nullptr);
    pointer_depth_pending_rgba_ = nullptr;
    peek_readback_.Reset();
    peek_rtv_heap_.Reset();
    peek_rtv_ = {};
    peek_readback_row_pitch_ = 0;
    s_clear_root_sig.Reset();
    clear_rgb_pipeline_.Reset();
    clear_alpha_pipeline_.Reset();
    timestamp_query_heap_.Reset();
    timestamp_readback_.Reset();
    timestamp_pending_.fill(false);
    timestamp_frequency_ = 0;
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0;
    next_efb_capture_path_.clear();
    readback_capture_path_.clear();
    backbuffer_readback_buf_.Reset();
    backbuffer_readback_pending_ = false;
    backbuffer_readback_fence_value_ = 0;
    backbuffer_readback_capture_ = false;
    backbuffer_readback_luma_trace_ = false;
    backbuffer_readback_width_ = backbuffer_readback_height_ =
        backbuffer_readback_row_pitch_ = 0;
    next_backbuffer_capture_path_.clear();
    backbuffer_readback_capture_path_.clear();
    backbuffer_readback_frame_ = 0;
    backbuffer_readback_xfb_addr_ = 0;
    backbuffer_readback_xfb_stamp_ = 0;
    selected_xfb_readback_buf_.Reset();
    selected_xfb_readback_source_.Reset();
    selected_xfb_readback_pending_ = false;
    selected_xfb_readback_fence_ = 0;
    selected_xfb_readback_width_ = selected_xfb_readback_height_ =
        selected_xfb_readback_pitch_ = 0;
    selected_xfb_readback_path_.clear();
    if (efb_copy_requests_ != 0u) {
        std::cerr << "[efb-copy-work] requests=" << efb_copy_requests_
                  << " shader-copies=" << efb_shader_copies_
                  << " identity-copies=" << efb_identity_copies_
                  << " identity-payload-bytes=" << efb_identity_payload_bytes_
                  << " scope=whole-renderer-lifetime\n";
    }
    efb_copy_requests_ = 0u;
    efb_shader_copies_ = 0u;
    efb_identity_copies_ = 0u;
    efb_identity_payload_bytes_ = 0u;
    efb_copy_dests_.clear();
    efb_copy_dests_bytes_ = 0u;
    efb_copy_dests_tick_ = 0u;
    for (auto& retired : retired_efb_copy_textures_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
        retired.clear();
    }
    efb_color_.Reset();
    efb_depth_.Reset();
    for (auto& b : backbuffers_) b.Reset();
    rtv_heap_.Reset(); dsv_heap_.Reset(); sampler_heap_.Reset(); root_signature_.Reset();
    for (auto& heap : sampler_heaps_) heap.Reset();
    for (auto& tables : sampler_tables_) tables.reset();
    default_sampler_table_ = {};
    default_sampler_valid_.fill(false);
    swap_chain_.Reset(); command_list_.Reset();
    for (auto& a : allocators_) a.Reset();
    vertex_ring_.reset();
    index_ring_.reset();
    constant_ring_.reset();
    matrix_ring_.reset();
    srv_ring_.reset();
    invalidate_gx_bindings();
    last_latency_wait_us_ = last_frame_slot_wait_us_ =
        last_timestamp_read_us_ = last_begin_reset_us_ = 0;
    telemetry_window_start_qpc_ = last_present_qpc_ =
        last_paced_present_qpc_ = 0;
    telemetry_first_serial_start_ = telemetry_xfb_copy_start_ = 0;
    has_presented_swap_chain_ = window_shown_ = false;
    telemetry_present_count_ = telemetry_adjacent_serial_transition_count_ = 0;
    telemetry_interval_count_ = 0;
    last_presented_xfb_serial_ = ~std::uint64_t{0};
    telemetry_interval_ms_ = telemetry_interval_max_ms_ = 0.0;
    telemetry_present_ms_ = telemetry_present_max_ms_ = 0.0;
    telemetry_pace_wait_ms_ = telemetry_pace_wait_max_ms_ = 0.0;
    telemetry_latency_wait_ms_ = telemetry_latency_wait_max_ms_ = 0.0;
    telemetry_gpu_ms_ = 0.0;
    telemetry_gpu_samples_ = telemetry_no_xfb_present_count_ =
        telemetry_backbuffer_clear_count_ = 0;
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
    resize_swap_chain(static_cast<UINT>(extent >> 32u), static_cast<UINT>(extent), false);
}

void RendererD3D12::resize_swap_chain(UINT new_width, UINT new_height, bool force) {
    if (!swap_chain_ || !rtv_heap_ || new_width == 0u || new_height == 0u) {
        return;
    }
    if (!force && new_width == s_bb_width && new_height == s_bb_height) {
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

namespace {
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
}  // namespace

bool RendererD3D12::enter_exclusive_fullscreen(
    const std::wstring& monitor_device, unsigned width, unsigned height,
    unsigned refresh_hz) {
    if (!swap_chain_ || !adapter3_) {
        return false;
    }
    ComPtr<IDXGIOutput> output;
    for (UINT index = 0u;; ++index) {
        ComPtr<IDXGIOutput> candidate;
        if (adapter3_->EnumOutputs(index, &candidate) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_OUTPUT_DESC desc{};
        if (candidate && SUCCEEDED(candidate->GetDesc(&desc)) &&
            _wcsicmp(desc.DeviceName, monitor_device.c_str()) == 0) {
            output = candidate;
            break;
        }
    }
    if (!output) {
        // The monitor is driven by another GPU (hybrid laptops): borderless
        // fullscreen on it is the closest equivalent.
        std::cerr << "[Renderer] exclusive fullscreen: monitor not on the rendering GPU;"
                     " using borderless\n";
        return false;
    }
    DXGI_MODE_DESC wanted{};
    wanted.Width = width;
    wanted.Height = height;
    wanted.RefreshRate = {refresh_hz, refresh_hz != 0u ? 1u : 0u};
    wanted.Format = kBackbufferFormat;
    DXGI_MODE_DESC closest{};
    if (width == 0u || height == 0u ||
        FAILED(output->FindClosestMatchingMode(&wanted, &closest, device_.Get()))) {
        DXGI_OUTPUT_DESC desc{};
        (void)output->GetDesc(&desc);
        closest = wanted;
        closest.Width = static_cast<UINT>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
        closest.Height = static_cast<UINT>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
        closest.RefreshRate = {0u, 0u};
    }
    wait_for_gpu();
    (void)swap_chain_->ResizeTarget(&closest);
    const HRESULT hr = swap_chain_->SetFullscreenState(TRUE, output.Get());
    if (FAILED(hr)) {
        std::cerr << "[Renderer] exclusive fullscreen unavailable hr=0x" << std::hex
                  << static_cast<unsigned long>(hr) << std::dec << "; using borderless\n";
        return false;
    }
    exclusive_fullscreen_active_ = true;
    s_exclusive_fullscreen_active.store(true, std::memory_order_release);
    resize_swap_chain(closest.Width, closest.Height, true);
    std::cout << "[Renderer] exclusive fullscreen " << closest.Width << 'x' << closest.Height
              << " @ " << (closest.RefreshRate.Denominator != 0u
                               ? closest.RefreshRate.Numerator / closest.RefreshRate.Denominator
                               : 0u)
              << " Hz\n";
    return true;
}

void RendererD3D12::leave_exclusive_fullscreen() {
    if (!swap_chain_ || !exclusive_fullscreen_active_) {
        return;
    }
    wait_for_gpu();
    (void)swap_chain_->SetFullscreenState(FALSE, nullptr);
    exclusive_fullscreen_active_ = false;
    s_exclusive_fullscreen_active.store(false, std::memory_order_release);
}

void RendererD3D12::reconcile_display_state() {
    if (!swap_chain_ || window_ == nullptr || hidden_window_mode_enabled()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    const unsigned mode = std::min(cfg.display_mode, kDisplayExclusive);
    AppliedDisplay& applied = applied_display_;
    const bool same_display = applied.valid && applied.mode == mode &&
        applied.monitor == cfg.monitor &&
        (mode != kDisplayExclusive ||
         (applied.fullscreen_width == cfg.fullscreen_width &&
          applied.fullscreen_height == cfg.fullscreen_height &&
          applied.fullscreen_refresh_hz == cfg.fullscreen_refresh_hz));
    if (same_display) {
        if (mode == kDisplayExclusive) {
            // Alt+Tab leaves exclusive fullscreen; return once the game is
            // in front again. Checked twice a second.
            if (++display_checks_ % 30u == 0u) {
                BOOL fullscreen = FALSE;
                if (SUCCEEDED(swap_chain_->GetFullscreenState(&fullscreen, nullptr)) &&
                    !fullscreen && GetForegroundWindow() == window_) {
                    exclusive_fullscreen_active_ = false;
                    s_exclusive_fullscreen_active.store(false, std::memory_order_release);
                    applied.valid = false;
                }
            }
            return;
        }
        if (mode != kDisplayWindowed ||
            (applied.window_width == cfg.window_width &&
             applied.window_height == cfg.window_height) ||
            s_move_size_active.load(std::memory_order_acquire)) {
            return;
        }
    }

    const std::vector<MonitorInfo> monitors = enumerate_monitors();
    const MonitorInfo* monitor = find_monitor(monitors, widen_utf8(cfg.monitor.data()));
    if (monitor == nullptr) {
        return;
    }
    if (exclusive_fullscreen_active_) {
        leave_exclusive_fullscreen();
    }
    WindowPlacement placement{};
    if (mode == kDisplayWindowed) {
        placement.style = kWindowStyle;
        placement.rect = windowed_rect_on_monitor(*monitor, cfg.window_width, cfg.window_height);
        placement.borderless = false;
    } else {
        placement.style = WS_POPUP;
        placement.rect = monitor->bounds;
        placement.borderless = true;
    }
    DWORD_PTR result = 0u;
    if (SendMessageTimeoutW(window_, kPlaceWindowMessage, 0u,
                            reinterpret_cast<LPARAM>(&placement),
                            SMTO_NORMAL, 2000u, &result) == 0) {
        std::cerr << "[Renderer] display change: the window did not respond\n";
        return;
    }
    bool exclusive = false;
    if (mode == kDisplayExclusive) {
        exclusive = enter_exclusive_fullscreen(monitor->device, cfg.fullscreen_width,
                                               cfg.fullscreen_height, cfg.fullscreen_refresh_hz);
    }
    if (!exclusive) {
        // The placed window's client size (WM_SIZE can lag behind the
        // cross-thread placement).
        RECT client{};
        if (GetClientRect(window_, &client) && client.right > 0 && client.bottom > 0) {
            resize_swap_chain(static_cast<UINT>(client.right),
                              static_cast<UINT>(client.bottom), false);
        }
    }
    applied.valid = true;
    applied.mode = mode;
    applied.monitor = cfg.monitor;
    applied.fullscreen_width = cfg.fullscreen_width;
    applied.fullscreen_height = cfg.fullscreen_height;
    applied.fullscreen_refresh_hz = cfg.fullscreen_refresh_hz;
    applied.window_width = cfg.window_width;
    applied.window_height = cfg.window_height;
    std::cout << "[Renderer] display " << (mode == kDisplayWindowed ? "windowed"
                                           : mode == kDisplayBorderless ? "borderless"
                                                                        : "fullscreen")
              << ' ' << narrow_utf8(monitor->device) << ' ' << s_bb_width << 'x'
              << s_bb_height << '\n';
}

void RendererD3D12::ensure_native_settings_corner_texture_loaded() {
    if (ui_corner_texture_load_attempted_) {
        return;
    }
    ui_corner_texture_load_attempted_ = true;

    // Diagnostic-only isolation switch, never set automatically. It lets a crash repro separate the
    // creation of the corner-texture SRV, root signature and PSO from clear_ui_rects as the trigger.
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
        param.Constants.Num32BitValues = 13; // rect/color/screen + glyph mask/mode
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
    const UiSolidQuadConstants constants{
        {left, top, right - left, bottom - top},
        {color[0], color[1], color[2], color[3]},
        {s_bb_width, s_bb_height},
        {0u, 0u}, 0u,
    };
    command_list_->SetGraphicsRootSignature(ui_solid_root_sig_.Get());
    command_list_->SetPipelineState(ui_solid_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 13, &constants, 0);
    command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list_->DrawInstanced(4, 1, 0, 0);
    invalidate_gx_bindings();
}

void RendererD3D12::draw_ui_glyph(
    int x, int y, int scale, std::uint64_t bitmap,
    const std::array<float, 4>& color) {
    if (scale <= 0 || bitmap == 0u || s_bb_width == 0u || s_bb_height == 0u) return;
    ensure_native_settings_solid_pipeline_loaded();
    if (!ui_solid_pipeline_ready_) return;
    const UiSolidQuadConstants constants{
        {x, y, scale, scale},
        {color[0], color[1], color[2], color[3]},
        {s_bb_width, s_bb_height},
        {static_cast<std::uint32_t>(bitmap),
         static_cast<std::uint32_t>(bitmap >> 32u)}, 1u,
    };
    command_list_->SetGraphicsRootSignature(ui_solid_root_sig_.Get());
    command_list_->SetPipelineState(ui_solid_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 13, &constants, 0);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    // One instance per cell, row-major; unlit cells have degenerate geometry.
    command_list_->DrawInstanced(4, 35, 0, 0);
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
    bound_vertex_blend_valid_ = false;
    bound_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bound_vertex_view_ = {};
    bound_vertex_view_valid_ = false;
    bound_index_view_ = {};
    bound_index_view_valid_ = false;
    bound_scissor_valid_ = false;
    bound_scissor_input_valid_ = false;
    bound_viewport_valid_ = false;
}

void RendererD3D12::begin_frame(bool present_swap_chain) {
    // Diagnostic stall watch, gated on its own opt-in flag. GALAXY_TRACE_STALL_WATCH is unset in the
    // shipped runtime-env.json, and trace_stall_watch_enabled() holds a function-local
    // `static const bool`, so with the flag off this is a constant false and clang deletes the
    // timestamps, the subtraction and the branch below outright.
    const bool stall_watch = trace_stall_watch_enabled();
    const auto stall_watch_begin_frame_start = stall_watch
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    // Live resize lands here: the one point guaranteed to be between frames
    // with no in-flight command-list recording on this renderer. Contains
    // its own full GPU-idle wait, so it belongs before any per-slot state
    // below is touched.
    reconcile_display_state();
    apply_pending_resize();
    follow_window_internal_resolution();
    apply_pending_efb_scale();
    sync_post_process_settings();
    reconcile_ray_tracing();
    {
        if (temporal_interp_suspended_ && temporal_interp_key() != temporal_interp_key_) {
            // Other settings: measured again.
            temporal_interp_suspended_ = false;
            s_temporal_interp_suspended.store(false, std::memory_order_relaxed);
        }
        const RingMode mode = interpolation_mode() ? RingMode::Interp
            : temporal_mode() ? RingMode::Temporal : RingMode::Normal;
        if (mode != ring_mode_) {
            wait_for_gpu();
            const unsigned segments = mode == RingMode::Interp ? kInterpSegments : kFramesInFlight;
            const unsigned matrix_segments = mode == RingMode::Normal ? kFramesInFlight
                : mode == RingMode::Interp ? kInterpSegments : kRingSegments;
            // Interpolated sub-frames add one VS constant block per scene
            // record each; the constant segment holds a whole game frame.
            const std::size_t constant_bytes = cb_per_frame_bytes() * (mode == RingMode::Interp ? 4u : 1u);
            const auto place = [&](UploadRing& ring, const char* name, std::size_t bytes, unsigned count) {
                if (ring.resegment(bytes, count)) {
                    return true;
                }
                ring.reset();
                return ring.initialize(device_.Get(), name, bytes, count);
            };
            if (!place(vertex_ring_, "vertex", vb_per_frame_bytes(), segments) ||
                !place(index_ring_, "index", ib_per_frame_bytes(), segments) ||
                !place(constant_ring_, "constant", constant_bytes, segments) ||
                !place(matrix_ring_, "matrix", mtx_per_frame_bytes(), matrix_segments)) {
                throw std::runtime_error("ring resize failed");
            }
            matrix_segments_ = matrix_segments;
            matrix_segment_ = 0u;
            matrix_segment_fence_.fill(0u);
            matrix_segment_used_ = false;
            matrix_segment_started_ = false;
            ring_segment_ = 0u;
            ring_segment_fence_.fill(0u);
            ring_advance_pending_ = true;
            replay_valid_ = false;
            replay_recording_ = false;
            // The logs name memory of the rings just rebuilt.
            run_valid_ = false;
            prev_run_valid_ = false;
            copy_writes_.clear();
            replay_textures_.clear();
            play_textures_.clear();
            run_textures_.clear();
            release_copy_snapshots(~0u);
            for (auto& retired : retired_segment_textures_) {
                retired.clear();
            }
            ring_mode_ = mode;
        }
    }
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    temporal_interp_ = ring_mode_ == RingMode::Interp && temporal_mode();
    if (ring_mode_ != RingMode::Interp) {
        if (replay_valid_ && frame_slot_ == replay_source_slot_) {
            // The recorded frame's ring segments are about to be rewound.
            replay_valid_ = false;
        }
        replay_recording_ = false;
    }
    last_latency_wait_us_ = 0;
    last_frame_slot_wait_us_ = 0;
    last_timestamp_read_us_ = 0;
    last_begin_reset_us_ = 0;
    const bool collect_begin_timing = trace_present_stats_enabled();
    if (present_swap_chain && has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(collect_begin_timing);
    }

    // Wait only on the slot about to be reused.
    const std::uint64_t wait_val = fence_values_[frame_slot_];
    if (wait_val != 0u) {
        const auto fence_wait_start = collect_begin_timing
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const bool timeline_slot_wait =
            galaxy::timeline::enabled() && completed_fence_value() < wait_val;
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitBegin, wait_val);
        }
        wait_for_fence_value(wait_val);
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitEnd, wait_val);
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
            if (galaxy::timeline::enabled() && timestamps[1] >= timestamps[0]) {
                // GPU ticks map onto QPC through a calibration pair refreshed
                // every 60 frames; the two clocks drift by parts per million.
                static std::uint64_t calibration_gpu = 0u;
                static std::uint64_t calibration_qpc = 0u;
                static unsigned calibration_age = 0u;
                if (calibration_qpc == 0u || ++calibration_age >= 60u) {
                    UINT64 gpu = 0u;
                    UINT64 cpu = 0u;
                    if (SUCCEEDED(queue_->GetClockCalibration(&gpu, &cpu))) {
                        calibration_gpu = gpu;
                        calibration_qpc = cpu;
                        calibration_age = 0u;
                    }
                }
                if (calibration_qpc != 0u && qpc_frequency_.QuadPart > 0) {
                    const double scale =
                        static_cast<double>(qpc_frequency_.QuadPart) /
                        static_cast<double>(timestamp_frequency_);
                    const auto to_qpc = [&](std::uint64_t gpu_ticks) {
                        const double delta = static_cast<double>(
                            static_cast<std::int64_t>(gpu_ticks - calibration_gpu));
                        return static_cast<std::uint64_t>(
                            static_cast<std::int64_t>(calibration_qpc) +
                            static_cast<std::int64_t>(delta * scale));
                    };
                    galaxy::timeline::record_at(galaxy::timeline::Event::GpuBegin,
                                            to_qpc(timestamps[0]), wait_val);
                    galaxy::timeline::record_at(galaxy::timeline::Event::GpuEnd,
                                            to_qpc(timestamps[1]), wait_val);
                }
            }
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

    ray_tracer_.begin_frame(frame_index_);

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
    dred_marker(command_list_.Get(), L"frame slot=%u", frame_slot_);
    if (timestamp_query_heap_) {
        command_list_->EndQuery(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u);
    }

    if (ring_mode_ == RingMode::Interp) {
        // One segment per game frame: the frames (chunks) of a game frame
        // append to it, so its draws stay replayable until the segment
        // comes round again.
        if (ring_advance_pending_) {
            ring_advance_pending_ = false;
            ring_segment_ = (ring_segment_ + 1u) % kInterpSegments;
            if (replay_valid_ && ring_segment_ == replay_segment_) {
                replay_valid_ = false;
            }
            if (run_valid_ && ring_segment_ == run_segment_) {
                run_valid_ = false;
            }
            if (prev_run_valid_ && ring_segment_ == prev_run_segment_) {
                prev_run_valid_ = false;
            }
            if (ring_segment_fence_[ring_segment_] != 0u) {
                wait_for_fence_value(ring_segment_fence_[ring_segment_]);
            }
            retired_segment_textures_[ring_segment_].clear();
            // The segment's logs are gone (and their sub-frames complete).
            release_copy_snapshots(ring_segment_);
            vertex_ring_.begin_frame(ring_segment_);
            index_ring_.begin_frame(ring_segment_);
            constant_ring_.begin_frame(ring_segment_);
            matrix_ring_.begin_frame(ring_segment_);
            srv_ring_.begin_frame(ring_segment_);
            sampler_slot_ = ring_segment_;
            reset_sampler_frame();
            // A frame that never reached its display copy is dropped.
            begin_replay_record();
        }
    } else {
    vertex_ring_  .begin_frame(frame_slot_);
    index_ring_   .begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    if (matrix_segments_ == kFramesInFlight) {
        matrix_ring_.begin_frame(frame_slot_);
    } else {
        // Temporal mode: a frame that wrote no palette keeps appending to the
        // current segment, so the previous scene's palettes stay addressable
        // across present, peek and utility frames.
        if (matrix_segment_used_ || !matrix_segment_started_) {
            matrix_segment_ = (matrix_segment_ + 1u) % matrix_segments_;
            ++matrix_segment_serial_;
            if (matrix_segment_fence_[matrix_segment_] != 0u) {
                wait_for_fence_value(matrix_segment_fence_[matrix_segment_]);
            }
            matrix_ring_.begin_frame(matrix_segment_);
            matrix_segment_started_ = true;
        }
        matrix_begin_cursor_ = matrix_ring_.cursor();
    }
    srv_ring_     .begin_frame(frame_slot_);
    sampler_slot_ = frame_slot_;
    reset_sampler_frame();
    }
    bound_rt_aux_ = 0u;
    if (ring_mode_ == RingMode::Interp) {
        // A logged frame's sub-frames may still name these copy textures:
        // they live until the current game-frame segment comes round again.
        auto& keep = retired_segment_textures_[ring_segment_];
        for (auto& texture : retired_efb_copy_textures_[frame_slot_]) {
            keep.push_back(std::move(texture));
        }
    }
    retired_efb_copy_textures_[frame_slot_].clear();

    ensure_frame_values();
    // Bind both heaps for the entire frame.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    if (efb_carry_color_ || efb_carry_depth_) {
        carry_over_efb_contents();
    }
    if (collect_begin_timing) {
        last_begin_reset_us_ = elapsed_us(
            reset_start,
            std::chrono::steady_clock::now());
    }
    if (stall_watch) {
        const double stall_watch_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() -
            stall_watch_begin_frame_start).count();
        if (stall_watch_ms > 12.0) {
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

void RendererD3D12::refresh_display_rate() {
    if (const unsigned hz = refresh_override_hz(); hz != 0u) {
        if (display_refresh_hz_ != hz) {
            std::cerr << "[display] refresh-hz=" << hz << " (GALAXY_DISPLAY_REFRESH_HZ)\n";
        }
        display_refresh_hz_ = hz;
        return;
    }
    if (swap_chain_ == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (FAILED(swap_chain_->GetContainingOutput(&output)) ||
        FAILED(output->GetDesc(&desc))) {
        return;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode) == FALSE ||
        mode.dmDisplayFrequency <= 1u) {
        return;
    }
    const double hz = static_cast<double>(mode.dmDisplayFrequency);
    if (hz != display_refresh_hz_) {
        std::cerr << "[display] refresh-hz=" << hz << '\n';
    }
    display_refresh_hz_ = hz;
}

UINT RendererD3D12::present_sync_interval(unsigned configured, float max_fps) {
    if (configured != 1u) {
        return configured;
    }
    if (display_refresh_presents_++ % 120u == 0u) {
        refresh_display_rate();
    }
    const double hz = display_refresh_hz_;
    if (hz <= 0.0) {
        return 1u;
    }
    // EnumDisplaySettings reports whole hertz: 119 for 119.88, 239 for 239.76.
    const double rate = present_frame_rate(max_fps, configured, hz);
    const double multiple = std::round(hz / rate);
    if (multiple >= 2.0 && multiple <= 4.0 &&
        std::abs(hz - rate * multiple) <= 0.015 * hz) {
        return static_cast<UINT>(multiple);
    }
    return 1u;
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
    // Interpolation presents on its own slot clock.
    if ((realtime_paced && realtime_wait_cap_us == 0u) ||
        ring_mode_ == RingMode::Interp ||
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

void RendererD3D12::present_to_swap_chain() {
    const bool stall_watch = trace_stall_watch_enabled();
    const RenderConfig cfg = get_render_config();
    const UINT vsync = present_sync_interval(std::min(2u, cfg.vsync_interval), cfg.max_fps);
    const UINT present_flags =
        (vsync == 0 && tearing_supported_ && !exclusive_fullscreen_active_) ? DXGI_PRESENT_ALLOW_TEARING
                                           : 0u;
    const auto stall_watch_pace_start = stall_watch
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    pace_present_if_needed(cfg.vsync_interval, cfg.max_fps);
    if (stall_watch) {
        const double stall_watch_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() -
                stall_watch_pace_start).count();
        if (stall_watch_ms > 12.0) {
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
    const auto stall_watch_present_start = stall_watch
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    static std::uint64_t timeline_present_ordinal = 0u;
    const bool timeline_present = galaxy::timeline::enabled();
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentBegin,
                             ++timeline_present_ordinal, vsync);
    }
    const HRESULT present_hr = swap_chain_->Present(vsync, present_flags);
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentEnd,
                             timeline_present_ordinal);
        // The OS's own record of which refresh showed which present.
        DXGI_FRAME_STATISTICS statistics{};
        // An occluded or not yet displayed swap chain reports a zero
        // sync time; that is no timeline instant.
        if (SUCCEEDED(swap_chain_->GetFrameStatistics(&statistics)) &&
            statistics.SyncQPCTime.QuadPart != 0) {
            galaxy::timeline::record_at(
                galaxy::timeline::Event::DisplayStats,
                static_cast<std::uint64_t>(statistics.SyncQPCTime.QuadPart),
                (static_cast<std::uint64_t>(statistics.PresentCount) << 32u) |
                    statistics.PresentRefreshCount,
                statistics.SyncRefreshCount);
        }
        if (timeline_present_ordinal % 30u == 1u) {
            using DwmTimingFn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
            static const DwmTimingFn dwm_timing = [] {
                const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
                return dwm == nullptr ? nullptr
                    : reinterpret_cast<DwmTimingFn>(
                          GetProcAddress(dwm, "DwmGetCompositionTimingInfo"));
            }();
            DWM_TIMING_INFO timing{};
            timing.cbSize = sizeof(timing);
            if (dwm_timing != nullptr && SUCCEEDED(dwm_timing(nullptr, &timing)) &&
                timing.qpcVBlank != 0u) {
                galaxy::timeline::record_at(galaxy::timeline::Event::VBlank, timing.qpcVBlank,
                                        timing.qpcRefreshPeriod);
            }
        }
    }
    if (stall_watch) {
        const double stall_watch_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() -
                stall_watch_present_start).count();
        if (stall_watch_ms > 12.0) {
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
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        ++measured_present_count_;
        if (measured_present_window_qpc_ == 0) {
            measured_present_window_qpc_ = now.QuadPart;
            measured_present_count_ = 0u;
        } else if (qpc_frequency_.QuadPart > 0 &&
                   now.QuadPart - measured_present_window_qpc_ >= qpc_frequency_.QuadPart) {
            const double hz = static_cast<double>(measured_present_count_) *
                static_cast<double>(qpc_frequency_.QuadPart) /
                static_cast<double>(now.QuadPart - measured_present_window_qpc_);
            measured_present_hz_.store(hz, std::memory_order_relaxed);
            s_measured_present_hz.store(hz, std::memory_order_relaxed);
            measured_present_window_qpc_ = now.QuadPart;
            measured_present_count_ = 0u;
        }
    }
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&present_end);
        DXGI_FRAME_STATISTICS shown{};
        if (SUCCEEDED(swap_chain_->GetFrameStatistics(&shown)) &&
            shown.PresentCount != last_stats_present_count_) {
            if (last_stats_present_count_ != 0u &&
                shown.PresentCount == last_stats_present_count_ + 1u) {
                const UINT refreshes =
                    shown.PresentRefreshCount - last_stats_present_refresh_count_;
                ++telemetry_shown_refreshes_[std::min<UINT>(refreshes, 4u)];
            }
            last_stats_present_count_ = shown.PresentCount;
            last_stats_present_refresh_count_ = shown.PresentRefreshCount;
        }
        if (display_refresh_hz_ == 0.0) {
            refresh_display_rate();
        }
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
            ++telemetry_interval_count_;
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
                telemetry_interval_count_ != 0u
                    ? telemetry_interval_ms_ /
                          static_cast<double>(telemetry_interval_count_)
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
                << " gpu-timing=" << (!gpu_timestamps_enabled() ? "disabled" :
                    telemetry_gpu_samples_ == 0u ? "pending" : "measured")
                << " gpu-ms(avg)=";
            if (telemetry_gpu_samples_ != 0u) message << gpu_avg;
            else message << "unavailable";
            message
                << " gpu-samples=" << telemetry_gpu_samples_
                << " gpu-total-ms=" << telemetry_gpu_ms_
                << " gpu-scope=completed-renderer-command-lists"
                << " no-xfb-presents="
                << telemetry_no_xfb_present_count_
                << " backbuffer-clears="
                << telemetry_backbuffer_clear_count_
                << " post-frames-total=" << post_process_.status().frames
                << " aa-wanted-frames-total=" << post_process_.status().aa_wanted_frames
                << " smaa-frames-total=" << post_process_.status().smaa_frames
                << " fxaa-frames-total=" << post_process_.status().fxaa_frames
                << " aa-skipped-frames-total=" << post_process_.status().aa_skipped_frames
                << " refresh-hz=" << display_refresh_hz_
                << " shown-refreshes(0/1/2/3/4+)="
                << telemetry_shown_refreshes_[0] << '/'
                << telemetry_shown_refreshes_[1] << '/'
                << telemetry_shown_refreshes_[2] << '/'
                << telemetry_shown_refreshes_[3] << '/'
                << telemetry_shown_refreshes_[4]
                << '\n';
            std::cerr << message.str();
            telemetry_shown_refreshes_ = {};

            telemetry_window_start_qpc_ = present_end.QuadPart;
            telemetry_first_serial_start_ = progress.first_serial_presentations;
            telemetry_xfb_copy_start_ = produced_copies;
            telemetry_present_count_ = 0;
            telemetry_interval_count_ = 0;
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

std::uint64_t RendererD3D12::end_frame(
    bool present_swap_chain,
    SubmissionCallback on_submitted,
    void* submission_context) {
    // Ray tracing: this chunk's traced draws read upload rings the next frame begin rewinds.
    if (ray_tracer_.scene_open()) {
        ray_tracer_.flush_geometry(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this);
        bound_pipeline_ = nullptr;
    }
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
    if (ring_mode_ == RingMode::Interp) {
        ring_segment_fence_[ring_segment_] = submitted_fence;
    } else if (matrix_segments_ != kFramesInFlight) {
        matrix_segment_used_ = matrix_ring_.cursor() != matrix_begin_cursor_;
        matrix_segment_fence_[matrix_segment_] = submitted_fence;
    }
    if (pointer_depth_in_frame_pending_) {
        pointer_depth_capture_fence_ = submitted_fence;
        pointer_depth_in_frame_pending_ = false;
    }
    for (AsyncPeekField& field : async_peek_fields_) {
        if (field.in_frame) {
            field.fence = submitted_fence;
            field.in_frame = false;
        }
    }
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
    // Submission completion and physical presentation have separate owners.
    // Signal failure never reaches this callback; no GPU wait is introduced.
    if (on_submitted != nullptr) {
        on_submitted(submission_context, submitted_fence);
    }

    if (present_swap_chain) {
        present_to_swap_chain();
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
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kFramePeCompletionTimeoutMs);
    const HRESULT event_hr =
        fence_->SetEventOnCompletion(fence_value, fence_event_);
    if (FAILED(event_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(event_hr, "frame PE SetEventOnCompletion");
    }
    // A previous timed-out registration can wake this reusable auto-reset
    // event later. Only the fence value authorizes resource reuse/readback.
    // Consume stale wakes without extending the original timeout budget.
    for (;;) {
        const std::uint64_t completed = completed_fence_value();
        if (completed >= fence_value) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            dump_device_removal(device_.Get());
            throw std::runtime_error(
                "frame-specific GPU PE completion wait timed out");
        }
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - now).count();
        const DWORD wait_result = WaitForSingleObjectEx(
            fence_event_, static_cast<DWORD>(remaining), FALSE);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_TIMEOUT) {
            // Recheck on both wake and timeout: completion can race the timeout.
            continue;
        }
        const DWORD wait_error = GetLastError();
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "frame-specific GPU PE completion wait failed: result=" +
            std::to_string(wait_result) +
            " win32=" + std::to_string(wait_error));
    }
}

// ---------------------------------------------------------------------------
// Temporal upscaling (upscaler mode 2)
// ---------------------------------------------------------------------------

bool RendererD3D12::temporal_mode() const {
    const unsigned upscaler = get_render_config().upscaler;
    return (upscaler == 2u && fsr3_available()) || (upscaler == 3u && streamline::dlss_supported());
}

unsigned RendererD3D12::temporal_render_scale(unsigned output_scale) const {
    return quality_render_scale(output_scale, get_render_config().temporal_quality);
}

bool RendererD3D12::ensure_motion_target() {
    if (motion_target_ && motion_target_scale_ == efb_scale_) {
        return true;
    }
    if (motion_target_) {
        // An internal-resolution change: frames in flight may still use them.
        wait_for_gpu();
    }
    motion_target_.Reset();
    reactive_target_.Reset();
    motion_target_scale_ = 0u;
    if (!motion_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 2;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&motion_rtv_heap_)),
                  "motion vector RTV heap");
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = kEfbWidth * efb_scale_;
    rd.Height = kEfbHeight * efb_scale_;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kMotionVectorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kMotionVectorFormat;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&motion_target_)))) {
        return false;
    }
    rd.Format = kReactiveMaskFormat;
    cv.Format = kReactiveMaskFormat;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&reactive_target_)))) {
        motion_target_.Reset();
        return false;
    }
    motion_rtv_ = motion_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    reactive_rtv_ = motion_rtv_;
    reactive_rtv_.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device_->CreateRenderTargetView(motion_target_.Get(), nullptr, motion_rtv_);
    device_->CreateRenderTargetView(reactive_target_.Get(), nullptr, reactive_rtv_);
    const float zero[4]{};
    command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
    command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
    motion_target_scale_ = efb_scale_;
    return true;
}

static constexpr char kMotionDebugPs[] =
    "Texture2D<float2> mv:register(t0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float4 main(V v):SV_Target{"
    " float2 m=mv.Load(int3(int2(v.p.xy),0));"
    " return float4(saturate(0.5+m.x/16.0),saturate(0.5+m.y/16.0),0.5,1.0);}";

void RendererD3D12::record_motion_debug_view() {
    if (!motion_target_) {
        return;
    }
    if (!motion_debug_pipeline_) {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 1;
        rsd.pParameters = &param;
        ComPtr<ID3DBlob> blob, err;
        d3d_check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
                  "SerializeMotionDebugRS");
        d3d_check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               IID_PPV_ARGS(&motion_debug_root_)),
                  "CreateMotionDebugRS");
        const ComPtr<ID3DBlob> vs = compile_hlsl(kBlitVs, sizeof(kBlitVs) - 1, "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(kMotionDebugPs, sizeof(kMotionDebugPs) - 1, "ps_5_1", "main");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = motion_debug_root_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kEfbColorFormat;
        pd.SampleDesc.Count = 1;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&motion_debug_pipeline_)),
                  "CreateMotionDebugPSO");
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = motion_target_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const DescriptorRing::Table table = srv_ring_.allocate(1u);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kMotionVectorFormat;
    device_->CreateShaderResourceView(motion_target_.Get(), &view, table.cpu);
    command_list_->SetGraphicsRootSignature(motion_debug_root_.Get());
    command_list_->SetPipelineState(motion_debug_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, table.gpu);
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(kEfbWidth * efb_scale_),
                            static_cast<float>(kEfbHeight * efb_scale_), 0.0f, 1.0f};
    const D3D12_RECT sci{0, 0, static_cast<LONG>(kEfbWidth * efb_scale_),
                         static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
}

// ---------------------------------------------------------------------------
// Interpolated redraw: replay log and sub-frames
// ---------------------------------------------------------------------------

bool RendererD3D12::interpolation_mode() const {
    // GALAXY_INTERPOLATION=0 turns it off (diagnostics).
    static const bool allowed = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value), "GALAXY_INTERPOLATION") == 0 &&
                 length > 1u && value[0] == '0');
    }();
    if (!allowed) {
        return false;
    }
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(cfg.max_fps, cfg.vsync_interval, display_refresh_hz_);
    if (temporal_mode()) {
        // The upscaler's history follows the presented frames: one per slot of
        // a clock locked to the game's 60 Hz.
        const double multiple = std::round(rate / 60.0);
        if (!temporal_interpolation_allowed() || temporal_interp_suspended_ ||
            std::abs(rate - 60.0 * multiple) > 0.5) {
            return false;
        }
    }
    return interpolated_frames_per_game_frame(rate) != 0u;
}

RendererD3D12::TemporalInterpKey RendererD3D12::temporal_interp_key() const {
    const RenderConfig cfg = get_render_config();
    TemporalInterpKey key{};
    key.upscaler = cfg.upscaler;
    key.quality = cfg.temporal_quality;
    key.efb_scale = output_efb_active_ ? alt_efb_.scale : efb_scale_;  // the render EFB's
    key.output_scale = output_efb_scale_for_window();
    key.max_fps = cfg.max_fps;
    key.vsync = cfg.vsync_interval;
    key.refresh_hz = display_refresh_hz_;
    return key;
}

void RendererD3D12::suspend_temporal_interpolation(double ms_per_frame) {
    if (temporal_interp_suspended_) {
        return;
    }
    temporal_interp_suspended_ = true;
    temporal_interp_key_ = temporal_interp_key();
    s_temporal_interp_suspended.store(true, std::memory_order_relaxed);
    std::cerr << "[interp] temporal upscaling of the replays off: " << ms_per_frame
              << " ms GPU per presented frame\n";
}

void RendererD3D12::note_stripe_vertices(std::span<const StripeVertex> vertices) {
    if (!replay_recording_ || vertices.empty()) {
        return;
    }
    replay_stripes_.draws.push_back({static_cast<std::uint32_t>(replay_draws_.size()),
                                     static_cast<std::uint32_t>(replay_stripes_.vertices.size()),
                                     static_cast<std::uint32_t>(vertices.size())});
    replay_stripes_.vertices.insert(replay_stripes_.vertices.end(), vertices.begin(), vertices.end());
}

void RendererD3D12::publish_replay_log() {
    std::swap(replay_stripes_, play_stripes_);
    std::swap(replay_ops_, play_ops_);
    std::swap(replay_draws_, play_draws_);
    std::swap(replay_viewports_, play_viewports_);
    std::swap(replay_clears_, play_clears_);
    std::swap(replay_copies_, play_copies_);
    std::swap(replay_textures_, play_textures_);
    play_start_output_active_ = replay_start_output_active_;
}

bool RendererD3D12::start_replay_playback() {
    if (!replay_valid_) {
        return false;
    }
    // The playing log becomes the one before (vertex blending reads it).
    prev_run_valid_ = run_valid_;
    if (run_valid_) {
        std::swap(prev_run_draws_, run_draws_);
        std::swap(prev_run_copies_, run_copies_);
        std::swap(prev_run_stripes_, run_stripes_);
        prev_run_segment_ = run_segment_;
        prev_run_serial_ = run_serial_;
    }
    std::swap(play_stripes_, run_stripes_);
    run_blend_vertices_.clear();
    std::swap(play_ops_, run_ops_);
    std::swap(play_draws_, run_draws_);
    std::swap(play_viewports_, run_viewports_);
    std::swap(play_clears_, run_clears_);
    std::swap(play_copies_, run_copies_);
    std::swap(play_textures_, run_textures_);
    run_start_output_active_ = play_start_output_active_;
    run_segment_ = replay_segment_;
    run_serial_ = play_serial_;
    run_valid_ = true;
    replay_valid_ = false;
    return true;
}

// Blend-pair check: one group per pair compares the positions of the two
// draws vertex by vertex (GxVertexOut, 144-byte stride; position first).
static constexpr char kBlendValidateCs[] = R"HLSL(
ByteAddressBuffer ring : register(t0);
ByteAddressBuffer resident : register(t1);
StructuredBuffer<uint4> pairs : register(t2);
RWStructuredBuffer<uint4> verdicts : register(u0);
// Last valid Load3 offset of each buffer. Root views have no bounds, and
// compilers may run both loads of a select: both stay inside their buffers.
cbuffer Limits : register(b0) {
    uint ring_last;
    uint resident_last;
};
groupshared uint g_same;
groupshared uint g_worst;
groupshared uint g_lo[3];
groupshared uint g_hi[3];
// Float bits ordered like the floats, for integer min/max.
uint ordered(float f) {
    const uint u = asuint(f);
    return (u & 0x80000000u) != 0u ? ~u : (u | 0x80000000u);
}
float unordered(uint u) {
    return asfloat((u & 0x80000000u) != 0u ? (u & 0x7FFFFFFFu) : ~u);
}
float3 position(uint in_resident, uint offset) {
    const float3 r = asfloat(resident.Load3(min(offset, resident_last)));
    const float3 g = asfloat(ring.Load3(min(offset, ring_last)));
    return in_resident != 0u ? r : g;
}
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
    const uint4 pair = pairs[group.x];
    if (lane == 0u) {
        g_same = 0u;
        g_worst = 0u;
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            g_lo[k] = 0xFFFFFFFFu;
            g_hi[k] = 0u;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    uint same = 0u, worst = 0u;
    uint lo[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    uint hi[3] = {0u, 0u, 0u};
    for (uint i = lane; i < pair.z; i += 64u) {
        const float3 c = position(pair.w & 1u, pair.x + i * 144u);
        const float3 p = position((pair.w >> 1u) & 1u, pair.y + i * 144u);
        same += all(c == p) ? 1u : 0u;
        const float3 d = c - p;
        const float dist = sqrt(dot(d, d));
        worst = max(worst, isfinite(dist) ? asuint(dist) : 0x7F800000u);
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            lo[k] = min(lo[k], min(ordered(c[k]), ordered(p[k])));
            hi[k] = max(hi[k], max(ordered(c[k]), ordered(p[k])));
        }
    }
    InterlockedAdd(g_same, same);
    InterlockedMax(g_worst, worst);
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        InterlockedMin(g_lo[k], lo[k]);
        InterlockedMax(g_hi[k], hi[k]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
        float3 extent;
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            extent[k] = unordered(g_hi[k]) - unordered(g_lo[k]);
        }
        const float size = sqrt(dot(extent, extent));
        verdicts[group.x] = uint4(pair.z, g_same, g_worst, asuint(isfinite(size) ? size : 0.0));
    }
}
)HLSL";

// Blend verdicts, then the presented frame's values (t, t of the frame before,
// jitter) at kFrameValuesElement and a zero element read by game frames. Made
// at the first frame, zeroed, so every draw can read it.
void RendererD3D12::ensure_frame_values() {
    if (blend_verdicts_ || !device_) {
        return;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(kZeroValuesElement + 1u) * 16u;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&blend_verdicts_)),
              "frame values CreateCommittedResource");
    blend_verdicts_->SetName(L"GX vertex blend verdicts and frame values");
    const UploadRing::Allocation zeros = constant_ring_.allocate(static_cast<std::size_t>(desc.Width), 256u);
    std::memset(zeros.cpu, 0, static_cast<std::size_t>(desc.Width));
    command_list_->CopyBufferRegion(blend_verdicts_.Get(), 0u, constant_ring_.resource(), zeros.offset, desc.Width);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

bool RendererD3D12::ensure_blend_validation() {
    if (blend_validate_pso_) {
        return true;
    }
    if (blend_validate_failed_ || !device_) {
        return false;
    }
    blend_validate_failed_ = true;
    if (!blend_verdicts_) {
        return false;
    }
    D3D12_ROOT_PARAMETER params[5]{};
    for (unsigned i = 0; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].Descriptor.ShaderRegister = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[4].Constants.ShaderRegister = 0;
    params[4].Constants.Num32BitValues = 2;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 5;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&blend_validate_root_)))) {
        return false;
    }
    try {
        const ComPtr<ID3DBlob> cs =
            compile_hlsl(kBlendValidateCs, sizeof(kBlendValidateCs) - 1u, "cs_5_1", "main");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = blend_validate_root_.Get();
        pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&blend_validate_pso_)))) {
            return false;
        }
    } catch (const std::exception& error) {
        std::cerr << "[interp-blend] validation shader: " << error.what() << '\n';
        return false;
    }
    blend_validate_failed_ = false;
    return true;
}

// At the start of the sequence's first sub-frame (before its draws).
// Earlier stripe vertices, one 144-byte GxVertexOut per output vertex, from
// the ring or the resident buffer at the source's byte offset.
static constexpr char kStripeGatherCs[] = R"HLSL(
ByteAddressBuffer ring : register(t0);
ByteAddressBuffer resident : register(t1);
StructuredBuffer<uint2> sources : register(t2);
RWByteAddressBuffer earlier : register(u0);
cbuffer Limits : register(b0) {
    uint ring_last;
    uint resident_last;
    uint vertices;
};
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint k = id.x / 36u;
    if (k >= vertices) {
        return;
    }
    const uint word = id.x - k * 36u;
    const uint2 source = sources[k];
    const uint at = source.x + word * 4u;
    // Both loads stay inside their buffers (compilers may run both).
    const uint a = ring.Load(min(at, ring_last));
    const uint b = resident.Load(min(at, resident_last));
    earlier.Store(k * 144u + word * 4u, source.y != 0u ? b : a);
}
)HLSL";

bool RendererD3D12::ensure_stripe_gather(std::uint64_t bytes) {
    if (!stripe_gather_pso_) {
        if (stripe_gather_failed_ || !device_) {
            return false;
        }
        stripe_gather_failed_ = true;
        D3D12_ROOT_PARAMETER params[5]{};
        for (unsigned i = 0; i < 3u; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[4].Constants.Num32BitValues = 3u;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 5;
        rsd.pParameters = params;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
            FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                IID_PPV_ARGS(&stripe_gather_root_)))) {
            return false;
        }
        try {
            const ComPtr<ID3DBlob> cs = compile_hlsl(kStripeGatherCs, sizeof(kStripeGatherCs) - 1u, "cs_5_1", "main");
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = stripe_gather_root_.Get();
            desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&stripe_gather_pso_)))) {
                return false;
            }
        } catch (const std::exception& error) {
            std::cerr << "[stripe-gather] shader: " << error.what() << '\n';
            return false;
        }
        stripe_gather_failed_ = false;
    }
    if (stripe_earlier_ && stripe_earlier_bytes_ >= bytes) {
        return true;
    }
    std::uint64_t size = std::max<std::uint64_t>(stripe_earlier_bytes_ * 2u, 1ull << 20u);
    while (size < bytes) {
        size *= 2u;
    }
    if (stripe_earlier_) {
        // Sub-frames in flight still read the old one.
        interp_retired_.push_back({std::move(stripe_earlier_), next_fence_value_});
        stripe_earlier_bytes_ = 0u;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&stripe_earlier_)))) {
        return false;
    }
    stripe_earlier_->SetName(L"GX stripe particle earlier vertices");
    stripe_earlier_bytes_ = size;
    stripe_earlier_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return true;
}

void RendererD3D12::record_stripe_gather() {
    const std::size_t bytes = stripe_gather_.size() * sizeof(stripe_gather_[0]);
    const UploadRing::Allocation table = constant_ring_.allocate(bytes, 256u);
    std::memcpy(table.cpu, stripe_gather_.data(), bytes);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = stripe_earlier_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = stripe_earlier_state_;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetComputeRootSignature(stripe_gather_root_.Get());
    command_list_->SetPipelineState(stripe_gather_pso_.Get());
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const bool resident = resident_vertex_base_ != 0u && resident_vertex_bytes_ >= 4u;
    const std::uint32_t limits[3]{
        static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 4u, 0xFFFFFFF0u)),
        static_cast<std::uint32_t>(std::min<std::uint64_t>((resident ? resident_vertex_bytes_ : ring_bytes) - 4u,
                                                           0xFFFFFFF0u)),
        static_cast<std::uint32_t>(stripe_gather_.size())};
    command_list_->SetComputeRootShaderResourceView(0, vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(1, resident ? resident_vertex_base_ : vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(2, table.gpu);
    command_list_->SetComputeRootUnorderedAccessView(3, stripe_earlier_->GetGPUVirtualAddress());
    command_list_->SetComputeRoot32BitConstants(4, 3u, limits, 0u);
    const auto threads = static_cast<std::uint64_t>(stripe_gather_.size()) * 36u;
    command_list_->Dispatch(static_cast<UINT>((threads + 63u) / 64u), 1u, 1u);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    stripe_earlier_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bound_pipeline_ = nullptr;
}

void RendererD3D12::reconcile_ray_tracing() {
    const bool wanted =
        get_render_config().ray_tracing_enabled && s_ray_tracing_offered.load(std::memory_order_relaxed);
    if (wanted == ray_tracer_.ready() || (wanted && rt_start_failed_) || !device_) {
        return;
    }
    if (!wanted) {
        // Recorded sub-frame replays composite the tracer's results: none may run again.
        wait_for_gpu();
        for (ReplayList& entry : replay_lists_) {
            entry.sequence = 0u;
        }
        ray_tracer_.shutdown();
        std::cerr << "[rt] off\n";
        return;
    }
    char value[8]{};
    std::size_t length = 0u;
    const bool validate =
        getenv_s(&length, value, sizeof(value), "GALAXY_RT_VALIDATE") == 0 && length > 1u && value[0] == '1';
    rt_start_failed_ = !ray_tracer_.initialize(device_.Get(), timestamp_frequency_, validate, kEfbColorFormat);
}

RtSources RendererD3D12::rt_sources() const {
    RtSources sources{};
    sources.vertex_ring = {vertex_ring_.gpu_base(), vertex_ring_.total_bytes()};
    sources.resident = {resident_vertex_base_, resident_vertex_bytes_};
    sources.index_ring = {index_ring_.gpu_base(), index_ring_.total_bytes()};
    sources.constant_ring = {constant_ring_.gpu_base(), constant_ring_.total_bytes()};
    sources.matrix_ring = {matrix_ring_.gpu_base(), matrix_ring_.total_bytes()};
    return sources;
}

RtUpload RendererD3D12::rt_allocate(void* renderer, std::size_t bytes, std::size_t alignment) {
    const UploadRing::Allocation allocation =
        static_cast<RendererD3D12*>(renderer)->constant_ring_.allocate(bytes, alignment);
    return {allocation.cpu, allocation.gpu};
}

// A traced draw's buffers: vertices in the frame ring or the resident decoded-vertex buffer,
// R16/R32 indices in the index ring, VS constants in the constant ring and its palette in the
// matrix ring. Draws whose data lives elsewhere are not traced.
void RendererD3D12::rt_note_draw(const DrawCall& call, const RtDrawInfo& info) {
    if (!ray_tracer_.accepts(info) || call.index_count < 3u ||
        call.topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
        call.vertex_view.StrideInBytes != kRtVertexStride || call.vertex_view.SizeInBytes == 0u) {
        return;
    }
    const auto offset_in = [](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint64_t bytes, D3D12_GPU_VIRTUAL_ADDRESS base,
                              std::uint64_t size, std::uint32_t& offset) {
        if (base == 0u || va < base || va + bytes > base + size || va - base > 0xFFFFFFFFu) {
            return false;
        }
        offset = static_cast<std::uint32_t>(va - base);
        return true;
    };
    RtDraw draw{};
    draw.flags = info.surface;
    const D3D12_GPU_VIRTUAL_ADDRESS vertices = call.vertex_view.BufferLocation;
    if (!offset_in(vertices, call.vertex_view.SizeInBytes, vertex_ring_.gpu_base(), vertex_ring_.total_bytes(),
                   draw.vertex_offset)) {
        if (!offset_in(vertices, call.vertex_view.SizeInBytes, resident_vertex_base_, resident_vertex_bytes_,
                       draw.vertex_offset)) {
            return;
        }
        draw.flags |= kRtDrawResident;
    }
    draw.vertex_count = call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes;
    if (call.index_view.BufferLocation != 0u) {
        if (!offset_in(call.index_view.BufferLocation, call.index_view.SizeInBytes, index_ring_.gpu_base(),
                       index_ring_.total_bytes(), draw.index_offset)) {
            return;
        }
        draw.flags |= kRtDrawIndexed;
        if (call.index_view.Format == DXGI_FORMAT_R32_UINT) {
            draw.flags |= kRtDrawWideIndices;
        }
    }
    if (!offset_in(call.vs_constants, sizeof(GxVsConstants), constant_ring_.gpu_base(), constant_ring_.total_bytes(),
                   draw.constants_offset)) {
        return;
    }
    if (call.matrix_palette != 0u &&
        !offset_in(call.matrix_palette, 16u, matrix_ring_.gpu_base(), matrix_ring_.total_bytes(),
                   draw.palette_offset)) {
        return;
    }
    draw.triangle_count = call.index_count / 3u;
    ray_tracer_.add_draw(draw, info);
}

void RendererD3D12::rt_trace_scene() {
    if (!ray_tracer_.scene_open()) {
        return;
    }
    const bool depth = efb_depth_ && conv_efb_depth_srv_.ptr != 0u;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (depth) {
        command_list_->ResourceBarrier(1, &barrier);
    }
    const D3D12_RESOURCE_DESC desc = depth ? efb_depth_->GetDesc() : D3D12_RESOURCE_DESC{};
    const auto width = static_cast<std::uint32_t>(desc.Width);
    dred_marker(command_list_.Get(), L"rt scene");
    static_cast<void>(ray_tracer_.trace_scene(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this,
                                              depth ? conv_efb_depth_srv_ : D3D12_GPU_DESCRIPTOR_HANDLE{}, width,
                                              desc.Height));
    // The occlusion multiplies the opaque scene before the translucent phases draw; a recorded
    // log applies it again on each sub-frame's replay.
    const int slot = ray_tracer_.light_slot();
    if (depth && slot >= 0 && ray_tracer_.reflections_active() && efb_color_ && conv_efb_color_srv_.ptr != 0u) {
        // Reflected light is the opaque scene's colour (the conversion views: colour, then depth).
        D3D12_RESOURCE_BARRIER color = barrier;
        color.Transition.pResource = efb_color_.Get();
        color.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        color.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &color);
        ray_tracer_.trace_reflections(command_list_.Get(), &RendererD3D12::rt_allocate, this, conv_efb_color_srv_,
                                      width, desc.Height);
        std::swap(color.Transition.StateBefore, color.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &color);
    }
    if (depth && slot >= 0) {
        ray_tracer_.composite(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_, width, desc.Height, slot, false);
        if (replay_recording_) {
            replay_ops_.push_back({ReplayKind::RtComposite, static_cast<std::uint32_t>(slot)});
        }
    }
    if (depth) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &barrier);
    }
    // The picture before the translucent phases: what the glows add to it measures their light.
    if (depth && ray_tracer_.glow_open() && efb_color_ && conv_efb_color_srv_.ptr != 0u) {
        D3D12_RESOURCE_BARRIER views[2]{};
        for (D3D12_RESOURCE_BARRIER& view : views) {
            view.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            view.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            view.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        views[0].Transition.pResource = efb_color_.Get();
        views[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        views[1].Transition.pResource = efb_depth_.Get();
        views[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        command_list_->ResourceBarrier(2, views);
        ray_tracer_.snapshot_glow_base(command_list_.Get(), conv_efb_color_srv_, width, desc.Height);
        for (D3D12_RESOURCE_BARRIER& view : views) {
            std::swap(view.Transition.StateBefore, view.Transition.StateAfter);
        }
        command_list_->ResourceBarrier(2, views);
    }
    // The compute and composite pipelines replaced the GX bindings.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::rt_capture_picture() {
    if (!ray_tracer_.lighting_active() || !efb_color_ || !efb_depth_ || conv_efb_color_srv_.ptr == 0u) {
        return;
    }
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.pResource = efb_depth_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    command_list_->ResourceBarrier(2, barriers);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    dred_marker(command_list_.Get(), L"rt picture");
    // The conversion views hold the EFB colour, then its depth.
    ray_tracer_.capture_picture(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this,
                                conv_efb_color_srv_, static_cast<std::uint32_t>(desc.Width), desc.Height);
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2, barriers);
    bound_pipeline_ = nullptr;
}

void RendererD3D12::record_rt_composite(int slot, bool reproject) {
    if (!efb_depth_ || conv_efb_depth_srv_.ptr == 0u) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = efb_depth_->GetDesc();
    ray_tracer_.composite(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_, static_cast<std::uint32_t>(desc.Width),
                          desc.Height, slot, reproject);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::record_blend_validation() {
    if (!blend_validate_pending_) {
        return;
    }
    blend_validate_pending_ = false;
    if (!stripe_gather_.empty()) {
        record_stripe_gather();
    }
    if (blend_pairs_.empty()) {
        return;
    }
    const std::size_t bytes = blend_pairs_.size() * sizeof(blend_pairs_[0]);
    const UploadRing::Allocation table = constant_ring_.allocate(bytes, 256u);
    std::memcpy(table.cpu, blend_pairs_.data(), bytes);
    if (blend_verdicts_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = blend_verdicts_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = blend_verdicts_state_;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        command_list_->ResourceBarrier(1, &barrier);
    }
    command_list_->SetComputeRootSignature(blend_validate_root_.Get());
    command_list_->SetPipelineState(blend_validate_pso_.Get());
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const bool resident = resident_vertex_base_ != 0u && resident_vertex_bytes_ >= 12u;
    const std::uint32_t limits[2]{
        static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 12u, 0xFFFFFFF0u)),
        static_cast<std::uint32_t>(std::min<std::uint64_t>((resident ? resident_vertex_bytes_ : ring_bytes) - 12u,
                                                           0xFFFFFFF0u))};
    command_list_->SetComputeRootShaderResourceView(0, vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(1, resident ? resident_vertex_base_ : vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(2, table.gpu);
    command_list_->SetComputeRootUnorderedAccessView(3, blend_verdicts_->GetGPUVirtualAddress());
    command_list_->SetComputeRoot32BitConstants(4, 2u, limits, 0u);
    dred_marker(command_list_.Get(), L"blend-validate pairs=%zu", blend_pairs_.size());
    command_list_->Dispatch(static_cast<UINT>(blend_pairs_.size()), 1u, 1u);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bound_pipeline_ = nullptr;
}

void RendererD3D12::prepare_vertex_blend(std::span<const std::int32_t> previous_record) {
    run_blend_vertices_.assign(run_draws_.size(), BlendSource{});
    blend_pairs_.clear();
    stripe_gather_.clear();
    blend_validate_pending_ = false;
    std::int32_t ps_records = 0;
    for (const DrawCall& call : run_draws_) {
        ps_records = std::max(ps_records, call.ps_record + 1);
    }
    run_ps_previous_.assign(static_cast<std::size_t>(ps_records), -1);
    if (!prev_run_valid_ || prev_run_serial_ + 1u != run_serial_ || previous_record.empty()) {
        return;
    }
    // Where a draw's vertices are: the frame vertex ring (0) or the resident
    // decoded-vertex buffer (1), and the byte offset there.
    const D3D12_GPU_VIRTUAL_ADDRESS ring_base = vertex_ring_.gpu_base();
    const std::uint64_t ring_bytes =
        vertex_ring_.resource() != nullptr ? vertex_ring_.resource()->GetDesc().Width : 0u;
    const auto locate = [&](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint32_t bytes, std::uint32_t& offset) {
        if (va >= ring_base && va + bytes <= ring_base + ring_bytes) {
            offset = static_cast<std::uint32_t>(va - ring_base);
            return 0;
        }
        if (resident_vertex_base_ != 0u && va >= resident_vertex_base_ &&
            va + bytes <= resident_vertex_base_ + resident_vertex_bytes_) {
            offset = static_cast<std::uint32_t>(va - resident_vertex_base_);
            return 1;
        }
        return -1;
    };
    // The draws of each previous record, in log order (counting sort).
    std::int32_t records = 0;
    for (const DrawCall& call : prev_run_draws_) {
        records = std::max(records, call.replay_record + 1);
    }
    std::vector<std::uint32_t>& scratch = blend_scratch_;
    const std::size_t count = static_cast<std::size_t>(records);
    scratch.assign(2u * count + 1u + prev_run_draws_.size(), 0u);
    std::uint32_t* const first = scratch.data();              // [records + 1]
    std::uint32_t* const fill = first + count + 1u;           // [records]
    std::uint32_t* const order = fill + count;                // [draws]
    for (const DrawCall& call : prev_run_draws_) {
        if (call.replay_record >= 0) ++first[call.replay_record + 1];
    }
    for (std::size_t r = 0; r < count; ++r) first[r + 1u] += first[r];
    for (std::size_t d = 0; d < prev_run_draws_.size(); ++d) {
        const std::int32_t r = prev_run_draws_[d].replay_record;
        if (r >= 0) order[first[r] + fill[r]++] = static_cast<std::uint32_t>(d);
    }
    // Reuse `fill` as each previous record's cursor: a record's draws pair in
    // order, skipping up to three unmatched draws (one drawn in only one of
    // the frames) to the next one with as many vertices.
    std::fill(fill, fill + count, 0u);
    // Stripe draws of both logs, by draw index (GALAXY_INTERP_STRIPES=0: paired
    // as other draws, diagnostics).
    static const bool stripes = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_STRIPES") == 0 && length > 1u &&
                 value[0] == '0');
    }();
    stripe_of_draw_.assign(run_draws_.size(), -1);
    for (std::size_t i = 0; stripes && i < run_stripes_.draws.size(); ++i) {
        const StripeDraw& stripe = run_stripes_.draws[i];
        if (stripe.draw < stripe_of_draw_.size()) stripe_of_draw_[stripe.draw] = static_cast<std::int32_t>(i);
    }
    stripe_of_prev_draw_.assign(prev_run_draws_.size(), -1);
    for (std::size_t i = 0; i < prev_run_stripes_.draws.size(); ++i) {
        const StripeDraw& stripe = prev_run_stripes_.draws[i];
        if (stripe.draw < stripe_of_prev_draw_.size()) {
            stripe_of_prev_draw_[stripe.draw] = static_cast<std::int32_t>(i);
        }
    }
    const bool pairs_checked = ensure_blend_validation();
    // Per stripe draw: its draw and first gathered vertex (addresses once the
    // gather buffer is sized).
    std::vector<std::array<std::uint32_t, 2>> stripe_blends;
    std::vector<std::uint32_t> seen;
    static const bool trace = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_TRACE_BLEND") == 0 &&
               length > 1u && value[0] != '0';
    }();
    std::size_t in_scene = 0u, matched = 0u, paired = 0u, size_differs = 0u, same_buffer = 0u, blended = 0u;
    std::ostringstream detail;
    for (std::size_t d = 0; d < run_draws_.size(); ++d) {
        const DrawCall& call = run_draws_[d];
        const std::int32_t r = call.replay_record;
        if (r < 0 || static_cast<std::size_t>(r) >= previous_record.size()) continue;
        const std::int32_t m = previous_record[static_cast<std::size_t>(r)];
        ++in_scene;
        if (m < 0 || m >= records) continue;
        ++matched;
        if (seen.size() <= static_cast<std::size_t>(r)) seen.resize(static_cast<std::size_t>(r) + 1u, 0u);
        const std::uint32_t k = seen[static_cast<std::size_t>(r)]++;
        std::uint32_t at = first[m] + fill[m];
        const std::uint32_t end = std::min(first[m + 1], at + 4u);
        const std::int32_t stripe = stripe_of_draw_[d];
        if (stripe >= 0) {
            // A stripe pairs with its record's next stripe draw whatever the
            // particle count, and then particle by particle.
            while (at < end && stripe_of_prev_draw_[order[at]] < 0) {
                ++at;
            }
        } else {
            while (at < end && prev_run_draws_[order[at]].vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
                ++at;
            }
        }
        if (at >= end) {
            if (first[m] + fill[m] < first[m + 1] && trace && size_differs < 6u) {
                const DrawCall& next = prev_run_draws_[order[first[m] + fill[m]]];
                detail << "  size r=" << r << " m=" << m << " k=" << k << " vertices "
                       << next.vertex_view.SizeInBytes / 144u << "->" << call.vertex_view.SizeInBytes / 144u
                       << " indices " << next.index_count << "->" << call.index_count << '\n';
            }
            size_differs += first[m] + fill[m] < first[m + 1] ? 1u : 0u;
            continue;
        }
        fill[m] = at + 1u - first[m];
        ++paired;
        const DrawCall& before = prev_run_draws_[order[at]];
        if (call.ps_record >= 0 && before.ps_record >= 0) {
            std::int32_t& pair = run_ps_previous_[static_cast<std::size_t>(call.ps_record)];
            pair = pair == -1 || pair == before.ps_record ? before.ps_record : -2;
        }
        same_buffer += before.vertex_view.BufferLocation == call.vertex_view.BufferLocation ? 1u : 0u;
        std::uint32_t current_offset = 0u, previous_offset = 0u;
        const int current_buffer =
            locate(call.vertex_view.BufferLocation, call.vertex_view.SizeInBytes, current_offset);
        const int previous_buffer =
            locate(before.vertex_view.BufferLocation, before.vertex_view.SizeInBytes, previous_offset);
        // A strip of the same length slides along itself (index pairing, as
        // any pair); one that grew or shrank pairs particle by particle.
        if (stripe >= 0 && before.vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
            const std::uint32_t vertices = call.vertex_view.StrideInBytes == 144u
                ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
            const std::uint32_t previous_vertices = before.vertex_view.StrideInBytes == 144u
                ? before.vertex_view.SizeInBytes / before.vertex_view.StrideInBytes : 0u;
            if (vertices == 0u || current_buffer < 0 || previous_buffer < 0 ||
                stripe_gather_.size() + vertices > (1u << 20u)) {
                continue;
            }
            // Previous particles by (particle, pass, side, lifetime); a match
            // continues only if its age moved on by at most one frame.
            const StripeDraw& prev_draw =
                prev_run_stripes_.draws[static_cast<std::size_t>(stripe_of_prev_draw_[order[at]])];
            const auto key_of = [](const StripeVertex& v) {
                return (static_cast<std::uint64_t>(v.particle) << 32u) |
                       (static_cast<std::uint64_t>(v.strip) << 24u) | (static_cast<std::uint64_t>(v.side) << 16u) |
                       (v.age_lifetime & 0xFFFFu);
            };
            stripe_keys_.clear();
            for (std::uint32_t i = 0; i < prev_draw.count; ++i) {
                const StripeVertex& v = prev_run_stripes_.vertices[prev_draw.first + i];
                if (v.vertex < previous_vertices) stripe_keys_.emplace_back(key_of(v), prev_draw.first + i);
            }
            std::sort(stripe_keys_.begin(), stripe_keys_.end());
            const auto base = static_cast<std::uint32_t>(stripe_gather_.size());
            for (std::uint32_t i = 0; i < vertices; ++i) {
                stripe_gather_.push_back({current_offset + i * 144u, static_cast<std::uint32_t>(current_buffer)});
            }
            stripe_matched_.assign(vertices, -1);
            const StripeDraw& draw = run_stripes_.draws[static_cast<std::size_t>(stripe)];
            for (std::uint32_t i = 0; i < draw.count; ++i) {
                const StripeVertex& v = run_stripes_.vertices[draw.first + i];
                if (v.vertex >= vertices) continue;
                const std::uint64_t key = key_of(v);
                const auto found = std::lower_bound(stripe_keys_.begin(), stripe_keys_.end(),
                                                    std::pair<std::uint64_t, std::uint32_t>{key, 0u});
                if (found == stripe_keys_.end() || found->first != key) continue;
                const StripeVertex& was = prev_run_stripes_.vertices[found->second];
                const auto steps = static_cast<std::uint16_t>((v.age_lifetime >> 16u) - (was.age_lifetime >> 16u));
                if (steps > 1u) continue;
                stripe_gather_[base + v.vertex] = {previous_offset + was.vertex * 144u,
                                                   static_cast<std::uint32_t>(previous_buffer)};
                stripe_matched_[v.vertex] = static_cast<std::int32_t>(was.vertex);
            }
            // A new particle grows out of where its nearest surviving
            // neighbour (same side of the strip) was.
            for (std::uint32_t i = 0; i < vertices; ++i) {
                if (stripe_matched_[i] >= 0) continue;
                for (std::uint32_t step = 2u; step <= 16u; step += 2u) {
                    const std::int64_t below = static_cast<std::int64_t>(i) - step;
                    const std::uint32_t above = i + step;
                    std::int32_t from = -1;
                    if (above < vertices && stripe_matched_[above] >= 0) {
                        from = stripe_matched_[above];
                    } else if (below >= 0 && stripe_matched_[static_cast<std::size_t>(below)] >= 0) {
                        from = stripe_matched_[static_cast<std::size_t>(below)];
                    }
                    if (from >= 0) {
                        stripe_gather_[base + i] = {previous_offset + static_cast<std::uint32_t>(from) * 144u,
                                                    static_cast<std::uint32_t>(previous_buffer)};
                        break;
                    }
                }
            }
            stripe_blends.push_back({static_cast<std::uint32_t>(d), base});
            ++blended;
            continue;
        }
        if (pairs_checked && before.vertex_view.BufferLocation != call.vertex_view.BufferLocation &&
            before.vertex_view.StrideInBytes == call.vertex_view.StrideInBytes &&
            call.vertex_view.SizeInBytes != 0u && current_buffer >= 0 && previous_buffer >= 0 &&
            blend_pairs_.size() < kStripeVerdict) {
            const auto slot = static_cast<std::uint32_t>(blend_pairs_.size());
            blend_pairs_.push_back({current_offset, previous_offset,
                                    call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes,
                                    static_cast<std::uint32_t>(current_buffer) |
                                        (static_cast<std::uint32_t>(previous_buffer) << 1u)});
            run_blend_vertices_[d] = {before.vertex_view.BufferLocation, slot};
            ++blended;
        }
    }
    // Texture copies of both logs paired by destination and order: those whose
    // source rectangle moved (screen boxes that follow an object).
    moving_copies_ = 0u;
    for (std::size_t c = 0; c < run_copies_.size(); ++c) {
        const ReplayCopy& copy = run_copies_[c];
        if (copy.mode != 0u || copy.dest == nullptr) continue;
        std::size_t ordinal = 0u;
        for (std::size_t k = 0; k < c; ++k) ordinal += run_copies_[k].dest == copy.dest ? 1u : 0u;
        for (const ReplayCopy& was : prev_run_copies_) {
            if (was.mode != 0u || was.dest != copy.dest) continue;
            if (ordinal-- != 0u) continue;
            moving_copies_ += was.width == copy.width && was.height == copy.height &&
                                      (was.geometry[0] != copy.geometry[0] || was.geometry[1] != copy.geometry[1])
                                  ? 1u : 0u;
            break;
        }
    }
    if (!stripe_gather_.empty()) {
        if (ensure_stripe_gather(static_cast<std::uint64_t>(stripe_gather_.size()) * 144u)) {
            const D3D12_GPU_VIRTUAL_ADDRESS earlier = stripe_earlier_->GetGPUVirtualAddress();
            for (const auto& [draw, base] : stripe_blends) {
                run_blend_vertices_[draw] = {earlier + static_cast<std::uint64_t>(base) * 144u, kStripeVerdict};
            }
        } else {
            stripe_gather_.clear();
        }
    }
    blend_validate_pending_ = !blend_pairs_.empty() || !stripe_gather_.empty();
    if (trace) {
        std::cerr << "[interp-blend] log=" << run_serial_ << " draws=" << run_draws_.size()
                  << " in-scene=" << in_scene << " matched-record=" << matched << " paired=" << paired
                  << " same-buffer=" << same_buffer << " size-differs=" << size_differs
                  << " blended=" << blended << " stripe-draws=" << stripe_blends.size()
                  << " stripe-vertices=" << stripe_gather_.size() << " moving-copies=" << moving_copies_ << '\n'
                  << detail.str();
    }
}

void RendererD3D12::begin_replay_record() {
    replay_scene_count_ = 0u;
    replay_stripes_.clear();
    replay_ops_.clear();
    replay_draws_.clear();
    replay_viewports_.clear();
    replay_clears_.clear();
    replay_copies_.clear();
    replay_textures_.clear();
    if (log_start_clear_valid_) {
        log_start_clear_valid_ = false;
        replay_ops_.push_back({ReplayKind::Clear, 0u});
        replay_clears_.push_back(log_start_clear_);
    }
    if (ring_mode_ != RingMode::Interp) {
        replay_valid_ = false;
    }
    replay_recording_ = true;
    replay_start_output_active_ = output_efb_active_;
    std::erase_if(copy_writes_, [this](const CopyWrite& write) {
        return write.serial + kInterpSegments < replay_serial_;
    });
    replay_source_slot_ = frame_slot_;
}

void RendererD3D12::end_replay_record(bool keep) {
    replay_recording_ = false;
    bool has_display_copy = false;
    for (const ReplayCopy& copy : replay_copies_) {
        has_display_copy = has_display_copy || copy.mode == 1u;
    }
    replay_valid_ = keep && has_display_copy;
    if (replay_valid_) {
        publish_replay_log();
    }
}

bool RendererD3D12::ensure_subframe_resources(unsigned xfb_width, unsigned xfb_height) {
    if (!subframe_allocators_[0]) {
        for (auto& allocator : subframe_allocators_) {
            if (FAILED(device_->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) {
                return false;
            }
        }
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kSubframeAllocators * 4u;
        if (timestamp_frequency_ != 0u) {
            (void)device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&subframe_query_heap_));
        }
        if (subframe_query_heap_) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = kSubframeAllocators * 4u * sizeof(std::uint64_t);
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&subframe_readback_)))) {
                subframe_query_heap_.Reset();
            }
        }
    }
    if (subframe_xfb_.texture && subframe_xfb_.width == xfb_width &&
        subframe_xfb_.height == xfb_height) {
        return true;
    }
    if (subframe_xfb_.texture) {
        wait_for_gpu();
    }
    subframe_xfb_ = XfbTexture{};
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = xfb_width;
    rd.Height = xfb_height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&subframe_xfb_.texture)))) {
        return false;
    }
    subframe_xfb_.width = static_cast<std::uint16_t>(xfb_width);
    subframe_xfb_.height = static_cast<std::uint16_t>(xfb_height);
    subframe_xfb_.presentable = true;
    return true;
}

void RendererD3D12::execute_replay_copy(const ReplayCopy& copy, ID3D12Resource* target) {
    if (copy.identity) {
        detail::record_efb_identity_copy(command_list_.Get(), efb_color_.Get(),
                                         target != nullptr ? target : copy.dest, copy.box);
        return;
    }
    if (copy.mode == 2u) {
        record_conversion_blit(
            alt_efb_.color.Get(), copy.width, copy.height, {}, copy.params,
            false, false, true, 15u, copy.geometry[0], copy.geometry[1],
            copy.geometry[2], copy.geometry[3], copy.geometry[4]);
        return;
    }
    ID3D12Resource* const dest = copy.mode == 1u ? subframe_xfb_.texture.Get()
        : target != nullptr ? target : copy.dest;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = dest;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);
    record_conversion_blit(
        dest, copy.width, copy.height, allocate_conversion_rtv(dest), copy.params,
        copy.depth, copy.intensity, copy.keep_alpha, copy.format,
        copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3],
        copy.geometry[4]);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
}

void RendererD3D12::note_copy_write(ID3D12Resource* dest) {
    if (replay_recording_ &&
        std::find(replay_textures_.written.begin(), replay_textures_.written.end(), dest) ==
            replay_textures_.written.end()) {
        replay_textures_.written.push_back(dest);
    }
    for (CopyWrite& write : copy_writes_) {
        if (write.dest == dest) {
            write.serial = replay_serial_;
            return;
        }
    }
    copy_writes_.push_back({dest, replay_serial_});
}

ID3D12Resource* RendererD3D12::acquire_subframe_scratch(ID3D12Resource* dest) {
    const D3D12_RESOURCE_DESC want = dest->GetDesc();
    for (SubframeScratch& scratch : subframe_scratch_) {
        const D3D12_RESOURCE_DESC& have = scratch.desc;
        if (scratch.used != subframe_counter_ && have.Width == want.Width &&
            have.Height == want.Height && have.Format == want.Format &&
            have.MipLevels == want.MipLevels &&
            have.DepthOrArraySize == want.DepthOrArraySize) {
            scratch.used = subframe_counter_;
            return scratch.texture.Get();
        }
    }
    D3D12_RESOURCE_DESC desc = want;
    desc.Alignment = 0u;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    SubframeScratch scratch{};
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&scratch.texture)))) {
        return nullptr;
    }
    scratch.desc = want;
    scratch.used = subframe_counter_;
    subframe_scratch_.push_back(std::move(scratch));
    return subframe_scratch_.back().texture.Get();
}

// save: guarded textures -> scratch; otherwise scratch -> guarded textures.
// Textures rest in PIXEL_SHADER_RESOURCE, scratch in COPY_DEST.
void RendererD3D12::record_subframe_guard(bool save) {
    if (subframe_guarded_.empty()) {
        return;
    }
    const auto transition = [this](ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                    D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        subframe_barriers_.push_back(b);
    };
    const auto flush = [this] {
        command_list_->ResourceBarrier(static_cast<UINT>(subframe_barriers_.size()),
                                       subframe_barriers_.data());
        subframe_barriers_.clear();
    };
    constexpr auto kTexture = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto texture_copy_state = save ? D3D12_RESOURCE_STATE_COPY_SOURCE
                                         : D3D12_RESOURCE_STATE_COPY_DEST;
    subframe_barriers_.clear();
    for (const auto& [dest, scratch] : subframe_guarded_) {
        transition(dest, kTexture, texture_copy_state);
        if (!save) {
            transition(scratch, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
    }
    flush();
    for (const auto& [dest, scratch] : subframe_guarded_) {
        if (save) {
            command_list_->CopyResource(scratch, dest);
        } else {
            command_list_->CopyResource(dest, scratch);
        }
    }
    for (const auto& [dest, scratch] : subframe_guarded_) {
        transition(dest, texture_copy_state, kTexture);
        if (!save) {
            transition(scratch, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        }
    }
    flush();
}

bool RendererD3D12::copy_redirect_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_COPY_REDIRECT") == 0 &&
                 length > 1u && value[0] == '0');
    }();
    return enabled;
}

bool RendererD3D12::ensure_replay_store(ReplayTextures& log) {
    if (srv_increment_ == 0u) {
        srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (log.store) {
        return true;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = kReplayStoreDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&log.store)))) {
        log.store.Reset();
        return false;
    }
    log.store_base = log.store->GetCPUDescriptorHandleForHeapStart();
    return true;
}

void RendererD3D12::note_replay_texture_table(D3D12_GPU_DESCRIPTOR_HANDLE table,
                                              const D3D12_CPU_DESCRIPTOR_HANDLE* sources,
                                              ID3D12Resource* const* copies) {
    if (!replay_recording_ || ring_mode_ != RingMode::Interp || !copy_redirect_enabled() ||
        table.ptr == 0u) {
        return;
    }
    ReplayTextures& log = replay_textures_;
    if (log.incomplete || log.by_gpu.contains(table.ptr)) {
        return;
    }
    if (log.store_used + kReplayTableSlots > kReplayStoreDescriptors || !ensure_replay_store(log)) {
        log.incomplete = true;
        return;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE destination{
        log.store_base.ptr + static_cast<SIZE_T>(log.store_used) * srv_increment_};
    const UINT destination_size = kReplayTableSlots;
    device_->CopyDescriptors(1u, &destination, &destination_size, kReplayTableSlots, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ReplayTextureTable entry{};
    entry.gpu = table.ptr;
    entry.store = log.store_used;
    for (unsigned m = 0; m < kReplayTableSlots; ++m) {
        entry.copies[m] = copies[m];
    }
    log.by_gpu.emplace(table.ptr, static_cast<std::uint32_t>(log.tables.size()));
    log.tables.push_back(entry);
    log.store_used += kReplayTableSlots;
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererD3D12::copy_texture_srv(std::uint32_t index) const {
    return {copy_srv_base_.ptr + static_cast<SIZE_T>(copy_pool_[index].srv) * srv_increment_};
}

std::int32_t RendererD3D12::acquire_copy_texture(ID3D12Resource* like, bool snapshot) {
    const D3D12_RESOURCE_DESC want = like->GetDesc();
    for (std::size_t i = 0; i < copy_pool_.size(); ++i) {
        CopyTexture& entry = copy_pool_[i];
        const D3D12_RESOURCE_DESC& have = entry.desc;
        if (entry.snapshot != snapshot || have.Width != want.Width || have.Height != want.Height ||
            have.Format != want.Format || have.MipLevels != want.MipLevels ||
            have.DepthOrArraySize != want.DepthOrArraySize || have.Flags != want.Flags) {
            continue;
        }
        // Targets serve one destination per sub-frame; snapshots one log
        // until its ring segment comes round again.
        if (snapshot ? entry.segment != ~0u : entry.used == subframe_counter_) {
            continue;
        }
        if (snapshot) {
            entry.segment = ring_segment_;
        } else {
            entry.used = subframe_counter_;
        }
        return static_cast<std::int32_t>(i);
    }
    if (srv_increment_ == 0u) {
        srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (!copy_srv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = kCopySrvCapacity;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&copy_srv_heap_)))) {
            copy_srv_heap_.Reset();
            return -1;
        }
        copy_srv_base_ = copy_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    if (copy_srv_used_ >= kCopySrvCapacity) {
        return -1;
    }
    CopyTexture entry{};
    D3D12_RESOURCE_DESC desc = want;
    desc.Alignment = 0u;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&entry.texture)))) {
        return -1;
    }
    entry.texture->SetName(snapshot ? L"GX sub-frame copy snapshot" : L"GX sub-frame copy target");
    entry.desc = want;
    entry.srv = copy_srv_used_++;
    entry.snapshot = snapshot;
    if (snapshot) {
        entry.segment = ring_segment_;
    } else {
        entry.used = subframe_counter_;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = want.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = want.MipLevels;
    copy_pool_.push_back(std::move(entry));
    const auto index = static_cast<std::uint32_t>(copy_pool_.size() - 1u);
    device_->CreateShaderResourceView(copy_pool_[index].texture.Get(), &view, copy_texture_srv(index));
    return static_cast<std::int32_t>(index);
}

// Recording: before the first draw with this table, every copy texture it
// samples that this log has not written yet (an earlier frame's capture) is
// copied, so the log's sub-frames read it as this frame did.
void RendererD3D12::take_replay_snapshots(std::uint64_t table) {
    ReplayTextures& log = replay_textures_;
    const auto found = log.by_gpu.find(table);
    if (found == log.by_gpu.end()) {
        return;
    }
    ReplayTextureTable& entry = log.tables[found->second];
    if (entry.snapshots_checked || log.incomplete) {
        return;
    }
    entry.snapshots_checked = true;
    for (ID3D12Resource* source : entry.copies) {
        if (source == nullptr ||
            std::find(log.written.begin(), log.written.end(), source) != log.written.end()) {
            continue;
        }
        bool taken = false;
        for (const auto& snapshot : log.snapshots) {
            taken = taken || snapshot.first == source;
        }
        if (taken) {
            continue;
        }
        const std::int32_t index = acquire_copy_texture(source, true);
        if (index < 0) {
            log.incomplete = true;
            return;
        }
        ID3D12Resource* const copy = copy_pool_[static_cast<std::size_t>(index)].texture.Get();
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        barriers[0].Transition.pResource = source;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource = copy;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        command_list_->ResourceBarrier(2u, barriers);
        command_list_->CopyResource(copy, source);
        for (auto& barrier : barriers) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        }
        command_list_->ResourceBarrier(2u, barriers);
        log.snapshots.emplace_back(source, static_cast<std::uint32_t>(index));
        ++copy_redirect_stats_.snapshots;
        // GALAXY_INTERP_TRACE: which copy texture is read before its write
        // (the copy that wrote it in an earlier log, when still known).
        // =2: every one.
        static const int trace = [] {
            char value[8]{};
            std::size_t length = 0u;
            return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_TRACE") == 0 &&
                   length > 1u ? value[0] - '0' : 0;
        }();
        static std::uint64_t traced = 0u;
        if (trace > 0 && (trace >= 2 || traced++ % 120u == 0u)) {
            const ReplayCopy* writer = nullptr;
            for (const auto* copies : {&play_copies_, &run_copies_}) {
                for (const ReplayCopy& logged : *copies) {
                    if (logged.dest == source) writer = &logged;
                }
            }
            const D3D12_RESOURCE_DESC desc = source->GetDesc();
            std::ostringstream message;
            message << "[interp-snapshot] log=" << replay_serial_ << " texture=" << desc.Width << 'x'
                    << desc.Height << ":fmt" << static_cast<unsigned>(desc.Format);
            if (writer != nullptr) {
                message << " copy dest=0x" << std::hex << writer->params.dest_addr << std::dec
                        << " src=" << writer->params.src_x << ',' << writer->params.src_y << ' '
                        << writer->params.src_width << 'x' << writer->params.src_height
                        << " format=" << static_cast<unsigned>(writer->format)
                        << " half=" << writer->params.half_scale << " depth=" << writer->depth;
            }
            message << '\n';
            std::cerr << message.str();
        }
    }
}

// Playback: the table with every slot that names a redirected copy texture
// pointed at its stand-in (a copy in the current descriptor segment).
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::patch_replay_table(D3D12_GPU_DESCRIPTOR_HANDLE table) {
    const auto found = run_textures_.by_gpu.find(table.ptr);
    if (found == run_textures_.by_gpu.end()) {
        return table;
    }
    const ReplayTextureTable& entry = run_textures_.tables[found->second];
    D3D12_CPU_DESCRIPTOR_HANDLE sources[kReplayTableSlots]{};
    bool redirected = false;
    for (unsigned m = 0; m < kReplayTableSlots; ++m) {
        sources[m].ptr = run_textures_.store_base.ptr +
            static_cast<SIZE_T>(entry.store + m) * srv_increment_;
        if (entry.copies[m] != nullptr) {
            if (const auto stand_in = redirect_.find(entry.copies[m]); stand_in != redirect_.end()) {
                sources[m] = copy_texture_srv(stand_in->second);
                redirected = true;
            }
        }
    }
    if (!redirected) {
        return table;
    }
    auto& cached = patched_tables_[table.ptr];
    if (cached.first == redirect_epoch_ + 1u) {
        return {cached.second};
    }
    const DescriptorRing::Table patched = srv_ring_.allocate(kReplayTableSlots);
    const UINT destination_size = kReplayTableSlots;
    device_->CopyDescriptors(1u, &patched.cpu, &destination_size, kReplayTableSlots, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cached = {redirect_epoch_ + 1u, patched.gpu.ptr};
    ++copy_redirect_stats_.patched_tables;
    return patched.gpu;
}

void RendererD3D12::release_copy_snapshots(unsigned segment) {
    for (CopyTexture& entry : copy_pool_) {
        if (entry.snapshot && (segment == ~0u || entry.segment == segment)) {
            entry.segment = ~0u;
        }
    }
}

// Interpolated constants (interp_gpu.h, evaluate()): copy_main fills a
// sequence's blocks from the rings before its first sub-frame, eval_main writes
// each op at t. Root views have no bounds: every access is clamped to its buffer.
static constexpr char kInterpEvalCs[] = R"HLSL(
ByteAddressBuffer program : register(t0);
ByteAddressBuffer constant_ring : register(t1);
ByteAddressBuffer matrix_ring : register(t2);
RWByteAddressBuffer output : register(u0);
RWStructuredBuffer<uint4> verdicts : register(u1);
cbuffer Frame : register(b0) {
    float4 camera0;
    float4 camera1;
    float4 camera2;
    float t;
    uint count;
    uint table;
    uint payload;
    uint program_last;
    uint output_last;
    uint constants_last;
    uint matrices_last;
    uint frame_slot;
    float t_prev;
    float2 jitter;
    float4 camera_prev0;
    float4 camera_prev1;
    float4 camera_prev2;
    uint mode;  // 1: only the stores and the ops at the frame before (the frame at t = 1)
};
static const uint kSplit = 0x100u;
static const uint kStill = 0x200u;
static const uint kRigid = 0x400u;
static const uint kLightSplit = 0x800u;
static const uint kPrev = 0x1000u;
uint word(uint index) {
    return program.Load(min(payload + index * 4u, program_last));
}
float value(uint index) {
    return asfloat(word(index));
}
float4 row(uint index) {
    return float4(value(index), value(index + 1u), value(index + 2u), value(index + 3u));
}
float3 vec(uint index) {
    return float3(value(index), value(index + 1u), value(index + 2u));
}
void put(uint index, uint bits) {
    output.Store(min(index * 4u, output_last), bits);
}
struct Affine {
    float4 r[3];
};
Affine multiply(Affine a, Affine b) {
    Affine o;
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        o.r[i] = a.r[i].x * b.r[0] + a.r[i].y * b.r[1] + a.r[i].z * b.r[2];
        o.r[i].w += a.r[i].w;
    }
    return o;
}
Affine transform_at(uint p, uint flags, float at) {
    Affine m;
    if ((flags & kStill) != 0u) {
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            m.r[i] = row(p + 4u * i);
        }
        return m;
    }
    if ((flags & kRigid) == 0u) {
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            const float4 a = row(p + 4u * i);
            m.r[i] = a + (row(p + 12u + 4u * i) - a) * at;
        }
        return m;
    }
    const float theta = value(p + 32u);
    const float inv_sin = value(p + 33u);
    float wa = 1.0 - at;
    float wb = at;
    if (theta != 0.0) {
        wa = sin((1.0 - at) * theta) * inv_sin;
        wb = sin(at * theta) * inv_sin;
    }
    float4 q = row(p) * wa + row(p + 4u) * wb;
    q *= rsqrt(dot(q, q));
    const float3 r0 = float3(1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y - q.z * q.w),
                             2.0 * (q.x * q.z + q.y * q.w));
    const float3 r1 = float3(2.0 * (q.x * q.y + q.z * q.w), 1.0 - 2.0 * (q.x * q.x + q.z * q.z),
                             2.0 * (q.y * q.z - q.x * q.w));
    const float3 r2 = float3(2.0 * (q.x * q.z - q.y * q.w), 2.0 * (q.y * q.z + q.x * q.w),
                             1.0 - 2.0 * (q.x * q.x + q.y * q.y));
    float3 s[3];
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        const float3 a = vec(p + 8u + 3u * k);
        s[k] = a + (vec(p + 17u + 3u * k) - a) * at;
    }
    const float3 ta = vec(p + 26u);
    const float3 tr = ta + (vec(p + 29u) - ta) * at;
    m.r[0] = float4(r0.x * s[0] + r0.y * s[1] + r0.z * s[2], tr.x);
    m.r[1] = float4(r1.x * s[0] + r1.y * s[1] + r1.z * s[2], tr.y);
    m.r[2] = float4(r2.x * s[0] + r2.y * s[1] + r2.z * s[2], tr.z);
    return m;
}
[numthreads(64, 1, 1)]
void copy_main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
    if (group.x >= count) {
        return;
    }
    const uint4 c = program.Load4(min(table + group.x * 16u, program_last));
    for (uint w = lane; w < c.w; w += 64u) {
        const uint from = c.y + w * 4u;
        const uint a = constant_ring.Load(min(from, constants_last));
        const uint b = matrix_ring.Load(min(from, matrices_last));
        put(c.z / 4u + w, c.x == 0u ? a : b);
    }
}
[numthreads(64, 1, 1)]
void eval_main(uint3 id : SV_DispatchThreadID) {
    if (id.x == 0u) {
        verdicts[min(frame_slot, 8192u)] = uint4(asuint(t), asuint(t_prev), asuint(jitter));
    }
    if (id.x >= count) {
        return;
    }
    const uint4 op = program.Load4(min(table + id.x * 16u, program_last));
    const uint kind = op.x & 0xFFu;
    const uint flags = op.x & 0xFF00u;
    const uint n = min(op.x >> 16u, 16u);
    const uint dst = op.y;
    const uint p = op.z;
    const bool previous = (flags & kPrev) != 0u;
    if (!previous && mode != 0u && kind != 6u) {
        return;
    }
    const float at = previous ? t_prev : t;
    const float4 c0 = previous ? camera_prev0 : camera0;
    const float4 c1 = previous ? camera_prev1 : camera1;
    const float4 c2 = previous ? camera_prev2 : camera2;
    if (kind <= 1u) {
        Affine m = transform_at(p, flags, at);
        Affine c;
        c.r[0] = c0;
        c.r[1] = c1;
        c.r[2] = c2;
        if (kind == 1u) {
            [unroll] for (uint i = 0u; i < 3u; ++i) {
                m.r[i].w = 0.0;
                c.r[i].w = 0.0;
            }
        }
        if ((flags & kSplit) != 0u) {
            m = multiply(c, m);
        }
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            if (kind == 0u) {
                put(dst + 4u * i, asuint(m.r[i].x));
                put(dst + 4u * i + 1u, asuint(m.r[i].y));
                put(dst + 4u * i + 2u, asuint(m.r[i].z));
                put(dst + 4u * i + 3u, asuint(m.r[i].w));
            } else {
                put(dst + 3u * i, asuint(m.r[i].x));
                put(dst + 3u * i + 1u, asuint(m.r[i].y));
                put(dst + 3u * i + 2u, asuint(m.r[i].z));
            }
        }
    } else if (kind <= 3u) {
        for (uint i = 0u; i < n; ++i) {
            const float a = value(p + i);
            const float b = value(p + n + i);
            const float v = a == b ? b : a + (b - a) * at;
            put(dst + i, kind == 2u ? asuint(v) : asuint(int(v >= 0.0 ? floor(v + 0.5) : ceil(v - 0.5))));
        }
    } else if (kind == 4u) {
        const uint a = word(p);
        const uint b = word(p + 1u);
        uint c = 0u;
        [unroll] for (uint s = 0u; s < 32u; s += 8u) {
            const float x = float((a >> s) & 0xFFu);
            const float y = float((b >> s) & 0xFFu);
            c |= uint(floor(clamp(x + (y - x) * at, 0.0, 255.0) + 0.5)) << s;
        }
        put(dst, c);
    } else if (kind == 5u) {
        const float3 wa = vec(p);
        const float3 va = vec(p + 6u);
        float3 pos = wa + (vec(p + 3u) - wa) * at;
        float3 dir = va + (vec(p + 9u) - va) * at;
        if ((flags & kLightSplit) != 0u) {
            pos = float3(dot(c0.xyz, pos) + c0.w, dot(c1.xyz, pos) + c1.w, dot(c2.xyz, pos) + c2.w);
            dir = float3(dot(c0.xyz, dir), dot(c1.xyz, dir), dot(c2.xyz, dir));
        }
        const float la = value(p + 12u);
        const float lb = value(p + 13u);
        const float ld = length(dir);
        if (ld > 1e-20) {
            dir *= (la + (lb - la) * at) / ld;
        }
        put(dst, asuint(pos.x));
        put(dst + 1u, asuint(pos.y));
        put(dst + 2u, asuint(pos.z));
        put(dst + 3u, asuint(dir.x));
        put(dst + 4u, asuint(dir.y));
        put(dst + 5u, asuint(dir.z));
    } else if (kind == 6u) {
        for (uint i = 0u; i < n; ++i) {
            put(dst + i, word(p + i));
        }
    }
}
)HLSL";

namespace {

// Root constants of kInterpEvalCs (cbuffer Frame).
struct InterpEvalFrame {
    float camera[12];
    float t;
    std::uint32_t count;
    std::uint32_t table;
    std::uint32_t payload;
    std::uint32_t program_last;
    std::uint32_t output_last;
    std::uint32_t constants_last;
    std::uint32_t matrices_last;
    std::uint32_t frame_slot;
    float t_prev;
    float jitter[2];
    float camera_prev[12];
    std::uint32_t mode;
};
static_assert(sizeof(InterpEvalFrame) == 37u * 4u);

constexpr D3D12_RESOURCE_STATES kInterpOutputRead =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

}  // namespace

bool RendererD3D12::ensure_interp_eval() {
    if (interp_eval_pso_) {
        return true;
    }
    if (interp_eval_failed_ || !device_) {
        return false;
    }
    interp_eval_failed_ = true;
    D3D12_ROOT_PARAMETER params[6]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = sizeof(InterpEvalFrame) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (unsigned i = 0; i < 3u; ++i) {
        params[1u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1u + i].Descriptor.ShaderRegister = i;
        params[1u + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        params[4u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[4u + i].Descriptor.ShaderRegister = i;
        params[4u + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 6;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&interp_eval_root_)))) {
        return false;
    }
    try {
        for (const auto& [entry, pso] : {std::pair{"copy_main", std::addressof(interp_copy_pso_)},
                                          std::pair{"eval_main", std::addressof(interp_eval_pso_)}}) {
            const ComPtr<ID3DBlob> cs = compile_hlsl(kInterpEvalCs, sizeof(kInterpEvalCs) - 1u, "cs_5_1", entry);
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = interp_eval_root_.Get();
            desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso->ReleaseAndGetAddressOf())))) {
                interp_eval_pso_.Reset();
                return false;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "[interp-eval] shader: " << error.what() << '\n';
        interp_eval_pso_.Reset();
        return false;
    }
    interp_eval_failed_ = false;
    return true;
}

bool RendererD3D12::ensure_interp_output(std::uint64_t bytes) {
    bytes = std::max<std::uint64_t>(bytes, 256u);
    if (interp_output_ && interp_output_bytes_ >= bytes) {
        return true;
    }
    std::uint64_t size = std::max<std::uint64_t>(interp_output_bytes_ * 2u, 4ull << 20u);
    while (size < bytes) {
        size *= 2u;
    }
    if (interp_output_) {
        // Replays still in flight read the old one.
        interp_retired_.push_back({std::move(interp_output_), next_fence_value_});
        interp_output_bytes_ = 0u;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&interp_output_)))) {
        return false;
    }
    interp_output_->SetName(L"GX interpolated constants");
    interp_output_bytes_ = size;
    interp_output_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
}

void RendererD3D12::transition_interp_output(D3D12_RESOURCE_STATES state) {
    if (interp_output_state_ == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = interp_output_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = interp_output_state_;
    barrier.Transition.StateAfter = state;
    command_list_->ResourceBarrier(1, &barrier);
    interp_output_state_ = state;
}

void RendererD3D12::transition_blend_verdicts(D3D12_RESOURCE_STATES state) {
    if (!blend_verdicts_ || blend_verdicts_state_ == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = blend_verdicts_state_;
    barrier.Transition.StateAfter = state;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = state;
}

// The sequence's base copies (first sub-frame) and its ops at t, into the
// output read by the replay. The frame at t = 1 takes the copies again (its
// constants as the game frame recorded them), the stores (output addressing)
// and the ops at the frame before (its motion vectors).
void RendererD3D12::record_interp_eval(const SubframeTime& time, const Affine34& camera_t,
                                       const Affine34& camera_prev) {
    transition_interp_output(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition_blend_verdicts(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // The ray-traced occlusion of the replayed frame follows this sub-frame's camera.
    ray_tracer_.write_reprojection(command_list_.Get(), camera_t);
    const bool last = time.t >= 1.0f;
    InterpEvalFrame frame{};
    std::memcpy(frame.camera, camera_t.data(), sizeof(frame.camera));
    std::memcpy(frame.camera_prev, camera_prev.data(), sizeof(frame.camera_prev));
    frame.mode = last ? 1u : 0u;
    frame.t = time.t;
    frame.t_prev = time.t_prev;
    frame.jitter[0] = time.jitter_x;
    frame.jitter[1] = time.jitter_y;
    frame.payload = sequence_.payload_offset;
    frame.program_last = sequence_.program_bytes - 16u;
    frame.output_last = static_cast<std::uint32_t>(interp_output_bytes_ - 4u);
    frame.constants_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(constant_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    frame.matrices_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(matrix_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    frame.frame_slot = kFrameValuesElement;
    command_list_->SetComputeRootSignature(interp_eval_root_.Get());
    command_list_->SetComputeRootShaderResourceView(1, sequence_.program);
    command_list_->SetComputeRootShaderResourceView(2, constant_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(3, matrix_ring_.gpu_base());
    command_list_->SetComputeRootUnorderedAccessView(4, interp_output_->GetGPUVirtualAddress());
    command_list_->SetComputeRootUnorderedAccessView(5, blend_verdicts_->GetGPUVirtualAddress());
    if ((sequence_.first || last) && sequence_.copies != 0u) {
        frame.count = sequence_.copies;
        frame.table = sequence_.copies_offset;
        command_list_->SetPipelineState(interp_copy_pso_.Get());
        command_list_->SetComputeRoot32BitConstants(0, sizeof(frame) / 4u, &frame, 0u);
        command_list_->Dispatch(sequence_.copies, 1u, 1u);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = interp_output_.Get();
        command_list_->ResourceBarrier(1, &barrier);
    }
    frame.count = sequence_.ops;
    frame.table = sequence_.ops_offset;
    command_list_->SetPipelineState(interp_eval_pso_.Get());
    command_list_->SetComputeRoot32BitConstants(0, sizeof(frame) / 4u, &frame, 0u);
    command_list_->Dispatch(std::max(1u, (sequence_.ops + 63u) / 64u), 1u, 1u);
    transition_interp_output(kInterpOutputRead);
    transition_blend_verdicts(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bound_pipeline_ = nullptr;
}

bool RendererD3D12::begin_replay_sequence(const ReplaySequence& sequence) {
    sequence_.ready = false;
    if (ring_mode_ != RingMode::Interp && replay_valid_ && !run_valid_) {
        (void)start_replay_playback();
    }
    if (!run_valid_ || !device_ || !swap_chain_ || sequence.program == nullptr) {
        replay_failure_ = run_valid_ ? 2u : 1u;
        return false;
    }
    const ReplayCopy* display = nullptr;
    unsigned output_scale = 0u;  // the log switches to the output EFB at this scale
    for (const ReplayCopy& copy : run_copies_) {
        if (copy.mode == 1u) display = &copy;
        if (copy.mode == 2u) output_scale = copy.width / kEfbWidth;
    }
    if (display == nullptr || run_start_output_active_) {
        // Logs begin at the ring advance after a display copy, on the render
        // EFB; the private sets below assume it.
        replay_failure_ = display == nullptr ? 3u : 6u;
        return false;
    }
    if (!ensure_subframe_resources(display->width, display->height)) {
        replay_failure_ = 4u;
        return false;
    }
    if (!ensure_interp_eval()) {
        replay_failure_ = 7u;
        return false;
    }
    if (!ensure_interp_output(sequence.layout.bytes())) {
        replay_failure_ = 8u;
        return false;
    }
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(interp_retired_, [completed](const RetiredResource& retired) {
        return retired.fence <= completed;
    });
    const auto record_start = std::chrono::steady_clock::now();

    // Base copies: each moving block as its draws read it this game frame.
    const interp_gpu::Layout& layout = sequence.layout;
    const D3D12_GPU_VIRTUAL_ADDRESS constants_base = constant_ring_.gpu_base();
    const std::uint64_t constants_bytes = constant_ring_.total_bytes();
    const std::uint64_t matrices_bytes = matrix_ring_.total_bytes();
    interp_copies_.clear();
    interp_copied_.assign(static_cast<std::size_t>(layout.vs_count) + layout.ps_count, 0u);
    const auto copy_block = [&](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint32_t bytes, std::uint32_t dst) {
        if (va < constants_base || va + bytes > constants_base + constants_bytes) {
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Constants),
                                  static_cast<std::uint32_t>(va - constants_base), dst, bytes / 4u});
        return true;
    };
    for (const DrawCall& call : run_draws_) {
        if (call.replay_record >= 0 && static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
            const std::int32_t slot = sequence.record_vs_slot[static_cast<std::size_t>(call.replay_record)];
            if (slot >= 0 && interp_copied_[static_cast<std::size_t>(slot)] == 0u) {
                interp_copied_[static_cast<std::size_t>(slot)] = 1u;
                if (!copy_block(call.vs_constants, interp_gpu::kVsBytes,
                                layout.vs_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
        if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
            const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)];
            const std::size_t seen = layout.vs_count + static_cast<std::size_t>(slot);
            if (slot >= 0 && interp_copied_[seen] == 0u) {
                interp_copied_[seen] = 1u;
                if (!copy_block(call.ps_constants, interp_gpu::kPsBytes,
                                layout.ps_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
    }
    for (std::uint32_t q = 0; q < layout.palette_count && q < sequence.palette_source.size(); ++q) {
        const std::uint64_t source = static_cast<std::uint64_t>(sequence.palette_source[q]) * 16u;
        if (source + interp_gpu::kPaletteBytes > matrices_bytes) {
            replay_failure_ = 9u;
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                  static_cast<std::uint32_t>(source), layout.palette_offset(q),
                                  interp_gpu::kPaletteWords});
        if (q < layout.prev_palette_count) {
            interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                      static_cast<std::uint32_t>(source), layout.prev_palette_offset(q),
                                      interp_gpu::kPrevPaletteWords});
        }
    }

    // The program: copies, ops, payload (16 bytes of tail room for Load4).
    const interp_gpu::Program& program = *sequence.program;
    const std::size_t copies_bytes = interp_copies_.size() * sizeof(interp_gpu::Copy);
    const std::size_t ops_bytes = program.ops.size() * sizeof(interp_gpu::Op);
    const std::size_t payload_bytes = program.payload.size() * sizeof(std::uint32_t);
    const std::size_t table_bytes = copies_bytes + ops_bytes + payload_bytes + 16u;
    const UploadRing::Allocation table = constant_ring_.allocate(table_bytes, 256u);
    std::memcpy(table.cpu, interp_copies_.data(), copies_bytes);
    std::memcpy(table.cpu + copies_bytes, program.ops.data(), ops_bytes);
    std::memcpy(table.cpu + copies_bytes + ops_bytes, program.payload.data(), payload_bytes);
    std::memset(table.cpu + copies_bytes + ops_bytes + payload_bytes, 0, 16u);

    sequence_ = SequenceState{};
    sequence_.ready = true;
    sequence_.first = true;
    sequence_.serial = ++sequence_serial_;
    sequence_.output_scale = output_scale;
    sequence_.overrides = sequence;
    sequence_.alloc_segment = ring_segment_;
    sequence_.program = table.gpu;
    sequence_.program_bytes = static_cast<std::uint32_t>(table_bytes);
    sequence_.copies = static_cast<std::uint32_t>(interp_copies_.size());
    sequence_.ops = static_cast<std::uint32_t>(program.ops.size());
    sequence_.copies_offset = 0u;
    sequence_.ops_offset = static_cast<std::uint32_t>(copies_bytes);
    sequence_.payload_offset = static_cast<std::uint32_t>(copies_bytes + ops_bytes);
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++sequence_record_samples_;
    return true;
}

// The playing log's replay into `entry`'s list, with the sequence's
// overrides. A list may run again only once its last execution completed
// (D3D12 rejects a resubmission in flight), so a sequence records another
// list when its sub-frames come faster than the GPU finishes them.
bool RendererD3D12::record_replay_list(ReplayList& entry) {
    const ReplaySequence& sequence = sequence_.overrides;
    const interp_gpu::Layout& layout = sequence.layout;
    const unsigned output_scale = sequence_.output_scale;
    const bool temporal = temporal_interp_ && temporal_ != nullptr;
    if (!entry.list) {
        d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&entry.allocator)),
                  "replay CreateCommandAllocator");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&entry.list)),
                  "replay CreateCommandList");
        d3d_check(entry.list->Close(), "replay Close");
    }
    if (temporal && !entry.list_after) {
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&entry.list_after)),
                  "replay CreateCommandList");
        d3d_check(entry.list_after->Close(), "replay Close");
    }
    const auto record_start = std::chrono::steady_clock::now();
    d3d_check(entry.allocator->Reset(), "replay allocator Reset");
    d3d_check(entry.list->Reset(entry.allocator.Get(), nullptr), "replay Reset");
    entry.split = false;
    // The next game frame may be recording: the replay must not log into it.
    const bool recording = replay_recording_;
    replay_recording_ = false;
    // The list being recorded: the replay, then (temporal upscaling) the part
    // after the output switch.
    // std::addressof: a ComPtr's unary & yields a ComPtrRef that releases it on conversion.
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>* active = std::addressof(entry.list);
    struct Restore {
        RendererD3D12& renderer;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>*& active;
        bool recording;
        ~Restore() {
            renderer.replay_recording_ = recording;
            std::swap(renderer.command_list_, *active);
        }
    };
    std::swap(command_list_, entry.list);
    {
        const Restore restore{*this, active, recording};
        dred_marker(command_list_.Get(), L"replay log=%llu", static_cast<unsigned long long>(run_serial_));
        const bool interp_rings = ring_mode_ == RingMode::Interp;
        ID3D12DescriptorHeap* heaps[] = {
            srv_ring_.heap(),
            interp_rings ? sampler_heaps_[run_segment_].Get() : sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_gx_bindings();
        bound_rt_aux_ = 0u;

        ++subframe_counter_;
        subframe_guarded_.clear();
        // Copy redirection: the log's texture copies go to stand-ins, its
        // read-before-write copy textures read the recording frame's snapshots.
        const bool want_redirect = subframe_copy_redirect_ >= 0 ? subframe_copy_redirect_ == 1
                                                                : copy_redirect_enabled();
        const bool redirect = interp_rings && want_redirect && copy_redirect_enabled() &&
                              !run_textures_.incomplete;
        redirect_.clear();
        patched_tables_.clear();
        if (redirect) {
            for (const auto& [source, snapshot] : run_textures_.snapshots) {
                redirect_[source] = snapshot;
            }
        } else if (interp_rings && want_redirect && copy_redirect_enabled()) {
            ++copy_redirect_stats_.fallback_logs;
        }
        // Without redirection: copy textures a later frame already rewrote (or,
        // by default, every one) keep their content.
        static const bool copy_guard = [] {
            char value[8]{};
            std::size_t length = 0u;
            return !(getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_COPY_GUARD") == 0 &&
                     length > 1u && value[0] == '0');
        }();
        for (const ReplayCopy& copy : run_copies_) {
            if (!copy_guard || redirect || subframe_copy_redirect_ == 2) break;
            if (copy.mode != 0u || copy.dest == nullptr) continue;
            bool listed = false;
            for (const auto& guarded : subframe_guarded_) listed = listed || guarded.first == copy.dest;
            if (listed) continue;
            bool rewritten = false;
            for (const CopyWrite& write : copy_writes_) {
                rewritten = rewritten || (write.dest == copy.dest && write.serial >= run_serial_);
            }
            // Every destination is restored after the sub-frame: real frames read
            // copies of earlier frames (feedback blur, camera-cut cover), never a
            // sub-frame's. GALAXY_INTERP_COPY_GUARD=2 keeps only rewritten ones.
            static const bool rewritten_only = [] {
                char value[8]{};
                std::size_t length = 0u;
                return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_COPY_GUARD") == 0 &&
                       length > 1u && value[0] == '2';
            }();
            if (rewritten_only && !rewritten) continue;
            if (ID3D12Resource* scratch = acquire_subframe_scratch(copy.dest)) {
                subframe_guarded_.emplace_back(copy.dest, scratch);
            }
        }
        // Private EFB sets stand in for the live ones, so a later frame's partial
        // EFBs (its chunks interleave with the sub-frames) stay intact: one at the
        // render EFB's scale and, for a log that switches to the output EFB (HUD
        // at output resolution), one at that scale.
        const unsigned render_scale = output_efb_active_ ? alt_efb_.scale : efb_scale_;
        if (((!replay_efb_.color || replay_efb_.scale != render_scale) &&
             !create_efb_set(replay_efb_, replay_efb_rtv_heap_, replay_efb_dsv_heap_, render_scale)) ||
            (output_scale != 0u &&
             (!replay_output_efb_.color || replay_output_efb_.scale != output_scale) &&
             !create_efb_set(replay_output_efb_, replay_output_efb_rtv_heap_,
                             replay_output_efb_dsv_heap_, output_scale, false, true))) {
            command_list_->Close();
            entry.sequence = 0u;
            replay_failure_ = 5u;
            return false;
        }
        if (output_scale != 0u) {
            // Its conversion-source views live in this game frame's descriptor
            // segment (the persistent prefix is sized for the live sets).
            write_efb_set_views(replay_output_efb_, srv_ring_.allocate(2u));
        }
        record_subframe_guard(true);

        // The live sets at rest (render EFB current), then the private ones in.
        const bool output_before = output_efb_active_;
        if (output_before) {
            swap_efb_sets();
        }
        swap_efb_with(replay_efb_);
        if (output_scale != 0u) {
            std::swap(alt_efb_, replay_output_efb_);
        }
        // Sub-frames write their motion vectors and reactive mask to their own
        // targets (the game frame's are live meanwhile).
        const bool motion_swapped = temporal && ensure_replay_motion_targets();
        if (motion_swapped) {
            swap_motion_targets();
            const float zero[4]{};
            command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
            command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
        }
        replay_playing_ = true;
        bind_efb(nullptr);
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        // The log before's vertices are gone once its segment came round (a
        // later game frame records there while this sequence still plays).
        const bool blend = sequence.vertex_blend && prev_run_valid_ &&
                           (!blend_pairs_.empty() || !stripe_gather_.empty()) &&
                           run_blend_vertices_.size() == run_draws_.size();
        entry.blend_segment = prev_run_segment_;
        const D3D12_GPU_VIRTUAL_ADDRESS output = interp_output_->GetGPUVirtualAddress();
        std::uint64_t blended = 0u;
        // GALAXY_INTERP_GPU_PROFILE=<n>: every n-th recording takes a timestamp
        // around each copy and clear (diagnostics).
        static const std::uint64_t profile_every = [] {
            char value[16]{};
            std::size_t length = 0u;
            return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_GPU_PROFILE") == 0 && length > 1u
                ? std::strtoull(value, nullptr, 10) : 0ull;
        }();
        const bool profile = profile_every != 0u && timestamp_frequency_ != 0u && !replay_profile_.armed &&
                             ++replay_profile_recordings_ % profile_every == 0u && ensure_replay_profile();
        if (profile) {
            replay_profile_.segments.clear();
            replay_profile_.list = &entry;
            command_list_->EndQuery(replay_profile_.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u);
        }
        const auto mark = [&](std::uint32_t kind, std::uint32_t detail) {
            if (profile && replay_profile_.segments.size() + 1u < kReplayProfileQueries) {
                replay_profile_.segments.push_back({kind, detail});
                command_list_->EndQuery(replay_profile_.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                        static_cast<UINT>(replay_profile_.segments.size()));
            }
        };
        for (const ReplayOp& op : run_ops_) {
            if (op.kind == ReplayKind::Copy || op.kind == ReplayKind::Clear) {
                mark(0u, op.index);
            }
            switch (op.kind) {
            case ReplayKind::Draw: {
                DrawCall call = run_draws_[op.index];
                if (call.replay_record >= 0 &&
                    static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
                    const auto record = static_cast<std::size_t>(call.replay_record);
                    if (const std::int32_t slot = sequence.record_vs_slot[record]; slot >= 0) {
                        call.vs_constants = output + layout.vs_offset(static_cast<std::uint32_t>(slot));
                    }
                    if (record < sequence.record_palette_slot.size() && sequence.record_palette_slot[record] >= 0) {
                        call.matrix_palette = output + layout.palette_base();
                    }
                }
                if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
                    if (const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)];
                        slot >= 0) {
                        call.ps_constants = output + layout.ps_offset(static_cast<std::uint32_t>(slot));
                    }
                }
                if (blend && run_blend_vertices_[op.index].vertices != 0u) {
                    call.blend_vertices = run_blend_vertices_[op.index].vertices;
                    call.blend_slot = run_blend_vertices_[op.index].slot;
                    ++blended;
                }
                if (!redirect_.empty() && call.texture_table.ptr != 0u) {
                    call.texture_table = patch_replay_table(call.texture_table);
                }
                dred_marker(command_list_.Get(), L"replay draw %u blend=%u slot=%u rec=%d n=%u",
                            op.index, call.blend_vertices != 0u ? 1u : 0u, call.blend_slot, call.replay_record,
                            call.index_count);
                draw(call);
                break;
            }
            case ReplayKind::BindEfb: {
                const ReplayViewport& viewport = run_viewports_[op.index];
                bind_efb(viewport.valid ? viewport.values : nullptr);
                break;
            }
            case ReplayKind::Clear: {
                const ReplayClear& c = run_clears_[op.index];
                clear_efb(c.ar, c.gb, c.z24, c.color, c.alpha, c.depth, c.x, c.y, c.w, c.h, c.format);
                break;
            }
            case ReplayKind::Copy: {
                const ReplayCopy& copy = run_copies_[op.index];
                if (motion_swapped && copy.mode == 2u && !entry.split) {
                    // The output switch is the temporal upscale, recorded per
                    // presented frame (record_subframe_upscale); the rest of
                    // the log goes to the second list.
                    entry.split = true;
                    entry.upscale = {efb_color_.Get(), efb_depth_.Get(), motion_target_.Get(), reactive_target_.Get(),
                                     alt_efb_.color.Get(), kEfbWidth * efb_scale_, kEfbHeight * efb_scale_,
                                     copy.width, copy.height, op.index};
                    d3d_check(command_list_->Close(), "replay Close");
                    std::swap(command_list_, *active);
                    active = std::addressof(entry.list_after);
                    d3d_check(entry.list_after->Reset(entry.allocator.Get(), nullptr), "replay Reset");
                    std::swap(command_list_, *active);
                    const bool interp_segment = ring_mode_ == RingMode::Interp;
                    ID3D12DescriptorHeap* after_heaps[] = {
                        srv_ring_.heap(),
                        interp_segment ? sampler_heaps_[run_segment_].Get() : sampler_heap_.Get()};
                    command_list_->SetDescriptorHeaps(2, after_heaps);
                    command_list_->SetGraphicsRootSignature(root_signature_.Get());
                    invalidate_gx_bindings();
                    bound_rt_aux_ = 0u;
                    break;
                }
                if (redirect && copy.mode == 0u && copy.dest != nullptr) {
                    // One stand-in per destination per sequence (never a snapshot).
                    std::int32_t stand_in = -1;
                    if (const auto current = redirect_.find(copy.dest);
                        current != redirect_.end() && !copy_pool_[current->second].snapshot) {
                        stand_in = static_cast<std::int32_t>(current->second);
                    } else {
                        stand_in = acquire_copy_texture(copy.dest, false);
                    }
                    if (stand_in >= 0) {
                        execute_replay_copy(copy, copy_pool_[static_cast<std::size_t>(stand_in)].texture.Get());
                        redirect_[copy.dest] = static_cast<std::uint32_t>(stand_in);
                        ++redirect_epoch_;
                        ++copy_redirect_stats_.redirected;
                        break;
                    }
                    ++copy_redirect_stats_.fallback_logs;
                }
                execute_replay_copy(copy);
                break;
            }
            case ReplayKind::SwapEfbSets:
                swap_efb_sets();
                break;
            case ReplayKind::ClearDepth:
                command_list_->ClearDepthStencilView(
                    efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                    far_values.reversed_depth, 0u, 0u, nullptr);
                break;
            case ReplayKind::RtComposite:
                record_rt_composite(static_cast<int>(op.index), true);
                break;
            }
            if (op.kind == ReplayKind::Copy) {
                const ReplayCopy& copy = run_copies_[op.index];
                mark(1u + copy.mode, op.index);
            } else if (op.kind == ReplayKind::Clear) {
                mark(4u, op.index);
            }
        }
        if (profile) {
            mark(0u, ~0u);
            command_list_->ResolveQueryData(replay_profile_.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u,
                                            static_cast<UINT>(replay_profile_.segments.size() + 1u),
                                            replay_profile_.readback.Get(), 0u);
            replay_profile_.armed = true;
            replay_profile_.fence = 0u;
        }
        // The log ends after its display copy, on the output EFB when it switched.
        if (output_efb_active_) {
            swap_efb_sets();
        }
        if (output_scale != 0u) {
            std::swap(alt_efb_, replay_output_efb_);
        }
        swap_efb_with(replay_efb_);
        if (output_before) {
            swap_efb_sets();
        }
        if (motion_swapped) {
            swap_motion_targets();
        }
        replay_playing_ = false;
        record_subframe_guard(false);
        // Scratch textures unused for many sequences (a copy size changed) go;
        // the allocator fence waits retired their last use long ago.
        std::erase_if(subframe_scratch_, [this](const SubframeScratch& scratch) {
            return scratch.used + 1024u < subframe_counter_;
        });
        d3d_check(command_list_->Close(), "replay Close");
        invalidate_gx_bindings();

        entry.sequence = sequence_.serial;
        entry.blend = blend && blended != 0u;
        entry.redirect = redirect;
        entry.blended_draws = blended;
        entry.guarded = subframe_guarded_.size();
        entry.alloc_segment = ring_segment_;
    }
    invalidate_gx_bindings();
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++replay_records_;
    return true;
}

bool RendererD3D12::present_replay_subframe(const SubframeTime& given, const Affine34& camera_t,
                                            const Affine34& camera_prev) {
    if (!sequence_.ready || !run_valid_ || !device_ || !swap_chain_) {
        replay_failure_ = sequence_.ready ? 2u : 10u;
        return false;
    }
    SubframeTime time = given;
    if (temporal_interp_ && temporal_ && temporal_render_w_ != 0u) {
        // Each presented frame takes the next jitter phase.
        const unsigned phases = std::max(temporal_->jitter_phase_count(temporal_render_w_, temporal_output_w_), 1u);
        temporal_->jitter_offset(static_cast<unsigned>(temporal_frame_ % phases), phases, time.jitter_x,
                                 time.jitter_y);
        ++temporal_frame_;
    }
    // A list of this sequence whose last execution completed; else a free one
    // (completed longest ago) recorded now. A list that blends with the log
    // before is not reused once that log's segment came round.
    const std::uint64_t completed = completed_fence_value();
    ReplayList* replay = nullptr;
    for (ReplayList& entry : replay_lists_) {
        if (entry.sequence == sequence_.serial && entry.fence <= completed &&
            (!entry.blend || (prev_run_valid_ && entry.blend_segment == prev_run_segment_))) {
            replay = &entry;
            break;
        }
    }
    if (replay == nullptr) {
        ReplayList* oldest = &replay_lists_[0];
        for (ReplayList& entry : replay_lists_) {
            if (entry.fence < oldest->fence) oldest = &entry;
        }
        if (oldest->fence > completed) {
            wait_for_fence_value(oldest->fence);
        }
        if (!record_replay_list(*oldest)) {
            return false;
        }
        replay = oldest;
    }
    if (has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(trace_present_stats_enabled());
    }
    const unsigned index = subframe_next_;
    subframe_next_ = (subframe_next_ + 1u) % kSubframeAllocators;
    if (subframe_fences_[index] != 0u) {
        wait_for_fence_value(subframe_fences_[index]);
    }
    if (subframe_timestamp_pending_[index] && subframe_readback_ && timestamp_frequency_ != 0u) {
        const SIZE_T offset = static_cast<SIZE_T>(index) * 4u * sizeof(std::uint64_t);
        const D3D12_RANGE range{offset, offset + 4u * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(subframe_readback_->Map(0, &range, &mapped))) {
            const auto* ticks = reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::byte*>(mapped) + offset);
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[3] >= ticks[2] && ticks[2] >= ticks[1] && ticks[1] >= ticks[0]) {
                subframe_phase_ms_[0] += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                subframe_phase_ms_[1] += static_cast<double>(ticks[2] - ticks[1]) * to_ms;
                subframe_phase_ms_[2] += static_cast<double>(ticks[3] - ticks[2]) * to_ms;
            }
            if (ticks[3] >= ticks[0]) {
                const double ms = static_cast<double>(ticks[3] - ticks[0]) * to_ms;
                subframe_gpu_ms_ += ms;
                ++subframe_gpu_samples_;
                subframe_gpu_ema_ms_ = subframe_gpu_ema_ms_ == 0.0
                    ? ms : subframe_gpu_ema_ms_ + (ms - subframe_gpu_ema_ms_) * 0.0625;
                subframe_path_gpu_ms_[subframe_timestamp_redirect_[index]] += ms;
                ++subframe_path_gpu_samples_[subframe_timestamp_redirect_[index]];
            }
            const D3D12_RANGE none{0, 0};
            subframe_readback_->Unmap(0, &none);
        }
        subframe_timestamp_pending_[index] = false;
    }
    const auto record_start = std::chrono::steady_clock::now();
    d3d_check(subframe_allocators_[index]->Reset(), "sub-frame allocator Reset");
    if (!subframe_eval_list_) {
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, subframe_allocators_[index].Get(),
                                             nullptr, IID_PPV_ARGS(&subframe_eval_list_)),
                  "sub-frame CreateCommandList");
    } else {
        d3d_check(subframe_eval_list_->Reset(subframe_allocators_[index].Get(), nullptr),
                  "sub-frame evaluation Reset");
    }
    {
        // The evaluation list: the blend check and base copies of a sequence's
        // first sub-frame, then the ops at t.
        struct Swap {
            RendererD3D12& renderer;
            ~Swap() { std::swap(renderer.command_list_, renderer.subframe_eval_list_); }
        };
        std::swap(command_list_, subframe_eval_list_);
        const Swap swap{*this};
        dred_marker(command_list_.Get(), L"subframe log=%llu t=%.3f", static_cast<unsigned long long>(run_serial_),
                    static_cast<double>(time.t));
        if (subframe_query_heap_) {
            command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u);
        }
        if (replay->blend && !sequence_.validated) {
            record_blend_validation();
            sequence_.validated = true;
        }
        record_interp_eval(time, camera_t, camera_prev);
        if (subframe_query_heap_) {
            command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 1u);
        }
        d3d_check(command_list_->Close(), "sub-frame evaluation Close");
    }
    if (replay->split) {
        // The temporal upscale of this presented frame, between the replay's two lists.
        if (!subframe_upscale_list_) {
            d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, subframe_allocators_[index].Get(),
                                                 nullptr, IID_PPV_ARGS(&subframe_upscale_list_)),
                      "sub-frame CreateCommandList");
        } else {
            d3d_check(subframe_upscale_list_->Reset(subframe_allocators_[index].Get(), nullptr),
                      "sub-frame upscale Reset");
        }
        struct Swap {
            RendererD3D12& renderer;
            ~Swap() { std::swap(renderer.command_list_, renderer.subframe_upscale_list_); }
        };
        std::swap(command_list_, subframe_upscale_list_);
        const Swap swap{*this};
        ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_gx_bindings();
        record_subframe_upscale(*replay, time);
        d3d_check(command_list_->Close(), "sub-frame upscale Close");
    }
    d3d_check(command_list_->Reset(subframe_allocators_[index].Get(), nullptr), "sub-frame CommandList Reset");
    if (subframe_query_heap_) {
        command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 2u);
    }
    {
        ID3D12DescriptorHeap* live[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, live);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_gx_bindings();
    }
    (void)present(&subframe_xfb_, subframe_capture_);
    const bool readback = eval_readback_requested_ && eval_readback_fence_ == 0u;
    if (readback) {
        eval_readback_requested_ = false;
        if (!eval_readback_ || eval_readback_bytes_ < interp_output_bytes_) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = interp_output_bytes_;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            eval_readback_.Reset();
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&eval_readback_)))) {
                eval_readback_.Reset();
            }
            eval_readback_bytes_ = eval_readback_ ? interp_output_bytes_ : 0u;
        }
        if (eval_readback_) {
            transition_interp_output(D3D12_RESOURCE_STATE_COPY_SOURCE);
            command_list_->CopyBufferRegion(eval_readback_.Get(), 0u, interp_output_.Get(), 0u,
                                            interp_output_bytes_);
            transition_interp_output(kInterpOutputRead);
            eval_readback_t_ = time.t;
        }
    }
    if (subframe_query_heap_) {
        command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 3u);
        command_list_->ResolveQueryData(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                        index * 4u, 4u, subframe_readback_.Get(),
                                        static_cast<UINT64>(index) * 4u * sizeof(std::uint64_t));
        subframe_timestamp_pending_[index] = true;
        subframe_timestamp_redirect_[index] =
            replay->redirect ? 1u : subframe_copy_redirect_ == 2 ? 2u : 0u;
    }
    d3d_check(command_list_->Close(), "sub-frame CommandList Close");
    if (replay->split) {
        ID3D12CommandList* lists[] = {subframe_eval_list_.Get(), replay->list.Get(), subframe_upscale_list_.Get(),
                                      replay->list_after.Get(), command_list_.Get()};
        queue_->ExecuteCommandLists(5, lists);
    } else {
        ID3D12CommandList* lists[] = {subframe_eval_list_.Get(), replay->list.Get(), command_list_.Get()};
        queue_->ExecuteCommandLists(3, lists);
    }
    const std::uint64_t fence = ++next_fence_value_;
    d3d_check(queue_->Signal(fence_.Get(), fence), "sub-frame Signal");
    subframe_fences_[index] = fence;
    replay->fence = fence;
    if (readback && eval_readback_) {
        eval_readback_fence_ = fence;
    }
    if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ == 0u) {
        backbuffer_readback_fence_value_ = fence;
    }
    if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ == 0u) {
        selected_xfb_readback_fence_ = fence;
    }
    if (ring_mode_ == RingMode::Interp) {
        // The replay reads the log's segment (and the log before's for blended
        // vertices) and the sequence's allocations; the present list allocates
        // from the current segment. Frame slots are not involved: a game frame's
        // chunks never wait for its sub-frames.
        for (const unsigned segment : {run_segment_, sequence_.alloc_segment, replay->alloc_segment, ring_segment_}) {
            ring_segment_fence_[segment] = std::max(ring_segment_fence_[segment], fence);
        }
        if (replay->blend) {
            ring_segment_fence_[replay->blend_segment] = std::max(ring_segment_fence_[replay->blend_segment], fence);
        }
    } else {
        // The replay read the recorded frame's slot rings and appended to the
        // current ones: neither may be rewound before it completes.
        fence_values_[replay_source_slot_] = std::max(fence_values_[replay_source_slot_], fence);
        fence_values_[(replay_source_slot_ + 1u) % kFramesInFlight] =
            std::max(fence_values_[(replay_source_slot_ + 1u) % kFramesInFlight], fence);
        fence_values_[frame_slot_] = std::max(fence_values_[frame_slot_], fence);
        if (matrix_segments_ != kFramesInFlight) {
            matrix_segment_fence_[matrix_segment_] = std::max(matrix_segment_fence_[matrix_segment_], fence);
        }
    }
    if (replay_profile_.armed && replay_profile_.list == replay && replay_profile_.fence == 0u) {
        replay_profile_.fence = fence;
    }
    report_replay_profile();
    sequence_.first = false;
    vertex_blend_draws_ += replay->blended_draws;
    subframe_guarded_total_ += replay->guarded;
    subframe_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++subframe_record_samples_;
    present_to_swap_chain();
    return true;
}

// Motion and reactive targets of the sub-frames, at the replay EFB's scale
// (the scale current while a replay records).
bool RendererD3D12::ensure_replay_motion_targets() {
    if (replay_motion_.target && replay_motion_.scale == efb_scale_) {
        return true;
    }
    if (replay_motion_.target) {
        // A scale change: lists recorded with the old targets are recorded again.
        wait_for_gpu();
        for (ReplayList& list : replay_lists_) {
            list.sequence = 0u;
        }
    }
    replay_motion_ = ReplayMotionTargets{};
    if (!replay_motion_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 2;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&replay_motion_rtv_heap_)))) {
            return false;
        }
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = kEfbWidth * efb_scale_;
    rd.Height = kEfbHeight * efb_scale_;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kMotionVectorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kMotionVectorFormat;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.target)))) {
        return false;
    }
    rd.Format = kReactiveMaskFormat;
    cv.Format = kReactiveMaskFormat;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.reactive)))) {
        replay_motion_ = ReplayMotionTargets{};
        return false;
    }
    replay_motion_.target->SetName(L"GX sub-frame motion vectors");
    replay_motion_.reactive->SetName(L"GX sub-frame reactive mask");
    replay_motion_.rtv = replay_motion_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    replay_motion_.reactive_rtv = replay_motion_.rtv;
    replay_motion_.reactive_rtv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device_->CreateRenderTargetView(replay_motion_.target.Get(), nullptr, replay_motion_.rtv);
    device_->CreateRenderTargetView(replay_motion_.reactive.Get(), nullptr, replay_motion_.reactive_rtv);
    replay_motion_.scale = efb_scale_;
    return true;
}

void RendererD3D12::swap_motion_targets() {
    std::swap(motion_target_, replay_motion_.target);
    std::swap(reactive_target_, replay_motion_.reactive);
    std::swap(motion_rtv_, replay_motion_.rtv);
    std::swap(reactive_rtv_, replay_motion_.reactive_rtv);
    bound_rt_aux_ = 0xFFu;
}

// The output switch of one presented frame: the temporal upscale of the
// replay's EFB into its output EFB, or the plain resample when the upscaler
// could not record it.
void RendererD3D12::record_subframe_upscale(const ReplayList& entry, const SubframeTime& time) {
    const bool recording = replay_recording_;
    replay_recording_ = false;
    const bool output_before = output_efb_active_;
    if (output_before) {
        swap_efb_sets();
    }
    swap_efb_with(replay_efb_);
    std::swap(alt_efb_, replay_output_efb_);
    bool upscaled = false;
    if (temporal_) {
        TemporalUpscaleInputs in{};
        in.list = command_list_.Get();
        in.color = entry.upscale.color;
        in.depth = entry.upscale.depth;
        in.motion = entry.upscale.motion;
        in.reactive = entry.upscale.reactive;
        in.output = entry.upscale.output;
        in.render_width = entry.upscale.render_width;
        in.render_height = entry.upscale.render_height;
        in.output_width = entry.upscale.output_width;
        in.output_height = entry.upscale.output_height;
        in.jitter_x = time.jitter_x;
        in.jitter_y = time.jitter_y;
        in.camera_near = scene_camera_[0];
        in.camera_far = scene_camera_[1];
        in.fov_y = scene_camera_[2];
        const RenderConfig cfg = get_render_config();
        in.frame_ms = static_cast<float>(1000.0 / present_frame_rate(cfg.max_fps, cfg.vsync_interval, display_refresh_hz_));
        in.reset = time.reset || !temporal_history_valid_;
        upscaled = temporal_inputs_match(in) && temporal_->evaluate(in);
        ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_gx_bindings();
    }
    temporal_history_valid_ = upscaled;
    if (!upscaled && entry.upscale.copy < run_copies_.size()) {
        execute_replay_copy(run_copies_[entry.upscale.copy]);
    }
    std::swap(alt_efb_, replay_output_efb_);
    swap_efb_with(replay_efb_);
    if (output_before) {
        swap_efb_sets();
    }
    replay_recording_ = recording;
}

bool RendererD3D12::ensure_replay_profile() {
    if (replay_profile_.heap) {
        return true;
    }
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = kReplayProfileQueries;
    if (FAILED(device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&replay_profile_.heap)))) {
        return false;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = kReplayProfileQueries * sizeof(std::uint64_t);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&replay_profile_.readback)))) {
        replay_profile_.heap.Reset();
        return false;
    }
    return true;
}

// One sampled replay's GPU time: the draws before each copy or clear, each copy
// (by kind and size) and each clear.
void RendererD3D12::report_replay_profile() {
    if (!replay_profile_.armed || replay_profile_.fence == 0u || completed_fence_value() < replay_profile_.fence) {
        return;
    }
    replay_profile_.armed = false;
    const std::size_t count = replay_profile_.segments.size() + 1u;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, count * sizeof(std::uint64_t)};
    if (FAILED(replay_profile_.readback->Map(0, &range, &mapped))) {
        return;
    }
    std::vector<std::uint64_t> ticks(count);
    std::memcpy(ticks.data(), mapped, count * sizeof(std::uint64_t));
    const D3D12_RANGE none{0, 0};
    replay_profile_.readback->Unmap(0, &none);
    const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
    double draws = 0.0, copies = 0.0, display = 0.0, output = 0.0, clears = 0.0;
    std::vector<std::pair<double, std::size_t>> slowest;
    for (std::size_t i = 0; i < replay_profile_.segments.size(); ++i) {
        const double ms = ticks[i + 1u] >= ticks[i] ? static_cast<double>(ticks[i + 1u] - ticks[i]) * to_ms : 0.0;
        switch (replay_profile_.segments[i].kind) {
        case 0u: draws += ms; break;
        case 1u: copies += ms; break;
        case 2u: display += ms; break;
        case 3u: output += ms; break;
        default: clears += ms; break;
        }
        slowest.emplace_back(ms, i);
    }
    std::sort(slowest.begin(), slowest.end(), std::greater<>());
    std::ostringstream line;
    line << "[interp-gpu-profile] log=" << run_serial_ << " total-ms="
         << static_cast<double>(ticks.back() - ticks.front()) * to_ms << " draws-ms=" << draws
         << " copies-ms=" << copies << " display-copy-ms=" << display << " output-switch-ms=" << output
         << " clears-ms=" << clears << " copies=" << run_copies_.size() << " slowest:";
    for (std::size_t k = 0; k < slowest.size() && k < 8u; ++k) {
        const ReplayProfileSegment& segment = replay_profile_.segments[slowest[k].second];
        line << ' ' << (segment.kind == 0u ? "draws" : segment.kind == 4u ? "clear" : "copy") << '=' << slowest[k].first;
        if (segment.kind >= 1u && segment.kind <= 3u && segment.detail < run_copies_.size()) {
            const ReplayCopy& copy = run_copies_[segment.detail];
            line << '(' << copy.width << 'x' << copy.height << " fmt" << static_cast<unsigned>(copy.format)
                 << (copy.depth ? " depth" : "") << ')';
        }
    }
    line << '\n';
    std::cerr << line.str();
}

bool RendererD3D12::take_eval_readback(std::vector<std::uint32_t>& words, float& t) {
    if (eval_readback_fence_ == 0u || completed_fence_value() < eval_readback_fence_ || !eval_readback_) {
        return false;
    }
    eval_readback_fence_ = 0u;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(eval_readback_bytes_)};
    if (FAILED(eval_readback_->Map(0, &range, &mapped))) {
        return false;
    }
    words.resize(static_cast<std::size_t>(eval_readback_bytes_ / 4u));
    std::memcpy(words.data(), mapped, words.size() * 4u);
    const D3D12_RANGE none{0, 0};
    eval_readback_->Unmap(0, &none);
    t = eval_readback_t_;
    return true;
}

bool RendererD3D12::begin_motion_scene() {
    if (!scene_temporal_ || !ensure_motion_target()) {
        return false;
    }
    const float zero[4]{};
    command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
    command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
    return true;
}

bool RendererD3D12::scene_temporal_begin(float& jitter_x, float& jitter_y) {
    jitter_x = 0.0f;
    jitter_y = 0.0f;
    scene_temporal_ = false;
    if (!temporal_mode() || output_efb_active_) {
        temporal_.reset();
        temporal_history_valid_ = false;
        return false;
    }
    temporal_interp_ = ring_mode_ == RingMode::Interp;
    const unsigned output_scale = output_efb_scale_for_window();
    if (output_scale == 0u || efb_scale_ > output_scale) {
        temporal_history_valid_ = false;
        return false;
    }
    const unsigned rw = kEfbWidth * efb_scale_;
    const unsigned rh = kEfbHeight * efb_scale_;
    const unsigned ow = kEfbWidth * output_scale;
    const unsigned oh = kEfbHeight * output_scale;
    const RenderConfig cfg = get_render_config();
    const unsigned kind = cfg.upscaler;
    const unsigned quality = std::min(cfg.temporal_quality, 4u);
    if (!temporal_ || rw != temporal_render_w_ || rh != temporal_render_h_ ||
        ow != temporal_output_w_ || oh != temporal_output_h_ || kind != temporal_kind_ ||
        (kind == 3u && quality != temporal_quality_)) {
        if (temporal_) {
            // The old context's resources may still be in use by frames in flight.
            wait_for_gpu();
            temporal_.reset();
        }
        temporal_ = kind == 3u ? create_dlss_upscaler(rw, rh, ow, oh, quality)
                               : create_fsr3_upscaler(device_.Get(), rw, rh, ow, oh);
        temporal_kind_ = kind;
        temporal_quality_ = quality;
        temporal_history_valid_ = false;
        if (!temporal_) {
            temporal_render_w_ = temporal_render_h_ = 0u;
            temporal_output_w_ = temporal_output_h_ = 0u;
            return false;
        }
        temporal_render_w_ = rw;
        temporal_render_h_ = rh;
        temporal_output_w_ = ow;
        temporal_output_h_ = oh;
        std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh
                  << " -> " << ow << "x" << oh << "\n";
    }
    if (temporal_interp_) {
        // Game frames feed the sub-frames: no jitter, and no upscale
        // (begin_output_efb). Each presented frame takes the next phase.
        scene_jitter_[0] = 0.0f;
        scene_jitter_[1] = 0.0f;
        scene_temporal_ = true;
        return true;
    }
    const unsigned phases = std::max(temporal_->jitter_phase_count(rw, ow), 1u);
    temporal_->jitter_offset(static_cast<unsigned>(temporal_frame_ % phases), phases,
                             jitter_x, jitter_y);
    ++temporal_frame_;
    scene_jitter_[0] = jitter_x;
    scene_jitter_[1] = jitter_y;
    scene_temporal_ = true;
    return true;
}

void RendererD3D12::set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept {
    scene_camera_[0] = near_plane;
    scene_camera_[1] = far_plane;
    scene_camera_[2] = fov_y;
}

// The upscaler's context was created for exactly these sizes and the resources cover them. An
// internal-resolution change applied between a scene's start and its upscale (a render chunk
// boundary) would otherwise dispatch past the context's maximum (FidelityFX OUT_OF_RANGE) or
// outside the resources.
bool RendererD3D12::temporal_inputs_match(const TemporalUpscaleInputs& in) const {
    const auto covers = [](ID3D12Resource* resource, unsigned width, unsigned height) {
        if (resource == nullptr) {
            return true;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        return desc.Width >= width && desc.Height >= height;
    };
    const bool match =
        in.render_width == temporal_render_w_ && in.render_height == temporal_render_h_ &&
        in.output_width == temporal_output_w_ && in.output_height == temporal_output_h_ &&
        covers(in.color, in.render_width, in.render_height) &&
        covers(in.depth, in.render_width, in.render_height) &&
        covers(in.motion, in.render_width, in.render_height) &&
        covers(in.reactive, in.render_width, in.render_height) &&
        covers(in.output, in.output_width, in.output_height);
    if (!match) {
        static std::uint64_t reported = 0u;
        if (reported++ < 8u) {
            std::cerr << "[temporal] skipped: " << in.render_width << 'x' << in.render_height << " -> "
                      << in.output_width << 'x' << in.output_height << " context " << temporal_render_w_ << 'x'
                      << temporal_render_h_ << " -> " << temporal_output_w_ << 'x' << temporal_output_h_ << '\n';
        }
    }
    return match;
}

bool RendererD3D12::record_temporal_upscale(unsigned output_scale) {
    if (!scene_temporal_ || !temporal_ || !ensure_motion_target()) {
        return false;
    }
    TemporalUpscaleInputs in{};
    in.list = command_list_.Get();
    in.color = efb_color_.Get();
    in.depth = efb_depth_.Get();
    in.motion = motion_target_.Get();
    in.reactive = reactive_target_.Get();
    in.output = alt_efb_.color.Get();
    in.render_width = kEfbWidth * efb_scale_;
    in.render_height = kEfbHeight * efb_scale_;
    in.output_width = kEfbWidth * output_scale;
    in.output_height = kEfbHeight * output_scale;
    in.jitter_x = scene_jitter_[0];
    in.jitter_y = scene_jitter_[1];
    in.camera_near = scene_camera_[0];
    in.camera_far = scene_camera_[1];
    in.fov_y = scene_camera_[2];
    in.frame_ms = 1000.0f / 60.0f;
    in.reset = !temporal_history_valid_;
    const bool recorded = temporal_inputs_match(in) && temporal_->evaluate(in);
    // The upscaler binds its own heaps, root signature and pipelines.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    temporal_history_valid_ = recorded;
    return recorded;
}

// ---------------------------------------------------------------------------
// Output-resolution EFB (HUD split)
// ---------------------------------------------------------------------------

unsigned RendererD3D12::output_efb_scale_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect =
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect;
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(s_bb_width), static_cast<int>(s_bb_height), content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return 0u;
    }
    // The smallest integer scale whose 640x456 picture covers the window's
    // content rectangle on both axes (the launcher's Match Output rule).
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth),
                                   content.height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
}

bool RendererD3D12::ensure_output_efb(unsigned scale) {
    if (alt_efb_.color && alt_efb_.scale == scale) {
        return true;
    }
    const UINT w = kEfbWidth * scale;
    const UINT h = kEfbHeight * scale;
    if (w > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        h > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        return false;
    }
    if (alt_efb_.color) {
        // A window resize: frames in flight may still read the old views.
        wait_for_gpu();
    }
    alt_efb_.color.Reset();
    alt_efb_.depth.Reset();
    alt_efb_.scale = 0u;
    if (!alt_efb_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_efb_rtv_heap_)),
                  "output EFB RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_efb_dsv_heap_)),
                  "output EFB DSV heap");
        alt_efb_.rtv = alt_efb_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        alt_efb_.dsv = alt_efb_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kEfbColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&alt_efb_.color)))) {
        return false;
    }
    rd.Format = kEfbDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kEfbDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &dcv, IID_PPV_ARGS(&alt_efb_.depth)))) {
        alt_efb_.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(alt_efb_.color.Get(), nullptr, alt_efb_.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(alt_efb_.depth.Get(), &dsv, alt_efb_.dsv);
    if (!alt_efb_.conv_table) {
        alt_efb_.conv_table = srv_ring_.allocate_persistent(2u);
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(
        alt_efb_.color.Get(), &view, alt_efb_.conv_table->cpu);
    auto depth_cpu = alt_efb_.conv_table->cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(alt_efb_.depth.Get(), &view, depth_cpu);
    alt_efb_.conv_color_srv = alt_efb_.conv_table->gpu;
    alt_efb_.conv_depth_srv = alt_efb_.conv_table->gpu;
    alt_efb_.conv_depth_srv.ptr += stride;
    alt_efb_.scale = scale;
    std::cout << "[Renderer] output EFB " << scale << "x (" << w << "x" << h << ")\n";
    return true;
}

void RendererD3D12::write_efb_set_views(EfbTargetSet& set, const DescriptorRing::Table& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(set.color.Get(), &view, table.cpu);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(set.depth.Get(), &view, depth_cpu);
    set.conv_table = table;
    set.conv_color_srv = table.gpu;
    set.conv_depth_srv = table.gpu;
    set.conv_depth_srv.ptr += stride;
}

bool RendererD3D12::create_efb_set(
    EfbTargetSet& set, ComPtr<ID3D12DescriptorHeap>& rtv_heap,
    ComPtr<ID3D12DescriptorHeap>& dsv_heap, unsigned scale, bool persistent_views,
    bool unordered_access) {
    const UINT w = kEfbWidth * scale;
    const UINT h = kEfbHeight * scale;
    if (set.color) {
        wait_for_gpu();
    }
    set.color.Reset();
    set.depth.Reset();
    set.scale = 0u;
    if (!rtv_heap) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtv_heap)), "EFB set RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap)), "EFB set DSV heap");
        set.rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        set.dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               (unordered_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kEfbColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&set.color)))) {
        return false;
    }
    rd.Format = kEfbDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kEfbDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &dcv, IID_PPV_ARGS(&set.depth)))) {
        set.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(set.color.Get(), nullptr, set.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(set.depth.Get(), &dsv, set.dsv);
    if (persistent_views) {
        write_efb_set_views(set, set.conv_table ? *set.conv_table
                                                : srv_ring_.allocate_persistent(2u));
    } else {
        // The caller writes views into the frame's descriptor segment per use.
        set.conv_table.reset();
    }
    set.scale = scale;
    return true;
}

void RendererD3D12::swap_efb_with(EfbTargetSet& other) {
    std::swap(efb_color_, other.color);
    std::swap(efb_depth_, other.depth);
    std::swap(efb_scale_, other.scale);
    std::swap(efb_rtv_, other.rtv);
    std::swap(efb_dsv_, other.dsv);
    std::swap(conv_efb_source_table_, other.conv_table);
    std::swap(conv_efb_color_srv_, other.conv_color_srv);
    std::swap(conv_efb_depth_srv_, other.conv_depth_srv);
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
}

void RendererD3D12::swap_efb_sets() {
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SwapEfbSets, 0u});
    }
    std::swap(efb_color_, alt_efb_.color);
    std::swap(efb_depth_, alt_efb_.depth);
    std::swap(efb_scale_, alt_efb_.scale);
    std::swap(efb_rtv_, alt_efb_.rtv);
    std::swap(efb_dsv_, alt_efb_.dsv);
    std::swap(conv_efb_source_table_, alt_efb_.conv_table);
    std::swap(conv_efb_color_srv_, alt_efb_.conv_color_srv);
    std::swap(conv_efb_depth_srv_, alt_efb_.conv_depth_srv);
    output_efb_active_ = !output_efb_active_;
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
    bind_efb(nullptr);
}

bool RendererD3D12::begin_output_efb() {
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    if (output_efb_active_ || (!temporal && !get_render_config().hud_output_resolution)) {
        return false;
    }
    const unsigned scale = output_efb_scale_for_window();
    if (scale == 0u || scale < efb_scale_ || (scale == efb_scale_ && !temporal) ||
        !ensure_output_efb(scale)) {
        temporal_history_valid_ = false;
        return false;
    }
    scene_temporal_ = temporal;
    const bool upscaled = temporal && !temporal_interp_ && record_temporal_upscale(scale);
    scene_temporal_ = false;
    if (upscaled) {
        swap_efb_sets();
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        command_list_->ClearDepthStencilView(
            efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
        return true;
    }
    // The whole scene EFB, resampled into the output target with the display
    // copy's identity settings (no filter, gamma 1.0, alpha kept).
    EfbCopyParams params{};
    params.src_width = static_cast<std::uint16_t>(kEfbWidth);
    params.src_height = static_cast<std::uint16_t>(kEfbHeight);
    params.y_scale_raw = 256u;
    params.filter0_raw = (21u << 12u) | (22u << 18u);
    params.filter1_raw = 21u;
    const unsigned w = kEfbWidth * scale;
    const unsigned h = kEfbHeight * scale;
    const EfbCopySamplingGeometry sampling =
        compute_xfb_sampling_geometry(params, w, h, efb_scale_);
    if (replay_recording_) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.keep_alpha = true;
        copy.format = 15u;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        copy.mode = 2u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        const bool recording = replay_recording_;
        replay_recording_ = false;  // the blit's own EFB rebinding is part of the copy
        record_conversion_blit(
            alt_efb_.color.Get(), w, h, {}, params,
            /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
            /*copy_format=*/15u,
            sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
            sampling.filter_row_offset);
        replay_recording_ = recording;
        swap_efb_sets();
        replay_ops_.push_back({ReplayKind::ClearDepth, 0u});
    } else {
        record_conversion_blit(
            alt_efb_.color.Get(), w, h, {}, params,
            /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
            /*copy_format=*/15u,
            sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
            sampling.filter_row_offset);
        swap_efb_sets();
    }
    const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
    command_list_->ClearDepthStencilView(
        efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        far_values.reversed_depth, 0u, 0u, nullptr);
    return true;
}

void RendererD3D12::end_output_efb() {
    if (output_efb_active_) {
        swap_efb_sets();
    }
}

// ---------------------------------------------------------------------------
// EFB binding and clearing
// ---------------------------------------------------------------------------

void RendererD3D12::bind_efb(const float xf_vp[6]) {
    if (replay_recording_) [[unlikely]] {
        ReplayViewport viewport{};
        viewport.valid = xf_vp != nullptr;
        if (xf_vp != nullptr) std::memcpy(viewport.values, xf_vp, sizeof(viewport.values));
        replay_ops_.push_back({ReplayKind::BindEfb, static_cast<std::uint32_t>(replay_viewports_.size())});
        replay_viewports_.push_back(viewport);
    }
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, &efb_dsv_);
    bound_rt_aux_ = 0u;

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
    // Full-EFB binding can overwrite a previously applied GX scissor even
    // though its raw draw input is unchanged on the next draw.
    bound_scissor_input_valid_ = false;
    bound_viewport_valid_ = true;
    bound_scissor_valid_ = true;
}

void RendererD3D12::clear_efb(
    std::uint32_t clear_ar, std::uint32_t clear_gb, std::uint32_t clear_z24,
    bool color_enable, bool alpha_enable, bool depth_enable,
    unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h,
    std::uint8_t pixel_format) {
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::Clear, static_cast<std::uint32_t>(replay_clears_.size())});
        replay_clears_.push_back({clear_ar, clear_gb, clear_z24, color_enable, alpha_enable,
                                  depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format});
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replay_playing_) [[unlikely]] {
        log_start_clear_ = {clear_ar, clear_gb, clear_z24, color_enable, alpha_enable,
                            depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format};
        log_start_clear_valid_ = true;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = efb_rtv_;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = efb_dsv_;

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
            bound_rt_aux_ = 0u;
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
        // REVERSED-Z buffer: GX z24 (near=0, far=2^24) inverts into the D3D depth value.
        // GX_MAX_Z24 (0xFFFFFF = "clear to farthest") becomes about 0.0 here, which the flipped
        // GEQUAL-style funcs pass.
        command_list_->ClearDepthStencilView(
            dsv, D3D12_CLEAR_FLAG_DEPTH, clear.reversed_depth, 0, num_rects, rects);
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void RendererD3D12::check_draw_addresses(const DrawCall& call) {
    static std::uint32_t reported = 0u;
    if (reported >= 40u) {
        return;
    }
    const auto known = [this](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint64_t bytes) {
        const auto in = [va, bytes](D3D12_GPU_VIRTUAL_ADDRESS base, std::uint64_t size) {
            return base != 0u && va >= base && va + bytes <= base + size;
        };
        return in(vertex_ring_.gpu_base(), vertex_ring_.total_bytes()) ||
               in(index_ring_.gpu_base(), index_ring_.total_bytes()) ||
               in(constant_ring_.gpu_base(), constant_ring_.total_bytes()) ||
               in(matrix_ring_.gpu_base(), matrix_ring_.total_bytes()) ||
               in(resident_vertex_base_, resident_vertex_bytes_) ||
               (blend_verdicts_ && in(blend_verdicts_->GetGPUVirtualAddress(), blend_verdicts_->GetDesc().Width)) ||
               (interp_output_ && in(interp_output_->GetGPUVirtualAddress(), interp_output_bytes_));
    };
    const auto check = [&](const char* field, D3D12_GPU_VIRTUAL_ADDRESS va, std::uint64_t bytes) {
        if (va == 0u || known(va, bytes) || reported >= 40u) {
            return;
        }
        ++reported;
        std::cerr << "[draw-address] field=" << field << " va=0x" << std::hex << va << std::dec
                  << " bytes=" << bytes << " replaying=" << (replay_playing_ ? 1 : 0)
                  << " blend=" << (call.blend_vertices != 0u ? 1 : 0)
                  << " record=" << call.ps_record << '\n';
    };
    check("vs-constants", call.vs_constants, 16u);
    check("ps-constants", call.ps_constants, 16u);
    check("matrix-palette", call.matrix_palette, 16u);
    check("blend-vertices", call.blend_vertices, 144u);
    check("vertex-view", call.vertex_view.BufferLocation, call.vertex_view.SizeInBytes);
    check("index-view", call.index_view.BufferLocation, call.index_view.SizeInBytes);
    // A blended draw reads the earlier vertices at SV_VertexID * stride: every
    // index must be below the vertex count, and the earlier run must be whole.
    if (call.blend_vertices != 0u && call.vertex_view.StrideInBytes != 0u && call.index_count != 0u &&
        reported < 40u) {
        const std::uint32_t vertices = call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes;
        std::uint32_t highest = 0u;
        if (call.index_view.BufferLocation != 0u) {
            const auto* indices = reinterpret_cast<const std::uint16_t*>(index_ring_.cpu_at(call.index_view.BufferLocation));
            highest = 0u;
            for (std::uint32_t i = 0u; indices != nullptr && i < call.index_count; ++i) {
                highest = std::max<std::uint32_t>(highest, indices[i]);
            }
        } else {
            highest = call.index_count - 1u;
        }
        const std::uint64_t reach = (static_cast<std::uint64_t>(highest) + 1u) * call.vertex_view.StrideInBytes;
        if (highest >= vertices || !known(call.blend_vertices, reach)) {
            ++reported;
            std::cerr << "[draw-address] blend highest-index=" << highest << " vertices=" << vertices
                      << " earlier=0x" << std::hex << call.blend_vertices << std::dec << " reach=" << reach
                      << " in-known-buffer=" << (known(call.blend_vertices, reach) ? 1 : 0)
                      << " resident=0x" << std::hex << resident_vertex_base_ << std::dec << '+' << resident_vertex_bytes_
                      << " slot=" << call.blend_slot << '\n';
        }
    }
}

void RendererD3D12::draw(const DrawCall& call) {
    static const bool validate_addresses = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_VALIDATE_DRAW_ADDRESSES") == 0 &&
               length > 1u && value[0] == '1';
    }();
    if (validate_addresses) [[unlikely]] {
        check_draw_addresses(call);
    }
    if (!replay_playing_) {
        dred_marker(command_list_.Get(), L"draw rec=%d n=%u vb=%u", call.replay_record, call.index_count,
                    call.vertex_view.SizeInBytes);
    }
    if (replay_recording_) [[unlikely]] {
        if (call.texture_table.ptr != 0u && !replay_textures_.by_gpu.empty()) {
            take_replay_snapshots(call.texture_table.ptr);
        }
        replay_ops_.push_back({ReplayKind::Draw, static_cast<std::uint32_t>(replay_draws_.size())});
        replay_draws_.push_back(call);
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replay_playing_) [[unlikely]] {
        ++log_gap_ops_;
    }
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
    if (!bound_vertex_blend_valid_ || call.blend_vertices != bound_blend_vertices_ ||
        call.blend_slot != bound_blend_slot_) {
        // Unblended draws bind valid addresses the shaders never read.
        command_list_->SetGraphicsRootShaderResourceView(
            5, call.blend_vertices != 0u ? call.blend_vertices : vertex_ring_.gpu_base());
        const std::uint32_t vertices = call.vertex_view.StrideInBytes != 0u
            ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
        const std::uint32_t blend[4]{call.blend_vertices != 0u ? 1u : 0u,
                                     replay_playing_ ? kFrameValuesElement : kZeroValuesElement,
                                     std::min(call.blend_slot, kMaxBlendPairs - 1u),
                                     call.blend_vertices != 0u && vertices != 0u ? vertices - 1u : 0u};
        command_list_->SetGraphicsRoot32BitConstants(6, 4u, blend, 0u);
        command_list_->SetGraphicsRootShaderResourceView(7, blend_verdicts_->GetGPUVirtualAddress());
        bound_blend_vertices_ = call.blend_vertices;
        bound_blend_slot_ = call.blend_slot;
        bound_vertex_blend_valid_ = true;
    }
    if (call.aux_target != bound_rt_aux_) [[unlikely]] {
        if (call.aux_target != 0u) {
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{
                efb_rtv_, call.aux_target == 1u ? motion_rtv_ : reactive_rtv_};
            command_list_->OMSetRenderTargets(2, targets, FALSE, &efb_dsv_);
        } else {
            command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, &efb_dsv_);
        }
        bound_rt_aux_ = call.aux_target;
    }

    // Per-draw GX scissor, scaled to the EFB and clamped.  GX titles rely on
    // the scissor for UI clipping and split renders; an empty scissor is a
    // faithful "rasterize nothing" (also how CullMode::All is expressed).
    // The raw key is recorded only after a nonempty input has been applied.
    // Its conversion is deterministic for these fixed EFB extents and scale.
    if (!bound_scissor_valid_ || !bound_scissor_input_valid_ ||
        bound_scissor_input_scale_ != efb_scale_ ||
        call.scissor.left != bound_scissor_input_.left ||
        call.scissor.top != bound_scissor_input_.top ||
        call.scissor.right != bound_scissor_input_.right ||
        call.scissor.bottom != bound_scissor_input_.bottom) {
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
        bound_scissor_input_ = call.scissor;
        bound_scissor_input_scale_ = efb_scale_;
        bound_scissor_input_valid_ = true;
    }

    // Per-draw XF viewport. SMG never uses EFB copy-clear. It clears depth by drawing fullscreen
    // quads with GXSetViewport near==far==1.0, so the quad writes z=FAR regardless of its geometry z.
    // Without the depth-range plumbing those clear quads wrote z=NEAR, which fails every LEQUAL 3D draw.
    // XF layout (hardware/Dolphin xfmem): wd, ht, zRange, xOrig, yOrig, farZ.
    // Depth range (Dolphin BPFunctions::SetScissorAndViewport, the REVERSED-Z form for backends
    // without reversed-range support):
    //   gx_min = (farZ - zRange) / 2^24,  gx_max = farZ / 2^24
    //   MinDepth = 1 - gx_max,  MaxDepth = 1 - gx_min
    // Composes with the negated projection z (fill_projection): GX near maps to the depth MaxDepth
    // side, GX far to the MinDepth side; depth funcs are flipped in pipeline_cache to match.
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
    // A replayed display list rebinds the same vertex ring allocation for
    // thousands of consecutive draws, so compare the whole 24-byte view rather
    // than issuing the call unconditionally. draw() is the only writer of
    // stream 0 on this command list (the UI helpers, the EFB conversion blit and
    // the clear quad each own their own pipeline, and every one of them is
    // followed by invalidate_gx_bindings()), and begin_frame() re-arms the
    // cached view after the vertex ring has been rewound for the new slot.
    if (!bound_vertex_view_valid_ ||
        call.vertex_view.BufferLocation != bound_vertex_view_.BufferLocation ||
        call.vertex_view.SizeInBytes != bound_vertex_view_.SizeInBytes ||
        call.vertex_view.StrideInBytes != bound_vertex_view_.StrideInBytes) {
        command_list_->IASetVertexBuffers(0, 1, &call.vertex_view);
        bound_vertex_view_ = call.vertex_view;
        bound_vertex_view_valid_ = true;
    }
    if (call.index_count > 0) {
        if (call.index_view.BufferLocation) {
            // Same redundant-state elision as the vertex view above. A replayed
            // display list binds the same index-ring allocation for thousands of
            // consecutive draws, and IASetIndexBuffer is the one per-draw binding
            // that was still unconditional: it was issued once per draw even when
            // the whole 20-byte view was unchanged. draw() is the only writer of
            // the index binding on this command list, and every other writer (the
            // UI helpers, the EFB conversion blit) is followed by
            // invalidate_gx_bindings(), which re-arms the cached view.
            if (!bound_index_view_valid_ ||
                call.index_view.BufferLocation !=
                    bound_index_view_.BufferLocation ||
                call.index_view.SizeInBytes != bound_index_view_.SizeInBytes ||
                call.index_view.Format != bound_index_view_.Format) {
                command_list_->IASetIndexBuffer(&call.index_view);
                bound_index_view_ = call.index_view;
                bound_index_view_valid_ = true;
            }
            command_list_->DrawIndexedInstanced(call.index_count, 1, 0, 0, 0);
        } else {
            command_list_->DrawInstanced(call.index_count, 1, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// EFB copy paths
// ---------------------------------------------------------------------------

std::optional<D3D12_BOX> detail::efb_identity_copy_box(
    const EfbCopyParams& params, bool is_depth_copy, unsigned efb_scale,
    unsigned dest_width, unsigned dest_height) noexcept {
    if (is_depth_copy || params.copy_to_xfb || params.target_format != 6u ||
        params.intensity || params.yuv || (params.gamma & 3u) != 0u ||
        params.half_scale || params.scale_y || params.frame_to_field != 0u ||
        params.src_width == 0u || params.src_height == 0u ||
        static_cast<unsigned>(params.src_x) + params.src_width > kEfbWidth ||
        static_cast<unsigned>(params.src_y) + params.src_height > kEfbHeight) {
        return std::nullopt;
    }
    const unsigned upper = params.filter_upper();
    const unsigned middle = params.filter_middle();
    const unsigned lower = params.filter_lower();
    // All-zero startup registers use middle64 in the conversion shader too.
    if (upper != 0u || lower != 0u || (middle != 0u && middle != 64u)) {
        return std::nullopt;
    }
    const unsigned scale = std::clamp(efb_scale, 1u, kMaxEfbScale);
    if (dest_width != params.src_width * scale ||
        dest_height != params.src_height * scale) {
        return std::nullopt;
    }
    const UINT left = params.src_x * scale;
    const UINT top = params.src_y * scale;
    return D3D12_BOX{left, top, 0u, left + dest_width, top + dest_height, 1u};
}

void detail::record_efb_identity_copy(
    ID3D12GraphicsCommandList* commands, ID3D12Resource* source,
    ID3D12Resource* destination, const D3D12_BOX& source_box) {
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (auto& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = source;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.pResource = destination;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    commands->ResourceBarrier(2u, barriers);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = destination;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, &source_box);
    for (auto& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    commands->ResourceBarrier(2u, barriers);
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererD3D12::allocate_conversion_rtv(
    ID3D12Resource* resource) {
    if (resource == nullptr || conv_rtv_heap_ == nullptr || conv_scratch_rtv_.ptr == 0u) {
        throw std::runtime_error(
            "[RendererD3D12] conversion RTV descriptor unavailable");
    }
    // OMSetRenderTargets snapshots this CPU descriptor at recording time.
    // Reuse only if the shared slot still describes this exact resource.
    static_cast<void>(conv_scratch_rtv_slot_.bind(
        device_.Get(), resource, conv_scratch_rtv_));
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

    // Both immutable EFB views already live in the bound heap. Depth reads
    // retain the R24/X8 view of the typeless depth resource.
    const D3D12_GPU_DESCRIPTOR_HANDLE src_srv =
        is_depth_copy ? conv_efb_depth_srv_ : conv_efb_color_srv_;

    // CPU RTVs are consumed at recording time. Empty-handle callers use the
    // same shared slot writer, keeping its cached resource identity coherent.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dest_rtv;
    if (rtv.ptr == 0u) {
        rtv = allocate_conversion_rtv(dest);
    }

    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
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
    command_list_->SetGraphicsRootDescriptorTable(1, src_srv);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // EFB back to its steady state.
    ++efb_shader_copies_;
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Restore the GX drawing state (root signature changed; EFB RTV/DSV and full
    // viewport/scissor re-bound; per-draw viewport/scissor follow).
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
    ++efb_copy_requests_;
    // Another XFB or texture copy can overwrite the shared CPU RTV slot.
    // Check its current resource, even when this destination has copied before.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

    // Dest XFB texture -> render target for the stretch blit. The y-scale height difference between
    // the src rect and the dest texture is the stretch; CopyTextureRegion cannot express it.
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
    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = dest.width;
        copy.height = dest.height;
        copy.params = params;
        copy.keep_alpha = true;
        copy.format = 15u;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        copy.mode = 1u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }

    // Display copies never convert to intensity (Dolphin passes false on the
    // XFB path); gamma + copy filter (deflicker) do apply.
    record_conversion_blit(
        dest.texture.Get(), dest.width, dest.height, dest.rtv, params,
        /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
        /*copy_format=*/15u,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);
    replay_recording_ = recording;

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    if (ring_mode_ == RingMode::Interp) {
        // The display copy closes the game frame: its log is complete and the
        // next frame starts a new ring segment.
        if (recording) {
            replay_recording_ = false;
            // One 3D scene. An output-EFB switch (HUD at output resolution)
            // replays into the sub-frame's private output EFB.
            replay_valid_ = replay_scene_count_ == 1u;
            replay_reject_ = replay_scene_count_ == 0u ? 1u
                : replay_scene_count_ > 1u             ? 2u
                                                       : 0u;
            if (replay_valid_) {
                publish_replay_log();
            }
            replay_segment_ = ring_segment_;
            replay_xfb_addr_ = dest.guest_addr;
            ++replay_serial_;
            if (replay_valid_) {
                play_serial_ = replay_serial_;
            }
        }
        ring_advance_pending_ = true;
    }
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
    // Effect copies (destinations up to 320x228: the bloom pyramid, simple
    // bloom, depth of field, Mario's 160x160 shadow capture) are built at the
    // game's own resolution, box-filtered over the scaled EFB (kConvPs). Their
    // low resolution is part of the effect: the blur kernels use fixed
    // screen-relative tap offsets and rely on it to soften, so scaled copies
    // turn the taps into separate offset images. Full-size captures stay
    // scaled. GALAXY_NATIVE_EFFECT_COPIES=0 keeps only the three bloom
    // workspace copies native (GALAXY_NATIVE_BLOOM_WORKSPACE).
    static const bool native_effect_copies = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value),
            "GALAXY_NATIVE_EFFECT_COPIES") == 0 && length == 2u && value[0] == '0');
    }();
    static bool comparison_enabled = native_bloom_workspace || native_effect_copies;
    static ULONGLONG comparison_last_poll = 0u;
    const bool effect_copy = native_effect_copies
        ? dw_unscaled <= 320u && dh_unscaled <= 228u
        : is_galaxy_bloom_workspace_copy(params, is_depth_copy);
    const bool bloom_copy =
        (native_bloom_workspace || native_effect_copies || !bloom_comparison_file.empty()) &&
        efb_scale_ > 1u && effect_copy;
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
    ++efb_copy_requests_;

    // Keyed by (addr, format, depth-source): SMG reuses one address with
    // different copy formats and each needs its own converted texture.
    const std::uint64_t dest_key =
        static_cast<std::uint64_t>(params.dest_addr) |
        (static_cast<std::uint64_t>(params.target_format) << 32) |
        (is_depth_copy ? (1ull << 40) : 0ull) |
        // One guest address alternates between early scaled masks and the
        // final native bloom composite. Cache both resources to avoid
        // reallocating on every pass/frame; the latest alias still wins.
        (native_bloom ? (1ull << 41) : 0ull) |
        // Interpolation: consecutive game frames copy into different
        // textures, so a sub-frame replaying one frame's copies never
        // overwrites what the next frame's chunks already copied.
        (ring_mode_ == RingMode::Interp ? (replay_serial_ & 1ull) << 42 : 0ull);

    // Bound the destination map. Each entry is one copy's SCALED destination texture, so its size
    // scales with both the copy's logical extent and `efb_scale_`. At the full 640x456 EFB extent, RGBA8:
    //
    //     1x ->  640x 456 ->  1.11 MB
    //     4x -> 2560x1824 -> 17.81 MB
    //     6x -> 3840x2736 -> 40.08 MB
    //
    // A 192 MiB default holds about 10 full-extent entries at 4x. Most copies are sub-rect masks and
    // bloom workspaces, which are much smaller; the full-extent figure bounds how far the budget can
    // be lowered.
    //
    // Before this bound the map only grew: only a failed allocation or shutdown removed an entry. Evict
    // the least recently used entries until the byte budget is met. A destination is a pure function of
    // its key, so an evicted entry costs at most one later re-creation; the `created` path below rebuilds
    // the resource, RTV validity and size check as a first use does. `last_used` is stamped on every copy
    // before this runs, so the entry used by this call is never a victim, and no reference to it is
    // invalidated by the erases here.
    static const std::uint64_t kEfbCopyDestBudgetBytes =
        efb_copy_dest_budget_bytes();
    EfbCopyDest& dest = efb_copy_dests_[dest_key];
    dest.last_used = ++efb_copy_dests_tick_;
    const bool created =
        !dest.texture || dest.width != w || dest.height != h;
    if (created) {
        if (dest.texture) {
            retired_efb_copy_textures_[frame_slot_].push_back(
                std::move(dest.texture));
            // The replaced texture leaves the budget even though its memory is
            // still owned by the retirement list for kFramesInFlight. Counting
            // it here would leak the budget; the retirement list is bounded by
            // time, not by this map, so the accounting stays exact.
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, dest.bytes);
        }
        dest.bytes = 0u;
        dest.rtv = {};
        dest.rtv_valid = false;
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
        std::uint64_t allocation_bytes = 0u;
        const HRESULT create_hr = efb_copy_heap_.create(
            rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, dest.texture,
            allocation_bytes);
        if (SUCCEEDED(create_hr)) {
            // A placed render target starts with undefined contents and must
            // be cleared, discarded or copied before any other use. The code
            // below expects the shader-resource state.
            command_list_->DiscardResource(dest.texture.Get(), nullptr);
            D3D12_RESOURCE_BARRIER to_srv{};
            to_srv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_srv.Transition.pResource = dest.texture.Get();
            to_srv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_srv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            to_srv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            command_list_->ResourceBarrier(1, &to_srv);
        }
        if (FAILED(create_hr)) {
            dump_device_removal(device_.Get());
            if (device_->GetDeviceRemovedReason() != S_OK) {
                std::ostringstream message;
                message << "[efb-copy] device removed while allocating a "
                        << w << 'x' << h << " copy destination";
                throw std::runtime_error(message.str());
            }
            // Out of memory: skip this copy (its texture keeps its previous
            // contents) rather than ending the game.
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, dest.bytes);
            dest.bytes = 0u;
            efb_copy_dests_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[efb-copy] dest texture allocation failed hr=0x"
                          << std::hex << static_cast<unsigned long>(create_hr)
                          << " addr=0x" << params.dest_addr << std::dec
                          << " size=" << w << 'x' << h << "; copy skipped\n";
            }
            return false;
        }
        dest.width  = w;
        dest.height = h;
        // Budget by the resource's own allocation size rather than a hand-computed
        // w*h*bpp, so the accounting follows the format if kEfbColorFormat changes.
        dest.bytes = allocation_bytes;
        efb_copy_dests_bytes_ += dest.bytes;
    }
    // Trim against the updated allocation, not the destination's old extent.
    // Shrinking must not evict reusable neighbors; check growth in this call.
    if (kEfbCopyDestBudgetBytes != 0u &&
        efb_copy_dests_bytes_ > kEfbCopyDestBudgetBytes) {
        for (;;) {
            auto victim = efb_copy_dests_.end();
            for (auto it = efb_copy_dests_.begin(); it != efb_copy_dests_.end();
                 ++it) {
                if (it->first == dest_key) {
                    continue;
                }
                if (victim == efb_copy_dests_.end() ||
                    it->second.last_used < victim->second.last_used) {
                    victim = it;
                }
            }
            if (victim == efb_copy_dests_.end()) {
                break;  // only the entry in use remains
            }
            // Hand the texture to the per-frame-slot retirement list rather than
            // releasing it here. `retired_efb_copy_textures_[slot]` is cleared at
            // the start of that slot's next begin_frame(), i.e. after
            // kFramesInFlight frames of GPU work have retired, so a command list
            // recorded this frame that still references the victim cannot read a
            // freed resource. Same retirement path the size-change case uses.
            if (victim->second.texture) {
                retired_efb_copy_textures_[frame_slot_].push_back(
                    std::move(victim->second.texture));
            }
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, victim->second.bytes);
            efb_copy_dests_.erase(victim);
            if (efb_copy_dests_bytes_ <= kEfbCopyDestBudgetBytes) {
                break;
            }
        }
    }
    if (const auto source_box = detail::efb_identity_copy_box(
            params, is_depth_copy, efb_scale_, w, h)) {
        if (replay_recording_) [[unlikely]] {
            ReplayCopy copy{};
            copy.dest = dest.texture.Get();
            copy.width = w;
            copy.height = h;
            copy.identity = true;
            copy.box = *source_box;
            replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
            replay_copies_.push_back(copy);
        }
        detail::record_efb_identity_copy(command_list_.Get(), efb_color_.Get(),
            dest.texture.Get(), *source_box);
        if (ring_mode_ == RingMode::Interp) note_copy_write(dest.texture.Get());
        ++efb_identity_copies_;
        efb_identity_payload_bytes_ += static_cast<std::uint64_t>(w) * h * 4u;
        return textures.register_efb_copy(params.dest_addr, params, dest.texture);
    }

    // This descriptor shares its slot with XFB and other texture copies;
    // destination-local validity cannot prove its current contents.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

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

    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.dest = dest.texture.Get();
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.depth = is_depth_copy;
        copy.intensity = params.intensity;
        copy.keep_alpha = keep_alpha;
        copy.format = params.target_format;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }
    record_conversion_blit(
        dest.texture.Get(), w, h, dest.rtv, params,
        is_depth_copy, params.intensity, keep_alpha,
        params.target_format,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);
    replay_recording_ = recording;
    if (ring_mode_ == RingMode::Interp) note_copy_write(dest.texture.Get());

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
        peek_allocator_ && peek_command_list_) {
        return true;
    }
    if (!device_ || !queue_ || !fence_ || !efb_color_ || !efb_depth_ ||
        !conv_pipeline_ || !s_conv_root_sig || !srv_ring_.heap() ||
        conv_efb_color_srv_.ptr == 0u || conv_efb_depth_srv_.ptr == 0u) {
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

    return true;
}

bool RendererD3D12::peek_efb(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value) {
    return read_efb_pixels(x, y, kind, value, {});
}

void RendererD3D12::create_first_use_resources() {
    (void)ensure_peek_resources();
    (void)ensure_pointer_depth_resources();
}

bool RendererD3D12::ensure_pointer_depth_resources() {
    if (pointer_depth_target_ && pointer_depth_readback_) {
        return true;
    }
    if (!ensure_peek_resources()) {
        return false;
    }
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    ComPtr<ID3D12Resource> target_candidate;
    ComPtr<ID3D12Resource> readback_candidate;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = peek_target_->GetDesc();
    td.Width = width; td.Height = height;
    d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
        &td, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
        IID_PPV_ARGS(&target_candidate)), "pointer depth target");
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = peek_readback_->GetDesc();
    rd.Width = static_cast<UINT64>(row_pitch) * height;
    std::array<ComPtr<ID3D12Resource>, kPointerDepthRing> ring;
    for (auto& readback : ring) {
        d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback)), "pointer depth readback");
    }
    readback_candidate = ring[0];
    for (std::size_t i = 0; i < kPointerDepthRing; ++i) {
        pointer_depth_ring_rgba_[i] = pin_readback_mapping(ring[i].Get());
    }
    pointer_depth_ring_ = std::move(ring);
    pointer_depth_ring_next_ = 0;
    pointer_depth_target_ = std::move(target_candidate);
    pointer_depth_readback_ = std::move(readback_candidate);
    return true;
}

void RendererD3D12::select_pointer_depth_destination() {
    pointer_depth_readback_ = pointer_depth_ring_[pointer_depth_ring_next_];
    pointer_depth_pending_rgba_ = pointer_depth_ring_rgba_[pointer_depth_ring_next_];
    pointer_depth_ring_next_ = (pointer_depth_ring_next_ + 1u) % kPointerDepthRing;
}

// Mid-stream pointer depth field: the same conversion draw and readback copy
// as the utility path, recorded into the frame's command list so the frame's
// own submission carries it. No FIFO split, no utility chunk, no wait.
bool RendererD3D12::record_pointer_depth_capture_in_frame() {
    if (pointer_depth_in_frame_pending_ || pointer_depth_capture_fence_ != 0u) {
        return false;
    }
    if (!ensure_pointer_depth_resources()) {
        return false;
    }
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    select_pointer_depth_destination();
    ID3D12Resource* const target = pointer_depth_target_.Get();
    ID3D12Resource* const readback = pointer_depth_readback_.Get();
    ID3D12Resource* const src = efb_depth_.Get();

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(2, barriers);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = allocate_conversion_rtv(target);
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    command_list_->SetPipelineState(conv_pipeline_.Get());
    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, EfbPeekKind::Depth);
    constants.values[15] = -1.0f;
    command_list_->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, conv_efb_depth_srv_);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    command_list_->ResourceBarrier(2, barriers);

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
    command_list_->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);

    // Restore the GX drawing state exactly as record_conversion_blit does.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
    pointer_depth_in_frame_pending_ = true;
    return true;
}

bool RendererD3D12::capture_pointer_depth(std::span<std::uint32_t> pixels) {
    if (pixels.size() != kEfbWidth * kEfbHeight) {
        throw std::invalid_argument("pointer depth field requires 640x528 pixels");
    }
    std::uint32_t unused{};
    return read_efb_pixels(0u, 0u, EfbPeekKind::Depth, unused, pixels);
}

std::uint64_t RendererD3D12::submit_pointer_depth_capture() {
    std::uint32_t unused{};
    if (!read_efb_pixels(0u, 0u, EfbPeekKind::Depth, unused, {}, true)) {
        return 0u;
    }
    return pointer_depth_capture_fence_;
}

bool RendererD3D12::try_complete_pointer_depth_capture_in_place(
    const std::uint8_t*& rgba) {
    rgba = nullptr;
    if (pointer_depth_capture_fence_ == 0u) {
        if (pointer_depth_in_frame_pending_) {
            return false;  // recorded in the open frame; fenced at end_frame
        }
        throw std::logic_error("pointer depth completion has no owned fence");
    }
    if (completed_fence_value() < pointer_depth_capture_fence_) {
        return false;
    }
    rgba = pointer_depth_pending_rgba_;
    pointer_depth_capture_fence_ = 0u;
    return true;
}

bool RendererD3D12::try_complete_pointer_depth_capture(
    std::span<std::uint32_t> pixels) {
    if (pixels.size() != kEfbWidth * kEfbHeight) {
        throw std::invalid_argument("pointer depth field requires 640x528 pixels");
    }
    if (pointer_depth_capture_fence_ == 0u) {
        if (pointer_depth_in_frame_pending_) {
            return false;  // recorded in the open frame; fenced at end_frame
        }
        throw std::logic_error("pointer depth completion has no owned fence");
    }
    if (completed_fence_value() < pointer_depth_capture_fence_) {
        return false;
    }
    std::uint32_t unused{};
    copy_efb_readback_pixels(pointer_depth_readback_.Get(), kEfbWidth,
        kEfbHeight, kEfbWidth * 4u, true, unused, pixels);
    // Only release utility resources after the entire exact field is copied.
    pointer_depth_capture_fence_ = 0u;
    return true;
}

bool RendererD3D12::ensure_async_peek_field_resources(
    AsyncPeekField& field, EfbPeekKind kind) {
    if (field.allocator && field.command_list && field.target &&
        field.readbacks[0] && field.rtv_heap) {
        return true;
    }
    // The conversion pipeline, root signature and EFB views are shared with
    // the synchronous peek path; everything else here is private.
    if (!ensure_peek_resources()) {
        return false;
    }
    const bool is_depth = kind == EfbPeekKind::Depth;
    d3d_check(
        device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&field.allocator)),
        "async peek field command allocator");
    d3d_check(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            field.allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&field.command_list)),
        "async peek field command list");
    d3d_check(field.command_list->Close(), "async peek field list close");
    field.command_list->SetName(
        is_depth ? L"GxAsyncDepthFieldList" : L"GxAsyncColorFieldList");

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = kEfbWidth;
    target_desc.Height = kEfbHeight;
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
            IID_PPV_ARGS(&field.target)),
        "async peek field target");

    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = static_cast<UINT64>(kEfbWidth) * 4u * kEfbHeight;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (std::size_t slot = 0; slot < kAsyncPeekFieldSlots; ++slot) {
        d3d_check(
            device_->CreateCommittedResource(
                &readback_heap,
                D3D12_HEAP_FLAG_NONE,
                &readback_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&field.readbacks[slot])),
            "async peek field readback");
        field.rgba[slot] = pin_readback_mapping(field.readbacks[slot].Get());
        if (field.rgba[slot] == nullptr) {
            throw std::runtime_error("async peek field readback could not be mapped");
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    d3d_check(
        device_->CreateDescriptorHeap(
            &rtv_heap_desc, IID_PPV_ARGS(&field.rtv_heap)),
        "async peek field RTV heap");
    field.rtv = field.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(field.target.Get(), nullptr, field.rtv);
    return true;
}

std::uint64_t RendererD3D12::submit_async_peek_field(EfbPeekKind kind, std::size_t slot) {
    AsyncPeekField& field = async_peek_fields_[async_peek_field_index(kind)];
    if (field.fence != 0u || slot >= kAsyncPeekFieldSlots) {
        return 0u;
    }
    if (!ensure_async_peek_field_resources(field, kind)) {
        return 0u;
    }
    const bool is_depth = kind == EfbPeekKind::Depth;
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    static_assert(row_pitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0u);
    ID3D12Resource* const target = field.target.Get();
    ID3D12Resource* const readback = field.readbacks[slot].Get();
    ID3D12Resource* const src = is_depth ? efb_depth_.Get() : efb_color_.Get();
    ID3D12GraphicsCommandList* const list = field.command_list.Get();

    d3d_check(field.allocator->Reset(), "async peek field allocator reset");
    d3d_check(
        list->Reset(field.allocator.Get(), nullptr),
        "async peek field command list reset");

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore =
        is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                 : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(2, barriers);

    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap()};
    list->SetDescriptorHeaps(1, heaps);
    list->OMSetRenderTargets(1, &field.rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(s_conv_root_sig.Get());
    list->SetPipelineState(conv_pipeline_.Get());
    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, kind);
    constants.values[15] = -1.0f;
    list->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
    list->SetGraphicsRootDescriptorTable(
        1, is_depth ? conv_efb_depth_srv_ : conv_efb_color_srv_);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    list->ResourceBarrier(2, barriers);

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
    list->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);

    d3d_check(list->Close(), "async peek field command list close");
    ID3D12CommandList* lists[] = {list};
    queue_->ExecuteCommandLists(1, lists);
    const std::uint64_t fence_value = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "async peek field queue signal");
    }
    field.fence = fence_value;
    return fence_value;
}

bool RendererD3D12::record_async_peek_field_in_frame(EfbPeekKind kind, std::size_t slot) {
    AsyncPeekField& field = async_peek_fields_[async_peek_field_index(kind)];
    if (field.fence != 0u || field.in_frame || slot >= kAsyncPeekFieldSlots || !command_list_) {
        return false;
    }
    if (!ensure_async_peek_field_resources(field, kind)) {
        return false;
    }
    const bool is_depth = kind == EfbPeekKind::Depth;
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    ID3D12Resource* const target = field.target.Get();
    ID3D12Resource* const readback = field.readbacks[slot].Get();
    ID3D12Resource* const src = is_depth ? efb_depth_.Get() : efb_color_.Get();

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore =
        is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(2, barriers);

    command_list_->OMSetRenderTargets(1, &field.rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    command_list_->SetPipelineState(conv_pipeline_.Get());
    const EfbPeekSamplingGeometry sampling = compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_);
    EfbConversionShaderConstants constants = make_efb_peek_conversion_constants(sampling, kind);
    constants.values[15] = -1.0f;
    command_list_->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, is_depth ? conv_efb_depth_srv_ : conv_efb_color_srv_);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    command_list_->ResourceBarrier(2, barriers);

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
    command_list_->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);

    // Restore the GX drawing state exactly as record_conversion_blit does.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
    field.in_frame = true;
    return true;
}

bool RendererD3D12::try_complete_async_peek_field(EfbPeekKind kind) {
    AsyncPeekField& field = async_peek_fields_[async_peek_field_index(kind)];
    if (field.fence == 0u) {
        return false;
    }
    if (completed_fence_value() < field.fence) {
        return false;
    }
    field.fence = 0u;
    return true;
}

const std::uint8_t* RendererD3D12::async_peek_field_rgba(
    EfbPeekKind kind, std::size_t slot) const noexcept {
    return slot < kAsyncPeekFieldSlots
        ? async_peek_fields_[async_peek_field_index(kind)].rgba[slot] : nullptr;
}

bool RendererD3D12::read_efb_pixels(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
    std::uint32_t& value, std::span<std::uint32_t> depth_pixels,
    bool defer_pointer_depth) {
    value = 0;
    if (pointer_depth_capture_fence_ != 0u) {
        throw std::logic_error("GXPeek resources still own an unpublished pointer field");
    }
    if (x >= kEfbWidth || y >= kEfbHeight || !ensure_peek_resources()) {
        return false;
    }

    const bool field = defer_pointer_depth || !depth_pixels.empty();
    const UINT width = field ? kEfbWidth : 1u;
    const UINT height = field ? kEfbHeight : 1u;
    const UINT row_pitch = field ? kEfbWidth * 4u : peek_readback_row_pitch_;
    if (field && !ensure_pointer_depth_resources()) {
        return false;
    }
    if (field) {
        select_pointer_depth_destination();
    }
    ID3D12Resource* target = field ? pointer_depth_target_.Get() : peek_target_.Get();
    ID3D12Resource* readback = field ? pointer_depth_readback_.Get() : peek_readback_.Get();
    // All preceding uses of these private utility descriptors have completed
    // their exact fence before the next utility can enter on the render owner.
    device_->CreateRenderTargetView(target, nullptr, peek_rtv_);
    const bool is_depth = kind == EfbPeekKind::Depth;
    ID3D12Resource* src = is_depth ? efb_depth_.Get() : efb_color_.Get();

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

    // Utility and GX lists use the same heap and immutable EFB source views.
    // Frame-ring resets cannot touch them, including during a deferred field.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap()};
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
    peek_command_list_->SetGraphicsRootDescriptorTable(1,
        is_depth ? conv_efb_depth_srv_ : conv_efb_color_srv_);
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
    if (defer_pointer_depth) {
        pointer_depth_capture_fence_ = fence_value;
        return true;
    }
    wait_for_fence_value(fence_value);

    copy_efb_readback_pixels(readback, width, height, row_pitch, is_depth,
        value, depth_pixels);
    return true;
}

// A readback heap may stay mapped while the GPU writes it (D3D12 leaves the
// synchronization to the fences the callers already wait on). The reference
// taken here is never released; the resource's destruction ends the mapping.
const std::uint8_t* RendererD3D12::pin_readback_mapping(ID3D12Resource* readback) {
    if (readback == nullptr) return nullptr;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(readback->Map(0, &no_read, &mapped))) return nullptr;
    return static_cast<const std::uint8_t*>(mapped);
}

void RendererD3D12::copy_efb_readback_pixels(
    ID3D12Resource* readback, UINT width, UINT height, UINT row_pitch,
    bool is_depth, std::uint32_t& value,
    std::span<std::uint32_t> depth_pixels) {
    void* mapped = nullptr;
    const D3D12_RANGE read_range{
        0, static_cast<SIZE_T>(row_pitch) * height};
    d3d_check(
        readback->Map(0, &read_range, &mapped),
        "GXPeek readback map");
    const auto* rgba = static_cast<const std::uint8_t*>(mapped);
    if (!depth_pixels.empty()) {
        // RGBA8 bytes to 0x00RRGGBB (depth) or 0xAARRGGBB (colour): a byte shuffle per four pixels.
        // The full 640x528 field is converted for every published peek field and pointer-depth capture.
        const __m128i order = is_depth
            ? _mm_setr_epi8(2, 1, 0, -128, 6, 5, 4, -128, 10, 9, 8, -128, 14, 13, 12, -128)
            : _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
        for (std::size_t row = 0; row < height; ++row) {
            const std::uint8_t* source = rgba + row * row_pitch;
            std::uint32_t* destination = depth_pixels.data() + row * width;
            std::size_t col = 0;
            for (; col + 4u <= width; col += 4u) {
                const __m128i quad = _mm_loadu_si128(
                    reinterpret_cast<const __m128i*>(source + col * 4u));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + col),
                                 _mm_shuffle_epi8(quad, order));
            }
            for (; col < width; ++col) {
                const std::uint8_t* pixel = source + col * 4u;
                const std::uint32_t rgb =
                    (static_cast<std::uint32_t>(pixel[0]) << 16u) |
                    (static_cast<std::uint32_t>(pixel[1]) << 8u) | pixel[2];
                destination[col] = is_depth
                    ? rgb
                    : (static_cast<std::uint32_t>(pixel[3]) << 24u) | rgb;
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
            std::cerr << '[' << it->tag << "] failed path=" << it->path
                      << " error=" << error.what() << '\n';
            it = debug_ppm_writers_.erase(it);
            continue;
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
        std::cerr << '[' << tag << "] skipped=writer-queue-full path="
                  << path << '\n';
        return;
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

bool RendererD3D12::debug_copy_efb_to_readback() {
    debug_log_readback();
    if (readback_pending_) {
        std::cerr << "[efb-dump] skipped=prior-readback-pending path="
                  << next_efb_capture_path_ << '\n';
        return false;
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
            std::cerr << "[efb-dump] skipped=readback-allocation-failed path="
                      << next_efb_capture_path_ << '\n';
            return false;
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
    return true;
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
    bool mapped_active = true;
    const D3D12_RANGE no_write{0, 0};
    const ScopeExit unmap_readback([&]() noexcept {
        if (mapped_active) readback_buf_->Unmap(0, &no_write);
    });
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

    readback_buf_->Unmap(0, &no_write);
    mapped_active = false;
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
        std::uint64_t bitmap = 0u;
        for (unsigned row = 0; row < 7u; ++row) {
            for (unsigned col = 0; col < 5u; ++col) {
                if ((rows[row] & (1u << (4u - col))) != 0u) {
                    bitmap |= UINT64_C(1) << (row * 5u + col);
                }
            }
        }
        renderer.draw_ui_glyph(cursor_x, y, scale, bitmap, color);
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

std::string upper_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return text;
}

std::string native_settings_row_text(
    unsigned tab, unsigned row,
    const RenderConfig& render, const RuntimeSettings& runtime,
    const RenderFeatureCaps& caps) {
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
                "AUTO", "4:3", "16:10", "16:9", "21:9", "32:9"};
            return std::string("ASPECT RATIO: ") +
                   kAspects[std::min(render.aspect_mode, kAspectModeCount - 1u)];
        }
        case 5: {
            // FSR 3, DLSS and the bilinear/FSR 1 quality modes set the render resolution.
            if (temporal_upscaler(render) != 0u || spatial_quality_mode(render)) {
                static constexpr const char* kDriver[] = {"QUALITY", "FSR 1", "FSR 3", "DLSS"};
                std::snprintf(text, sizeof(text), "INTERNAL RES: BY %s",
                              kDriver[std::min(render.upscaler, 3u)]);
                return text;
            }
            if (render.internal_follow_window) return "INTERNAL RES: MATCH OUTPUT";
            const unsigned scale = std::clamp(render.efb_scale, 1u, kMaxEfbScale);
            std::snprintf(text, sizeof(text), "INTERNAL RES: %uX %uX%u%s", scale,
                          kEfbWidth * scale, kEfbHeight * scale,
                          scale >= settings_scale_cap(render) ? " MAX" : "");
            return text;
        }
        case 6: {
            const auto rate = static_cast<unsigned>(std::lround(std::max(0.0f, render.max_fps)));
            const double measured = s_measured_present_hz.load(std::memory_order_relaxed);
            // A temporal upscaler interpolates only at multiples of 60.
            const unsigned temporal = temporal_upscaler(render);
            const bool above_60 = rate == 0u ? settings_refresh_hz(render) > 61u : rate > 61u;
            const double present = present_frame_rate(render.max_fps, render.vsync_interval,
                                                      static_cast<double>(settings_refresh_hz(render)));
            const bool multiple = std::abs(present - 60.0 * std::round(present / 60.0)) <= 0.5;
            const bool capped = temporal != 0u && above_60 && (!temporal_interpolation_allowed() || !multiple);
            const bool gpu_bound = temporal != 0u && above_60 &&
                                   s_temporal_interp_suspended.load(std::memory_order_relaxed);
            const char* cap = gpu_bound ? " (60: GPU)" : !capped ? "" : temporal == 3u ? " (60: DLSS)" : " (60: FSR 3)";
            if (rate == 0u) {
                std::snprintf(text, sizeof(text), "FRAME RATE: UNLIMITED%s  %.1f", cap, measured);
            } else {
                std::snprintf(text, sizeof(text), "FRAME RATE: %u%s  %.1f", rate, cap, measured);
            }
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
            static constexpr const char* kUpscalers[] = {"BILINEAR", "FSR 1", "FSR 3", "DLSS"};
            const std::vector<unsigned> choices = available_upscalers();
            const bool offered = std::find(choices.begin(), choices.end(), render.upscaler) != choices.end();
            std::string upscaler = std::string("UPSCALER: ") + kUpscalers[offered ? std::min(render.upscaler, 3u) : 1u];
            if (offered && render.upscaler == 3u && !streamline::dlss_supported()) {
                upscaler += dlss_installed_now() ? " (AFTER RESTART)" : " (DOWNLOAD)";
            }
            return upscaler;
        }
        case 3: {
            static constexpr const char* kTemporal[] = {
                "NATIVE AA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
            static constexpr const char* kDlss[] = {
                "DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
            static constexpr const char* kSpatial[] = {
                "MANUAL", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
            const unsigned quality = render.upscaler >= 2u ? std::min(render.temporal_quality, 4u)
                                                           : std::min(render.spatial_quality, 4u);
            switch (std::min(render.upscaler, 3u)) {
            case 3u: return std::string("DLSS: ") + kDlss[quality];
            case 2u: return std::string("FSR 3 QUALITY: ") + kTemporal[quality];
            case 1u: return std::string("FSR 1 QUALITY: ") + kSpatial[quality];
            default: return std::string("QUALITY: ") + kSpatial[quality];
            }
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
        case 9:
            return std::string("RAY TRACING: ") + (render.ray_tracing_enabled ? "ON" : "OFF");
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
        case 6: return std::string("WIIMOTE SOUNDS: ") + percent_text(runtime.audio.wiimote_speaker);
        default: return {};
        }
    case 4:
        if (row == 0u) {
            std::snprintf(text, sizeof(text), "TEXT SPEED: %uX", std::clamp(runtime.text_speed, 1u, 3u));
            return text;
        }
        return {};
    case 5:
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
        case 5:
            return post_process_caps_text(get_post_process_status());
        default:
            return {};
        }
    default:
        return {};
    }
}

void draw_native_settings_overlay(RendererD3D12& renderer) {
    poll_dlss_download();
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
        "SETTINGS",
        text);

    const char* tabs[] = {"DISPLAY", "GRAPHICS", "CONTROLS", "AUDIO", "GENERAL", "CAPS"};
    static_assert(std::size(tabs) == kNativeSettingsTabCount);
    int tab_x = panel_x + 28;
    // The labels share the panel width; the longest is eight 6-unit glyphs
    // plus a gap, so the label scale shrinks on narrow panels.
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
    expire_display_confirmation();
    expire_controls_capture();
    const RenderConfig row_render = s_settings_draft;
    const RuntimeSettings row_runtime = get_runtime_settings();
    const RenderFeatureCaps row_caps = get_render_feature_caps();
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
            native_settings_row_text(tab, row, row_render, row_runtime, row_caps),
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
    } else if (bool dlss_error = false; !(status = dlss_footer(now_ms, dlss_error)).empty()) {
        status_color = dlss_error ? error : accent;
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

// For the first seconds of a session, until the overlay is first opened, a
// one-line hint names the keys that open it.
void draw_native_settings_hint(RendererD3D12& renderer) {
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
    if (s_bb_width < 427u || s_bb_height < 240u) {
        return;
    }
    static constexpr const char* kHint = "F1, VIEW+MENU OR MINUS+PLUS: SETTINGS";
    const int scale = s_bb_height >= 1440u ? 3 : (s_bb_height >= 720u ? 2 : 1);
    int text_w = -scale;
    for (const char* ch = kHint; *ch != '\0'; ++ch) {
        text_w += (*ch == ' ' ? 4 : 6) * scale;
    }
    const int pad = 4 * scale;
    const int x = 8 * scale;
    const int y = static_cast<int>(s_bb_height) - 8 * scale - 7 * scale - pad * 2;
    ui_rect(renderer, x, y, text_w + pad * 2, 7 * scale + pad * 2,
            {0.02f, 0.025f, 0.035f, 1.0f});
    ui_text(renderer, x + pad, y + pad, scale, kHint,
            {0.88f, 0.94f, 0.96f, 1.0f});
}

bool RendererD3D12::present(
    const XfbTexture* source, bool capture_backbuffer) {
    // Diagnostic stall watch, gated on its own opt-in flag. GALAXY_TRACE_STALL_WATCH is unset in the
    // shipped runtime-env.json, and trace_stall_watch_enabled() holds a function-local
    // `static const bool`, so with the flag off this folds to a constant false and clang deletes every
    // timestamp, subtraction and branch guarded by it in this function.
    const bool stall_watch = trace_stall_watch_enabled();
    // Optional diagnostic: measure the wall-clock gap
    // between successive present() *entries*, independent of the
    // trace_present_stats_enabled()-gated telemetry below. Everything that
    // runs between two present() calls -- the OS message pump, checkpoint
    // servicing, guest simulation -- lands in this gap, so it catches a
    // stall the begin_frame/Present/overlay-draw-local stall-watch probes
    // miss entirely because it happens *outside* all of them.
    if (stall_watch) {
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
        if (capture_waits_) {
            // Sequence captures: every present is captured, so the previous
            // readback is waited for instead of skipping this one.
            if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ != 0u) {
                wait_for_fence_value(selected_xfb_readback_fence_);
            }
            if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ != 0u) {
                wait_for_fence_value(backbuffer_readback_fence_value_);
            }
        }
        debug_log_backbuffer_readback();
        debug_log_selected_xfb_readback();
        if (backbuffer_readback_pending_ || selected_xfb_readback_pending_ ||
            next_backbuffer_capture_path_.empty()) {
            std::cerr << "[backbuffer-dump] skipped="
                      << (next_backbuffer_capture_path_.empty()
                              ? "no-output-path" : "prior-readback-pending")
                      << " path=" << next_backbuffer_capture_path_ << '\n';
            capture_backbuffer = false;
        } else {
            capture_backbuffer = debug_copy_selected_xfb_to_readback(*source);
        }
    }

    const auto ultrawide =
        galaxy::experimental_effective_host_aspect();
    // A frame copied before a live internal-resolution change is still a
    // full-width copy at the scale it was made with.
    const unsigned source_scale = source->width / kEfbWidth;
    if (ultrawide &&
        (source_scale == 0u ||
         source->width != kEfbWidth * source_scale ||
         source->height == 0u ||
         source->height > kEfbHeight * source_scale)) {
        throw std::runtime_error(
            "RMGE01 ultrawide aspect requires a full-width Wii XFB copy");
    }

    const std::uint64_t present_frame = frame_index_ + 1u;
    bool trace_luma = trace_present_luma_frame(present_frame);
    if (trace_luma) {
        debug_log_backbuffer_readback();
        if (backbuffer_readback_pending_) {
            std::cerr << "[present-luma] skipped=prior-readback-pending frame="
                      << present_frame << '\n';
            trace_luma = false;
        }
    }
    const bool record_backbuffer = capture_backbuffer || trace_luma;

    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    const UINT rtv_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * rtv_stride;

    // Transition backbuffer -> RENDER_TARGET.
    D3D12_RESOURCE_BARRIER bar{};
    bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource   = backbuffers_[buf_idx].Get();
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &bar);

    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
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
    if (post_process_.ready()) {
        const D3D12_RESOURCE_DESC source_desc = source->texture->GetDesc();
        post_process_.record(
            command_list_.Get(), srv_ring_, source->texture.Get(),
            static_cast<unsigned>(source_desc.Width), source_desc.Height,
            back_rtv, vp, frame_index_, kFramesInFlight);
        // The overlay reads a copy: publish when what it shows changes.
        const PostProcessStatus& post = post_process_.status();
        static PostProcessStatus published{};
        if (post.last_run != published.last_run || post.last_wanted != published.last_wanted ||
            post.last_supersampled != published.last_supersampled || post.live_aa != published.live_aa ||
            post.live_quality != published.live_quality || published.frames == 0u ||
            post.aa_skipped_frames != published.aa_skipped_frames) {
            published = post;
            set_post_process_status(post);
        }
    } else {
        // One-slot SRV in the frame's ring for the XFB texture.
        const DescriptorRing::Table tbl = srv_ring_.allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels       = 1;
        device_->CreateShaderResourceView(source->texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
        command_list_->RSSetViewports(1, &vp);
        command_list_->RSSetScissorRects(1, &sci);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }
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
        // Gated on the same opt-in flag as the other stall-watch probes; the
        // flag's function-local static makes this a constant false in the
        // shipping configuration, so the two clock reads and the branch are
        // deleted rather than merely skipped.
        const bool stall_watch_overlay = trace_stall_watch_enabled();
        const auto stall_watch_overlay_start = stall_watch_overlay
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        draw_native_settings_overlay(*this);
        draw_native_settings_hint(*this);
        if (stall_watch_overlay) {
            const double stall_watch_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() -
                    stall_watch_overlay_start).count();
            if (stall_watch_ms > 4.0) {
                std::ostringstream message;
                message << "[stall-watch] draw_native_settings_overlay took "
                          << stall_watch_ms << "ms\n";
                std::cerr << message.str();
            }
        }
    }

    // Transition backbuffer -> PRESENT.
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
    return capture_backbuffer;
}

// Extend the existing bounded capture with the actual selected source pixels.
// The copy and final blit share a command list and exact completion fence.
// Retain the source until completion; never wait here or change VI selection.
bool RendererD3D12::debug_copy_selected_xfb_to_readback(const XfbTexture& source) {
    debug_log_selected_xfb_readback();
    if (selected_xfb_readback_pending_ || next_backbuffer_capture_path_.empty()) {
        std::cerr << "[selected-xfb-capture] skipped="
                  << (next_backbuffer_capture_path_.empty()
                          ? "no-output-path" : "prior-readback-pending")
                  << " path=" << next_backbuffer_capture_path_ << '\n';
        return false;
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
    return true;
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

    bool mapped_active = true;
    const D3D12_RANGE no_write{0, 0};
    const ScopeExit unmap_readback([&]() noexcept {
        if (mapped_active) backbuffer_readback_buf_->Unmap(0, &no_write);
    });
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

    backbuffer_readback_buf_->Unmap(0, &no_write);
    mapped_active = false;
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

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const HWND window = reinterpret_cast<HWND>(
        s_native_settings_window.load(std::memory_order_acquire));
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
