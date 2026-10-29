#pragma once

// The renderer API the host calls (D3D12 or Vulkan behind one GxBackend): window, GX FIFO submission and
// presentation, the game's scene and light reports, and the native settings overlay.

#include "galaxy/gx/frame_completion.h"
#include "galaxy/gx/native_shapes.h"
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
#include <vector>

namespace galaxy::gx {

// render_frame's displayed_xfb_addr when the XFB the VI shows is unknown (the newest copy is presented) and when
// the VI shows black.
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
    // GetTickCount64 time (ms) at which the position of absolute_sequence was published; a host clock, not the
    // Wii time base of HID report deadlines.
    std::uint64_t absolute_acquired_ms = 0;
    std::uint32_t debug_flags = 0;
    std::uint32_t debug_error = 0;
    std::uintptr_t debug_window = 0;
};

struct HostClientExtent {
    int width = 0;
    int height = 0;
};

// The window's current client area, read directly rather than from the pointer state.
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

// Creates the window and the renderer (device, swap chain, queues, root signature, pipelines, upload buffers).
// Returns false on failure.
bool initialize(int width, int height);
// The reason initialize failed, as shown to the player; empty when unknown.
std::string startup_failure();

// The game dump's content root (GamePaths::content_root), from which the settings overlay reads the game's UI art
// (game_asset_extract.h). Call once, before the overlay is first shown; without it the overlay draws solid colors.
void set_native_settings_content_root(const std::string& utf8_path);

// Parses a captured GX FIFO, records its draws and optionally presents. `memory` and `services` resolve
// display-list addresses and indexed vertex-array reads. An empty capture submits no GX work (at most a present
// of the cached XFB) and returns an invalid token. Other tokens complete in submission order: at once for a
// capture without a PE FINISH or TOKEN event, else after its GPU fence and PE callbacks.
[[nodiscard]] FramePeCompletionToken render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    const GxServices* services,
    bool present_swap_chain = true,
    std::uint32_t displayed_xfb_addr = 0);

// Blocks until the render queue and every in-flight render chunk are idle.
void wait_for_render_idle();

// Waits up to `timeout_ms` for `token`. Returns true and consumes the token on completion, false without
// consuming it on timeout; a zero timeout polls. A renderer failure, a stale token or a stopped render thread
// throws.
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
// Simulation thread: whether the game keeps drawing clipped actors whose shadows can reach the view (while the ray
// tracer lights the scene, unless GALAXY_RT_OFFSCREEN_CASTERS=0).
[[nodiscard]] bool rt_shadow_casters_wanted() noexcept;
// Simulation thread: how the game's bloom goes through the renderer (NEBULA_OPTION_BLOOM: 0 not at all, 1 its scene
// kept, 2 drawn by it; galaxy/gx/game_bloom.h).
[[nodiscard]] unsigned bloom_route() noexcept;
// Simulation thread (BrightSun::control): the world direction towards the game's visible sun and
// the distance of its model from the camera. The ray tracer lights the scene from it while updates
// keep arriving.
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
// Number of set_game_sun calls so far.
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
    // Its identity while it lights (the game's light info or requesting actor; else its slot in the store, bit 31).
    std::uint32_t key = 0u;
};
// The lights noted within `max_age_ms` (not black), at most `out.size()`; returns the count.
std::size_t game_point_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept;
std::size_t game_world_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept;
// Simulation thread: one particle of a JPA stripe at `stream_offset` bytes into the next submitted FIFO; its two
// strip vertices follow. The next render_frame submission carries it.
void attach_stripe_particle(std::size_t stream_offset, std::uint32_t particle, std::uint32_t age_lifetime,
                            std::uint8_t strip);
// Simulation thread: a native shape record (native_shapes.h) at its offset into the next submitted FIFO.
void attach_shape_record(shapes::Record record);
// Simulation thread: the THP movie frame the game draws (movie_replacement.h); every later submission carries it
// until the movie closes (frame -1).
void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, std::uint32_t y, std::uint32_t u,
                        std::uint32_t v, std::uint32_t width, std::uint32_t height) noexcept;
// Any thread: a replacement video's frames show for the open movie (its THP frames need no decode).
[[nodiscard]] bool movie_replacement_showing() noexcept;
// The grey (0 black to 1 white) of the borders around a fitted picture for the scene being drawn: the strap
// screen's white backing as it fades.
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

void notify_guest_memory_write(
    std::uint32_t address,
    std::uint32_t size) noexcept;

// Pumps window messages; the host's main loop calls it periodically.
void pump_messages();
[[nodiscard]] bool quit_requested() noexcept;

// The latest pointer state of the native window, from its client-area mouse messages and the OS cursor position.
[[nodiscard]] HostPointerState poll_host_pointer_state() noexcept;
[[nodiscard]] host::HostPointerTransitionPoll poll_host_pointer_transition(
    std::uint64_t after_sequence) noexcept;

// Native settings overlay, drawn after the XFB blit and before Present. F1 toggles it whenever the native window
// has focus; every change arrives through the window thread's key messages.
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
    // Game-timed presents of a new XfbTexture::copy_serial, each counted once at its first presentation.
    // Frame-rate-decoupling re-presents and replays of an older copy do not count.
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
// Nebula's screens with small text (the settings menu, the achievement board) ask for the 2D layer at the window's
// resolution while they show, whatever the HUD's (the game module's sharp_2d).
// The renderer's start, step by step ("[boot] renderer ..." lines: milliseconds since the step before).
void boot_step(const char* step) noexcept;
void request_sharp_2d(bool on) noexcept;
[[nodiscard]] bool sharp_2d_requested() noexcept;
// The scale the 2D layer was last drawn at (the EFB in use as the scene ended: the output EFB, else the render EFB),
// over the Wii's 640 x 456; 0 before the first scene ends. Any thread.
[[nodiscard]] unsigned layer_2d_scale() noexcept;
// The 3D image's scale over the Wii's 640 x 456 as the last scene showed it (the temporal upscaler's output, else the
// render EFB's); 0 before the first scene. Any thread.
[[nodiscard]] unsigned scene_display_scale() noexcept;
// The mouse wheel's turn since the last call, in 120ths of a notch, positive away from the player (host_window's
// pointer_wheel: the settings menu takes the wheel itself while it shows). Any thread.
[[nodiscard]] std::int32_t take_pointer_wheel() noexcept;

// The native game's HOME Menu with the console menu's choices, taking the keyboard and controllers while open: return
// to the game, reset, or leave it (the console's Wii Menu), the last two after "are you sure?". The game opens it
// (HBMInit) and polls each frame: -1 nothing chosen yet, 0 return to the game, 1 leave the game, 2 reset. The game
// draws it from home_menu_view (NEBULA_HBM_VIEW, `drawing`) and sends the pointer's answers; until it draws it, the
// host draws its own panel.
void home_menu_open() noexcept;
[[nodiscard]] int home_menu_poll() noexcept;
void home_menu_close() noexcept;
[[nodiscard]] bool home_menu_visible() noexcept;
[[nodiscard]] int home_menu_view(bool drawing) noexcept;
void home_menu_point(unsigned answer) noexcept;
void home_menu_choose(unsigned answer) noexcept;
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
// The Joy-Con halves connected, for tests.
void native_settings_set_joycons_for_tests(unsigned lefts, unsigned rights);
// Whether a Wii Remote is connected, for tests.
void native_settings_set_wii_remote_for_tests(bool present);
// The text of a row of the open page as drawn.
[[nodiscard]] std::string native_settings_row_text_for_tests(unsigned row);
// The settings the menu would apply (its draft), for tests.
[[nodiscard]] RenderConfig native_settings_draft_for_tests();
// Delivers an overlay key to the window thread (controller navigation).
void native_settings_post_key(std::uintptr_t virtual_key) noexcept;

// The settings menu as it stands, for the native game's own menu (NebulaSettingsMenu) and the overlay alike: the
// pages, the open page's rows (the overlay's English), the selection and what the line under the page says.
struct NativeSettingsViewRow {
    std::string label;
    std::string value;
    bool heading = false;  // a section's title, never selected
    bool changes = false;  // left and right change it
    bool name = false;     // its value is a name (a monitor, a controller, a graphics card): shown as is
    bool inputs = false;   // its value lists inputs (keys, buttons, sticks), each named on its own
    bool active = false;   // its value is the controller in use
    bool label_name = false;  // its label is a name (a mod's, its option's): shown as is
    bool buttons = false;     // its value is its buttons' words, '|' between them
    unsigned mod_list = 0u;   // the MODS page's lists: 1 off, 2 on (a mod, or the list's title)
    bool held = false;        // a mod picked up to move
    bool shared = false;      // a binding with an input another control is bound to as well (both act together)
    unsigned slider_steps = 0u;  // a setting along a range: its steps, and the one it is at
    unsigned slider_at = 0u;
};
enum class NativeSettingsStatus : unsigned {
    Help,             // the selected row's description
    CaptureKey,       // a binding waits for a key or mouse button
    CapturePad,       // a binding waits for a button, stick or trigger
    Message,          // a message for a moment (status_text)
    DlssAsk,          // download DLSS? (yes: Apply, no: Close)
    DlssInstalling,   // DLSS downloads and installs
    DlssRestart,      // DLSS installed: restart now? (yes: Apply, later: Close)
    DlssFailed,       // DLSS was not installed
    KeepDisplay,      // keep the new display? (status_number seconds left)
    SaveFailed,       // the settings could not be written
    Pending,          // status_number changes wait to be applied
    Applied,          // just applied
    LanguageRestart,  // the new language applies at the next start
    ModsRestart,      // the mod changes apply at the next start
    GpuRestart,       // the new graphics card applies at the next start
    Leave,            // leaving with changes not applied: apply or discard them first
    ModsRestartAsk,   // mod changes applied: restart now? (yes: Apply, later: Close)
    JoyCon,           // Pair Joy-Con: status_number has the halves (kJoyCon* bits); Close leaves
    RtRestartAsk,     // ray tracing turned on before its shaders are prepared: restart now? (yes: Apply, later: Close)
    DiagnosticsAsk,   // restart and record a diagnostics run? (yes: Apply, no: Close)
};
// The Pair Joy-Con screen's status_number (nebula_game_api.h's NEBULA_JOYCON_*, checked alike in host_main.cpp).
inline constexpr int kJoyConLeftBit = 1;       // a left half is connected
inline constexpr int kJoyConRightBit = 2;      // a right half is connected
inline constexpr int kJoyConLeftHeldBit = 4;   // its L is held
inline constexpr int kJoyConRightHeldBit = 8;  // its R is held
inline constexpr int kJoyConPairedBit = 16;    // a pair was just made
inline constexpr int kJoyConLeftPressesShift = 8;    // 4 bits: the left halves' button presses, counted round
inline constexpr int kJoyConRightPressesShift = 12;  // 4 bits: the right halves'
struct NativeSettingsView {
    bool open = false;
    unsigned page = 0u;
    std::vector<std::string> pages;
    unsigned controls_page = 0u;
    unsigned input = 0u;  // what the player last used: 0 keyboard, 1 Xbox-style, 2 Nintendo, 3 PlayStation
    std::vector<NativeSettingsViewRow> rows;
    unsigned selected = 0u;
    unsigned selected_button = 0u;  // the selected row's, when it has buttons
    std::string help;
    NativeSettingsStatus status = NativeSettingsStatus::Help;
    int status_number = 0;
    std::string status_text;
};
// The menu as it stands. The first call hands the drawing to the game: the overlay draws nothing of its own after.
[[nodiscard]] NativeSettingsView native_settings_view();
// The pointer's input to the menu (the game's own menu takes the pointer; the window thread the keyboard and pads).
enum class NativeSettingsInput : unsigned { Open = 1, Close, SelectPage, SelectRow, Change, Apply, Revert, Press, MoveMod, SetValue };
void native_settings_input(NativeSettingsInput input, int arg);

// Number of EFB display copies submitted to XFB textures by the active backend.
[[nodiscard]] std::uint64_t xfb_copy_count() noexcept;
[[nodiscard]] XfbPresentStats xfb_present_stats() noexcept;

}  // namespace galaxy::gx
