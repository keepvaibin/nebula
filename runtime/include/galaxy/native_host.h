// The native game's Windows host services (native_host.cpp): input reports, vibration, the virtual Wii devices and
// the game's audio output.
#pragma once

#include "galaxy/controller_roster.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/guest_memory.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/synthetic_kpad_buttons.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::host {

// Once enabled, environment variables the input report reads are cached by literal name. The host enables it after
// its startup configuration; tests leave it off so the variables they change stay visible.
void set_env_flag_cache_enabled(bool enabled) noexcept;

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
    // The game window (HWND), to tell whether a click lands on it.
    std::uintptr_t debug_window = 0;
};

using HostPointerProvider = HostPointerState (*)() noexcept;
using HostPointerTransitionProvider = HostPointerTransitionPoll (*)(
    std::uint64_t after_sequence) noexcept;
using NativeKeyboardCaptureProvider = bool (*)() noexcept;
using NativeSettingsVisibleProvider = bool (*)() noexcept;
using NativeSettingsKeySink = void (*)(std::uintptr_t virtual_key) noexcept;

void install_host_pointer_provider(HostPointerProvider provider) noexcept;
void install_host_pointer_transition_provider(
    HostPointerTransitionProvider provider) noexcept;
// The settings overlay owns window-key navigation; while it is visible,
// polled keyboard state is not forwarded to the game. Null disables capture.
void install_native_keyboard_capture_provider(
    NativeKeyboardCaptureProvider provider) noexcept;
[[nodiscard]] bool native_keyboard_capture_active() noexcept;
// Controller access to the settings overlay (galaxy/native_settings_pad.h):
// the overlay's visibility and a sink that delivers its keys to the window
// thread. Null disables the controller chord.
void install_native_settings_pad_hooks(
    NativeSettingsVisibleProvider visible,
    NativeSettingsKeySink sink) noexcept;
void publish_native_window_key_press(std::uint32_t key) noexcept;
void clear_native_window_key_presses() noexcept;
// Window-thread key level (down on WM_KEYDOWN, up on WM_KEYUP); cleared with
// the press latch.
void publish_native_window_key_state(std::uint32_t key, bool down) noexcept;
// Whether the window message for `key` / the latest mouse move or button came
// from SendInput (a controller mapper such as Steam's desktop layout) instead
// of a device. In auto input mode with a controller attached such input is
// ignored so the controller is not read twice.
void publish_native_window_input_origin(std::uint32_t key, bool injected) noexcept;
void publish_native_pointer_input_origin(bool button, bool injected) noexcept;
[[nodiscard]] bool native_window_key_injected(std::uint32_t key) noexcept;
[[nodiscard]] bool native_pointer_input_injected(bool button) noexcept;

// Binding capture for the overlay's CONTROLS page. While armed, the controller
// of `scheme` (and `slot`, 0xff: any of the family) is read in every input
// mode, never reaches the overlay's pad navigation, and the first new press
// (button, stick direction past 0.6, trigger past 0.5) is stored; the overlay
// is told through its key sink with kBindingCaptureKey and collects it with
// take_controller_binding_capture.
inline constexpr std::uintptr_t kBindingCaptureKey = 0xE8u;
void arm_controller_binding_capture(input::ControlScheme scheme, std::uint8_t slot = input::kKeyboardSlot) noexcept;
void cancel_controller_binding_capture() noexcept;
[[nodiscard]] std::optional<input::InputSource> take_controller_binding_capture() noexcept;

// The controllers attached now by model name: XInput pads and HID controllers
// (Switch, PlayStation, Steam). The input reports keep it current while
// controllers are polled; otherwise XInput is probed at most once a second.
std::size_t connected_controllers(std::span<input::ControllerInfo> out) noexcept;
// The device that last gave input, a controller or the keyboard and mouse.
[[nodiscard]] std::optional<input::DeviceKey> last_input_device() noexcept;
// The game's button icons now (ButtonIcons setting, the controller last used and its bindings); `menu`: a menu shows.
[[nodiscard]] input::ButtonIconSpec button_icons(bool menu) noexcept;
// A person typed or clicked (overlay keys included).
void note_keyboard_input() noexcept;
// Synchronous check (settings UI only): a controller is attached now.
[[nodiscard]] bool controller_present_now() noexcept;

struct GamePaths {
    std::filesystem::path content_root;
};

GamePaths locate_game(const std::filesystem::path& input);

// The live controls (bindings, keyboard and mouse, controllers) as one Wii Remote input snapshot at retrace
// `current_vi`, and the virtual remote's EEPROM (DPD and accelerometer calibration) and Nunchuk calibration.
// `star_pointer_mode` (-1 none) and `tilt_section` (0 none, 1 Star Ball, 2 Ray) come from the game: the control
// mapping's menu or play context and the tilt steering.
[[nodiscard]] galaxy::input::WiimoteInputSnapshot native_game_wiimote_snapshot(
    std::uint64_t current_vi,
    bool ir_enabled,
    std::int32_t star_pointer_mode,
    std::uint8_t tilt_section);
// The game's motor bit to the controller in use, held off in menus (the HOME Menu, pause, error). Renew it with every
// input report: a motor whose state goes stale stops.
void native_game_rumble(bool motor_on, std::int32_t star_pointer_mode) noexcept;
void native_game_wiimote_calibration(
    std::uint8_t (&eeprom)[0x2A],
    std::uint8_t (&nunchuk_calibration)[32]);
// A valid, empty Mii database (RFL_DB.dat) for a NAND that has none.
[[nodiscard]] std::vector<std::byte> native_game_empty_mii_database();

// The game's sound: the JAudio DSP program run on the host (JAudioHostMixer) over the game's DSP memory, which the
// game keeps in the console's byte order, and the audio interface's blocks played on the output device (XAudio2,
// master volume, WAV dump).
class NativeGameAudio {
public:
    NativeGameAudio();
    ~NativeGameAudio();
    NativeGameAudio(const NativeGameAudio&) = delete;
    NativeGameAudio& operator=(const NativeGameAudio&) = delete;

    // The command words the game mails to the DSP (count 0: one word, a halted subframe's release), with physical
    // addresses resolved in `memory`. True when the words completed a subframe (the DSP's AUDIOMSG_DSP mail).
    bool dsp_command(const std::uint32_t* words, std::uint32_t count, const GuestMemory& memory);
    // One AI block: 16-bit samples, right then left, big-endian, as the console's DMA engine reads them. Call from one
    // thread; the first call opens the device.
    bool play(std::span<const std::byte> block, std::uint32_t sample_rate);
    // Blocks submitted and not yet played (the sink's pending queue and the device's).
    [[nodiscard]] unsigned queued_blocks() const;
    // The blocks the device holds before it plays (or plays again after running dry): the level to keep queued.
    [[nodiscard]] unsigned target_blocks() const;
    [[nodiscard]] bool output_available() const;
    // The device stopped while the app is out of sight (SDL hosts), queued blocks kept for its return.
    void set_paused(bool paused);
    // The game's name for the sound on a DSP channel (nebula_game_audio_channel_owner), for the volume categories.
    void set_channel_owner(std::uint32_t (*owner)(std::uint32_t channel, std::uint32_t* route));
    // The DSP program's counters for the [jaudio] log line.
    [[nodiscard]] std::string stats_line() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace galaxy::host
