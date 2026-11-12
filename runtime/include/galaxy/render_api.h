#pragma once

// The renderer API the host calls (D3D12 or Vulkan behind one RenderCore): window, the game's records and
// presentation, the game's scene and light reports, and the native settings overlay.

#include "galaxy/render/render_config.h"
#include "galaxy/render/pointer_response.h"
#include "galaxy/controller_roster.h"
#include "galaxy/host_pointer_events.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <vector>

struct NebulaFrameList;

struct NebulaScreenProbe;

namespace galaxy::render {
using ScreenProbe = ::NebulaScreenProbe;

class TextureStore;
namespace core {
class Registries;
}

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
    // The time base of HID report deadlines.
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

// Creates the window and the renderer (device, swap chain, queues, root signature, pipelines, upload buffers).
// Returns false on failure.
bool initialize(int width, int height);
// The reason initialize failed, as shown to the player; empty when unknown.
std::string startup_failure();

// Whether a game frame presents as soon as the renderer reaches its end (else only at the host's presents): off while
// the host's fast start shows one frame a display period. On by default.
void set_frame_end_presents(bool enabled) noexcept;

// Walks the frame list attached (attach_frame_list) and optionally presents: the newest picture, or black when
// `black`. Nothing attached: at most a present.
void render_frame(bool present_swap_chain = true, bool black = false);

// Blocks until the render queue and every in-flight render chunk are idle.
void wait_for_render_idle();


// The services' registries and texture store; any thread.
[[nodiscard]] core::Registries& registries() noexcept;
// The pipelines made, and those the settings in use still want.
struct PipelineProgress {
    std::uint32_t done = 0u, left = 0u;
};
[[nodiscard]] PipelineProgress pipeline_progress() noexcept;
[[nodiscard]] TextureStore& textures() noexcept;
// The list the next render_frame hands over.
void attach_frame_list(NebulaFrameList* list) noexcept;
// Simulation thread: how the game's bloom goes through the renderer (NEBULA_OPTION_BLOOM: 0 not at all, 1 its scene
// kept, 2 drawn by it; galaxy/render/game_bloom.h).
[[nodiscard]] unsigned bloom_route() noexcept;
// Simulation thread: the movie frame the game draws (movie_playback.h); every later submission carries it until the
// next. `data`: the cooked movie (null keeps the one given).
void attach_movie_frame(const char* name, std::int32_t frame, float frame_rate, const void* data, std::uint64_t size,
                        std::uint32_t width, std::uint32_t height);

// Channel `channel`'s latest finished probes (nebula_game_api.h NebulaHostServices::screen_probe_results).
[[nodiscard]] std::uint32_t screen_probe_results(std::uint32_t channel, ScreenProbe* probes, std::uint32_t capacity);


// Pumps window messages; the host's main loop calls it periodically.
void pump_messages();
[[nodiscard]] bool quit_requested() noexcept;

// The window is minimised: the renderer presents nothing of its own (no repeated pictures, no generated frames) and waits for
// work without polling. The host sets it together with the game's own suspension. A covered window is not this: the game
// goes on and the renderer stops presenting by itself. `window_occluded` is the OS's own word that the whole window is covered
// (Direct3D 12's present status; Vulkan on Windows reports none, so false there).
void set_window_hidden(bool hidden) noexcept;
[[nodiscard]] bool window_occluded() noexcept;
// The window is open but not in front (another program is, or its own dialog is not the active one): the renderer shows the game's
// own frames at the game's rate and makes no generated frames and no repeated pictures, so a window nobody is playing in costs
// the GPU the least a moving picture needs. The host sets it from the OS's word on the foreground; false brings everything back.
void set_window_background(bool background) noexcept;

// The latest pointer state of the native window, from its client-area mouse messages and the OS cursor position.
[[nodiscard]] HostPointerState poll_host_pointer_state() noexcept;
[[nodiscard]] host::HostPointerTransitionPoll poll_host_pointer_transition(
    std::uint64_t after_sequence) noexcept;

// Native settings overlay, drawn after the display picture blit and before Present. F1 toggles it whenever the native window
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
struct DisplayPresentStats {
    std::uint64_t presented = 0;
    // Game-timed presents of a new DisplayTexture::picture_number, each counted once at most.
    // Frame-rate-decoupling re-presents and replays of an older copy do not count.
    std::uint64_t new_pictures_shown = 0;
    std::uint64_t pictures_shown_again = 0;
    std::uint64_t last_presented_frame_stamp = 0;
    std::uint64_t last_presented_picture = 0;
    std::uint32_t last_presented_slot = 0;
};

[[nodiscard]] bool native_settings_overlay_visible() noexcept;
// Nebula's screens with small text (the settings menu, the achievement board) ask for the 2D layer at the window's
// resolution while they show, whatever the HUD's (the game module's sharp_2d).
// The renderer's start, step by step ("[boot] renderer ..." lines: milliseconds since the step before).
void boot_step(const char* step) noexcept;
void request_sharp_2d(bool on) noexcept;
[[nodiscard]] bool sharp_2d_requested() noexcept;
// The scale the 2D layer was last drawn at (the scene target in use as the scene ended: the output scene target, else the render scene target),
// in lines; 0 before the first scene ends. Any thread.
[[nodiscard]] unsigned layer_2d_lines() noexcept;
// The mouse wheel's turn since the last call, in 120ths of a notch, positive away from the player (host_window's
// pointer_wheel: the settings menu takes the wheel itself while it shows). Any thread.
[[nodiscard]] std::int32_t take_pointer_wheel() noexcept;

// The native game's HOME Menu with its choices, taking the keyboard and controllers while open: return
// to the game, reset, or leave it, the last two after "are you sure?". The game opens it
// (home_menu_open) and polls each frame: -1 nothing chosen yet, 0 return to the game, 1 leave the game, 2 reset. The game
// draws it from home_menu_view (NEBULA_HOME_MENU_VIEW) and sends the pointer's answers.
void home_menu_open() noexcept;
[[nodiscard]] int home_menu_poll() noexcept;
void home_menu_close() noexcept;
[[nodiscard]] bool home_menu_visible() noexcept;
[[nodiscard]] int home_menu_view() noexcept;
void home_menu_point(unsigned answer) noexcept;
void home_menu_choose(unsigned answer) noexcept;
[[nodiscard]] bool native_window_input_captured() noexcept;
// `was_down` is Win32 LPARAM bit 30. Repeated F1 messages are consumed without
// toggling so keyboard autorepeat cannot flap the overlay.
[[nodiscard]] bool native_settings_handle_window_key(
    std::uintptr_t virtual_key,
    bool was_down,
    std::uint32_t normalized = 0u);
// A key from a controller.
bool native_settings_pad_key(std::uintptr_t virtual_key);
// A mouse button while a binding waits for its input: true when it took it.
[[nodiscard]] bool native_settings_capture_mouse(galaxy::input::MouseButton button);
// The wheel's turn in 120ths of a notch; false while the menu is closed.
[[nodiscard]] bool native_settings_wheel(int delta);
// Text typed while a mod's text option is being typed in (UTF-8: WM_CHAR, SDL's text input). False: none is.
[[nodiscard]] bool native_settings_handle_window_text(std::string_view utf8);
// A text option is being typed in: the host shows its on-screen keyboard (SDL_StartTextInput).
[[nodiscard]] bool native_settings_text_wanted() noexcept;
void native_settings_files_dropped(std::vector<std::filesystem::path> files);
void native_settings_window_resized(unsigned width, unsigned height);
// The graphics device lost at the renderer's next frame, as a driver reset loses it (an extension's lose_gpu_device).
void lose_gpu_device() noexcept;
// Delivers an overlay key to the window thread (controller navigation).
void native_settings_post_key(std::uintptr_t virtual_key) noexcept;
// Delivers a keyboard key to the window thread, as the keyboard gives it (a harness's `key` step): a keyboard binding
// waiting for its key takes it, which a controller's key never does.
void native_settings_post_keyboard_key(std::uintptr_t virtual_key) noexcept;
// The bit the SDL and Apple hosts' menu-key queues mark a keyboard key with (above every key code).
inline constexpr std::uintptr_t kMenuKeyKeyboard = std::uintptr_t{1} << 30;
// The mods saved differ from the ones this run started with.
[[nodiscard]] bool native_settings_mods_changed();

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
    DlssAsk,          // download DLSS? (yes: Apply, its license: Revert, no: Close)
    DlssInstalling,   // DLSS downloads (status_number percent; Close stops it)
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
    ModsCodeAsk,      // a mod turned on brings code: allow it? (status_text: its name; yes: Apply, no: Close)
    XessAsk,          // download XeSS? (yes: Apply, its license: Revert, no: Close)
    XessInstalling,   // XeSS downloads (status_number percent; Close stops it)
    XessRestart,      // XeSS installed: restart now? (yes: Apply, later: Close)
    XessFailed,       // XeSS was not installed
    License,          // the maker's license: long_text a page (next: Apply, before: Revert, back: Close)
    Preparing,        // pipelines the settings in use want are still being made (status_number percent)
    Arrange,          // the phone's on-screen controls are dragged where they go (save: Apply, the game's places: Revert,
                      // cancel: Close)
    ModAbout,         // a mod's description and pictures, a page at a time: long_text and picture, status_number the
                      // page's from 1, or -(page * 4 + 1 loading, 2 unreadable, 3 missing) for a picture not shown
                      // (next: Apply, before: Revert, back: Close)
    ModRemoveAsk,     // remove the mod whose page is open? status_number 1: to the recycle bin (yes: Apply, no: Close)
    ModForgetAsk,     // forget what a removed mod kept? (status_text: its id; yes: Apply, no: Close)
};
// A picture the menu shows (a mod's icon or one of its pictures): RGBA8 rows, tightly packed, straight alpha.
struct NativeSettingsPicture {
    unsigned width = 0u;
    unsigned height = 0u;
    std::vector<std::uint8_t> rgba;
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
    bool help_as_is = false;  // the help is already in the played language (a mod's refusal): not translated
    NativeSettingsStatus status = NativeSettingsStatus::Help;
    int status_number = 0;
    std::string status_text;
    std::string long_text;
    std::shared_ptr<const NativeSettingsPicture> picture;
};
// The menu as it stands. The first call hands the drawing to the game: the overlay draws nothing of its own after.
[[nodiscard]] NativeSettingsView native_settings_view();
// The pointer's input to the menu (the game's own menu takes the pointer; the window thread the keyboard and pads).
enum class NativeSettingsInput : unsigned { Open = 1, Close, SelectPage, SelectRow, Change, Apply, Revert, Press, MoveMod, SetValue };
void native_settings_input(NativeSettingsInput input, int arg);

// GPU time by pass (the settings overlay's SYSTEM page): off at every start; while it is on, the renderer writes a timestamp at
// each pass boundary of every frame and reads them back a frame later; while it is off it records and reads nothing. A pass's
// time is a running average (about ten frames) over the frames that ran it, in milliseconds.
struct GpuPassTime {
    const char* name;  // static text
    float ms;
};
void set_gpu_timing(bool on) noexcept;
[[nodiscard]] bool gpu_timing_on() noexcept;
// The passes that have a time, at most `capacity`; how many.
[[nodiscard]] std::size_t gpu_pass_times(GpuPassTime* out, std::size_t capacity) noexcept;

// The frames the game has shown (display copies) and how the presents met them.
[[nodiscard]] std::uint64_t pictures_made() noexcept;
[[nodiscard]] DisplayPresentStats display_present_stats() noexcept;

}  // namespace galaxy::render
