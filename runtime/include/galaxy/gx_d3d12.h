#pragma once

#include "galaxy/gx/frame_completion.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/pointer_response.h"
#include "galaxy/controller_roster.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/guest_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    // GetTickCount64 milliseconds of the coherent absolute-position publication identified by
    // absolute_sequence. This is a host clock, not the Wii time base used by HID report deadlines.
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

// The picture shape selected by the aspect setting, latched each video frame: the window's client
// extent limited to 4:3..32:9, or the chosen ratio.
[[nodiscard]] HostClientExtent aspect_latch_extent() noexcept;

// Guest scene boundaries the simulation thread marks in the GX stream:
// GameScene::draw3D's entry and its closing MR::clearZBuffer call; the 2D
// layer, GameScene::draw2D's entry and epilogue (layouts, camera cover,
// pause, 2D models, wipes).
enum class FifoMarker : std::uint8_t {
    SceneBegin,
    SceneEnd,
    Scene2DBegin,
    Scene2DEnd,
};

enum class EfbPeekKind : std::uint8_t {
    Color,
    Depth,
};

// Creates the Win32 window and the D3D12 device, swap chain, command queue, root signature, PSO and
// upload buffers. Returns false on failure.
bool initialize(int width, int height);
// Why initialize failed, when the renderer knows (else empty): the player's error message.
std::string startup_failure();

// Records the game dump's content root (GamePaths::content_root). The native settings overlay reads
// read-only UI assets from it (see galaxy/gx/game_asset_extract.h), so it can use the game's own art.
// Call once, before the overlay is first shown. If unset or wrong, the overlay keeps its solid-color fallback.
void set_native_settings_content_root(const std::string& utf8_path);

// Parses accumulated GX FIFO bytes, issues D3D12 draw calls, and optionally presents. An empty capture
// submits no GX work and may only request a cached-XFB present. memory and services resolve display-list
// guest addresses and indexed vertex-array reads. A non-empty capture returns one monotonic completion
// token; an empty capture returns an invalid token. An exact FIFO preclassification makes event-free
// receipts immediately ready, while PE-bearing tokens keep exact fence and callback completion semantics.
[[nodiscard]] FramePeCompletionToken render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    const GxServices* services,
    bool present_swap_chain = true,
    std::uint32_t displayed_xfb_addr = 0);

// Wait until the render queue and in-flight render chunks are fully idle.
void wait_for_render_idle();

// Wait for at most `timeout_ms` for this exact token. Returns true and consumes
// it exactly once on completion; returns false without consuming it when the
// slice expires. Renderer failures, stale tokens, and shutdown still hard-fail.
// A zero timeout is a nonblocking probe.
[[nodiscard]] bool wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms);

// Simulation thread: the next render_frame submission handles `marker` when
// its parser reaches `fifo_offset` bytes into the submitted FIFO.
void attach_fifo_marker(FifoMarker marker, std::size_t fifo_offset) noexcept;
// Simulation thread, before attach_fifo_marker(SceneBegin): the game camera's
// world-to-eye matrix (row-major 3x4) for that scene, the player's up in the
// world (null when unknown: the enhanced sky then takes the view's up), and
// the stage being played (null when the game does not say).
void attach_scene_view(const float view[12], const float* up = nullptr, const char* stage = nullptr) noexcept;
// Simulation thread, at each SceneBegin: whether the renderer reads
// draw-identity markers (frame interpolation or motion vectors are on).
[[nodiscard]] bool draw_identity_wanted() noexcept;
// Simulation thread: whether the game keeps clipped actors whose shadows can reach the view drawn (the ray tracer
// lights the scene; GALAXY_RT_OFFSCREEN_CASTERS=0 never).
[[nodiscard]] bool rt_shadow_casters_wanted() noexcept;
// Simulation thread (BrightSun::control): the world direction towards the game's visible sun and
// the distance of its model from the camera. The ray tracer lights the scene from it while it
// keeps arriving.
void set_game_sun(const std::array<float, 3>& direction, float distance) noexcept;
// The direction (and distance) when one arrived within `max_age_ms`.
[[nodiscard]] bool game_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms,
                            float* distance = nullptr) noexcept;
// Render thread, each traced frame: the world direction towards the sun the ray tracer lights the scene from (the
// game's visible sun, the stage table's, else the key light; none on a sunless stage). Simulation thread: the
// direction when one was set within `max_age_ms` (the off-screen shadow casters sweep along it).
void set_traced_sun(const std::array<float, 3>& direction, bool present) noexcept;
[[nodiscard]] bool traced_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms) noexcept;
// Simulation thread: how far (world units) the light of a model's table light sources reaches (rt-materials.tsv
// emitter rows whose object names the model; `radius` its bounding radius), 0 when it has none.
[[nodiscard]] float rt_light_reach(const char* model, float radius) noexcept;
// Simulation thread, each scene while the Comet Observatory's star core (AstroCore) exists: its
// world centre and scale (it grows with the Grand Stars). The ray tracer lights the scene from it
// and gives it its finish.
void set_star_core(const std::array<float, 3>& position, float scale) noexcept;
[[nodiscard]] bool star_core(std::array<float, 3>& position, float& scale, std::uint64_t max_age_ms) noexcept;
// How many have arrived.
[[nodiscard]] std::uint64_t game_sun_updates() noexcept;
// Simulation thread: the game's lights as it loads them for drawing. A point light
// (LightFunction::loadPointLightInfo: world position, colour 0-1, the distance at which its GX
// distance attenuation halves it, 0 without attenuation) and a world-fixed light of an
// ActorLightInfo (follow-camera off: its far position names its direction).
void note_game_point_light(std::uint32_t info, const std::array<float, 3>& position,
                           const std::array<float, 3>& color, float half_distance) noexcept;
void note_game_world_light(const std::array<float, 3>& position, const std::array<float, 3>& color) noexcept;
// The game's area lights (native game, NEBULA_LIGHT_AREA and _PLAYER): an ambient and two lights, each with its
// colour (0-1), position (world, or the camera's eye space when it follows the camera).
struct GameAreaLight {
    std::array<float, 3> ambient{};
    struct Light {
        std::array<float, 3> color{};
        std::array<float, 3> position{};
        bool follows_camera = false;
    } lights[2];
};
enum class GameAreaLightKind : std::uint8_t { Stage, Player };
void set_game_area_light(GameAreaLightKind kind, const GameAreaLight& light) noexcept;
// The latest of `kind` when reported within `max_age_ms`.
[[nodiscard]] bool game_area_light(GameAreaLightKind kind, GameAreaLight& out, std::uint64_t max_age_ms) noexcept;

// Simulation thread (native game): a game object's name (NameObj::mName) by the id its host markers carry.
void note_object_name(std::uint32_t id, const char* name) noexcept;
// Any thread: that name into `out` (NUL-terminated, cut to `size`); false when the game never named `id`.
bool game_object_name(std::uint32_t id, char* out, std::size_t size) noexcept;
// Simulation thread: an actor asking for the game's one point light this frame (MR::requestPointLight: Lumas,
// Power Stars, item blocks, fireballs, Boos), as its own light. While requests arrive, the one light the game hands
// to the requester nearest the player (note_game_point_light) is not noted again.
void note_game_point_request(std::uint32_t actor, const std::array<float, 3>& position,
                             const std::array<float, 3>& color, float half_distance) noexcept;
struct GameLight {
    std::array<float, 3> position{};
    std::array<float, 3> color{};
    float half_distance = 0.0f;
};
// The lights noted within `max_age_ms` (not black), at most `out.size()`; returns the count.
std::size_t game_point_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept;
std::size_t game_world_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept;
// Simulation thread: one particle of a JPA stripe at `stream_offset` of the
// live GX stream (GuestAddressSpace::gx_stream_position); its two strip
// vertices follow. The next render_frame submission carries it.
void attach_stripe_particle(std::size_t stream_offset, std::uint32_t particle, std::uint32_t age_lifetime,
                            std::uint8_t strip);
// Simulation producer: the THP movie frame the game draws (movie_replacement.h); every later submission carries it
// until the movie closes (frame -1).
void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint32_t y, std::uint32_t u,
                        std::uint32_t v, std::uint32_t width, std::uint32_t height) noexcept;
// Any thread: a replacement video's frames show for the open movie (its THP frames need no decode).
[[nodiscard]] bool movie_replacement_showing() noexcept;
void set_safety_scene_surround(float intensity) noexcept;

[[nodiscard]] bool peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value);

// A peek made in draw-sync token `token`'s callback: the field captured where the stream reached that
// token (GxBackend::peek_efb_published).
[[nodiscard]] bool peek_efb_published(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint16_t token,
    std::uint32_t& value);

// The native game computes the arrays indexed loads read (matrices, lights) in host byte order: before its first
// frame, its host makes LOAD_INDX read them that way. Everything else the GPU reads stays big-endian.
void set_indexed_load_host_order(bool host_order) noexcept;

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

// Cold native settings overlay, drawn after the XFB blit and before Present. F1 is global while the native
// window has focus. All changes enter through the renderer window thread's key-message path.
struct NativeSettingsOverlaySnapshot {
    bool visible = false;
    bool dirty = false;
    bool save_error = false;
    unsigned tab = 0;
    unsigned row = 0;
    unsigned tab_count = 0;
    unsigned row_count = 0;
    // Draft changes not yet applied, and a display change awaiting "keep".
    unsigned pending_changes = 0;
    bool confirming_display = false;
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
    // Counts game-timed present requests only. Auxiliary frame-rate-decoupling re-presents are excluded. This
    // is serial identity, not pixel or content identity: a newer XfbTexture::copy_serial is counted once, at its
    // first game-timed presentation. Replaying an older A/B pair cannot advance the counter.
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

[[nodiscard]] bool native_settings_overlay_visible() noexcept;

// The native game's HOME Menu: Nebula's panel with the console menu's choices, drawn over the frame and taking the
// keyboard, controllers and mouse while open. The game opens it (HBMInit) and polls each frame: -1 nothing chosen yet,
// 0 return to the game, 1 Wii Menu (leave the game), 2 reset.
void home_menu_open() noexcept;
[[nodiscard]] int home_menu_poll() noexcept;
void home_menu_close() noexcept;
[[nodiscard]] bool home_menu_visible() noexcept;
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
// Waits until the settings files the overlay queued are written (saves run on a worker).
void native_settings_wait_saved_for_tests();
// The controllers the CONTROLS page lists in place of the attached ones, and
// the device marked active.
void native_settings_set_controllers_for_tests(std::span<const input::ControllerInfo> controllers,
                                               std::optional<input::DeviceKey> active);
// The text of a row of the open page as drawn.
[[nodiscard]] std::string native_settings_row_text_for_tests(unsigned row);
// The settings the menu would apply (its draft), for tests.
[[nodiscard]] RenderConfig native_settings_draft_for_tests();
// Delivers an overlay key to the window thread (controller navigation).
void native_settings_post_key(std::uintptr_t virtual_key) noexcept;

// Number of EFB display copies submitted to XFB textures by the active backend.
[[nodiscard]] std::uint64_t xfb_copy_count() noexcept;
[[nodiscard]] XfbPresentStats xfb_present_stats() noexcept;

}  // namespace galaxy::gx
