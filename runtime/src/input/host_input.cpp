// The host's controls: keyboard and mouse, controllers, the pointer and its sources, binding capture, button
// icons, vibration, and the scripted and replayed test input, as the input the game reads.
#include "galaxy/game_paths.h"
#include "galaxy/launch_args.h"

#include "galaxy/platform_time.h"
#include "galaxy/controller_hid.h"
#include "galaxy/wiimote_hid.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/live_controls_engine.h"
#include "galaxy/input/key_positions.h"
#include "galaxy/input/key_positions_win32.h"
#include "galaxy/input/keyboard_state.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/tilt.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render_api.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"

#include "galaxy/windows_api.h"
#include "galaxy/windows_power.h"
#include <Xinput.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::host {
using namespace controls;

static std::atomic<HostPointerProvider> g_host_pointer_provider{nullptr};
static std::atomic<HostPointerTransitionProvider>
    g_host_pointer_transition_provider{nullptr};
static std::atomic<NativeKeyboardCaptureProvider>
    g_native_keyboard_capture_provider{nullptr};
static std::atomic<NativeSettingsVisibleProvider> g_native_settings_visible{nullptr};
static std::atomic<NativeSettingsKeySink> g_native_settings_key_sink{nullptr};
static galaxy::input::KeyboardEventState g_keyboard_events;
// Source of the latest window message per key and for the mouse: true when it came from SendInput
// rather than a device (GetCurrentInputMessageSource).
static std::array<std::atomic<bool>, 256> g_window_key_injected{};
static std::atomic<bool> g_pointer_move_injected{false};
static std::atomic<bool> g_pointer_button_injected{false};

// Controller binding capture (overlay CONTROLS page). State: 0 idle, else
// (slot << 8) | (1 + scheme) while armed. The result is (1 << 16) | kind << 8 | code.
static std::atomic<std::uint64_t> g_binding_capture_armed{0u};
static std::atomic<std::uint32_t> g_binding_capture_result{0u};
// XInput pads (bit per user index) seen by the input path, and when it last looked.
static std::atomic<std::uint32_t> g_xinput_mask{0u};
static std::atomic<std::uint64_t> g_xinput_looked_ms{0u};
static std::atomic<HKL> g_keyboard_layout{nullptr};
// The device that last gave input, for the CONTROLS page: (scheme << 32) | slot.
constexpr std::uint64_t kLastInputNone = ~std::uint64_t{0};
constexpr std::uint64_t kLastInputKeyboard = ~std::uint64_t{0} - 1u;
static std::atomic<std::uint64_t> g_last_input{kLastInputNone};

// An armed capture holds the scheme as 1 + scheme in its low byte and the
// device slot above it.
constexpr input::ControlScheme capture_scheme_of(std::uint64_t armed) noexcept {
    return static_cast<input::ControlScheme>((std::max<std::uint64_t>(armed & 0xffu, 1u)) - 1u);
}

void arm_controller_binding_capture(input::ControlScheme scheme, std::uint32_t slot) noexcept {
    g_binding_capture_result.store(0u, std::memory_order_release);
    g_binding_capture_armed.store((static_cast<std::uint64_t>(slot) << 8u) | (1u + static_cast<std::uint64_t>(scheme)),
                                  std::memory_order_release);
    if (input::scheme_uses_hid(scheme)) {
        ensure_controller_hid_started();
    }
}

void cancel_controller_binding_capture() noexcept {
    g_binding_capture_armed.store(0u, std::memory_order_release);
}

std::optional<input::InputSource> take_controller_binding_capture() noexcept {
    const std::uint32_t result = g_binding_capture_result.exchange(0u, std::memory_order_acq_rel);
    if ((result & 0x10000u) == 0u) {
        return std::nullopt;
    }
    return input::InputSource{static_cast<input::SourceKind>((result >> 8u) & 0xffu),
                              static_cast<std::uint8_t>(result & 0xffu)};
}

namespace {
std::atomic<bool> g_input_missing_shown{false};
}  // namespace

void arrange_touch_controls() noexcept {}

bool touch_arranging() noexcept {
    return false;
}

void finish_arranging_touch_controls(bool) noexcept {}

void reset_arranged_touch_controls() noexcept {}

void input_mode_changed() noexcept {}

bool touch_controls_present() noexcept {
    return false;
}

TouchControlsSettings touch_controls_settings() noexcept {
    return {};
}

void set_touch_controls_settings(const TouchControlsSettings&) noexcept {}

void reset_touch_controls_places() noexcept {}

bool keyboard_or_mouse_attached() noexcept {
    const auto holds = [](const RAWINPUTDEVICELIST* devices, UINT listed) {
        for (UINT i = 0; i < listed; ++i) {
            if (devices[i].dwType == RIM_TYPEKEYBOARD || devices[i].dwType == RIM_TYPEMOUSE) {
                return true;
            }
        }
        return false;
    };
    RAWINPUTDEVICELIST devices[32];
    UINT count = static_cast<UINT>(std::size(devices));
    const UINT listed = GetRawInputDeviceList(devices, &count, sizeof(RAWINPUTDEVICELIST));
    if (listed != static_cast<UINT>(-1)) {
        return holds(devices, listed);
    }
    // No answer: nothing says they are gone.
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return true;
    }
    // More devices than the first list holds: `count` says how many, and a few more may have arrived since.
    try {
        std::vector<RAWINPUTDEVICELIST> more(static_cast<std::size_t>(count) + 8u);
        UINT room = static_cast<UINT>(more.size());
        const UINT all = GetRawInputDeviceList(more.data(), &room, sizeof(RAWINPUTDEVICELIST));
        return all == static_cast<UINT>(-1) || holds(more.data(), all);
    } catch (...) {
        return true;
    }
}

void set_input_missing_shown(bool shown) noexcept {
    g_input_missing_shown.store(shown, std::memory_order_relaxed);
}

bool input_missing_shown() noexcept {
    return g_input_missing_shown.load(std::memory_order_relaxed);
}

// What the arrays that receive this list hold (input::kMaxControllers) covers every source.
static_assert(input::kMaxControllers >= XUSER_MAX_COUNT + kMaxHidPads);

std::size_t connected_controllers(std::span<input::ControllerInfo> out) noexcept {
    std::uint32_t xinput = g_xinput_mask.load(std::memory_order_acquire);
    const std::uint64_t now = GetTickCount64();
    if (now - g_xinput_looked_ms.load(std::memory_order_relaxed) > 1000u) {
        xinput = 0u;
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
            XINPUT_STATE state{};
            if (XInputGetState(slot, &state) == ERROR_SUCCESS) xinput |= 1u << slot;
        }
        g_xinput_mask.store(xinput, std::memory_order_release);
        g_xinput_looked_ms.store(now, std::memory_order_relaxed);
    }
    std::size_t count = 0;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT && count < out.size(); ++slot) {
        if ((xinput & (1u << slot)) == 0u) continue;
        input::ControllerInfo& info = out[count++];
        info = {};
        info.scheme = input::ControlScheme::Xbox;
        info.slot = static_cast<std::uint32_t>(slot);
        info.model = "Xbox controller";
    }
    return count + hid_controllers(out.subspan(count));
}

std::optional<input::DeviceKey> last_input_device() noexcept {
    const std::uint64_t key = g_last_input.load(std::memory_order_relaxed);
    if (key == kLastInputNone) return std::nullopt;
    if (key == kLastInputKeyboard) return input::DeviceKey{input::ControlScheme::Keyboard, input::kKeyboardSlot};
    return input::DeviceKey{static_cast<input::ControlScheme>((key >> 32u) & 0xffu),
                            static_cast<std::uint32_t>(key & 0xffffffffu)};
}

void note_keyboard_input() noexcept {
    g_last_input.store(kLastInputKeyboard, std::memory_order_relaxed);
}

// The style Auto shows now (button_icons sets it, the settings menu reads it).
std::atomic<input::ButtonIconStyle> g_auto_icon_style{input::ButtonIconStyle::Game};

input::ButtonIconStyle button_icons_auto_style() noexcept {
    return g_auto_icon_style.load(std::memory_order_relaxed);
}

namespace {
bool current_process_runtime_window(HWND& window);
}  // namespace

const input::KeyPositionMap& keyboard_position_map(HKL layout) {
    return key_positions_for_win32_layout(reinterpret_cast<std::uintptr_t>(layout));
}

// What the layout keys (input::key_icon_layout_key) print on the keyboard the game's window reads: each one's
// character alone (a dead key's too: a German keyboard's ^ and its acute accent), as a capital; 0 where it prints none.
// Looked up at most once a second. GALAXY_TEST_KEY_LAYOUT=<hex>,... (13 UCS-2 codes) stands in for them: hidden runs
// show another country's keyboard.
std::array<std::uint16_t, input::kKeyIconLayoutCount> keyboard_layout_labels() noexcept {
    static std::array<std::uint16_t, input::kKeyIconLayoutCount> s_labels{};
    static std::uint64_t s_checked_ms = 0u;
    const std::uint64_t now_ms = GetTickCount64();
    if (s_checked_ms != 0u && now_ms - s_checked_ms < 1000u) return s_labels;
    s_checked_ms = now_ms;
    HWND window = nullptr;
    const HKL layout =
        GetKeyboardLayout(current_process_runtime_window(window) ? GetWindowThreadProcessId(window, nullptr) : 0u);
    for (std::size_t i = 0; i < s_labels.size(); ++i) {
        // The top bit marks a dead key; the character is in the low word.
        wchar_t character[2] = {static_cast<wchar_t>(
                                    MapVirtualKeyExW(input::key_icon_layout_key(i), MAPVK_VK_TO_CHAR, layout) & 0xFFFFu),
                                L'\0'};
        if (character[0] != L'\0') CharUpperBuffW(character, 1u);
        s_labels[i] = static_cast<std::uint16_t>(character[0]);
    }
    return s_labels;
}

input::ButtonIconSpec button_icons(bool menu) noexcept {
    using input::ButtonIconStyle;
    using input::ControlScheme;
    // The game's thread asks each frame.
    // The pads attached: read once a second and when the last input's device changes (the roster asks XInput's empty
    // slots, which is slow).
    static std::array<ControlScheme, input::kMaxControllers> s_attached{};
    static std::array<std::uint32_t, input::kMaxControllers> s_attached_slot{};
    static std::array<input::ButtonIconModel, input::kMaxControllers> s_attached_icons{};
    static std::size_t s_attached_count = 0u;
    static std::uint64_t s_attached_checked_ms = 0u;
    static std::optional<input::DeviceKey> s_device;
    static std::optional<ControlScheme> s_pad;
    static std::uint32_t s_pad_slot = 0u;
    const std::optional<input::DeviceKey> device = last_input_device();
    const std::uint64_t now_ms = GetTickCount64();
    if (device != s_device || now_ms - s_attached_checked_ms >= 1000u) {
        s_device = device;
        s_attached_checked_ms = now_ms;
        std::array<input::ControllerInfo, input::kMaxControllers> attached{};
        s_attached_count = connected_controllers(attached);
        for (std::size_t i = 0; i < s_attached_count; ++i) {
            s_attached[i] = attached[i].scheme;
            s_attached_slot[i] = attached[i].slot;
            s_attached_icons[i] = attached[i].icons;
        }
    }
    const auto attached = [](ControlScheme scheme) {
        return std::find(s_attached.begin(), s_attached.begin() + s_attached_count, scheme) !=
                   s_attached.begin() + s_attached_count;
    };
    // The pad in use while it is attached; else the family remembered while such a pad is attached, else the first
    // pad attached, else none.
    if (device.has_value() && input::scheme_is_gamepad(device->scheme) && attached(device->scheme)) {
        s_pad = device->scheme;
        s_pad_slot = device->slot;
    } else if (s_pad.has_value() && !attached(*s_pad)) {
        s_pad = s_attached_count != 0u ? std::optional<ControlScheme>(s_attached[0]) : std::nullopt;
        s_pad_slot = s_attached_slot[0];
    }
    const std::optional<ControlScheme> pad = s_pad;
    // That pad's own model, else that of the first of its family attached.
    input::ButtonIconModel model = input::ButtonIconModel::Standard;
    bool model_found = false;
    for (std::size_t i = 0; pad.has_value() && i < s_attached_count; ++i) {
        if (s_attached[i] == *pad && (!model_found || s_attached_slot[i] == s_pad_slot)) {
            model = s_attached_icons[i];
            model_found = true;
        }
    }
    // Auto's: a real Wii Remote's own while one is in use; the keyboard's while it is (a key or a mouse button pressed
    // since a controller's last input; with no controller attached and none used, from the start), and always in the
    // keyboard and mouse input mode, never in the controller one; else the controller's family. Moving the mouse alone
    // leaves them (a controller's player may aim with it).
    const RuntimeInputMode mode = get_runtime_settings().input_mode;
    const bool keyboard_last = device.has_value() ? device->scheme == ControlScheme::Keyboard : !pad.has_value();
    const bool keyboard = mode == RuntimeInputMode::KeyboardMouse ||
                          (mode != RuntimeInputMode::Controller && keyboard_last);
    ButtonIconStyle auto_style = ButtonIconStyle::Game;
    if (real_wiimote_in_use()) {
    } else if (keyboard) {
        auto_style = ButtonIconStyle::Keyboard;
    } else {
        auto_style = input::button_icon_style(pad.value_or(ControlScheme::Keyboard));
    }
    g_auto_icon_style.store(auto_style, std::memory_order_relaxed);
    ButtonIconStyle style = auto_style;
    switch (get_runtime_settings().button_icons) {
    case ButtonIcons::Auto: break;
    case ButtonIcons::Game: style = ButtonIconStyle::Game; break;
    case ButtonIcons::Xbox: style = ButtonIconStyle::Xbox; break;
    case ButtonIcons::Nintendo: style = ButtonIconStyle::Nintendo; break;
    case ButtonIcons::PlayStation: style = ButtonIconStyle::PlayStation; break;
    case ButtonIcons::Keyboard: style = ButtonIconStyle::Keyboard; break;
    }
    // The bindings of the controller in use; a style chosen without one takes its own family's. A chosen style shows
    // the bound buttons by place (an 8BitDo pad seen as an Xbox one, with Nintendo's labels). The keyboard's are its own.
    const ControlScheme scheme = style == ButtonIconStyle::Keyboard  ? ControlScheme::Keyboard
                                 : pad.has_value()                   ? *pad
                                 : style == ButtonIconStyle::Nintendo    ? ControlScheme::SwitchPro
                                 : style == ButtonIconStyle::PlayStation ? ControlScheme::PlayStation
                                                                         : ControlScheme::Xbox;
    // Worked out again only when the bindings, the style, the controller or the context change.
    static std::uint64_t s_generation = ~std::uint64_t{0};
    static ButtonIconStyle s_style = ButtonIconStyle::Game;
    static ControlScheme s_scheme = ControlScheme::Keyboard;
    static input::ButtonIconModel s_model = input::ButtonIconModel::Standard;
    static bool s_menu = false;
    static HKL s_key_layout = nullptr;
    static input::ButtonIconSpec s_spec;
    const std::uint64_t generation = input::controls_config_generation();
    const HKL key_layout = g_keyboard_layout.load(std::memory_order_acquire);
    if (generation != s_generation || style != s_style || scheme != s_scheme || model != s_model || menu != s_menu ||
        key_layout != s_key_layout) {
        s_generation = generation;
        s_style = style;
        s_scheme = scheme;
        s_model = model;
        s_menu = menu;
        s_key_layout = key_layout;
        auto bindings = input::get_controls_config().table(scheme);
        if (scheme == ControlScheme::Keyboard)
            bindings = input::key_position_labels(bindings, keyboard_position_map(key_layout));
        s_spec = input::button_icon_spec(style, bindings, menu, model);
    }
    if (style == ButtonIconStyle::Keyboard) s_spec.layout = keyboard_layout_labels();
    return s_spec;
}

void publish_native_window_key_state(std::uint32_t key, bool down) noexcept {
    if (key <= 0xffu) g_keyboard_events.legacy_level(static_cast<std::uint8_t>(key), down);
}

void publish_native_window_key_event(std::uint32_t key, std::uintptr_t message_bits,
                                    bool down, bool repeat, bool injected) {
    if (key > 0xffu) return;
    const auto scan = static_cast<std::uint8_t>((message_bits >> 16u) & 0xffu);
    const bool extended = (message_bits & (std::uintptr_t{1} << 24u)) != 0;
    const std::uint32_t token = scan != 0 ? scan | (extended ? 0x100u : 0u) : 0x10000u | key;
    g_keyboard_events.event(token, static_cast<std::uint8_t>(key), key_position_from_win32_scan(scan, extended),
                            down, repeat, injected);
    g_window_key_injected[key].store(injected, std::memory_order_relaxed);
    if (down && !injected) note_keyboard_input();
}

void publish_native_window_input_origin(std::uint32_t key, bool injected) noexcept {
    if (key < g_window_key_injected.size()) {
        g_window_key_injected[key].store(injected, std::memory_order_relaxed);
        g_keyboard_events.legacy_origin(static_cast<std::uint8_t>(key), injected);
    }
    if (!injected) note_keyboard_input();
}

void publish_native_pointer_input_origin(bool button, bool injected) noexcept {
    (button ? g_pointer_button_injected : g_pointer_move_injected)
        .store(injected, std::memory_order_relaxed);
    if (button && !injected) note_keyboard_input();
}

bool native_window_key_injected(std::uint32_t key) noexcept {
    return key < g_window_key_injected.size() &&
           g_window_key_injected[key].load(std::memory_order_relaxed);
}

bool native_pointer_input_injected(bool button) noexcept {
    return (button ? g_pointer_button_injected : g_pointer_move_injected)
        .load(std::memory_order_relaxed);
}

void publish_native_window_key_press(std::uint32_t key) noexcept {
    if (key <= 0xffu) g_keyboard_events.legacy_press(static_cast<std::uint8_t>(key));
}

void clear_native_window_key_presses() noexcept {
    g_keyboard_events.clear();
}

struct HostPointerTransitionConsumerState {
    std::uint64_t cursor{};
    std::uint64_t focus_epoch{1};
    bool left_button{};
    bool right_button{};
};

static std::mutex g_host_pointer_transition_mutex;
static HostPointerTransitionConsumerState g_host_pointer_transition_state{};

void install_host_pointer_provider(HostPointerProvider provider) noexcept {
    g_host_pointer_provider.store(provider, std::memory_order_release);
}

void install_host_pointer_transition_provider(
    HostPointerTransitionProvider provider) noexcept {
    const std::lock_guard<std::mutex> lock(g_host_pointer_transition_mutex);
    g_host_pointer_transition_provider.store(provider, std::memory_order_release);
    g_host_pointer_transition_state = {};
}

void install_native_keyboard_capture_provider(
    NativeKeyboardCaptureProvider provider) noexcept {
    g_native_keyboard_capture_provider.store(
        provider,
        std::memory_order_release);
}

bool native_keyboard_capture_active() noexcept {
    const NativeKeyboardCaptureProvider provider =
        g_native_keyboard_capture_provider.load(std::memory_order_acquire);
    return provider != nullptr && provider();
}

void install_native_settings_pad_hooks(
    NativeSettingsVisibleProvider visible,
    NativeSettingsKeySink sink) noexcept {
    g_native_settings_visible.store(visible, std::memory_order_release);
    g_native_settings_key_sink.store(sink, std::memory_order_release);
}

namespace {

std::uint64_t host_pointer_now_ms() noexcept {
    return static_cast<std::uint64_t>(GetTickCount64());
}

struct HostPointerButtonSample {
    bool left_button{};
    bool right_button{};
    bool has_transition_coordinates{};
    HostPointerCoordinates transition_coordinates{};
    std::uint64_t transition_sequence{};
    std::uint64_t transition_absolute_acquired_ms{};
};

HostPointerButtonSample consume_host_pointer_button_transitions(
    const HostPointerState& current,
    bool preserve_each_edge) {
    const std::lock_guard<std::mutex> lock(g_host_pointer_transition_mutex);
    const HostPointerTransitionProvider provider =
        g_host_pointer_transition_provider.load(std::memory_order_acquire);
    if (provider == nullptr) {
        return HostPointerButtonSample{
            current.left_button,
            current.right_button,
            false,
            {},
            0,
            0};
    }

    HostPointerButtonSample sample{};
    constexpr std::size_t kMaximumDrain =
        kHostPointerTransitionCapacity + 1u;
    bool drain_completed = false;
    for (std::size_t drained = 0; drained < kMaximumDrain; ++drained) {
        const HostPointerTransitionPoll poll =
            provider(g_host_pointer_transition_state.cursor);
        if (poll.current_focus_epoch < g_host_pointer_transition_state.focus_epoch) {
            throw std::runtime_error("host pointer focus epoch regressed");
        }
        if (poll.current_focus_epoch > g_host_pointer_transition_state.focus_epoch) {
            // Focus cancellation is authoritative even while older edges are queued. Those edges retire
            // without replaying a click after focus has changed; every current-epoch edge is kept.
            g_host_pointer_transition_state.focus_epoch = poll.current_focus_epoch;
            g_host_pointer_transition_state.left_button = false;
            g_host_pointer_transition_state.right_button = false;
            sample = {};
        }
        if (poll.result == HostPointerTransitionReadResult::Overflow) {
            // The window thread published more edges than the journal holds while this reader was away (a stall
            // of seconds under a hammered button): the lost edges are dropped and the buttons are what the pointer
            // state reports now, as after a focus change.
            std::cerr << "[input] pointer journal overrun: cursor=" << g_host_pointer_transition_state.cursor
                      << " latest=" << poll.latest_sequence << ", buttons resynchronised\n";
            g_host_pointer_transition_state.cursor = poll.latest_sequence;
            g_host_pointer_transition_state.left_button = current.left_button;
            g_host_pointer_transition_state.right_button = current.right_button;
            sample = {};
            drain_completed = true;
            break;
        }
        if (poll.result == HostPointerTransitionReadResult::Empty) {
            drain_completed = true;
            break;
        }
        if (poll.result != HostPointerTransitionReadResult::Ready) {
            throw std::runtime_error(
                "host pointer transition provider returned an invalid result");
        }
        const HostPointerTransition& transition = poll.transition;
        if (transition.sequence !=
                g_host_pointer_transition_state.cursor + 1u ||
            transition.focus_epoch > poll.current_focus_epoch) {
            throw std::runtime_error(
                "host pointer transition sequence/epoch invariant failed");
        }

        g_host_pointer_transition_state.cursor = transition.sequence;
        if (transition.focus_epoch < g_host_pointer_transition_state.focus_epoch) {
            continue;
        }
        switch (transition.kind) {
        case HostPointerTransitionKind::LeftDown:
            g_host_pointer_transition_state.left_button = true;
            break;
        case HostPointerTransitionKind::LeftUp:
            g_host_pointer_transition_state.left_button = false;
            break;
        case HostPointerTransitionKind::RightDown:
            g_host_pointer_transition_state.right_button = true;
            break;
        case HostPointerTransitionKind::RightUp:
            g_host_pointer_transition_state.right_button = false;
            break;
        case HostPointerTransitionKind::CancelAll:
            g_host_pointer_transition_state.left_button = false;
            g_host_pointer_transition_state.right_button = false;
            break;
        }
        sample.has_transition_coordinates =
            transition.coordinates.absolute_valid;
        sample.transition_coordinates = transition.coordinates;
        sample.transition_sequence = transition.sequence;
        sample.transition_absolute_acquired_ms =
            transition.absolute_acquired_ms;
        if (preserve_each_edge) {
            drain_completed = true;
            break;
        }
    }
    if (!drain_completed) {
        throw std::runtime_error(
            "host pointer transition provider did not terminate a bounded drain");
    }

    sample.left_button = g_host_pointer_transition_state.left_button;
    sample.right_button = g_host_pointer_transition_state.right_button;
    return sample;
}

struct NativeHidControllerState {
    bool connected = false;
    DWORD slot = XUSER_MAX_COUNT;
    WORD buttons = 0;
    BYTE left_trigger = 0;
    BYTE right_trigger = 0;
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
};

// Runs the settings-overlay controller chord and navigation on one pad sample (live or replayed).
// `buttons` is what the game sees; true means sticks and triggers must read neutral too.
bool route_native_settings_pad(std::uint16_t& buttons, float stick_x, float stick_y) {
    const NativeSettingsVisibleProvider visible =
        g_native_settings_visible.load(std::memory_order_acquire);
    const NativeSettingsKeySink sink =
        g_native_settings_key_sink.load(std::memory_order_acquire);
    if (visible == nullptr || sink == nullptr) {
        return false;
    }
    static galaxy::input::NativeSettingsPadState s_state{};
    const galaxy::input::NativeSettingsPadStep step =
        galaxy::input::native_settings_pad_step(
            s_state, buttons, stick_x, stick_y, visible(), GetTickCount64());
    for (std::size_t i = 0; i < step.key_count; ++i) {
        sink(step.keys[i]);
    }
    buttons = step.game_buttons;
    return step.game_neutral;
}

// Every connected XInput pad. Sticks are normalised with `left_deadzone` on the left stick and
// none on the right; the pointer applies a radial dead zone.
std::size_t poll_native_xinput_pads(
    std::int16_t left_deadzone,
    std::array<NativeHidControllerState, XUSER_MAX_COUNT>& out) {
    static ULONGLONG s_slot_retry_at[XUSER_MAX_COUNT]{};
    const ULONGLONG now_ms = GetTickCount64();
    std::size_t count = 0;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if (now_ms < s_slot_retry_at[slot]) {
            continue;
        }
        XINPUT_STATE state{};
        if (XInputGetState(slot, &state) != ERROR_SUCCESS) {
            s_slot_retry_at[slot] = now_ms + 2000u;
            continue;
        }
        s_slot_retry_at[slot] = 0u;
        NativeHidControllerState& controller = out[count++];
        controller = {};
        controller.connected = true;
        controller.slot = slot;
        controller.buttons = state.Gamepad.wButtons;
        controller.left_trigger = state.Gamepad.bLeftTrigger;
        controller.right_trigger = state.Gamepad.bRightTrigger;
        controller.left_x = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbLX, left_deadzone);
        controller.left_y = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbLY, left_deadzone);
        controller.right_x = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbRX, 0);
        controller.right_y = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbRY, 0);
    }
    return count;
}

// The remappable controls as the input consumer sees them: a cached copy
// refreshed when the published generation changes (one atomic load per
// sample). The window publishes keyboard events separately.
struct LiveControls {
    galaxy::input::ControlsConfig config = galaxy::input::default_controls_config();
    std::uint64_t generation = 0;
};

const LiveControls& live_controls() {
    static LiveControls s_live{};
    const std::uint64_t generation = galaxy::input::controls_config_generation();
    if (generation != s_live.generation) {
        s_live.config = galaxy::input::get_controls_config();
        s_live.generation = generation;
    }
    return s_live;
}

std::int16_t deadzone_counts(float fraction) {
    return static_cast<std::int16_t>(std::clamp(
        std::lround(static_cast<double>(fraction) * 32767.0), 0l, 32767l));
}

// Vibration (controller_hid.h): the controller in use, its strength setting and the window focus. The live input
// path sets these each sample and the game's motor state reads them when it is published. Game thread only; the
// replay path leaves it empty (no controller vibrates).
struct RumbleHost {
    RumbleTarget target{};
    std::uint8_t strength = 0;
    bool focused = false;
};
RumbleHost g_rumble_host{};
void publish_rumble_for_mode(std::uint8_t strength, std::optional<std::uint32_t> star_pointer_mode) noexcept;

// `right_deadzone` is radial (pointer_control.h); the right stick must arrive
// without a per-axis dead zone.
galaxy::input::PadSample pad_sample_from_xinput(const NativeHidControllerState& controller,
                                                float right_deadzone) {
    galaxy::input::PadSample sample{};
    sample.connected = controller.connected;
    sample.scheme = galaxy::input::ControlScheme::Xbox;
    sample.buttons = controller.buttons;
    sample.axes[0] = controller.left_x;
    sample.axes[1] = controller.left_y;
    const galaxy::input::StickVector right =
        galaxy::input::apply_radial_deadzone(controller.right_x, controller.right_y, right_deadzone);
    sample.axes[2] = right.x;
    sample.axes[3] = right.y;
    sample.axes[4] = static_cast<float>(controller.left_trigger) / 255.0f;
    sample.axes[5] = static_cast<float>(controller.right_trigger) / 255.0f;
    return sample;
}

std::size_t collect_hid_pads(const galaxy::input::ControlsConfig& config, std::vector<LivePad>& out) {
    static std::vector<HidPadState> pads;  // the game thread's, reused each frame
    const std::size_t count = read_hid_pads(pads, true);
    out.assign(count, LivePad{});
    const std::uint64_t now_us = controller_hid_now_us();
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::input::SchemeTuning& tuning = config.tune(pads[i].scheme);
        LivePad& live = out[i];
        live = {};
        live.sample = galaxy::input::switch_hid::pad_sample_from(
            pads[i].decoded, pads[i].scheme, pads[i].left, pads[i].right,
            tuning.stick_deadzone, 0.0f);
        const galaxy::input::StickVector right = galaxy::input::apply_radial_deadzone(
            live.sample.axes[2], live.sample.axes[3], tuning.pointer_deadzone);
        live.sample.axes[2] = right.x;
        live.sample.axes[3] = right.y;
        live.imu_live = pads[i].imu_live;
        live.gyro_missing = pads[i].gyro_missing;
        const bool shaking = pads[i].shake_until_us != 0u && now_us < pads[i].shake_until_us;
        live.shake_remote = shaking && tuning.motion_shake == galaxy::input::MotionShake::Remote;
        live.shake_nunchuk = shaking && tuning.motion_shake == galaxy::input::MotionShake::Nunchuk;
        live.gyro_x = pads[i].gyro_x;
        live.gyro_y = pads[i].gyro_y;
        live.touch_live = pads[i].touch_live;
        live.touch_dx = pads[i].touch_dx;
        live.touch_dy = pads[i].touch_dy;
        live.gravity_valid = pads[i].gravity_valid && pads[i].imu_live;
        live.gravity = pads[i].gravity;
        live.device_slot = pads[i].slot;
    }
    return count;
}

bool route_native_settings_pads(std::span<LivePad> pads, bool capture_finished) {
    static std::uint16_t s_capture_hold = 0u;
    std::uint16_t word = 0u;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    for (const LivePad& pad : pads) {
        if (!pad.sample.connected) {
            continue;
        }
        word = static_cast<std::uint16_t>(
            word | galaxy::input::settings_pad_buttons(pad.sample.scheme, pad.sample.buttons));
        if (std::fabs(pad.sample.axes[0]) > std::fabs(stick_x)) stick_x = pad.sample.axes[0];
        if (std::fabs(pad.sample.axes[1]) > std::fabs(stick_y)) stick_y = pad.sample.axes[1];
    }
    // A capture's press stays away from the overlay's navigation until it is
    // released, and while capturing the pad reaches neither overlay nor game.
    if (capture_finished || g_binding_capture_armed.load(std::memory_order_acquire) != 0u) {
        s_capture_hold = word;
        for (LivePad& pad : pads) {
            pad.sample.buttons = 0u;
            pad.sample.axes = {};
        }
        return true;
    }
    s_capture_hold = static_cast<std::uint16_t>(s_capture_hold & word);
    word = static_cast<std::uint16_t>(word & ~s_capture_hold);
    const bool hold_neutral = s_capture_hold != 0u;
    const bool any_connected = std::any_of(pads.begin(), pads.end(),
        [](const LivePad& pad) { return pad.sample.connected; });
    if (!any_connected) {
        return false;
    }
    std::uint16_t game_word = word;
    const bool neutral = route_native_settings_pad(game_word, stick_x, stick_y) || hold_neutral;
    for (LivePad& pad : pads) {
        if (!pad.sample.connected) {
            continue;
        }
        const std::uint16_t own =
            galaxy::input::settings_pad_buttons(pad.sample.scheme, pad.sample.buttons);
        pad.sample.buttons = galaxy::input::unified_from_settings_pad(
            pad.sample.scheme, static_cast<std::uint16_t>(own & game_word),
            neutral ? 0u : pad.sample.buttons);
        if (neutral) {
            pad.sample.axes = {};
            pad.shake_remote = pad.shake_nunchuk = false;
            pad.gyro_x = pad.gyro_y = 0.0f;
            pad.touch_dx = pad.touch_dy = 0.0f;
        }
    }
    return neutral;
}

// Returns true on the sample that completes a capture.
bool service_binding_capture(std::span<const LivePad> pads) {
    static galaxy::input::PadSample s_baseline{};
    static std::uint64_t s_baseline_for = 0u;
    const std::uint64_t armed = g_binding_capture_armed.load(std::memory_order_acquire);
    if (armed == 0u) {
        s_baseline_for = 0u;
        return false;
    }
    const auto scheme = capture_scheme_of(armed);
    const auto slot = static_cast<std::uint32_t>(armed >> 8u);
    const LivePad* match = nullptr;
    for (const LivePad& pad : pads) {
        if (pad.sample.connected && pad.sample.scheme == scheme &&
            (slot == galaxy::input::kKeyboardSlot || pad.device_slot == slot)) {
            match = &pad;
            break;
        }
    }
    if (match == nullptr) {
        return false;
    }
    if (s_baseline_for != armed) {
        s_baseline = match->sample;
        s_baseline_for = armed;
        return false;
    }
    const auto source = galaxy::input::detect_pad_capture(s_baseline, match->sample);
    s_baseline = match->sample;
    if (!source) {
        return false;
    }
    std::uint64_t expected = armed;
    if (!g_binding_capture_armed.compare_exchange_strong(expected, 0u, std::memory_order_acq_rel)) {
        return false;
    }
    g_binding_capture_result.store(
        0x10000u | (static_cast<std::uint32_t>(source->kind) << 8u) | source->code,
        std::memory_order_release);
    s_baseline_for = 0u;
    const NativeSettingsKeySink sink = g_native_settings_key_sink.load(std::memory_order_acquire);
    if (sink != nullptr) {
        sink(kBindingCaptureKey);
    }
    return true;
}

bool foreground_process_window(HWND& window) {
    window = GetForegroundWindow();
    if (window == nullptr || !IsWindow(window) || !IsWindowVisible(window)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

bool current_process_window_under_cursor(POINT& cursor, HWND& window) {
    if (!GetCursorPos(&cursor)) {
        window = nullptr;
        return false;
    }
    HWND hovered = WindowFromPoint(cursor);
    if (hovered == nullptr) {
        window = nullptr;
        return false;
    }
    if (HWND root = GetAncestor(hovered, GA_ROOT); root != nullptr) {
        hovered = root;
    }
    if (!IsWindow(hovered) || !IsWindowVisible(hovered)) {
        window = nullptr;
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hovered, &pid);
    if (pid != GetCurrentProcessId()) {
        window = nullptr;
        return false;
    }
    window = hovered;
    return true;
}

bool current_process_runtime_window(HWND& window) {
    // The runtime's own window once found: a search walks every top-level window of the desktop, and this is asked
    // each frame while the pointer is elsewhere. It is searched for again when that window is gone or has no client area.
    static std::atomic<HWND> s_runtime_window{nullptr};
    if (const HWND known = s_runtime_window.load(std::memory_order_relaxed); known != nullptr) {
        DWORD known_pid = 0;
        RECT known_client{};
        if (IsWindow(known) && IsWindowVisible(known) && GetWindowThreadProcessId(known, &known_pid) != 0u &&
            known_pid == GetCurrentProcessId() && GetClientRect(known, &known_client) &&
            known_client.right > known_client.left && known_client.bottom > known_client.top) {
            window = known;
            return true;
        }
        s_runtime_window.store(nullptr, std::memory_order_relaxed);
    }
    struct SearchState {
        DWORD pid = GetCurrentProcessId();
        HWND window = nullptr;
        HWND fallback_window = nullptr;
        std::int64_t fallback_area = 0;
    } state{};

    EnumWindows(
        [](HWND candidate, LPARAM user) -> BOOL {
            auto* search = reinterpret_cast<SearchState*>(user);
            if (candidate == nullptr || !IsWindow(candidate)) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            if (pid != search->pid) {
                return TRUE;
            }
            if (!IsWindowVisible(candidate)) {
                return TRUE;
            }
            RECT client{};
            if (!GetClientRect(candidate, &client) ||
                client.right <= client.left ||
                client.bottom <= client.top) {
                return TRUE;
            }
            const std::int64_t area =
                static_cast<std::int64_t>(client.right - client.left) *
                static_cast<std::int64_t>(client.bottom - client.top);
            if (area > search->fallback_area) {
                search->fallback_window = candidate;
                search->fallback_area = area;
            }
            wchar_t class_name[64]{};
            if (GetClassNameW(candidate, class_name,
                              static_cast<int>(
                                  sizeof(class_name) /
                                  sizeof(class_name[0]))) == 0 ||
                lstrcmpW(class_name, L"NebulaRuntimeWndR") != 0) {
                return TRUE;
            }
            search->window = candidate;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&state));

    if (state.window != nullptr) {
        s_runtime_window.store(state.window, std::memory_order_relaxed);
    }
    window = state.window != nullptr ? state.window : state.fallback_window;
    return window != nullptr;
}

galaxy::input::NativeMousePointerProjection project_client_mouse_pointer(
    bool window_focused,
    bool absolute_valid,
    bool inside_client,
    int client_x,
    int client_y,
    int client_width,
    int client_height) {
    const auto actual = galaxy::render::current_host_client_extent();
    if (actual.width > 1 && actual.height > 1 &&
        (actual.width != client_width ||
         actual.height != client_height)) {
        // A mouse sample from before a resize is not projected through the new window shape while the game's
        // aspect is being latched.
        return {};
    }
    const auto content=galaxy::g_presentation_content.read();
    if (!content || content->surface_width!=client_width ||
        content->surface_height!=client_height) return {};
    const auto viewport=content->viewport;
    return galaxy::input::project_native_mouse_pointer({
        window_focused,
        absolute_valid,
        inside_client,
        client_x,
        client_y,
        client_width,
        client_height,
        viewport,
    });
}

class WindowLiveControlsHost final : public LiveControlsHost {
public:
    WindowLiveControlsHost(HWND window, bool focused) : window_(window), focused_(focused) {}
    std::uint64_t now_ms() override { return host_pointer_now_ms(); }
    std::uint64_t now_us() override { return controller_hid_now_us(); }
    bool injected(bool button) override { return native_pointer_input_injected(button); }
    PointerSample pointer(bool buttons_allowed, bool check_hover) override {
        const auto provider = g_host_pointer_provider.load(std::memory_order_acquire);
        if (provider == nullptr) return {};
        auto position = provider();
        const auto buttons = consume_host_pointer_button_transitions(position, buttons_allowed);
        position.left_button = buttons.left_button;
        position.right_button = buttons.right_button;
        if (buttons.has_transition_coordinates) {
            position.absolute_valid = buttons.transition_coordinates.absolute_valid;
            position.inside_client = buttons.transition_coordinates.inside_client;
            position.client_x = buttons.transition_coordinates.client_x;
            position.client_y = buttons.transition_coordinates.client_y;
            position.client_width = buttons.transition_coordinates.client_width;
            position.client_height = buttons.transition_coordinates.client_height;
        }
        const auto projected = project_client_mouse_pointer(position.window_focused, position.absolute_valid,
            position.inside_client, position.client_x, position.client_y, position.client_width, position.client_height);
        bool deliver = false;
        if (check_hover && (position.left_button || position.right_button)) {
            POINT cursor{};
            HWND hovered_window = nullptr;
            HWND game_window = reinterpret_cast<HWND>(position.debug_window);
            if (game_window != nullptr) {
                if (HWND root = GetAncestor(game_window, GA_ROOT); root != nullptr) game_window = root;
            }
            const bool under_cursor = game_window != nullptr &&
                current_process_window_under_cursor(cursor, hovered_window) && hovered_window == game_window;
            deliver = select_host_pointer_mouse_button_delivery(position.window_focused, position.absolute_valid,
                position.inside_client, projected.visible, under_cursor);
        }
        // The pointer is the window's cursor position (WM_MOUSEMOVE, one OS pointer curve): no raw device motion is read.
        return {true, position.window_focused, position.absolute_valid, position.inside_client,
                position.left_button, position.right_button, deliver, buttons.has_transition_coordinates,
                position.absolute_sequence, buttons.transition_sequence, projected,
                false};
    }
    galaxy::input::NativeMousePointerProjection cursor() override {
        POINT position{};
        HWND window = window_;
        bool active = focused_ && window != nullptr;
        if (!active) active = current_process_window_under_cursor(position, window);
        else if (!GetCursorPos(&position)) active = false;
        if (!active && current_process_runtime_window(window) && GetCursorPos(&position)) active = true;
        RECT client{};
        if (!active || !ScreenToClient(window, &position) || !GetClientRect(window, &client)) return {};
        const int width = client.right - client.left, height = client.bottom - client.top;
        if (width > 1 && height > 1 &&
            (position.x < 0 || position.y < 0 || position.x >= width || position.y >= height)) active = false;
        return project_client_mouse_pointer(active, active && width > 1 && height > 1, active,
            static_cast<int>(position.x), static_cast<int>(position.y), width, height);
    }
private:
    HWND window_;
    bool focused_;
};

// The live controls as the player's input: the bindings of every connected controller and of the keyboard and mouse,
// the pointer from whichever source moved last, tilt, shake and the scripted test input.
// Co-star mode: player 2 is the last controller attached when two or more are; player 1 no longer hears it. It points with whichever stick its
// bindings aim the pointer with, moving the pointer at the pointer speed.
std::mutex g_co_star_lock;
bool g_co_star_on = false;
galaxy::input::PlayerInput g_co_star_input{};

double dt_seconds_since_co_star() noexcept {
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    return std::min(seconds, 0.1);
}

void update_co_star(std::span<LivePad> pads, const galaxy::input::ControlsConfig& config, float seconds) {
    namespace in = galaxy::input;
    using in::GameInput;
    in::PlayerInput player{};
    bool on = false;
    if (galaxy::get_runtime_settings().co_star) {
        std::size_t connected = 0u;
        LivePad* last = nullptr;
        for (LivePad& pad : pads) {
            if (pad.sample.connected) {
                ++connected;
                last = &pad;
            }
        }
        if (connected >= 2u) {
            static float s_x = 0.0f, s_y = 0.0f;
            const in::LogicalInput logic = in::evaluate_pad_bindings(config.table(last->sample.scheme), last->sample, 0.5f);
            const auto held = [&logic](GameInput target) { return logic.held(target); };
            s_x = std::clamp(s_x + logic.pointer_x * seconds * 1.5f, -1.0f, 1.0f);
            s_y = std::clamp(s_y - logic.pointer_y * seconds * 1.5f, -1.0f, 1.0f);
            in::set_pointer(player, s_x, s_y, true);
            player.buttons.a = held(GameInput::A);
            player.buttons.b = held(GameInput::B);
            player.buttons.one = held(GameInput::One);
            player.buttons.two = held(GameInput::Two);
            player.buttons.plus = held(GameInput::Plus);
            player.buttons.minus = held(GameInput::Minus);
            player.buttons.up = held(GameInput::DpadUp);
            player.buttons.down = held(GameInput::DpadDown);
            player.buttons.left = held(GameInput::DpadLeft);
            player.buttons.right = held(GameInput::DpadRight);
            *last = {};
            on = true;
        }
    }
    const std::lock_guard<std::mutex> lock(g_co_star_lock);
    g_co_star_on = on;
    g_co_star_input = player;
}

galaxy::input::PlayerInput build_live_input(
    std::uint64_t current_frame,
    const galaxy::RuntimeInputModeState& input_mode_state,
    galaxy::input::PlayContext play_context,
    galaxy::input::tilt::Section tilt_section) {
    const NativeInputScriptOverlay script = extension_controls(current_frame);
    // A hidden start (a test) is no keyboard or mouse source: no focus, key or cursor calls are made, while controllers
    // and extensions still work.
    const bool host_window_input_enabled = !::galaxy::launch_args().started_hidden;
    HWND focused_window = nullptr;
    const bool window_focused =
        host_window_input_enabled &&
        foreground_process_window(focused_window);
    const bool native_keyboard_captured =
        host_window_input_enabled && native_keyboard_capture_active();
    if (!window_focused || native_keyboard_captured) {
        clear_native_window_key_presses();
    }
    const LiveControls& controls = live_controls();
    input::KeyboardSnapshot keyboard_sample;
    if (window_focused && !native_keyboard_captured) keyboard_sample = g_keyboard_events.sample();
    if (window_focused) {
        g_keyboard_layout.store(keyboard_sample.positions_available
            ? GetKeyboardLayout(GetWindowThreadProcessId(focused_window, nullptr)) : nullptr,
            std::memory_order_release);
    }
    auto keyboard_levels = keyboard_sample.all;
    // Older window dispatchers supply logical keys only; keep their existing behavior until scan events arrive.
    if (!keyboard_sample.positions_available)
        keyboard_levels = input::project_key_positions(keyboard_levels, input::KeyPositionMap{});

    const galaxy::RuntimeInputMode input_mode = input_mode_state.mode;
    // Auto: keyboard, mouse and controllers together. The two overrides keep
    // one source; the real Wii Remote mode uses neither.
    const bool use_keyboard = input_mode == galaxy::RuntimeInputMode::Auto ||
                              input_mode == galaxy::RuntimeInputMode::KeyboardMouse;
    const std::uint64_t capture_armed = g_binding_capture_armed.load(std::memory_order_acquire);
    const galaxy::input::ControlScheme capture_scheme = capture_scheme_of(capture_armed);
    const bool capturing = capture_armed != 0u;
    const bool controller_mode_polls =
        (input_mode == galaxy::RuntimeInputMode::Controller ||
         input_mode == galaxy::RuntimeInputMode::Auto) &&
        window_focused;
    const bool poll_controller =
        controller_mode_polls || (capturing && capture_scheme == galaxy::input::ControlScheme::Xbox);
    const galaxy::input::SchemeTuning& xbox_tuning =
        controls.config.tune(galaxy::input::ControlScheme::Xbox);
    // Every XInput pad is read; the right stick arrives raw and the pointer
    // takes a radial dead zone.
    std::array<NativeHidControllerState, XUSER_MAX_COUNT> xinput_pads{};
    const std::size_t xinput_count = poll_controller
        ? poll_native_xinput_pads(deadzone_counts(xbox_tuning.stick_deadzone), xinput_pads)
        : 0u;
    if (poll_controller) {
        std::uint32_t xinput_mask = 0u;
        for (std::size_t i = 0; i < xinput_count; ++i) xinput_mask |= 1u << xinput_pads[i].slot;
        g_xinput_mask.store(xinput_mask, std::memory_order_release);
        g_xinput_looked_ms.store(GetTickCount64(), std::memory_order_relaxed);
    }
    const float xinput_pointer_deadzone_fraction =
        static_cast<float>(deadzone_counts(xbox_tuning.pointer_deadzone)) / 32767.0f;
    static std::vector<LivePad> pads;  // the game thread's, reused each frame
    pads.clear();
    for (std::size_t i = 0; i < std::max<std::size_t>(xinput_count, 1u); ++i) {
        LivePad& pad = pads.emplace_back();
        pad.device_slot = i < xinput_count ? static_cast<std::uint32_t>(xinput_pads[i].slot) : galaxy::input::kKeyboardSlot;
        pad.sample = pad_sample_from_xinput(
            i < xinput_count ? xinput_pads[i] : NativeHidControllerState{},
            xinput_pointer_deadzone_fraction);
    }
    if (controller_mode_polls || (capturing && galaxy::input::scheme_uses_hid(capture_scheme))) {
        ensure_controller_hid_started();
        static std::vector<LivePad> hid_pads;
        collect_hid_pads(controls.config, hid_pads);
        pads.insert(pads.end(), hid_pads.begin(), hid_pads.end());
    }
    const std::span<LivePad> live_pads(pads.data(), pads.size());
    update_co_star(live_pads, controls.config, static_cast<float>(dt_seconds_since_co_star()));
    // Before the overlay takes the pads: the controller the player is using, for the CONTROLS page.
    for (const LivePad& pad : live_pads) {
        if (!pad.sample.connected) continue;
        if (pad_has_input(pad.sample) || pad.shake_remote || pad.shake_nunchuk ||
            std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f ||
            std::fabs(pad.touch_dx) + std::fabs(pad.touch_dy) > 0.0005f) {
            g_last_input.store((static_cast<std::uint64_t>(pad.sample.scheme) << 32u) | pad.device_slot,
                               std::memory_order_relaxed);
        }
    }
    const bool capture_finished = service_binding_capture(live_pads);
    (void)route_native_settings_pads(live_pads, capture_finished);
    if (!controller_mode_polls) {
        for (LivePad& pad : live_pads) {
            pad = {};
        }
    }
    const bool controller_connected = std::any_of(live_pads.begin(), live_pads.end(),
        [](const LivePad& pad) { return pad.sample.connected; });
    const bool use_pads = controller_mode_polls && controller_connected;
    // With a controller attached, keys and mouse input synthesised by SendInput would double every
    // press and drag the pointer: Steam's desktop controller layout turns the same buttons and right
    // stick into keys and mouse motion. Only physical keyboard and mouse input counts then.
    const bool drop_injected = use_pads && input_mode == galaxy::RuntimeInputMode::Auto;
    if (drop_injected) {
        keyboard_levels = keyboard_sample.physical;
        if (!keyboard_sample.positions_available)
            keyboard_levels = input::project_key_positions(keyboard_levels, input::KeyPositionMap{});
    }
    if (keyboard_levels.key(VK_MBUTTON)) keyboard_levels.mouse |= 1u << 2u;
    if (keyboard_levels.key(VK_XBUTTON1)) keyboard_levels.mouse |= 1u << 3u;
    if (keyboard_levels.key(VK_XBUTTON2)) keyboard_levels.mouse |= 1u << 4u;
    const bool allow_absolute_pointer =
        host_window_input_enabled && use_keyboard &&
        !galaxy::is_wii_remote_mode(input_mode);

    LiveControlsFrame frame{controls.config};
    frame.pads = live_pads;
    frame.keyboard_levels = keyboard_levels;
    frame.script = script;
    frame.game_frame = current_frame;
    frame.script_pointer_grace_ms = 250u;
    frame.window_focused = window_focused;
    frame.use_keyboard = use_keyboard;
    frame.use_pads = use_pads;
    frame.allow_absolute_pointer = allow_absolute_pointer;
    frame.drop_injected = drop_injected;
    frame.context = play_context;
    frame.tilt_section = tilt_section;
    static LiveControlsEngine engine;
    WindowLiveControlsHost host(focused_window, window_focused);
    auto result = engine.sample(frame, host);
    g_rumble_host = {};
    g_rumble_host.focused = result.rumble.focused;
    g_rumble_host.strength = result.rumble.strength;
    if (result.rumble.device) {
        const auto [scheme, slot] = *result.rumble.device;
        if (scheme == galaxy::input::ControlScheme::Xbox && slot < XUSER_MAX_COUNT) {
            g_rumble_host.target = {RumbleTarget::Kind::XInput, slot};
        } else if (galaxy::input::scheme_uses_hid(scheme) && slot < kMaxHidPads) {
            g_rumble_host.target = {RumbleTarget::Kind::Hid, slot, scheme};
        }
    }
    return result.input;
}

}  // namespace

bool native_game_co_star(galaxy::input::PlayerInput* input) noexcept {
    const std::lock_guard<std::mutex> lock(g_co_star_lock);
    if (!g_co_star_on) {
        return false;
    }
    *input = g_co_star_input;
    return true;
}

galaxy::input::PlayerInput native_game_input(
    std::uint64_t current_frame,
    std::int32_t star_pointer_mode,
    std::uint8_t tilt_section) {
    const galaxy::input::PlayContext context =
        star_pointer_mode >= 0 && static_cast<std::uint32_t>(star_pointer_mode) < galaxy::input::kStarPointerModeCount
        ? galaxy::input::play_context_from_star_pointer_mode(static_cast<std::uint32_t>(star_pointer_mode))
        : galaxy::input::PlayContext::Play;
    const galaxy::input::tilt::Section section = tilt_section == 1u ? galaxy::input::tilt::Section::StarBall
        : tilt_section == 2u ? galaxy::input::tilt::Section::Ray
        : galaxy::input::tilt::Section::None;
    return build_live_input(current_frame, galaxy::get_runtime_input_mode_state(), context, section);
}

void native_game_rumble(std::uint8_t strength, std::int32_t star_pointer_mode) noexcept {
    publish_rumble_for_mode(
        strength, star_pointer_mode >= 0 ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(star_pointer_mode))
                                         : std::nullopt);
}

namespace {

void publish_rumble_for_mode(std::uint8_t strength, std::optional<std::uint32_t> star_pointer_mode) noexcept {
    RumbleRequest request{};
    request.motor_on = strength != 0u;
    request.level = galaxy::input::rumble::quantize_level(static_cast<float>(strength) / 255.0f);
    request.target = g_rumble_host.target;
    request.strength = g_rumble_host.strength;
    request.focused = g_rumble_host.focused;
    const std::optional<std::uint32_t>& mode = star_pointer_mode;
    const bool menu = mode.has_value() && (*mode == 0u || *mode == 2u || *mode == 7u);
    const NativeSettingsVisibleProvider overlay = g_native_settings_visible.load(std::memory_order_acquire);
    request.paused = menu || (overlay != nullptr && overlay());
    publish_rumble_request(request);
}

}  // namespace


}  // namespace galaxy::host
