#pragma once

#include "galaxy/gx/frame_completion.h"
#include "galaxy/gx/pointer_response.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/native_api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>

namespace galaxy::gx {

inline constexpr std::uint32_t kUnknownDisplayedXfbAddr = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoDisplayedXfbAddr = 0xFFFFFFFEu;

struct HostPointerState {
    bool window_focused = false;
    bool inside_client = false;
    bool absolute_valid = false;
    bool left_button = false;
    bool right_button = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    std::uint64_t absolute_sequence = 0;
    // GetTickCount64 milliseconds of the coherent absolute-position
    // publication identified by absolute_sequence. This is a host clock, not
    // the Wii time-base used by HID report deadlines.
    std::uint64_t absolute_acquired_ms = 0;
    std::uint32_t debug_flags = 0;
    std::uint32_t debug_error = 0;
    std::uintptr_t debug_window = 0;
};

struct HostClientExtent {
    int width = 0;
    int height = 0;
};

// Read the actual native client area independently of mouse movement/focus.
[[nodiscard]] HostClientExtent current_host_client_extent() noexcept;

enum class EfbPeekKind : std::uint8_t {
    Color,
    Depth,
};

// Create the Win32 window and initialize D3D12 device, swap chain, command
// queue, root signature, PSO, and upload buffers.  Returns false on failure.
bool initialize(int width, int height);

// Records the game dump's content root (GamePaths::content_root) so the
// native settings overlay can read read-only UI assets (see
// galaxy/gx/game_asset_extract.h) straight from the user's own game files
// to style itself with the game's own art. Call once, before the overlay
// is ever shown; purely cosmetic -- unset or wrong just means the overlay
// keeps its solid-color fallback.
void set_native_settings_content_root(const std::string& utf8_path);

// Parse accumulated GX FIFO command bytes, issue D3D12 draw calls, and
// optionally present. An empty capture submits no GX work and may only request
// a cached-XFB present. memory/services resolve display-list guest addresses
// and indexed vertex-array reads. A non-empty capture returns one monotonic
// completion token; an empty capture returns an invalid token. An exact FIFO
// preclassification makes event-free receipts immediately ready, while
// PE-bearing tokens retain exact fence/callback completion semantics.
[[nodiscard]] FramePeCompletionToken render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    bool present_swap_chain = true,
    std::uint32_t displayed_xfb_addr = 0);

// Wait until the render queue and in-flight render chunks are fully idle.
void wait_for_render_idle();

// Wait only until queued native render chunks have reached their FIFO callbacks.
// Guest-visible GX synchronization points such as GXWaitDrawDone use this so
// PE finish/token cannot be observed before the native XFB copy exists.
void wait_for_render_fifo_effects_idle();

// Consume exactly one FIFO capture receipt. Event-free captures do not block
// the simulation on renderer submission; PE-bearing captures wait for their
// exact PE-visible completion boundary. Unlike wait_for_render_fifo_effects_idle(),
// this does not drain later render or present chunks. Tokens are monotonic
// within a renderer epoch and must be consumed exactly once.
void wait_for_frame_pe_completion(FramePeCompletionToken token);

// Wait for at most `timeout_ms` for this exact token. Returns true and consumes
// it exactly once on completion; returns false without consuming it when the
// slice expires. Renderer failures, stale tokens, and shutdown still hard-fail.
// A zero timeout is a nonblocking probe.
[[nodiscard]] bool wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms);

// Synchronously reads the native EFB at Wii EFB coordinates. This is the
// device boundary used by GXPeekARGB/GXPeekZ; callers must pass untranslated
// 640x528 EFB coordinates.
// One native-resolution depth field at the existing pointer PE boundary.
// The caller owns the output until this render utility completes.
[[nodiscard]] bool capture_pointer_depth(std::span<std::uint32_t> pixels);
    [[nodiscard]] bool enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture);
void set_pointer_response(PointerResponseStamp stamp) noexcept;
// Simulation producer; copied into each owned FIFO chunk and selected XFB.
void set_prerecorded_movie_content(bool movie) noexcept;
void set_safety_scene_surround(float intensity) noexcept;

[[nodiscard]] bool peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value);

// Present an already-rendered XFB after the guest VI callback has selected
// the displayed buffer. This keeps the host swap-chain cadence tied to the
// game's own XFB exchange instead of to the pre-VI FIFO copy point.
void present_cached_xfb(
    std::uint32_t displayed_xfb_addr,
    bool present_swap_chain = true);

// Parse newly produced GX FIFO bytes on the simulation thread only for PE
// finish/token events. This does not render; it raises native hardware
// interrupt latches through NativeServicesV1 so GXWaitDrawDone/GXSetDrawSync
// observe completion at the CPU-visible FIFO boundary.
void scan_pe_events(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services);

void install_guest_dirty_tracker(GuestMemoryV1* memory) noexcept;

void notify_guest_memory_write(
    std::uint32_t address,
    std::uint32_t size) noexcept;

// Pump Win32 messages; call periodically from the guest execution loop.
void pump_messages();
[[nodiscard]] bool quit_requested() noexcept;

// Polls the latest host pointer state for the runtime window. Normal play uses
// client-area mouse messages; frame-time OS cursor polling is opt-in because it
// can trail live WM_MOUSEMOVE coordinates on Remote Desktop.
[[nodiscard]] HostPointerState poll_host_pointer_state() noexcept;
[[nodiscard]] host::HostPointerTransitionPoll poll_host_pointer_transition(
    std::uint64_t after_sequence) noexcept;

// Cold native settings overlay, drawn after XFB blit and before Present. F1 is
// deliberately global while the native window owns focus; all mutations enter
// through the renderer window thread's key-message path.
struct NativeSettingsOverlaySnapshot {
    bool visible = false;
    bool dirty = false;
    bool save_error = false;
    unsigned tab = 0;
    unsigned row = 0;
    unsigned tab_count = 0;
    unsigned row_count = 0;
};

struct NativeSettingsLayout {
    bool supported = false;
    int panel_x = 0;
    int panel_y = 0;
    int panel_width = 0;
    int panel_height = 0;
    int text_scale = 0;
    int rows_y = 0;
    int row_step = 0;
    int footer_y = 0;
};

struct XfbPresentStats {
    std::uint64_t requests = 0;
    std::uint64_t presented = 0;
    // Game-timed present requests only: auxiliary frame-rate-decoupling
    // re-presents are deliberately excluded. This is serial identity, not
    // pixel/content identity. A monotonically newer XfbTexture::copy_serial is
    // counted once on its first game-timed presentation; replaying an older
    // A/B pair cannot make this counter advance.
    std::uint64_t first_serial_presentations = 0;
    std::uint64_t misses = 0;
    std::uint64_t stale_fallbacks = 0;
    std::uint64_t missing_fallbacks = 0;
    std::uint64_t stale_preserved = 0;
    std::uint64_t missing_preserved = 0;
    std::uint64_t repeated_or_stale_serial_requests = 0;
    std::uint64_t skipped_duplicate_presents = 0;
    std::uint64_t duplicate_run_max = 0;
    std::uint64_t out_of_order_serial_requests = 0;
    std::uint64_t serial_delta_zero = 0;
    std::uint64_t serial_delta_one = 0;
    std::uint64_t serial_delta_multi = 0;
    std::uint64_t highest_first_presented_copy_serial = 0;
    std::uint64_t last_presented_frame_stamp = 0;
    std::uint64_t last_presented_copy_serial = 0;
    std::uint32_t last_presented_xfb_addr = 0;
};

// Inter-arrival timing is sampled with std::chrono::steady_clock on the render
// thread. These are monotonic timing facts about command/presentation cadence;
// they do not inspect or hash XFB pixels.
struct XfbMonotonicCadenceStats {
    std::uint64_t samples = 0;
    std::uint64_t intervals = 0;
    std::uint64_t total_interval_ns = 0;
    std::uint64_t min_interval_ns = 0;
    std::uint64_t max_interval_ns = 0;
    std::uint64_t first_sample_ns = 0;
    std::uint64_t last_sample_ns = 0;
    // Exact nearest-rank percentiles of the retained interval samples.
    // A measurement that exceeds the fixed retention bound is explicitly
    // marked instead of silently reporting an approximate percentile.
    std::uint64_t p99_interval_ns = 0;
    std::uint64_t retained_interval_samples = 0;
    bool interval_samples_overflowed = false;
    std::uint64_t p95_interval_ns = 0;
};

struct XfbMeasurementCadenceStats {
    XfbMonotonicCadenceStats copy_production{};
    XfbMonotonicCadenceStats first_serial_presentation{};
};

[[nodiscard]] bool native_settings_overlay_visible() noexcept;
[[nodiscard]] bool native_window_input_captured() noexcept;
[[nodiscard]] NativeSettingsOverlaySnapshot
native_settings_overlay_snapshot() noexcept;
[[nodiscard]] NativeSettingsLayout compute_native_settings_layout(
    unsigned backbuffer_width,
    unsigned backbuffer_height,
    unsigned tab) noexcept;
// `was_down` is Win32 LPARAM bit 30. Repeated F1 messages are consumed without
// toggling so keyboard autorepeat cannot flap the overlay.
[[nodiscard]] bool native_settings_handle_window_key(
    std::uintptr_t virtual_key,
    bool was_down);
void native_settings_reset_overlay_state_for_tests() noexcept;

// Number of EFB display copies submitted to XFB textures by the active backend.
[[nodiscard]] std::uint64_t xfb_copy_count() noexcept;
// Opt-in causal-trace only. This identifies the last completed renderer frame
// while GALAXY_TRACE_XFB_CAUSAL is enabled, so a bounded simulation-side XFB
// state trace can select the same frame window without guessing an offset.
[[nodiscard]] std::uint64_t xfb_causal_trace_frame_index() noexcept;
[[nodiscard]] XfbPresentStats xfb_present_stats() noexcept;
[[nodiscard]] XfbMeasurementCadenceStats
xfb_measurement_cadence_stats() noexcept;
// The caller must first wait_for_render_idle(); this defines an uncontaminated
// measurement boundary and makes the render-thread cadence reset race-free.
void reset_xfb_measurement_window_stats() noexcept;

// Release all D3D12 resources and destroy the window.
void shutdown();

}  // namespace galaxy::gx
