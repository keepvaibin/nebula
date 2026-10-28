// SPDX-License-Identifier: GPL-3.0-only
// The game window and input on SDL3 platforms (sdl_host.h, host_window.h and the window functions of gx_d3d12.h):
// the SDL window and its Vulkan surface; its events (size, focus, quit, the finger and the mouse as the pointer, keys,
// gamepads); on Android the activity's touch controls and HOME Menu through JNI.

#include "sdl_host.h"

#include "galaxy/gx/display_modes.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/vk/vk_context.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_host.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/virtual_keys.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

namespace galaxy::sdl {

// The device that gave input last (button_icons' Auto): none (touch controls), the keyboard and mouse, or a
// controller: kLastPad plus its scheme.
constexpr std::uint32_t kLastNone = 0u;
constexpr std::uint32_t kLastKeyboard = 1u;
constexpr std::uint32_t kLastPad = 2u;
std::atomic<std::uint32_t> g_last_input{kLastNone};
std::atomic<SDL_JoystickID> g_last_pad{0};

namespace {

std::mutex g_input_mutex;
InputState g_input;                 // guarded by g_input_mutex
SDL_Window* g_window = nullptr;
std::atomic<int> g_home_choice{-1};
std::atomic<bool> g_home_visible{false};
std::atomic<bool> g_background{false};
std::atomic<bool> g_menu_context{false};

// SDL's thread, the only one that calls SDL's window, gamepad and Java functions. The game's threads run on stacks of
// their own, which the Java VM refuses to attach (it aborts the process), so they leave requests here instead, done
// after each pump (every 4 ms at most).
std::thread::id g_sdl_thread;
std::atomic<int> g_home_request{0};  // 1 show the HOME Menu, 2 hide it
std::atomic<std::uint32_t> g_rumble_pad{0};
std::atomic<std::uint32_t> g_rumble_level{0};
// What SDL's thread last sent, and when (it renews a rumble that lasts).
std::uint32_t g_rumble_sent_pad = 0;
std::uint32_t g_rumble_sent_level = 0;
std::uint64_t g_rumble_sent_ms = 0;

// An on-screen button stays pressed for the game at least this long: the game reads the remote once a frame, and a
// quick tap would otherwise end between two reads.
constexpr std::uint64_t kMinTouchPressMs = 80;
std::array<std::uint64_t, 32> g_touch_held_until{};  // per TouchButton bit, steady ms; guarded by g_input_mutex
std::array<SDL_Gamepad*, kMaxPads> g_gamepads{};

// A touch that ends within this time, having moved less than this fraction of the window's shorter side, is a tap.
constexpr std::uint64_t kTapMs = 280;
constexpr float kTapTravel = 0.03f;
std::uint64_t g_pointer_finger = 0;
bool g_pointer_finger_valid = false;
std::uint64_t g_finger_down_ms = 0;
float g_finger_down_x = 0.0f;
float g_finger_down_y = 0.0f;
float g_finger_travel = 0.0f;

#if defined(__ANDROID__)
// stdout and stderr to logcat and to <data>/logs/game.log, line by line (the host and the renderer log there).
void forward_output_to_logcat() {
    static int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return;
    }
    std::FILE* file = nullptr;
    if (const std::filesystem::path data = galaxy::nebula_data_directory(); !data.empty()) {
        std::error_code error;
        std::filesystem::create_directories(data / "logs", error);
        std::filesystem::rename(data / "logs" / "game.log", data / "logs" / "game.previous.log", error);
        file = galaxy::open_path(data / "logs" / "game.log", "w");
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    std::thread([file] {
        char line[1024];
        std::size_t used = 0;
        const auto emit = [file](const char* text) {
            __android_log_write(ANDROID_LOG_INFO, "Nebula", text);
            if (file != nullptr) {
                std::fputs(text, file);
                std::fputc('\n', file);
                std::fflush(file);
            }
        };
        for (;;) {
            const ssize_t n = read(pipe_fds[0], line + used, sizeof(line) - 1 - used);
            if (n <= 0) {
                return;
            }
            used += static_cast<std::size_t>(n);
            std::size_t start = 0;
            for (std::size_t i = 0; i < used; ++i) {
                if (line[i] == '\n') {
                    line[i] = '\0';
                    emit(line + start);
                    start = i + 1;
                }
            }
            if (start == 0 && used == sizeof(line) - 1) {
                line[used] = '\0';
                emit(line);
                used = 0;
            } else if (start != 0) {
                std::memmove(line, line + start, used - start);
                used -= start;
            }
        }
    }).detach();
}

// A method of the activity (GameActivity.java) on its UI thread's terms: it posts the work itself.
void call_activity(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass type = env->GetObjectClass(activity);
    if (jmethodID id = env->GetMethodID(type, method, "()V"); id != nullptr) {
        env->CallVoidMethod(activity, id);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
#endif

// SDL scancodes to the runtime's key numbering (virtual_keys.h): letters, digits, function keys and the keys the
// default keyboard layout binds.
int virtual_key(SDL_Scancode code) noexcept {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return 'A' + (code - SDL_SCANCODE_A);
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return '1' + (code - SDL_SCANCODE_1);
    if (code == SDL_SCANCODE_0) return '0';
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return VK_F1 + (code - SDL_SCANCODE_F1);
    if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) return VK_NUMPAD1 + (code - SDL_SCANCODE_KP_1);
    switch (code) {
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_RETURN: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS: return VK_ADD;
    case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    default: return 0;
    }
}

void set_key(int vk, bool down) {
    if (vk <= 0 || vk > 0xFF) {
        return;
    }
    g_input.keys.set_key(static_cast<std::uint8_t>(vk), down);
    if (down) {
        g_last_input.store(kLastKeyboard, std::memory_order_relaxed);
    }
    // The runtime's bindings name the generic modifier keys too.
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) g_input.keys.set_key(VK_SHIFT, down);
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) g_input.keys.set_key(VK_CONTROL, down);
    if (vk == VK_LMENU || vk == VK_RMENU) g_input.keys.set_key(VK_MENU, down);
}

// The unified layout's scheme for an SDL gamepad: Nintendo's positional layout for Switch controllers, PlayStation's
// for Sony's (as the Windows host's HID controllers).
galaxy::input::ControlScheme scheme_of(SDL_Gamepad* pad) noexcept {
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return galaxy::input::ControlScheme::SwitchPro;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return galaxy::input::ControlScheme::PlayStation;
    default:
        return galaxy::input::ControlScheme::Xbox;
    }
}

float axis(SDL_Gamepad* pad, SDL_GamepadAxis which) noexcept {
    return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f, -1.0f, 1.0f);
}

void sample_gamepads() {
    namespace pad = galaxy::input::pad;
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        SDL_Gamepad* gamepad = g_gamepads[i];
        galaxy::input::PadSample& sample = g_input.pads[i];
        sample = {};
        g_input.pad_ids[i] = 0;
        if (gamepad == nullptr || !SDL_GamepadConnected(gamepad)) {
            continue;
        }
        g_input.pad_ids[i] = SDL_GetGamepadID(gamepad);
        sample.connected = true;
        sample.scheme = scheme_of(gamepad);
        const auto button = [gamepad](SDL_GamepadButton b) { return SDL_GetGamepadButton(gamepad, b); };
        std::uint32_t bits = 0;
        if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) bits |= pad::bit(pad::kDpadUpBit);
        if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) bits |= pad::bit(pad::kDpadDownBit);
        if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) bits |= pad::bit(pad::kDpadLeftBit);
        if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) bits |= pad::bit(pad::kDpadRightBit);
        if (button(SDL_GAMEPAD_BUTTON_START)) bits |= pad::bit(pad::kStartBit);
        if (button(SDL_GAMEPAD_BUTTON_BACK)) bits |= pad::bit(pad::kBackBit);
        if (button(SDL_GAMEPAD_BUTTON_LEFT_STICK)) bits |= pad::bit(pad::kLeftStickBit);
        if (button(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) bits |= pad::bit(pad::kRightStickBit);
        if (button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) bits |= pad::bit(pad::kLeftShoulderBit);
        if (button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) bits |= pad::bit(pad::kRightShoulderBit);
        if (button(SDL_GAMEPAD_BUTTON_GUIDE)) bits |= pad::bit(pad::kHomeBit);
        if (button(SDL_GAMEPAD_BUTTON_MISC1)) bits |= pad::bit(pad::kCaptureBit);
        if (button(SDL_GAMEPAD_BUTTON_SOUTH)) bits |= pad::bit(pad::kSouthBit);
        if (button(SDL_GAMEPAD_BUTTON_EAST)) bits |= pad::bit(pad::kEastBit);
        if (button(SDL_GAMEPAD_BUTTON_WEST)) bits |= pad::bit(pad::kWestBit);
        if (button(SDL_GAMEPAD_BUTTON_NORTH)) bits |= pad::bit(pad::kNorthBit);
        sample.buttons = bits;
        // SDL's Y axes grow downward; the unified layout's grow upward.
        sample.axes[0] = axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        sample.axes[1] = -axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        sample.axes[2] = axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        sample.axes[3] = -axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        sample.axes[4] = std::max(0.0f, axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        sample.axes[5] = std::max(0.0f, axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (bits != 0u || std::any_of(std::begin(sample.axes), std::end(sample.axes), [](float a) { return std::abs(a) > 0.5f; })) {
            g_last_input.store(kLastPad + static_cast<std::uint32_t>(sample.scheme), std::memory_order_relaxed);
            g_last_pad.store(g_input.pad_ids[i], std::memory_order_relaxed);
        }
    }
}

void open_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad*& slot : g_gamepads) {
        if (slot == nullptr) {
            slot = SDL_OpenGamepad(id);
            if (slot != nullptr) {
                std::fprintf(stderr, "[input] gamepad \"%s\" connected\n", SDL_GetGamepadName(slot));
            }
            return;
        }
    }
}

void close_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad*& slot : g_gamepads) {
        if (slot != nullptr && SDL_GetGamepadID(slot) == id) {
            SDL_CloseGamepad(slot);
            slot = nullptr;
        }
    }
    if (g_last_pad.load(std::memory_order_relaxed) == id && g_last_input.load(std::memory_order_relaxed) >= kLastPad) {
        g_last_input.store(kLastNone, std::memory_order_relaxed);
    }
}

// The first finger on the picture is the pointer; x, y in window pixels. Called with g_input_mutex held.
void finger_event(std::uint64_t id, float x, float y, bool down, bool up) {
    const std::uint64_t now = galaxy::steady_ms();
    if (down && !g_pointer_finger_valid) {
        g_pointer_finger = id;
        g_pointer_finger_valid = true;
        g_finger_down_ms = now;
        g_finger_down_x = x;
        g_finger_down_y = y;
        g_finger_travel = 0.0f;
    }
    if (!g_pointer_finger_valid || id != g_pointer_finger) {
        return;
    }
    const float side = static_cast<float>(std::max(1, std::min(g_input.window_width, g_input.window_height)));
    g_finger_travel = std::max(g_finger_travel, std::hypot(x - g_finger_down_x, y - g_finger_down_y) / side);
    g_input.finger_x = x;
    g_input.finger_y = y;
    g_input.finger_ms = now;
    g_input.finger_down = !up;
    if (up) {
        g_pointer_finger_valid = false;
        if (now - g_finger_down_ms <= kTapMs && g_finger_travel <= kTapTravel) {
            ++g_input.taps;
            g_input.tap_x = x;
            g_input.tap_y = y;
            g_input.tap_down_ms = g_finger_down_ms;
        }
    }
}

void update_window_size() {
    int width = 0;
    int height = 0;
    if (g_window != nullptr && SDL_GetWindowSizeInPixels(g_window, &width, &height) && width > 0 && height > 0) {
        g_input.window_width = width;
        g_input.window_height = height;
    }
}

}  // namespace

bool start() noexcept {
    g_sdl_thread = std::this_thread::get_id();
#if defined(__ANDROID__)
    forward_output_to_logcat();
#endif
    // Landscape either way up: the game lays out for shapes from 4:3 wide; taller ones come with Vert+ framing.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // The host stops the game's clocks itself while the app is out of sight (in_background, from the activity);
    // SDL's thread keeps pumping events instead of blocking in them.
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR)) {
        std::fprintf(stderr, "[sdl] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// Requests from other threads (g_home_request, rumble), on SDL's thread.
void run_requests() {
#if defined(__ANDROID__)
    if (const int request = g_home_request.exchange(0, std::memory_order_acq_rel); request != 0) {
        call_activity(request == 1 ? "showHomeMenu" : "hideHomeMenu");
    }
#endif
    const std::uint32_t pad = g_rumble_pad.load(std::memory_order_relaxed);
    const std::uint32_t level = g_rumble_level.load(std::memory_order_relaxed);
    const std::uint64_t now = galaxy::steady_ms();
    // Renewed while it lasts: 100 ms outlasts the next renewal, so it never gaps.
    if (pad != g_rumble_sent_pad || level != g_rumble_sent_level || (level != 0u && now - g_rumble_sent_ms >= 50u)) {
        for (SDL_Gamepad* gamepad : g_gamepads) {
            if (gamepad == nullptr) {
                continue;
            }
            const SDL_JoystickID id = SDL_GetGamepadID(gamepad);
            if (id == pad || id == g_rumble_sent_pad) {
                const auto strength = static_cast<Uint16>(id == pad ? level : 0u);
                (void)SDL_RumbleGamepad(gamepad, strength, strength, strength != 0u ? 100u : 0u);
            }
        }
        g_rumble_sent_pad = pad;
        g_rumble_sent_level = level;
        g_rumble_sent_ms = now;
    }
}

void pump_events(int timeout_ms) noexcept {
    SDL_Event event;
    // Every 10 s: this thread's CPU time and how much SDL's event wait took (a wait that polls rather than sleeps).
    static std::int64_t report_cpu = 0;
    static std::int64_t wait_cpu = 0;
    static std::uint32_t waits = 0;
    static auto report_time = std::chrono::steady_clock::now();
    const std::int64_t wait_start = galaxy::gx::vk::thread_cpu_ns();
#if defined(__ANDROID__)
    // SDL's timed event wait polls without sleeping on Android, keeping a core busy: the events are pumped here and
    // the thread sleeps between pumps.
    SDL_PumpEvents();
    bool any = SDL_PollEvent(&event);
    if (!any && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        SDL_PumpEvents();
        any = SDL_PollEvent(&event);
    }
#else
    bool any = SDL_WaitEventTimeout(&event, timeout_ms);
#endif
    wait_cpu += galaxy::gx::vk::thread_cpu_ns() - wait_start;
    ++waits;
    if (const auto now = std::chrono::steady_clock::now(); now - report_time >= std::chrono::seconds(10)) {
        const std::int64_t cpu = galaxy::gx::vk::thread_cpu_ns();
        const double seconds = std::chrono::duration<double>(now - report_time).count();
        if (report_cpu != 0) {
            std::fprintf(stderr, "[host-events] %u waits; main thread %.1f ms/s, %.1f ms/s of it in the event wait\n",
                         waits, static_cast<double>(cpu - report_cpu) / 1e6 / seconds,
                         static_cast<double>(wait_cpu) / 1e6 / seconds);
        }
        report_cpu = cpu;
        report_time = now;
        wait_cpu = 0;
        waits = 0;
    }
    {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    while (any) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            galaxy::gx::host_window::quit_request.store(true, std::memory_order_release);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            update_window_size();
            galaxy::gx::host_window::pending_resize_extent.store(
                (static_cast<std::uint64_t>(g_input.window_width) << 32u) |
                    static_cast<std::uint32_t>(g_input.window_height),
                std::memory_order_release);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_input.window_focused = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_input.window_focused = false;
            g_input.keys = {};
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            // Platforms whose touches reach SDL (Android's go to the activity's controls, nativePointer).
            g_last_input.store(kLastNone, std::memory_order_relaxed);
            finger_event(event.tfinger.fingerID, event.tfinger.x * static_cast<float>(g_input.window_width),
                         event.tfinger.y * static_cast<float>(g_input.window_height),
                         event.type == SDL_EVENT_FINGER_DOWN,
                         event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            g_input.finger_x = event.motion.x;
            g_input.finger_y = event.motion.y;
            g_input.finger_ms = galaxy::steady_ms();
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            // In 120ths of a notch, positive away from the player (Windows' WHEEL_DELTA), as the game takes it.
            const float notches = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            galaxy::gx::host_window::pointer_wheel.fetch_add(static_cast<std::int32_t>(std::lround(notches * 120.0f)),
                                                             std::memory_order_relaxed);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            const unsigned bit = event.button.button == SDL_BUTTON_LEFT ? 0u
                : event.button.button == SDL_BUTTON_RIGHT ? 1u
                : event.button.button == SDL_BUTTON_MIDDLE ? 2u
                : event.button.button == SDL_BUTTON_X1 ? 3u : 4u;
            g_input.keys.mouse = static_cast<std::uint8_t>(
                down ? (g_input.keys.mouse | (1u << bit)) : (g_input.keys.mouse & ~(1u << bit)));
            if (down) {
                g_last_input.store(kLastKeyboard, std::memory_order_relaxed);
            }
            g_input.finger_x = event.button.x;
            g_input.finger_y = event.button.y;
            g_input.finger_ms = galaxy::steady_ms();
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (event.key.scancode == SDL_SCANCODE_AC_BACK) {
                // Android's back gesture: the HOME button, as the HOME Menu opens with it.
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    g_input.touch_buttons |= kTouchHome;
                    g_touch_held_until[std::countr_zero(static_cast<std::uint32_t>(kTouchHome))] =
                        galaxy::steady_ms() + kMinTouchPressMs;
                } else {
                    g_input.touch_buttons &= ~kTouchHome;
                }
                break;
            }
            set_key(virtual_key(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(event.gdevice.which);
            break;
        default:
            break;
        }
        any = SDL_PollEvent(&event);
    }
    sample_gamepads();
    }
    run_requests();
}

void show_fatal(const char* message) noexcept {
    if (const std::filesystem::path data = galaxy::nebula_data_directory(); !data.empty()) {
        if (std::FILE* file = galaxy::open_path(data / "last_error.txt", "w")) {
            std::fputs(message, file);
            std::fclose(file);
        }
    }
    // A message box is Java's on Android: only SDL's thread may show it. From another, the launcher shows the file.
    if (std::this_thread::get_id() == g_sdl_thread) {
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Nebula", message, g_window);
    }
}

void set_menu_context(bool menu) noexcept {
    g_menu_context.store(menu, std::memory_order_relaxed);
}

bool in_background() noexcept {
    return g_background.load(std::memory_order_acquire);
}

InputState input_state() noexcept {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    InputState state = g_input;
    const std::uint64_t now = galaxy::steady_ms();
    for (std::uint32_t bit = 0; bit < g_touch_held_until.size(); ++bit) {
        if (g_touch_held_until[bit] > now) {
            state.touch_buttons |= 1u << bit;
        }
    }
    return state;
}

void rumble(std::uint32_t pad_id, float strength) noexcept {
    g_rumble_level.store(static_cast<std::uint32_t>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f),
                         std::memory_order_relaxed);
    g_rumble_pad.store(pad_id, std::memory_order_relaxed);
}

void home_menu_show() noexcept {
    g_home_choice.store(-1, std::memory_order_relaxed);
    g_home_visible.store(true, std::memory_order_release);
    g_home_request.store(1, std::memory_order_release);
}

void home_menu_hide() noexcept {
    g_home_visible.store(false, std::memory_order_release);
    g_home_request.store(2, std::memory_order_release);
}

int home_menu_choice() noexcept {
    return g_home_choice.load(std::memory_order_acquire);
}

}  // namespace galaxy::sdl

#if defined(__ANDROID__)
// The activity's on-screen controls (GameActivity.java): every change of the held buttons or the stick.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchControls(
    JNIEnv*, jclass, jint buttons, jfloat stick_x, jfloat stick_y) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    const std::uint32_t back = galaxy::sdl::g_input.touch_buttons & galaxy::sdl::kTouchHome;
    const auto held = static_cast<std::uint32_t>(buttons);
    const std::uint32_t pressed = held & ~galaxy::sdl::g_input.touch_buttons;
    if (pressed != 0u) {
        const std::uint64_t until = galaxy::steady_ms() + galaxy::sdl::kMinTouchPressMs;
        for (std::uint32_t bit = 0; bit < galaxy::sdl::g_touch_held_until.size(); ++bit) {
            if ((pressed & (1u << bit)) != 0u) {
                galaxy::sdl::g_touch_held_until[bit] = until;
            }
        }
    }
    galaxy::sdl::g_input.touch_buttons = held | back;
    galaxy::sdl::g_input.touch_stick_x = std::clamp(stick_x, -1.0f, 1.0f);
    galaxy::sdl::g_input.touch_stick_y = std::clamp(stick_y, -1.0f, 1.0f);
}

// A finger on the picture that is not on a control (the activity's controls take every touch): action 0 down, 1 moved,
// 2 up or cancelled; x, y in the picture's pixels.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePointer(
    JNIEnv*, jclass, jint action, jint id, jfloat x, jfloat y) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    galaxy::sdl::finger_event(static_cast<std::uint64_t>(id), x, y, action == 0, action == 2);
}

// Whether the game is on a pointer screen (set_menu_context), for the controls deciding what a touch is.
extern "C" JNIEXPORT jboolean JNICALL Java_dev_nebula_galaxy_GameActivity_nativeMenuContext(JNIEnv*, jclass) {
    return galaxy::sdl::g_menu_context.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

// The HOME Menu's choice (0 return, 1 quit, 2 reset).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeHomeChoice(JNIEnv*, jclass, jint choice) {
    galaxy::sdl::g_home_choice.store(static_cast<int>(choice), std::memory_order_release);
}

// The activity in sight (onStart) or not (onStop): the host stops the game's clocks while it is not, and nothing
// stays held meanwhile.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSetVisible(JNIEnv*, jclass, jboolean visible) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    if (!visible) {
        galaxy::sdl::g_input.keys = {};
        galaxy::sdl::g_input.touch_buttons = 0;
        galaxy::sdl::g_touch_held_until.fill(0);
        galaxy::sdl::g_input.touch_stick_x = 0.0f;
        galaxy::sdl::g_input.touch_stick_y = 0.0f;
        galaxy::sdl::g_input.finger_down = false;
        galaxy::sdl::g_pointer_finger_valid = false;
    }
    galaxy::sdl::g_background.store(!visible, std::memory_order_release);
}

// The activity's SurfaceView is going (surfaceDestroyed, before SDL lets the window go): returns once no present uses
// it (host_window.h). Two seconds at most, so a stalled render thread cannot hold the UI thread.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSurfaceWithdrawn(JNIEnv*, jclass) {
    galaxy::gx::host_window::surface_withdrawn.store(true, std::memory_order_release);
    if (galaxy::gx::host_window::surface_use.try_lock_for(std::chrono::seconds(2))) {
        galaxy::gx::host_window::surface_use.unlock();
    } else {
        std::fprintf(stderr, "[window] the renderer kept the surface past 2 s\n");
    }
}

// A new surface is ready (after SDL took it in surfaceChanged): the renderer makes its own for it.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSurfaceReturned(JNIEnv*, jclass) {
    galaxy::gx::host_window::surface_withdrawn.store(false, std::memory_order_release);
}
#endif

// The renderer's window (host_window.h).

namespace galaxy::gx::host_window {

unsigned backbuffer_width = 0;
unsigned backbuffer_height = 0;
std::atomic<double> measured_present_hz{0.0};
std::atomic<std::int32_t> pointer_wheel{0};
std::string native_settings_content_root;
std::atomic<bool> borderless_active{true};
std::atomic<bool> exclusive_fullscreen_active{false};
std::atomic<unsigned> efb_scale_rejected{0u};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0u};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;
std::atomic<bool> follow_window_rearm{false};

unsigned display_efb_scale_cap() {
    // The internal resolution whose picture covers the display the window is on.
    const SDL_DisplayID display = galaxy::sdl::g_window != nullptr ? SDL_GetDisplayForWindow(galaxy::sdl::g_window)
                                                                   : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    if (mode == nullptr || mode->w <= 0 || mode->h <= 0) {
        return kMaxEfbScale;
    }
    const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
    return efb_scale_covering(static_cast<unsigned>(static_cast<float>(mode->w) * density),
                              static_cast<unsigned>(static_cast<float>(mode->h) * density), kMaxEfbScale);
}

bool flush_native_settings_if_dirty() {
    return true;  // the app's settings screens write the files
}

void poll_render_config_file() {}

bool hidden_window_mode_enabled() {
    const char* value = std::getenv("GALAXY_HIDE_WINDOW");
    return value != nullptr && value[0] == '1';
}

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    const bool hidden = hidden_window_mode_enabled();
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__ANDROID__)
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    if (hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    SDL_Window* window = SDL_CreateWindow("Nebula", std::max(window_width, 320), std::max(window_height, 240), flags);
    if (window == nullptr) {
        std::fprintf(stderr, "[window] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return {};
    }
    {
        std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_window = window;
        galaxy::sdl::update_window_size();
        backbuffer_width = static_cast<unsigned>(galaxy::sdl::g_input.window_width);
        backbuffer_height = static_cast<unsigned>(galaxy::sdl::g_input.window_height);
    }
    owner.window = window;
    return {window, !hidden};
}

void close_game_window(WindowOwner& owner) noexcept {
    if (owner.window != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(owner.window));
        owner.window = nullptr;
    }
    galaxy::sdl::g_window = nullptr;
}

void show_window(NativeWindow window) noexcept {
    SDL_ShowWindow(static_cast<SDL_Window*>(window));
}

// The system's vsync rate where there is one (Android: SDL's display mode can lag a refresh-rate switch), to the
// whole Hz.
double display_refresh_hz(NativeWindow window) noexcept {
    if (const double vsync = galaxy::sdl::display_refresh_hz(); vsync > 1.0) {
        return std::round(vsync);
    }
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(static_cast<SDL_Window*>(window)));
    return mode != nullptr && mode->refresh_rate > 1.0f ? static_cast<double>(mode->refresh_rate) : 0.0;
}

bool vblank_grid(NativeWindow, std::int64_t& vblank, std::int64_t& period) noexcept {
    return galaxy::sdl::vsync_grid(vblank, period);  // qpc_now() is CLOCK_MONOTONIC nanoseconds off Windows
}

bool present_grid(std::int64_t& retrace_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now_ns) noexcept {
    return galaxy::sdl::display_grid(retrace_ns, period_ns, vsync_ns, now_ns);
}

// The overlay, the HOME Menu and the hint are the platform's own screens on SDL platforms (Android: the activity's).
void draw_native_settings_overlay(UiCanvas&) {}
void draw_home_menu(UiCanvas&) {}

std::vector<const char*> vulkan_instance_extensions() {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    return names != nullptr ? std::vector<const char*>(names, names + count) : std::vector<const char*>{};
}

std::string driver_log() {
#if defined(__ANDROID__)
    // An app reads its own log lines (logcat shows a process only its own uid's). The game's lines are in game.log
    // already; the rest from this process are the driver's and the system's.
    char command[96];
    std::snprintf(command, sizeof(command), "logcat -d -v brief --pid=%d -t 600 2>/dev/null", static_cast<int>(getpid()));
    std::FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string text;
    char line[1024];
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        if (std::strstr(line, "/Nebula(") == nullptr && std::strstr(line, "/SDL(") == nullptr) {
            text += line;
        }
    }
    pclose(pipe);
    return text;
#else
    return {};
#endif
}

VkSurfaceKHR create_vulkan_surface(VkInstance instance, NativeWindow window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window), instance, nullptr, &surface)) {
        std::fprintf(stderr, "[window] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return VK_NULL_HANDLE;
    }
#if defined(__ANDROID__)
    // The game's 60 frames a second, so a variable-rate display runs at a whole multiple of it (each frame shown for
    // the same time) rather than at a rate the frames divide unevenly.
    if (auto* native = static_cast<ANativeWindow*>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(window)), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,
            nullptr))) {
        ANativeWindow_setFrameRate(native, 60.0f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
    }
#endif
    return surface;
}

}  // namespace galaxy::gx::host_window

// The window functions the host and the backend call (gx_d3d12.h).

namespace galaxy::gx {

void set_native_settings_content_root(const std::string& utf8_path) {
    host_window::native_settings_content_root = utf8_path;
}

HostClientExtent current_host_client_extent() noexcept {
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    return {state.window_width, state.window_height};
}

HostClientExtent aspect_latch_extent() noexcept {
    return current_host_client_extent();
}

HostPointerState poll_host_pointer_state() noexcept {
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    HostPointerState pointer{};
    pointer.window_focused = state.window_focused;
    pointer.inside_client = true;
    pointer.absolute_valid = state.finger_down;
    pointer.client_x = static_cast<int>(state.finger_x);
    pointer.client_y = static_cast<int>(state.finger_y);
    pointer.client_width = state.window_width;
    pointer.client_height = state.window_height;
    pointer.absolute_acquired_ms = state.finger_ms;
    return pointer;
}

host::HostPointerTransitionPoll poll_host_pointer_transition(std::uint64_t) noexcept {
    return {};
}

// The settings menu is the Windows host's (host_window_win32.cpp: its pages, draft and keys); the SDL hosts do not
// have it yet, so it stays closed.
bool native_settings_overlay_visible() noexcept {
    return false;
}

NativeSettingsView native_settings_view() {
    return {};
}

void native_settings_input(NativeSettingsInput, int) {}

void request_sharp_2d(bool) noexcept {}

bool native_window_input_captured() noexcept {
    return home_menu_visible();
}

void home_menu_open() noexcept {
    galaxy::sdl::home_menu_show();
}

int home_menu_poll() noexcept {
    return galaxy::sdl::home_menu_choice();
}

void home_menu_close() noexcept {
    galaxy::sdl::home_menu_hide();
}

bool home_menu_visible() noexcept {
    return galaxy::sdl::g_home_visible.load(std::memory_order_acquire);
}

// The activity shows its own HOME Menu: the game draws none.
int home_menu_view(bool) noexcept {
    return -1;
}

void home_menu_point(unsigned) noexcept {}

void home_menu_choose(unsigned) noexcept {}

void native_settings_post_key(std::uintptr_t) noexcept {}

std::int32_t take_pointer_wheel() noexcept {
    return host_window::pointer_wheel.exchange(0, std::memory_order_relaxed);
}

}  // namespace galaxy::gx

namespace galaxy::host {

// The game's button icons (the Windows host's rules, native_host.cpp): the ButtonIcons setting; Auto's the keyboard's
// after a key or mouse button (always in the keyboard and mouse input mode, never in the controller one), a
// controller's family after its input while it stays attached, else the Wii's (touch controls).
input::ButtonIconSpec button_icons(bool menu) noexcept {
    using input::ButtonIconStyle;
    using input::ControlScheme;
    const std::uint32_t last = galaxy::sdl::g_last_input.load(std::memory_order_relaxed);
    const std::optional<ControlScheme> pad =
        last >= galaxy::sdl::kLastPad ? std::optional<ControlScheme>(static_cast<ControlScheme>(last - galaxy::sdl::kLastPad))
                                      : std::nullopt;
    const RuntimeInputMode mode = get_runtime_settings().input_mode;
    const bool keyboard = mode == RuntimeInputMode::KeyboardMouse ||
                          (mode != RuntimeInputMode::Controller && last == galaxy::sdl::kLastKeyboard);
    ButtonIconStyle style = ButtonIconStyle::Wii;
    if (keyboard) {
        style = ButtonIconStyle::Keyboard;
    } else if (pad.has_value()) {
        switch (*pad) {
        case ControlScheme::Xbox:
        case ControlScheme::Steam: style = ButtonIconStyle::Xbox; break;
        case ControlScheme::SwitchPro:
        case ControlScheme::SwitchPro2: style = ButtonIconStyle::Nintendo; break;
        case ControlScheme::PlayStation: style = ButtonIconStyle::PlayStation; break;
        case ControlScheme::Keyboard:
        case ControlScheme::Generic: break;
        }
    }
    switch (get_runtime_settings().button_icons) {
    case ButtonIcons::Auto: break;
    case ButtonIcons::Wii: style = ButtonIconStyle::Wii; break;
    case ButtonIcons::Xbox: style = ButtonIconStyle::Xbox; break;
    case ButtonIcons::Nintendo: style = ButtonIconStyle::Nintendo; break;
    case ButtonIcons::PlayStation: style = ButtonIconStyle::PlayStation; break;
    case ButtonIcons::Keyboard: style = ButtonIconStyle::Keyboard; break;
    }
    // The bindings of the controller in use; a style chosen without one takes its own family's.
    const ControlScheme scheme = style == ButtonIconStyle::Keyboard      ? ControlScheme::Keyboard
                                 : pad.has_value()                       ? *pad
                                 : style == ButtonIconStyle::Nintendo    ? ControlScheme::SwitchPro
                                 : style == ButtonIconStyle::PlayStation ? ControlScheme::PlayStation
                                                                         : ControlScheme::Xbox;
    // The game's thread asks each frame: worked out again only when the bindings, the style, the controller or the
    // context change.
    static std::uint64_t s_generation = ~std::uint64_t{0};
    static ButtonIconStyle s_style = ButtonIconStyle::Wii;
    static ControlScheme s_scheme = ControlScheme::Keyboard;
    static bool s_menu = false;
    static input::ButtonIconSpec s_spec;
    const std::uint64_t generation = input::controls_config_generation();
    if (generation != s_generation || style != s_style || scheme != s_scheme || menu != s_menu) {
        s_generation = generation;
        s_style = style;
        s_scheme = scheme;
        s_menu = menu;
        s_spec = input::button_icon_spec(style, input::get_controls_config().table(scheme), menu);
    }
    return s_spec;
}

}  // namespace galaxy::host
