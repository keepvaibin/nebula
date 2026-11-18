// SPDX-License-Identifier: GPL-3.0-only
// The game window and input on SDL3 platforms (sdl_host.h, host_window.h and the window functions of render_api.h):
// the SDL window and its Vulkan surface; its events (size, focus, quit, the finger and the mouse as the pointer, keys,
// gamepads); on Android the activity's touch controls and HOME Menu through JNI.

#include "galaxy/benchmark.h"
#include "galaxy/content_viewport.h"
#include "galaxy/switch_controller.h"
#include "galaxy/playstation_controller.h"
#include "galaxy/steam_controller.h"
#include "galaxy/controller_hid.h"
#include "galaxy/input/host_input.h"
#include "galaxy/input/press_latch.h"
#include "galaxy/mod_import.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/render_api.h"
#include "galaxy/render/host_window.h"
#include "galaxy/luma_system_platform.h"
#include "galaxy/luma_input_service.h"
#include "galaxy/ui/launcher.h"
#include <deque>
#include "sdl_host.h"

#include "galaxy/render/display_modes.h"
#include "galaxy/render/display_reconcile.h"
#include "galaxy/render/host_window.h"
#if defined(NEBULA_GPU_METAL)
#include <cstdint>
#include <ctime>
// The Metal development build: the calling thread's CPU time, which the Vulkan context gives the other builds.
namespace galaxy::render::vk {
[[nodiscard]] inline std::int64_t thread_cpu_ns() noexcept {
    return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID));
}
}  // namespace galaxy::render::vk
#else
#include "galaxy/render/vk/vk_context.h"
#endif
#include "galaxy/render/render_config.h"
#include "galaxy/render_api.h"
#include "galaxy/input_bindings.h"
#include "galaxy/game_paths.h"
#include "galaxy/host_text.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/virtual_keys.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(NEBULA_GPU_METAL)
#include <SDL3/SDL_metal.h>
#else
#include <SDL3/SDL_vulkan.h>
#endif

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
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace galaxy::sdl {

// The device that gave input last (button_icons' Auto): none (touch controls), the keyboard and mouse, or a
// controller: kLastPad plus its scheme.
constexpr std::uint32_t kLastNone = 0u;
constexpr std::uint32_t kLastKeyboard = 1u;
constexpr std::uint32_t kLastPad = 2u;
std::atomic<std::uint32_t> g_last_input{kLastNone};
// The icon model (input::ButtonIconModel) of the pad that gave input last.
std::atomic<std::uint8_t> g_last_pad_icons{0};
std::atomic<SDL_JoystickID> g_last_pad{0};

namespace {

std::mutex g_input_mutex;
InputState g_input;                 // guarded by g_input_mutex
// A mod's first-person look wants the mouse (set_mouse_look, any thread); the window holds it now (SDL's relative mode,
// the pump's thread).
std::atomic<bool> g_mouse_look_wanted{false};
bool g_mouse_look_held = false;
SDL_Window* g_window = nullptr;
std::atomic<bool> g_background{false};
std::atomic<bool> g_low_memory{false};
std::atomic<bool> g_menu_context{false};
std::atomic<bool> g_title_screen{false};
float g_wheel_carry = 0.0f;  // the wheel's turn short of a 120th of a notch (pump_events, the event thread)
std::atomic<bool> g_direct_touch{false};

// SDL's thread, the only one that calls SDL's window, gamepad and Java functions. The game's threads run on stacks of
// their own, which the Java VM refuses to attach (it aborts the process), so they leave requests here instead, done
// after each pump (every 4 ms at most).
std::thread::id g_sdl_thread;
// Keys the settings menu takes from other threads and from the pads, in order, on SDL's thread.
std::mutex g_menu_keys_mutex;
std::deque<std::uintptr_t> g_menu_keys;
galaxy::input::NativeSettingsPadState g_menu_pad{};
std::optional<galaxy::input::DeviceKey> g_capture_device;
std::optional<galaxy::input::InputSource> g_capture_result;
std::uint32_t g_capture_previous = 0u;
// Each pad's motion integrator, its last gyro sample's sensor time and its last accelerometer sample, touch surface
// state: SDL's thread.
std::array<galaxy::input::switch_hid::SwitchMotion, kMaxPads> g_motion{};
std::array<std::uint64_t, kMaxPads> g_motion_ns{};
std::array<std::array<float, 3>, kMaxPads> g_accel{};
std::array<bool, kMaxPads> g_accel_valid{};
// How each pad's motion arrives. A driver that posts sensor events (SDL_EVENT_GAMEPAD_SENSOR_UPDATE) gives every sample
// with its own time; one that only keeps the latest values (some Android controller drivers) is read once a pump, as
// the values held over the pump. A pad that has posted a gyro event stays on events, so no sample counts twice.
struct PadMotionSource {
    bool has_gyro = false;
    bool has_accel = false;
    bool by_events = false;
    std::uint64_t last_feed_us = 0u;  // steady clock of the last sample fed
    std::uint64_t gyro_events = 0u;
    std::uint64_t accel_events = 0u;
    std::uint64_t polled = 0u;
    std::uint64_t reported_us = 0u;
    // Android: the controller's own sensors through PadSensors.java own the pad (SDL's are not turned on).
    bool android = false;
    std::uint64_t android_gyro = 0u;
    std::uint64_t android_accel = 0u;
    std::uint64_t reported_gyro = 0u;  // android_gyro at the last report
};
std::array<PadMotionSource, kMaxPads> g_motion_source{};
std::array<bool, kMaxPads> g_touch_down{};
std::array<float, kMaxPads> g_touch_x{};
std::array<float, kMaxPads> g_touch_y{};
std::atomic<std::uint32_t> g_rumble_pad{0};
std::atomic<std::uint32_t> g_rumble_level{0};
// What SDL's thread last sent, and when (it renews a rumble that lasts).
std::uint32_t g_rumble_sent_pad = 0;
std::uint32_t g_rumble_sent_level = 0;
std::uint64_t g_rumble_sent_ms = 0;

// A finger's travel on a pointer screen, in pixels, not yet turned into the wheel's steps. Guarded by g_input_mutex.
float g_menu_swipe_px = 0.0f;
float g_finger_last_y = 0.0f;
// On-screen buttons pressed since the game's last read: a quick tap can end between two reads, and is not lost
// (native_game_input then holds it for the game's frames). Guarded by g_input_mutex.
std::uint32_t g_touch_pressed = 0u;
bool g_back_held = false;  // Android's back gesture held as the HOME button; guarded by g_input_mutex
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
// How far a finger moves before it counts as moving, in window pixels: Android's touch slop for this display
// (GameActivity, ViewConfiguration), as its own views use it; elsewhere any move.
std::atomic<float> g_touch_slop_px{0.0f};

// The pointer's finger's recent places, for its speed as it is lifted (a list's fling). The speed is the slope of a
// straight line fitted to the places of the last kFingerSpeedWindowUs; a finger that held still for kFingerStillUs before
// it lifted has none (Android's VelocityTracker: a 100 ms horizon, a 40 ms pause). Guarded by g_input_mutex.
struct FingerSample {
    std::uint64_t us;
    float x;
    float y;
};
constexpr std::uint64_t kFingerSpeedWindowUs = 100000u;
constexpr std::uint64_t kFingerStillUs = 40000u;
std::array<FingerSample, 32> g_finger_samples{};
std::size_t g_finger_sample_count = 0;  // the newest at g_finger_sample_count - 1, the list shifted when full

void add_finger_sample(std::uint64_t us, float x, float y) {
    if (g_finger_sample_count == g_finger_samples.size()) {
        std::move(g_finger_samples.begin() + 1, g_finger_samples.end(), g_finger_samples.begin());
        --g_finger_sample_count;
    }
    g_finger_samples[g_finger_sample_count++] = {us, x, y};
}

// The finger's speed at `now_us` in window pixels a second, from its samples.
void finger_speed(std::uint64_t now_us, float& vx, float& vy) {
    vx = 0.0f;
    vy = 0.0f;
    if (g_finger_sample_count < 2u) return;
    const FingerSample& newest = g_finger_samples[g_finger_sample_count - 1u];
    if (now_us > newest.us + kFingerStillUs) return;
    std::size_t first = g_finger_sample_count - 1u;
    while (first > 0u && newest.us - g_finger_samples[first - 1u].us <= kFingerSpeedWindowUs) --first;
    const std::size_t count = g_finger_sample_count - first;
    if (count < 2u) return;
    double mean_t = 0.0, mean_x = 0.0, mean_y = 0.0;
    for (std::size_t i = first; i < g_finger_sample_count; ++i) {
        mean_t += static_cast<double>(newest.us - g_finger_samples[i].us);
        mean_x += g_finger_samples[i].x;
        mean_y += g_finger_samples[i].y;
    }
    mean_t /= static_cast<double>(count);
    mean_x /= static_cast<double>(count);
    mean_y /= static_cast<double>(count);
    double tt = 0.0, tx = 0.0, ty = 0.0;
    for (std::size_t i = first; i < g_finger_sample_count; ++i) {
        // Time counted back from the newest sample, in seconds: the slope's sign turns.
        const double t = (mean_t - static_cast<double>(newest.us - g_finger_samples[i].us)) * 1e-6;
        tt += t * t;
        tx += t * (g_finger_samples[i].x - mean_x);
        ty += t * (g_finger_samples[i].y - mean_y);
    }
    if (tt <= 0.0) return;
    vx = static_cast<float>(tx / tt);
    vy = static_cast<float>(ty / tt);
}

#if defined(__ANDROID__)
// stdout and stderr to logcat and to <data>/logs/game.log, line by line (the host and the renderer log there).
void forward_output_to_logcat() {
    static int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        install_crash_log(-1);
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
    // A crash's report goes to the file itself: the thread below stops with the rest when the system dumps the process.
    install_crash_log(file != nullptr ? fileno(file) : -1);
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

// A method of the activity (GameActivity.java) on its UI thread's terms: it posts the work itself. `method` is a
// string literal; with `has_argument` it takes one int, `(I)V`.
struct ActivityCall {
    const char* method = nullptr;
    bool has_argument = false;
    int argument = 0;
};
std::mutex g_activity_calls_mutex;
std::deque<ActivityCall> g_activity_calls;  // from other threads, for SDL's (run_requests)

void call_activity_now(const ActivityCall& call) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass type = env->GetObjectClass(activity);
    jmethodID id = env->GetMethodID(type, call.method, call.has_argument ? "(I)V" : "()V");
    if (id == nullptr) {
        // A method the build left out (R8: platform/android/app/proguard-rules.pro): said, never silent.
        std::fprintf(stderr, "[android] the activity has no method %s%s: the call is lost\n", call.method,
                     call.has_argument ? "(int)" : "()");
    } else if (call.has_argument) {
        env->CallVoidMethod(activity, id, static_cast<jint>(call.argument));
    } else {
        env->CallVoidMethod(activity, id);
    }
    if (env->ExceptionCheck()) {
        if (id != nullptr) std::fprintf(stderr, "[android] the activity's %s threw\n", call.method);
        env->ExceptionClear();
    }
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

// The Java VM is SDL's thread's to call (the game's threads run on stacks of their own, which it cannot attach): from
// any other thread the call waits for SDL's next pump.
void call_activity(ActivityCall call) {
    if (std::this_thread::get_id() != g_sdl_thread) {
        const std::lock_guard<std::mutex> lock(g_activity_calls_mutex);
        g_activity_calls.push_back(call);
        return;
    }
    call_activity_now(call);
}

void call_activity(const char* method) {
    call_activity(ActivityCall{method, false, 0});
}

void run_activity_calls() {
    for (;;) {
        ActivityCall call;
        {
            const std::lock_guard<std::mutex> lock(g_activity_calls_mutex);
            if (g_activity_calls.empty()) {
                return;
            }
            call = g_activity_calls.front();
            g_activity_calls.pop_front();
        }
        call_activity_now(call);
    }
}

// The panel's fastest refresh rate at its resolution (GameActivity: Display.getSupportedModes). The settings offer
// interpolation by it, not by the rate the system holds the app to now: a game showing 60 frames is held at 60 Hz on
// most variable-rate panels until it asks for more.
std::atomic<float> g_panel_fastest_hz{0.0f};

// The surface's frame-rate vote for the frame-rate setting (max_fps; 0: the display's own; unlimited: the panel's
// fastest). The game's own 30 or 60
// frames are a fixed source, so a variable-rate panel shows each for the same time; interpolated rates are the app's
// own rate, switched to at once (the player chose it) rather than at the next seamless moment.
void vote_frame_rate(ANativeWindow* native, float max_fps) {
    if (max_fps > 0.0f && max_fps <= 61.0f) {
        ANativeWindow_setFrameRate(native, max_fps, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        return;
    }
    const float rate = max_fps > 0.0f && std::isfinite(max_fps) ? max_fps : g_panel_fastest_hz.load(std::memory_order_relaxed);
    ANativeWindow_setFrameRateWithChangeStrategy(native, rate, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT,
                                                 ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS);
}

// The setting last given to the display (-1: none yet).
float g_display_rate_applied = -1.0f;

// The frame-rate setting as the display's, whenever it changes: the surface's vote, and the activity's display mode
// (GameActivity.displayRateChanged). Once a pump; while the surface is away the change waits for it.
void sync_display_rate() {
    const float max_fps = galaxy::render::get_render_config().max_fps;
    if (max_fps == g_display_rate_applied || g_window == nullptr ||
        galaxy::render::host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        return;
    }
    auto* native = static_cast<ANativeWindow*>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
    if (native == nullptr) {
        return;
    }
    g_display_rate_applied = max_fps;
    vote_frame_rate(native, max_fps);
    call_activity("displayRateChanged");
    std::fprintf(stderr, "[display] frame rate setting %.0f (0: the display's own); panel's fastest %.0f Hz\n",
                 static_cast<double>(max_fps), static_cast<double>(g_panel_fastest_hz.load(std::memory_order_relaxed)));
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

// The unified layout's scheme for an SDL gamepad, as the Windows host names the same controllers: Switch 2 controllers
// by their product (SDL types the Pro Controller 2 as a standard pad), Nintendo's positional layout for Switch
// controllers and the GameCube pad, PlayStation's for Sony's, Steam's for Valve's and SDL's Steam type, and the Xbox
// layout SDL's standard mapping follows for the rest (Xbox 360, One and Series, Stadia, Luna, Shield, generic and
// unknown pads).
galaxy::input::ControlScheme scheme_of(SDL_Gamepad* pad) noexcept {
    namespace sw = galaxy::input::switch_hid;
    if (SDL_GetGamepadVendor(pad) == galaxy::input::steam::kValveVendorId) {
        return galaxy::input::ControlScheme::Steam;
    }
    if (SDL_GetGamepadVendor(pad) == sw::kNintendoVendorId) {
        switch (SDL_GetGamepadProduct(pad)) {
        case sw::kPro2ControllerProductId:
        case sw::kJoyCon2LeftProductId:
        case sw::kJoyCon2RightProductId:
        case sw::kJoyCon2GripProductId:
        case sw::kGameCubeProductId:
            return galaxy::input::ControlScheme::SwitchPro2;
        default:
            break;
        }
    }
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return galaxy::input::ControlScheme::SwitchPro2;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        return galaxy::input::ControlScheme::SwitchPro;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return galaxy::input::ControlScheme::PlayStation;
    case SDL_GAMEPAD_TYPE_STEAM:
        return galaxy::input::ControlScheme::Steam;
    default:
        break;
    }
    // A pad SDL types as standard or unknown still names its maker: Sony's and Nintendo's pads keep their families.
    const Uint16 vendor = SDL_GetGamepadVendor(pad);
    if (vendor == galaxy::input::playstation::kSonyVendorId) {
        return galaxy::input::ControlScheme::PlayStation;
    }
    if (vendor == sw::kNintendoVendorId) {
        return galaxy::input::ControlScheme::SwitchPro;
    }
    return galaxy::input::ControlScheme::Xbox;
}

// The icon model of a gamepad whose buttons are not its family's: the DualShock 3 (Select and Start), the GameCube
// pad (face buttons, analog L and R, ZL and Z, Start), any Steam controller (Steam button, quick access, back buttons,
// trackpads) and the DualSense Edge (back paddles, Fn buttons).
galaxy::input::ButtonIconModel icon_model_of(SDL_Gamepad* pad) noexcept {
    namespace sw = galaxy::input::switch_hid;
    const SDL_GamepadType type = SDL_GetGamepadType(pad);
    if (type == SDL_GAMEPAD_TYPE_PS3) {
        return galaxy::input::ButtonIconModel::Sixaxis;
    }
    if (type == SDL_GAMEPAD_TYPE_GAMECUBE ||
        (SDL_GetGamepadVendor(pad) == sw::kNintendoVendorId && SDL_GetGamepadProduct(pad) == sw::kGameCubeProductId)) {
        return galaxy::input::ButtonIconModel::GameCube;
    }
    // The same test scheme_of makes for the Steam family.
    if (type == SDL_GAMEPAD_TYPE_STEAM || SDL_GetGamepadVendor(pad) == galaxy::input::steam::kValveVendorId) {
        return galaxy::input::ButtonIconModel::Steam;
    }
    if (SDL_GetGamepadVendor(pad) == galaxy::input::playstation::kSonyVendorId &&
        SDL_GetGamepadProduct(pad) == galaxy::input::playstation::kDualSenseEdgeProductId) {
        return galaxy::input::ButtonIconModel::DualSenseEdge;
    }
    return galaxy::input::ButtonIconModel::Standard;
}

float axis(SDL_Gamepad* pad, SDL_GamepadAxis which) noexcept {
    return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f, -1.0f, 1.0f);
}

// One motion sample of pad `index` (gyro in radians a second, accelerometer in g) over dt seconds into its integrator
// and g_input.motion, as the Windows host's worker feeds each HID report. Called with g_input_mutex held.
void feed_pad_motion(std::size_t index, const float (&gyro)[3], const std::array<float, 3>& accel, float dt,
                     std::uint64_t now_us) {
    namespace sw = galaxy::input::switch_hid;
    sw::ImuSample imu{};
    for (int axis_index = 0; axis_index < 3; ++axis_index) {
        imu.gyro[axis_index] = gyro[axis_index];
        imu.accel[axis_index] = accel[static_cast<std::size_t>(axis_index)];
    }
    const galaxy::input::SchemeTuning& tuning =
        galaxy::input::get_controls_config().tune(scheme_of(g_gamepads[index]));
    sw::MotionSettings settings{};
    settings.shake = tuning.motion_shake != galaxy::input::MotionShake::Off;
    settings.shake_sensitivity = tuning.shake_sensitivity;
    settings.gyro_pointer = tuning.gyro_pointer;
    settings.gyro_sensitivity = tuning.gyro_sensitivity;
    settings.picture_aspect = static_cast<float>(galaxy::kDesignContentAspect);
    const sw::SwitchMotion::Output out = g_motion[index].feed(imu, dt, now_us, settings);
    PadMotion& motion = g_input.motion[index];
    motion.imu_live = true;
    motion.gyro_x = std::clamp(motion.gyro_x + out.pointer_x, -4.0f, 4.0f);
    motion.gyro_y = std::clamp(motion.gyro_y + out.pointer_y, -4.0f, 4.0f);
    motion.gravity = g_motion[index].gravity();
    motion.gravity_valid = g_motion[index].gravity_ready();
    if (out.shake_started) {
        motion.shake_until_us = now_us + sw::SwitchMotion::kShakeHoldUs;
    }
    g_motion_source[index].last_feed_us = now_us;
}

// The steady-clock time since pad `index` was last fed, at most 0.1 s (0 for its first sample).
float steady_motion_dt(std::size_t index, std::uint64_t now_us) {
    const std::uint64_t last = g_motion_source[index].last_feed_us;
    return last != 0u && now_us > last ? std::min(0.1f, static_cast<float>(now_us - last) * 1e-6f) : 0.0f;
}

// One of a pad's sensor events, as it came: every gyro sample turns the pointer by its own time step (the sensor's
// clock), with the latest accelerometer sample for gravity. A sensor clock that does not advance by a plausible step
// (0 to 0.1 s) is replaced by the steady clock. Called with g_input_mutex held.
void pad_sensor_event(const SDL_GamepadSensorEvent& event) {
    std::size_t index = kMaxPads;
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        if (g_gamepads[i] != nullptr && SDL_GetGamepadID(g_gamepads[i]) == event.which) {
            index = i;
            break;
        }
    }
    if (index == kMaxPads) {
        return;
    }
    PadMotionSource& source = g_motion_source[index];
    if (source.android) {
        return;
    }
    if (event.sensor == SDL_SENSOR_ACCEL) {
        for (int axis_index = 0; axis_index < 3; ++axis_index) {
            g_accel[index][static_cast<std::size_t>(axis_index)] = event.data[axis_index] / SDL_STANDARD_GRAVITY;
        }
        g_accel_valid[index] = true;
        ++source.accel_events;
        return;
    }
    if (event.sensor != SDL_SENSOR_GYRO) {
        return;
    }
    ++source.gyro_events;
    source.by_events = true;
    if (!g_accel_valid[index] && source.has_accel) {
        // A driver that posts gyro events but keeps the accelerometer's values only: they are read here.
        float accel[3]{};
        if (!SDL_GetGamepadSensorData(g_gamepads[index], SDL_SENSOR_ACCEL, accel, 3) ||
            (accel[0] == 0.0f && accel[1] == 0.0f && accel[2] == 0.0f)) {
            return;  // gravity comes with the first accelerometer sample
        }
        for (int axis_index = 0; axis_index < 3; ++axis_index) {
            g_accel[index][static_cast<std::size_t>(axis_index)] = accel[axis_index] / SDL_STANDARD_GRAVITY;
        }
    }
    const std::uint64_t now_us = galaxy::steady_us();
    const std::uint64_t sensor_ns = event.sensor_timestamp != 0u ? event.sensor_timestamp : event.timestamp;
    const std::uint64_t last_ns = g_motion_ns[index];
    g_motion_ns[index] = sensor_ns;
    float dt = 0.0f;
    if (last_ns != 0u && sensor_ns > last_ns && sensor_ns - last_ns <= 100'000'000u) {
        dt = static_cast<float>(sensor_ns - last_ns) * 1e-9f;
    } else if (last_ns != 0u) {
        dt = steady_motion_dt(index, now_us);
    }
    const float gyro[3] = {event.data[0], event.data[1], event.data[2]};  // radians a second
    feed_pad_motion(index, gyro, g_accel[index], dt, now_us);
}

// Pads whose driver posts no sensor events: their latest sensor values, held over the time since the last pump. Called
// with g_input_mutex held, after the pump's events.
void poll_pad_motion(std::size_t index, SDL_Gamepad* gamepad) {
    PadMotionSource& source = g_motion_source[index];
    if (source.by_events || !source.has_gyro || source.android) {
        return;
    }
    float gyro[3]{};
    if (!SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, gyro, 3)) {
        return;
    }
    if (source.has_accel) {
        float accel[3]{};
        if (!SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, accel, 3) ||
            (accel[0] == 0.0f && accel[1] == 0.0f && accel[2] == 0.0f)) {
            return;  // no sample yet: a pad at rest always reads gravity
        }
        for (int axis_index = 0; axis_index < 3; ++axis_index) {
            g_accel[index][static_cast<std::size_t>(axis_index)] = accel[axis_index] / SDL_STANDARD_GRAVITY;
        }
        g_accel_valid[index] = true;
    }
    const std::uint64_t now_us = galaxy::steady_us();
    ++source.polled;
    feed_pad_motion(index, gyro, g_accel[index], steady_motion_dt(index, now_us), now_us);
}

#if defined(__ANDROID__)
// Controllers' own motion sensors as Android gives them (InputDevice.getSensorManager; PadSensors.java): every sample the
// controller sends (a Switch Pro Controller's three per report, 200 a second), queued by Java's sensor thread and fed
// here on SDL's thread in order, each with its own sensor time. SDL's Android driver reads the same sensors at a fixed
// 50 Hz (SENSOR_DELAY_GAME: the samples between are dropped), on their own axes, and only when they were there as the
// controller connected; so these take a pad SDL reads through Android's input devices. A pad SDL drives itself
// (HIDAPI, over USB) keeps SDL's reading of the controller's reports.
struct AndroidPadDevice {
    int id = 0;
    std::uint16_t crc = 0u;  // of Android's descriptor, as SDL keeps it in the GUID
    std::uint16_t vendor = 0u;
    std::uint16_t product = 0u;
    bool gyro = false;
    bool accel = false;
    SDL_JoystickID pad_id = 0;  // the SDL gamepad these sensors are, 0 before one is found
};
struct AndroidPadSample {
    int id;
    int type;  // Sensor.TYPE_ACCELEROMETER 1 (metres a second squared), Sensor.TYPE_GYROSCOPE 4 (radians a second)
    std::uint64_t time_ns;
    float v[3];
};
constexpr int kAndroidAccelerometer = 1;
constexpr int kAndroidGyroscope = 4;
constexpr std::size_t kAndroidSampleLimit = 4096u;
std::mutex g_android_pad_mutex;                   // after g_input_mutex when both are held
std::vector<AndroidPadDevice> g_android_pads;     // guarded by g_android_pad_mutex
std::vector<AndroidPadSample> g_android_samples;  // guarded by g_android_pad_mutex
std::vector<AndroidPadSample> g_android_drained;  // SDL's thread

// A pad SDL reads through its own HID drivers (driver signature 'h' in its GUID), not Android's input devices.
bool sdl_hid_driver(SDL_Gamepad* gamepad) {
    return SDL_GetJoystickGUID(SDL_GetGamepadJoystick(gamepad)).data[14] == 'h';
}

// Linux's Nintendo driver (hid-nintendo, built into Android's kernels) reports the IMU on the controller's own axes:
// X toward the triggers, Y to the left, Z up out of the face. SDL's frame (switch_controller.h ImuSample), as its own
// Switch driver turns the same reports, is X right, Y up, Z toward the player. hid-playstation and hid-sony report
// SDL's frame already.
void android_to_sdl_frame(std::uint16_t vendor, const float (&in)[3], float (&out)[3]) {
    if (vendor == 0x057eu) {
        out[0] = -in[1];
        out[1] = in[2];
        out[2] = -in[0];
    } else {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
    }
}

// The pad of an Android device's sensors: the SDL gamepad of the same descriptor, else the first of the same vendor and
// product that no other device's sensors have. Called with both locks held.
std::size_t android_pad_index(AndroidPadDevice& device) {
    if (device.pad_id != 0) {
        for (std::size_t i = 0; i < kMaxPads; ++i) {
            if (g_gamepads[i] != nullptr && SDL_GetGamepadID(g_gamepads[i]) == device.pad_id) {
                return i;
            }
        }
        device.pad_id = 0;
    }
    const auto claimed = [&device](SDL_JoystickID id) {
        return std::any_of(g_android_pads.begin(), g_android_pads.end(),
                           [&](const AndroidPadDevice& other) { return other.id != device.id && other.pad_id == id; });
    };
    std::size_t found = kMaxPads;
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        SDL_Gamepad* const gamepad = g_gamepads[i];
        if (gamepad == nullptr || sdl_hid_driver(gamepad) || SDL_GetGamepadVendor(gamepad) != device.vendor ||
            SDL_GetGamepadProduct(gamepad) != device.product || claimed(SDL_GetGamepadID(gamepad))) {
            continue;
        }
        const SDL_GUID guid = SDL_GetJoystickGUID(SDL_GetGamepadJoystick(gamepad));
        if (static_cast<std::uint16_t>(guid.data[2] | (guid.data[3] << 8u)) == device.crc) {
            found = i;
            break;
        }
        if (found == kMaxPads) {
            found = i;
        }
    }
    if (found == kMaxPads) {
        return kMaxPads;
    }
    device.pad_id = SDL_GetGamepadID(g_gamepads[found]);
    PadMotionSource& source = g_motion_source[found];
    if (!source.android) {
        source.android = true;
        source.has_gyro = source.has_gyro || device.gyro;
        source.has_accel = source.has_accel || device.accel;
        g_motion_ns[found] = 0u;
        g_accel_valid[found] = false;
        std::fprintf(stderr, "[input-motion] pad %zu: the controller's own sensors (gyro %d, accelerometer %d), every sample\n",
                     found, device.gyro ? 1 : 0, device.accel ? 1 : 0);
    }
    return found;
}

// The samples Java's thread queued since the last pump. Called with g_input_mutex held.
void drain_android_pad_sensors() {
    std::lock_guard<std::mutex> lock(g_android_pad_mutex);
    g_android_drained.swap(g_android_samples);
    for (const AndroidPadSample& sample : g_android_drained) {
        const auto device = std::find_if(g_android_pads.begin(), g_android_pads.end(),
                                         [&](const AndroidPadDevice& d) { return d.id == sample.id; });
        if (device == g_android_pads.end()) {
            continue;
        }
        const std::size_t index = android_pad_index(*device);
        if (index == kMaxPads) {
            continue;
        }
        PadMotionSource& source = g_motion_source[index];
        float value[3]{};
        android_to_sdl_frame(device->vendor, sample.v, value);
        if (sample.type == kAndroidAccelerometer) {
            for (std::size_t axis_index = 0; axis_index < 3u; ++axis_index) {
                g_accel[index][axis_index] = value[axis_index] / SDL_STANDARD_GRAVITY;
            }
            g_accel_valid[index] = true;
            ++source.android_accel;
            continue;
        }
        if (sample.type != kAndroidGyroscope) {
            continue;
        }
        ++source.android_gyro;
        if (device->accel && !g_accel_valid[index]) {
            continue;  // gravity comes with the first accelerometer sample
        }
        const std::uint64_t now_us = galaxy::steady_us();
        const std::uint64_t last_ns = g_motion_ns[index];
        g_motion_ns[index] = sample.time_ns;
        float dt = 0.0f;
        if (last_ns != 0u && sample.time_ns > last_ns && sample.time_ns - last_ns <= 100'000'000u) {
            dt = static_cast<float>(sample.time_ns - last_ns) * 1e-9f;
        } else if (last_ns != 0u) {
            dt = steady_motion_dt(index, now_us);
        }
        feed_pad_motion(index, value, g_accel[index], dt, now_us);
    }
    g_android_drained.clear();
}
#endif

// Every 10 s while a pad has sensors: how its motion arrives, so a log shows whether a controller's motion reaches the
// game.
void report_pad_motion(std::size_t index) {
    PadMotionSource& source = g_motion_source[index];
    const std::uint64_t now_us = galaxy::steady_us();
    if (!source.has_gyro && !source.has_accel) {
        return;
    }
    if (source.reported_us != 0u && now_us - source.reported_us < 10'000'000u) {
        return;
    }
    const double seconds = source.reported_us != 0u ? static_cast<double>(now_us - source.reported_us) * 1e-6 : 0.0;
    source.reported_us = now_us;
    if (source.android) {
        std::fprintf(stderr, "[input-motion] pad %zu by the controller's sensors: gyro %llu (%.0f a second), accelerometer %llu, live %d\n",
                     index, static_cast<unsigned long long>(source.android_gyro),
                     seconds > 0.0 ? static_cast<double>(source.android_gyro - source.reported_gyro) / seconds : 0.0,
                     static_cast<unsigned long long>(source.android_accel), g_input.motion[index].imu_live ? 1 : 0);
        source.reported_gyro = source.android_gyro;
        return;
    }
    std::fprintf(stderr, "[input-motion] pad %zu by %s: gyro events %llu, accelerometer events %llu, polled %llu, live %d\n",
                 index, source.by_events ? "events" : "polling", static_cast<unsigned long long>(source.gyro_events),
                 static_cast<unsigned long long>(source.accel_events), static_cast<unsigned long long>(source.polled),
                 g_input.motion[index].imu_live ? 1 : 0);
}

// A pad's touch surface into g_input.motion, as the Windows host's worker does.
void sample_pad_motion(std::size_t index, SDL_Gamepad* gamepad) {
    PadMotion& motion = g_input.motion[index];
    const PadMotionSource& source = g_motion_source[index];
    motion.has_gyro = source.has_gyro || motion.imu_live;
    motion.has_accel = source.has_accel || motion.imu_live;
#if defined(__ANDROID__)
    if (!motion.has_gyro && !sdl_hid_driver(gamepad)) {
        // The controller's own sensors as Android lists them, before their first sample.
        std::lock_guard<std::mutex> lock(g_android_pad_mutex);
        const std::uint16_t vendor = SDL_GetGamepadVendor(gamepad);
        const std::uint16_t product = SDL_GetGamepadProduct(gamepad);
        for (const AndroidPadDevice& device : g_android_pads) {
            if (device.vendor == vendor && device.product == product) {
                motion.has_gyro = motion.has_gyro || device.gyro;
                motion.has_accel = motion.has_accel || device.accel;
            }
        }
    }
#endif
    motion.has_touch = SDL_GetNumGamepadTouchpads(gamepad) > 0;
    if (SDL_GetNumGamepadTouchpads(gamepad) > 0) {
        bool down = false;
        float x = 0.0f;
        float y = 0.0f;
        if (SDL_GetGamepadTouchpadFinger(gamepad, 0, 0, &down, &x, &y, nullptr)) {
            motion.touch_live = true;
            if (down && g_touch_down[index]) {
                motion.touch_dx = std::clamp(motion.touch_dx + x - g_touch_x[index], -2.0f, 2.0f);
                motion.touch_dy = std::clamp(motion.touch_dy + y - g_touch_y[index], -2.0f, 2.0f);
            }
            g_touch_down[index] = down;
            g_touch_x[index] = x;
            g_touch_y[index] = y;
        }
    }
}

// Each pad's presses since the game last took the input (P-075): from every sample and every button event. Guarded by
// g_input_mutex.
std::array<galaxy::input::PressLatch, kMaxPads> g_pad_presses;

// The unified layout's bit for one of SDL's buttons on a controller of `scheme` (0: not used). Back buttons: SDL's upper
// pair is the Edge's paddles (the Steam controllers' L4 R4), its lower pair the Edge's Fn buttons (L5 R5).
std::uint32_t pad_bit(galaxy::input::ControlScheme scheme, SDL_GamepadButton button) {
    namespace pad = galaxy::input::pad;
    const bool steam = scheme == galaxy::input::ControlScheme::Steam;
    const bool back = steam || scheme == galaxy::input::ControlScheme::PlayStation;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return pad::bit(pad::kDpadUpBit);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return pad::bit(pad::kDpadDownBit);
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return pad::bit(pad::kDpadLeftBit);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return pad::bit(pad::kDpadRightBit);
    case SDL_GAMEPAD_BUTTON_START: return pad::bit(pad::kStartBit);
    case SDL_GAMEPAD_BUTTON_BACK: return pad::bit(pad::kBackBit);
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return pad::bit(pad::kLeftStickBit);
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return pad::bit(pad::kRightStickBit);
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return pad::bit(pad::kLeftShoulderBit);
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return pad::bit(pad::kRightShoulderBit);
    case SDL_GAMEPAD_BUTTON_GUIDE: return pad::bit(pad::kHomeBit);
    case SDL_GAMEPAD_BUTTON_MISC1: return pad::bit(pad::kCaptureBit);
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return back ? pad::bit(pad::kGripLeftBit) : 0u;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return back ? pad::bit(pad::kGripRightBit) : 0u;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return back ? pad::bit(steam ? pad::kGripLeft2Bit : pad::kFnLeftBit) : 0u;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return back ? pad::bit(steam ? pad::kGripRight2Bit : pad::kFnRightBit) : 0u;
    case SDL_GAMEPAD_BUTTON_SOUTH: return pad::bit(pad::kSouthBit);
    case SDL_GAMEPAD_BUTTON_EAST: return pad::bit(pad::kEastBit);
    case SDL_GAMEPAD_BUTTON_WEST: return pad::bit(pad::kWestBit);
    case SDL_GAMEPAD_BUTTON_NORTH: return pad::bit(pad::kNorthBit);
    default: return 0u;
    }
}

// A button's press event, between two samples. Called with g_input_mutex held.
void pad_button_event(const SDL_GamepadButtonEvent& event) {
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        if (g_gamepads[i] != nullptr && SDL_GetGamepadID(g_gamepads[i]) == event.which) {
            g_pad_presses[i].press(pad_bit(scheme_of(g_gamepads[i]), static_cast<SDL_GamepadButton>(event.button)));
            return;
        }
    }
}

void sample_gamepads() {
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        SDL_Gamepad* gamepad = g_gamepads[i];
        galaxy::input::PadSample& sample = g_input.pads[i];
        sample = {};
        g_input.pad_ids[i] = 0;
        g_input.pad_icons[i] = galaxy::input::ButtonIconModel::Standard;
        if (gamepad == nullptr || !SDL_GamepadConnected(gamepad)) {
            continue;
        }
        g_input.pad_ids[i] = SDL_GetGamepadID(gamepad);
        sample.connected = true;
        sample.scheme = scheme_of(gamepad);
        g_input.pad_icons[i] = icon_model_of(gamepad);
        std::uint32_t bits = 0;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(b))) {
                bits |= pad_bit(sample.scheme, static_cast<SDL_GamepadButton>(b));
            }
        }
        sample.buttons = bits;
        g_pad_presses[i].press(bits);
        // SDL's Y axes grow downward; the unified layout's grow upward.
        sample.axes[0] = axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        sample.axes[1] = -axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        sample.axes[2] = axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        sample.axes[3] = -axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        sample.axes[4] = std::max(0.0f, axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        sample.axes[5] = std::max(0.0f, axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (bits != 0u || std::any_of(std::begin(sample.axes), std::end(sample.axes), [](float a) { return std::abs(a) > 0.5f; })) {
            g_last_input.store(kLastPad + static_cast<std::uint32_t>(sample.scheme), std::memory_order_relaxed);
            g_last_pad_icons.store(static_cast<std::uint8_t>(g_input.pad_icons[i]), std::memory_order_relaxed);
            g_last_pad.store(g_input.pad_ids[i], std::memory_order_relaxed);
        }
        sample_pad_motion(i, gamepad);
        poll_pad_motion(i, gamepad);
        report_pad_motion(i);
        if (g_capture_device.has_value() && g_capture_device->scheme == sample.scheme && !g_capture_result.has_value()) {
            const std::uint32_t fresh = bits & ~g_capture_previous;
            if (fresh != 0u) {
                g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::Button, static_cast<std::uint8_t>(std::countr_zero(fresh))};
            } else {
                for (std::size_t axis_index = 0; axis_index < std::size(sample.axes); ++axis_index) {
                    if (sample.axes[axis_index] >= 0.6f) {
                        g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::AxisPositive, static_cast<std::uint8_t>(axis_index)};
                        break;
                    }
                    if (sample.axes[axis_index] <= -0.6f) {
                        g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::AxisNegative, static_cast<std::uint8_t>(axis_index)};
                        break;
                    }
                }
            }
            // The menu waits for the press: told as the Windows host tells it, it takes the input and binds it.
            if (g_capture_result.has_value()) {
                const std::lock_guard<std::mutex> keys(g_menu_keys_mutex);
                g_menu_keys.push_back(galaxy::host::kBindingCaptureKey);
            }
        }
        g_capture_previous = bits;
    }
}

void open_gamepad(SDL_JoystickID id) {
    for (std::size_t index = 0; index < kMaxPads; ++index) {
        SDL_Gamepad*& slot = g_gamepads[index];
        if (slot == nullptr) {
            slot = SDL_OpenGamepad(id);
            if (slot != nullptr) {
                // A new controller's motion starts from rest, never from the last one in this slot.
                g_motion[index] = {};
                g_motion_ns[index] = 0u;
                g_accel_valid[index] = false;
                g_touch_down[index] = false;
                g_input.motion[index] = {};
                g_input.pad_names[index] = galaxy::input::kept_model_name(SDL_GetGamepadName(slot) != nullptr ? SDL_GetGamepadName(slot) : "");
                PadMotionSource& source = g_motion_source[index];
                source = {};
#if defined(__ANDROID__)
                // A pad read through Android's input devices takes its motion from PadSensors.java: SDL turning on the
                // same sensors would set them to its 50 a second for both.
                const bool sdl_sensors = sdl_hid_driver(slot);
#else
                const bool sdl_sensors = true;
#endif
                source.has_gyro = sdl_sensors && SDL_GamepadHasSensor(slot, SDL_SENSOR_GYRO) &&
                                  SDL_SetGamepadSensorEnabled(slot, SDL_SENSOR_GYRO, true);
                source.has_accel = sdl_sensors && SDL_GamepadHasSensor(slot, SDL_SENSOR_ACCEL) &&
                                   SDL_SetGamepadSensorEnabled(slot, SDL_SENSOR_ACCEL, true);
                std::fprintf(stderr, "[input] gamepad \"%s\" %04x:%04x scheme %u connected; gyro %d (%.0f Hz), accelerometer %d\n",
                             SDL_GetGamepadName(slot), SDL_GetGamepadVendor(slot), SDL_GetGamepadProduct(slot),
                             static_cast<unsigned>(scheme_of(slot)), source.has_gyro ? 1 : 0,
                             static_cast<double>(SDL_GetGamepadSensorDataRate(slot, SDL_SENSOR_GYRO)),
                             source.has_accel ? 1 : 0);
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
        g_input.finger_land_ms = now;
        g_input.finger_land_x = x;
        g_input.finger_land_y = y;
        g_input.finger_moved = false;
        ++g_input.finger_touches;
        g_input.mouse_pointer = false;  // Android's touches come here from the activity, not as SDL's finger events
        g_input.finger_travel_px = 0.0f;
        g_input.finger_vx = 0.0f;
        g_input.finger_vy = 0.0f;
        g_finger_sample_count = 0u;
    }
    if (!g_pointer_finger_valid || id != g_pointer_finger) {
        return;
    }
    const std::uint64_t now_us = galaxy::steady_us();
    // The lift's own place is not a sample: Android's reports it late, where the last move left it.
    if (!up) {
        add_finger_sample(now_us, x, y);
    }
    const float side = static_cast<float>(std::max(1, std::min(g_input.window_width, g_input.window_height)));
    g_input.finger_travel_px = std::max(g_input.finger_travel_px, std::hypot(x - g_finger_down_x, y - g_finger_down_y));
    g_finger_travel = std::max(g_finger_travel, std::hypot(x - g_finger_down_x, y - g_finger_down_y) / side);
    if (!g_input.finger_moved &&
        std::hypot(x - g_finger_down_x, y - g_finger_down_y) > g_touch_slop_px.load(std::memory_order_relaxed)) {
        g_input.finger_moved = true;
    }
    // A swipe on a pointer screen scrolls, once it has gone further than a tap can, the list following the finger.
    if (!down && g_finger_travel > kTapTravel && g_menu_context.load(std::memory_order_relaxed) &&
        !g_direct_touch.load(std::memory_order_relaxed)) {
        g_menu_swipe_px += y - g_finger_last_y;
    }
    g_finger_last_y = y;
    g_input.finger_x = x;
    g_input.finger_y = y;
    g_input.finger_ms = now;
    g_input.finger_down = !up;
    if (up) {
        g_pointer_finger_valid = false;
        finger_speed(now_us, g_input.finger_vx, g_input.finger_vy);
        if (now - g_finger_down_ms <= kTapMs && g_finger_travel <= kTapTravel) {
            ++g_input.taps;
            g_input.tap_x = x;
            g_input.tap_y = y;
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

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
// The display setting on the desktop (P-103): MONITOR, DISPLAY MODE and RESOLUTION, applied on SDL's thread at each
// pump by the rule the Windows host keeps (display_reconcile.h). A size or display the player gives the window by hand
// is written back once the window has stopped changing for kPlacedByHandSettle, so a drag writes the settings once.
struct SdlDisplay {
    SDL_DisplayID id = 0;
    std::string key;   // RenderConfig::monitor: the name, with " (2)" for a second screen of one name
    bool primary = false;
    unsigned width = 0, height = 0, refresh_hz = 0;  // the desktop mode, in pixels
    long max_client_width = 0, max_client_height = 0;
    std::vector<std::pair<unsigned, unsigned>> resolutions;
};

std::mutex g_display_mutex;            // g_displays: written on SDL's thread, read by the settings
std::vector<SdlDisplay> g_displays;
bool g_displays_changed = true;        // SDL's thread only: displays came, went or changed mode (the window placed again)
bool g_displays_stale = false;         // SDL's thread only: a scale changed (the sizes read again, the window left)
galaxy::render::host_window::AppliedDisplay g_applied_display;  // SDL's thread only
bool g_placed_by_hand = false;         // SDL's thread only
bool g_full_screen_seen = false;       // SDL's thread only: the window entered or left full screen (its events)
std::chrono::steady_clock::time_point g_placed_by_hand_at{};
constexpr auto kPlacedByHandSettle = std::chrono::milliseconds(300);

std::vector<SdlDisplay> read_displays() {
    std::vector<SdlDisplay> out;
    int count = 0;
    SDL_DisplayID* const ids = SDL_GetDisplays(&count);
    if (ids == nullptr) {
        return out;
    }
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        const char* const name = SDL_GetDisplayName(ids[i]);
        names.emplace_back(name != nullptr && name[0] != '\0' ? name : "Display");
    }
    const std::vector<std::string> keys = galaxy::render::host_window::display_keys(names);
    // The window's frame, as Windows takes it from the monitor's work area (settings_monitors).
    int top = 0, left = 0, bottom = 0, right = 0;
    if (g_window == nullptr || !SDL_GetWindowBordersSize(g_window, &top, &left, &bottom, &right)) {
        top = left = bottom = right = 0;
    }
    const float density = g_window != nullptr ? std::max(SDL_GetWindowPixelDensity(g_window), 1.0f) : 1.0f;
    const auto pixels = [](int points, float scale) { return static_cast<unsigned>(std::lround(static_cast<float>(points) * scale)); };
    for (int i = 0; i < count; ++i) {
        SdlDisplay display;
        display.id = ids[i];
        display.key = keys[static_cast<std::size_t>(i)];
        display.primary = ids[i] == primary;
        if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(ids[i]); desktop != nullptr) {
            const float scale = std::max(desktop->pixel_density, 1.0f);
            display.width = pixels(desktop->w, scale);
            display.height = pixels(desktop->h, scale);
            display.refresh_hz = static_cast<unsigned>(std::lround(desktop->refresh_rate));
        }
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(ids[i], &usable)) {
            display.max_client_width = static_cast<long>(pixels(std::max(usable.w - left - right, 0), density));
            display.max_client_height = static_cast<long>(pixels(std::max(usable.h - top - bottom, 0), density));
        } else {
            display.max_client_width = static_cast<long>(display.width);
            display.max_client_height = static_cast<long>(display.height);
        }
        int modes = 0;
        if (SDL_DisplayMode** const list = SDL_GetFullscreenDisplayModes(ids[i], &modes); list != nullptr) {
            for (int m = 0; m < modes; ++m) {
                const float scale = std::max(list[m]->pixel_density, 1.0f);
                const std::pair<unsigned, unsigned> size{pixels(list[m]->w, scale), pixels(list[m]->h, scale)};
                if (std::find(display.resolutions.begin(), display.resolutions.end(), size) == display.resolutions.end()) {
                    display.resolutions.push_back(size);
                }
            }
            SDL_free(list);
        }
        if (display.resolutions.empty() && display.width > 0) {
            display.resolutions.emplace_back(display.width, display.height);
        }
        out.push_back(std::move(display));
    }
    SDL_free(ids);
    std::stable_sort(out.begin(), out.end(), [](const SdlDisplay& a, const SdlDisplay& b) { return a.primary > b.primary; });
    return out;
}

const SdlDisplay* find_display(const std::vector<SdlDisplay>& displays, const char* key) {
    for (const SdlDisplay& display : displays) {
        if (display.key == key) return &display;
    }
    for (const SdlDisplay& display : displays) {
        if (display.primary) return &display;
    }
    return displays.empty() ? nullptr : &displays.front();
}

std::string window_display_key() {
    const SDL_DisplayID id = SDL_GetDisplayForWindow(g_window);
    const std::lock_guard<std::mutex> lock(g_display_mutex);
    for (const SdlDisplay& display : g_displays) {
        if (display.id == id) return display.key;
    }
    return {};
}

galaxy::render::host_window::DisplayState display_state_of(const galaxy::render::RenderConfig& cfg) {
    galaxy::render::host_window::DisplayState state;
    state.mode = std::min(cfg.display_mode, galaxy::render::kDisplayBorderless);
    state.monitor = cfg.monitor;
    state.window_width = cfg.window_width;
    state.window_height = cfg.window_height;
    return state;
}

// SDL's thread, each pump: the displays read again when they changed, then the window placed for the setting.
void reconcile_display() {
    using namespace galaxy::render::host_window;
    if (g_window == nullptr || hidden_window_mode_enabled()) {
        return;
    }
    const bool monitors_changed = g_displays_changed;
    if (monitors_changed || g_displays_stale) {
        std::vector<SdlDisplay> displays = read_displays();
        const std::lock_guard<std::mutex> lock(g_display_mutex);
        g_displays = std::move(displays);
        g_displays_changed = false;
        g_displays_stale = false;
    }
    const galaxy::render::RenderConfig cfg = galaxy::render::get_render_config();
    const DisplayState wanted = display_state_of(cfg);
    const bool windowed = wanted.mode == galaxy::render::kDisplayWindowed;
    // A drag never differs from the setting here: what the player places is written to both (keep_placed_by_hand).
    if (!display_needs_placing(g_applied_display, wanted, monitors_changed, windowed, false)) {
        return;
    }
    SdlDisplay target;
    {
        const std::lock_guard<std::mutex> lock(g_display_mutex);
        const SdlDisplay* const found = find_display(g_displays, wanted.monitor.data());
        if (found == nullptr) {
            return;
        }
        target = *found;
    }
    if (windowed) {
        const float density = std::max(SDL_GetWindowPixelDensity(g_window), 1.0f);
        const auto points = [density](unsigned pixels, long most) {
            const long fit = most > 0 ? std::min(static_cast<long>(pixels), most) : static_cast<long>(pixels);
            return std::max(static_cast<int>(std::lround(static_cast<float>(std::max(fit, 1L)) / density)), 1);
        };
        (void)SDL_SetWindowFullscreen(g_window, false);
        (void)SDL_SetWindowSize(g_window, points(wanted.window_width, target.max_client_width),
                                points(wanted.window_height, target.max_client_height));
        (void)SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(target.id), SDL_WINDOWPOS_CENTERED_DISPLAY(target.id));
    } else {
        // Borderless: the desktop's own mode on that display (a null fullscreen mode). A Wayland compositor picks the
        // display itself; elsewhere the window is moved there first.
        if (SDL_GetDisplayForWindow(g_window) != target.id) {
            (void)SDL_SetWindowFullscreen(g_window, false);
            (void)SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(target.id),
                                        SDL_WINDOWPOS_CENTERED_DISPLAY(target.id));
        }
        (void)SDL_SetWindowFullscreenMode(g_window, nullptr);
        (void)SDL_SetWindowFullscreen(g_window, true);
    }
    // A window not shown yet (until its first picture) takes the placement when shown: an X server answers nothing
    // for it, so there is nothing to wait for.
    if ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN) == 0u) {
        (void)SDL_SyncWindow(g_window);
    }
    g_applied_display = {true, wanted};
    std::fprintf(stderr, "[window] display %s %s\n", windowed ? "windowed" : "borderless", target.key.c_str());
}

// The window manager took the window to full screen or out of it (a key or a menu of its own): DISPLAY MODE follows, as
// on the Mac (P-105). The host's own changes have ended before their events are read (SDL_SyncWindow), so the window is
// then as the setting has it and nothing is written.
void keep_full_screen_by_hand() {
    if (!std::exchange(g_full_screen_seen, false) || g_window == nullptr ||
        galaxy::render::host_window::hidden_window_mode_enabled()) {
        return;
    }
    const bool full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0u;
    const unsigned mode = full ? galaxy::render::kDisplayBorderless : galaxy::render::kDisplayWindowed;
    if (std::min(galaxy::render::get_render_config().display_mode, galaxy::render::kDisplayBorderless) == mode) {
        return;
    }
    galaxy::render::native_settings_display_mode_placed(mode);
    g_applied_display = {true, display_state_of(galaxy::render::get_render_config())};
}

// A size or display the player gave the window: noted at its events, written once the window has settled.
void note_placed_by_hand() {
    if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0u ||
        galaxy::render::get_render_config().display_mode != galaxy::render::kDisplayWindowed) {
        return;
    }
    g_placed_by_hand = true;
    g_placed_by_hand_at = std::chrono::steady_clock::now();
}

void keep_placed_by_hand() {
    using namespace galaxy::render::host_window;
    if (!g_placed_by_hand || std::chrono::steady_clock::now() - g_placed_by_hand_at < kPlacedByHandSettle) {
        return;
    }
    g_placed_by_hand = false;
    int width = 0, height = 0;
    if (g_window == nullptr || !SDL_GetWindowSizeInPixels(g_window, &width, &height) || width <= 0 || height <= 0) {
        return;
    }
    const std::string key = window_display_key();
    galaxy::render::native_settings_window_placed(static_cast<unsigned>(width), static_cast<unsigned>(height), key.c_str());
    g_applied_display = {true, placed_by_player(display_state_of(galaxy::render::get_render_config()),
                                                static_cast<unsigned>(width), static_cast<unsigned>(height), key)};
}

#endif

}  // namespace

bool start() noexcept {
    g_sdl_thread = std::this_thread::get_id();
#if defined(__ANDROID__)
    forward_output_to_logcat();
#else
    keep_output_in_log(galaxy::nebula_data_directory());
    install_crash_log(-1);
#endif
    // Landscape either way up: the game lays out for shapes from 4:3 wide; taller ones come with Vert+ framing.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // The host stops the game's clocks itself while the app is out of sight (in_background, from the activity);
    // SDL's thread keeps pumping events instead of blocking in them.
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#if defined(__ANDROID__)
    // A Switch or PlayStation controller on a cable is read from its own reports by SDL's drivers (HIDAPI), motion
    // included, as on a PC; Android asks once whether Nebula may use the USB device. SDL leaves HIDAPI off on Android
    // (for that question), and Android's kernels before Linux 6.1 give a Switch controller no motion. Over Bluetooth
    // Android's driver serves the controller and PadSensors.java its motion. Other HIDAPI drivers stay off (the
    // DolphinBar is WiimoteUsb.java's).
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR)) {
        std::fprintf(stderr, "[sdl] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// Requests from other threads (menu keys, rumble), on SDL's thread.
void run_requests() {
    for (;;) {
        std::uintptr_t key = 0u;
        {
            const std::lock_guard<std::mutex> lock(g_menu_keys_mutex);
            if (g_menu_keys.empty()) {
                break;
            }
            key = g_menu_keys.front();
            g_menu_keys.pop_front();
        }
        if ((key & galaxy::render::kMenuKeyKeyboard) != 0u) {
            const std::uintptr_t vk = key & ~galaxy::render::kMenuKeyKeyboard;
            (void)galaxy::render::native_settings_handle_window_key(vk, false, static_cast<std::uint32_t>(vk));
        } else {
            (void)galaxy::render::native_settings_pad_key(key);
        }
    }
#if defined(__ANDROID__)
    run_activity_calls();
    // The on-screen controls stand aside while a menu takes the screen's touches.
    static bool s_captured = false;
    if (const bool captured = galaxy::render::native_window_input_captured(); captured != s_captured) {
        s_captured = captured;
        call_activity(captured ? "menuOpened" : "menuClosed");
    }
    // The controls follow the game to and from a pointer screen or the title, and its corner buttons coming, going or
    // moving (TouchControlsView.syncMenuContext).
    static bool s_pointer_screen = false;
    static float s_corner_foot = -1.0f;
    const bool pointer_screen = g_menu_context.load(std::memory_order_relaxed) || g_title_screen.load(std::memory_order_relaxed);
    const float corner_foot = galaxy::host::corner_buttons_foot();
    if (pointer_screen != s_pointer_screen || std::fabs(corner_foot - s_corner_foot) >= 1.0f) {
        s_pointer_screen = pointer_screen;
        s_corner_foot = corner_foot;
        call_activity("menuContextChanged");
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
    // A mod's first-person look: the window holds the mouse while it is in front (SDL's relative mode hides the cursor
    // and gives motion only).
    if (g_window != nullptr) {
        bool focused = false;
        {
            const std::lock_guard<std::mutex> lock(g_input_mutex);
            focused = g_input.window_focused;
        }
        const bool hold = g_mouse_look_wanted.load(std::memory_order_acquire) && focused;
        if (hold != g_mouse_look_held && SDL_SetWindowRelativeMouseMode(g_window, hold)) {
            g_mouse_look_held = hold;
            const std::lock_guard<std::mutex> lock(g_input_mutex);
            g_input.mouse_dx = g_input.mouse_dy = 0.0f;
        }
    }
    // A mod's text option being typed in, or LumaCore's text request (input.text_request) open: SDL's text events, and
    // the system's on-screen keyboard where there is one.
    if (g_window != nullptr) {
        const bool wanted = galaxy::render::native_settings_text_wanted() || galaxy::luma_input::text_open();
        if (wanted != SDL_TextInputActive(g_window)) {
            (void)(wanted ? SDL_StartTextInput(g_window) : SDL_StopTextInput(g_window));
        }
    }
    // Every 10 s: this thread's CPU time and how much SDL's event wait took (a wait that polls rather than sleeps).
    static std::int64_t report_cpu = 0;
    static std::int64_t wait_cpu = 0;
    static std::uint32_t waits = 0;
    static auto report_time = std::chrono::steady_clock::now();
    const std::int64_t wait_start = galaxy::render::vk::thread_cpu_ns();
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    keep_full_screen_by_hand();
    reconcile_display();
    keep_placed_by_hand();
#endif
#if defined(__ANDROID__)
    sync_display_rate();
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
    wait_cpu += galaxy::render::vk::thread_cpu_ns() - wait_start;
    ++waits;
    if (const auto now = std::chrono::steady_clock::now(); now - report_time >= std::chrono::seconds(10)) {
        const std::int64_t cpu = galaxy::render::vk::thread_cpu_ns();
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
    struct MenuKey {
        int key;
        bool down;
        bool repeat;
    };
    std::vector<MenuKey> menu_keys;
    int menu_wheel = 0;
    std::vector<std::filesystem::path> dropped;
#if !defined(__ANDROID__)
    // What the system opened Nebula with (--import; the desktop entry's %u), at the first pump: a
    // file goes where a drop goes, as on Windows (host_window_win32.cpp); a nebula:// link too, which opens the MODS
    // page (native_settings_files_dropped).
    for (std::filesystem::path& opened : galaxy::render::native_settings_take_launch_imports()) {
        dropped.push_back(std::move(opened));
    }
#endif
    bool luma_display = false;  // LumaCore's display snapshot changes (services-system 4), read after the events
    {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    while (any) {
        luma_display = luma_display || galaxy::luma_system::sdl_display_event(event.type);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            galaxy::render::host_window::quit_request.store(true, std::memory_order_release);
            break;
        case SDL_EVENT_LOW_MEMORY:
            g_low_memory.store(true, std::memory_order_release);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            update_window_size();
            galaxy::render::host_window::pending_resize_extent.store(
                (static_cast<std::uint64_t>(g_input.window_width) << 32u) |
                    static_cast<std::uint32_t>(g_input.window_height),
                std::memory_order_release);
            break;
#if !defined(__ANDROID__)
        // A minimised desktop window: the host stops the game's clocks and sound (in_background) as for an app out of
        // sight, so nothing renders to a surface of no size (Android's activity reports this itself).
        case SDL_EVENT_WINDOW_MINIMIZED:
            g_background.store(true, std::memory_order_release);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            g_background.store(false, std::memory_order_release);
            break;
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
            g_displays_changed = true;
            break;
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            g_displays_stale = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            note_placed_by_hand();
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            g_displays_stale = true;
            note_placed_by_hand();
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            g_full_screen_seen = true;
            break;
#endif
#endif
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            g_input.mouse_inside = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            g_input.mouse_inside = false;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_input.window_focused = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_input.window_focused = false;
            g_input.keys = {};
            galaxy::luma_input::text_focus_lost();
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            // Platforms whose touches reach SDL (Android's go to the activity's controls, nativePointer).
            g_last_input.store(kLastNone, std::memory_order_relaxed);
            g_input.mouse_pointer = false;
            finger_event(event.tfinger.fingerID, event.tfinger.x * static_cast<float>(g_input.window_width),
                         event.tfinger.y * static_cast<float>(g_input.window_height),
                         event.type == SDL_EVENT_FINGER_DOWN,
                         event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED);
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            if (g_mouse_look_held) {
                // Held for a first-person look: the mouse's own motion (SDL's relative mode), not a place.
                g_input.mouse_dx += event.motion.xrel;
                g_input.mouse_dy += event.motion.yrel;
                g_input.mouse_pointer = true;
                g_input.mouse_inside = true;
                break;
            }
            // SDL's mouse is in window units; the picture is in pixels (two a unit on a Retina display).
            const float density = g_window != nullptr ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
            g_input.finger_x = event.motion.x * (density > 0.0f ? density : 1.0f);
            g_input.finger_y = event.motion.y * (density > 0.0f ? density : 1.0f);
            g_input.finger_ms = galaxy::steady_ms();
            g_input.mouse_pointer = true;
            g_input.mouse_inside = true;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            // In 120ths of a notch, positive scrolling back up the page (Windows' WHEEL_DELTA), as the game takes it. The
            // turn as the system gives it: with natural scrolling on (SDL_MOUSEWHEEL_FLIPPED) the list moves the way every
            // other page on the system does. A high-resolution wheel's or a touchpad's parts of a notch add up (the
            // carry), so a slow turn scrolls as far as a quick one.
            g_wheel_carry += event.wheel.y * 120.0f;
            const float whole = std::trunc(g_wheel_carry);
            g_wheel_carry -= whole;
            menu_wheel += static_cast<int>(whole);
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
            const float density = g_window != nullptr ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
            g_input.finger_x = event.button.x * (density > 0.0f ? density : 1.0f);
            g_input.finger_y = event.button.y * (density > 0.0f ? density : 1.0f);
            g_input.finger_ms = galaxy::steady_ms();
            g_input.mouse_pointer = true;
            g_input.mouse_inside = true;
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            // An open text request takes the text first (its keys too, below); otherwise a Settings text field.
            if (event.text.text != nullptr) {
                if (galaxy::luma_input::text_open()) {
                    galaxy::luma_input::text_utf8(event.text.text);
                } else {
                    (void)galaxy::render::native_settings_handle_window_text(event.text.text);
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (event.type == SDL_EVENT_KEY_DOWN && galaxy::luma_input::text_open()) {
                using galaxy::luma_input::TextKey;
                switch (event.key.scancode) {
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER: galaxy::luma_input::text_key(TextKey::Enter); break;
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_AC_BACK: galaxy::luma_input::text_key(TextKey::Escape); break;
                case SDL_SCANCODE_BACKSPACE: galaxy::luma_input::text_key(TextKey::Backspace); break;
                case SDL_SCANCODE_DELETE: galaxy::luma_input::text_key(TextKey::Delete); break;
                case SDL_SCANCODE_LEFT: galaxy::luma_input::text_key(TextKey::Left); break;
                case SDL_SCANCODE_RIGHT: galaxy::luma_input::text_key(TextKey::Right); break;
                case SDL_SCANCODE_HOME: galaxy::luma_input::text_key(TextKey::Home); break;
                case SDL_SCANCODE_END: galaxy::luma_input::text_key(TextKey::End); break;
                default: break;
                }
                break;
            }
            if (event.key.scancode == SDL_SCANCODE_AC_BACK) {
                // Android's back gesture: the HOME button, as the HOME Menu opens with it; in an open menu, back.
                const bool down = event.type == SDL_EVENT_KEY_DOWN;
                if (!down) {
                    g_back_held = false;
                }
                if (galaxy::render::native_window_input_captured()) {
                    menu_keys.push_back({virtual_key(SDL_SCANCODE_ESCAPE), down, event.key.repeat});
                } else if (down) {
                    g_back_held = true;
                    g_touch_pressed |= kTouchHome;
                }
                break;
            }
            menu_keys.push_back({virtual_key(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN, event.key.repeat});
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            pad_sensor_event(event.gsensor);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            pad_button_event(event.gbutton);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(event.gdevice.which);
            break;
#if !defined(__ANDROID__)
        // Files dropped on the window: a save to the save import, mods to the importer, Mii files to the Mii import
        // (native_settings_files_dropped, after the input lock). Android's launcher takes files there ("Open with", share, drag and drop).
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) {
                dropped.emplace_back(galaxy::text_path(event.drop.data));
            }
            break;
#endif
        default:
            break;
        }
        any = SDL_PollEvent(&event);
    }
#if defined(__ANDROID__)
    drain_android_pad_sensors();
#endif
    sample_gamepads();
    }
    if (luma_display) {
        galaxy::luma_system::sdl_window_changed(g_window);
    }
    // Pad 0 of the connected ones drives the menu as a controller does on Windows.
    {
        const InputState state = input_state();
        for (const galaxy::input::PadSample& sample : state.pads) {
            if (!sample.connected) {
                continue;
            }
            // The unified buttons are the menu's, by their labels (Nintendo's A is the right face button), as on Windows.
            const std::uint16_t buttons = galaxy::input::settings_pad_buttons(sample.scheme, sample.buttons);
            const galaxy::input::NativeSettingsPadStep step = galaxy::input::native_settings_pad_step(
                g_menu_pad, buttons, sample.axes[0], sample.axes[1],
                galaxy::render::native_settings_overlay_visible() || galaxy::render::home_menu_visible() ||
                    galaxy::ui::launcher_visible() || galaxy::bench::running(), galaxy::steady_ms());
            for (std::size_t i = 0; i < step.key_count; ++i) {
                galaxy::render::native_settings_post_key(step.keys[i] | (((step.repeats >> i) & 1u) != 0u ? galaxy::render::kMenuKeyRepeat : 0u));
            }
            break;
        }
    }
    {
        // A tenth of the window's height is a wheel notch. The game's list scrolls it itself and takes no key from it.
        const std::lock_guard<std::mutex> lock(g_input_mutex);
        const float notch_px = static_cast<float>(std::max(1, g_input.window_height)) / 10.0f;
        const int steps = static_cast<int>(g_menu_swipe_px * 120.0f / notch_px);
        if (steps != 0) {
            g_menu_swipe_px -= static_cast<float>(steps) * notch_px / 120.0f;
            galaxy::render::host_window::pointer_wheel.fetch_add(steps, std::memory_order_relaxed);
        }
    }
    if (menu_wheel != 0 && !galaxy::render::native_settings_wheel(menu_wheel)) {
        galaxy::render::host_window::pointer_wheel.fetch_add(menu_wheel, std::memory_order_relaxed);
    }
    for (const MenuKey& key : menu_keys) {
        const bool taken = key.down && galaxy::render::native_settings_handle_window_key(static_cast<std::uintptr_t>(key.key), key.repeat, static_cast<std::uint32_t>(key.key));
        if (!taken) {
            std::lock_guard<std::mutex> lock(g_input_mutex);
            set_key(key.key, key.down);
        }
    }
    if (!dropped.empty()) {
        galaxy::render::native_settings_files_dropped(std::move(dropped));
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

void set_title_screen(bool title) noexcept {
    g_title_screen.store(title, std::memory_order_relaxed);
}

void set_menu_context(bool menu) noexcept {
    g_menu_context.store(menu, std::memory_order_relaxed);
}

void set_direct_touch(bool direct) noexcept {
    g_direct_touch.store(direct, std::memory_order_relaxed);
}

bool in_background() noexcept {
    return g_background.load(std::memory_order_acquire);
}

bool take_low_memory() noexcept {
    return g_low_memory.exchange(false, std::memory_order_acq_rel);
}

InputState input_state() noexcept {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    InputState state = g_input;
    state.touch_buttons |= g_touch_pressed | (g_back_held ? kTouchHome : 0u);
    state.touch_slop_px = g_touch_slop_px.load(std::memory_order_relaxed);
    return state;
}

InputState take_input_state() noexcept {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    InputState state = g_input;
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        const std::uint32_t buttons = g_pad_presses[i].take(state.pads[i].buttons);
        if (state.pads[i].connected) state.pads[i].buttons = buttons;
    }
    state.touch_buttons |= g_touch_pressed | (g_back_held ? kTouchHome : 0u);
    g_touch_pressed = 0u;
    for (PadMotion& motion : g_input.motion) {
        motion.gyro_x = motion.gyro_y = 0.0f;
        motion.touch_dx = motion.touch_dy = 0.0f;
    }
    g_input.mouse_dx = g_input.mouse_dy = 0.0f;
    return state;
}

void set_mouse_look(bool hold) noexcept {
    g_mouse_look_wanted.store(hold, std::memory_order_release);
}

void rumble(std::uint32_t pad_id, float strength) noexcept {
    g_rumble_level.store(static_cast<std::uint32_t>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f),
                         std::memory_order_relaxed);
    g_rumble_pad.store(pad_id, std::memory_order_relaxed);
}

}  // namespace galaxy::sdl

#if defined(__ANDROID__)
// The activity's on-screen controls (GameActivity.java): every change of the held buttons or the stick.
// The phone's accelerometer (metres a second squared) and gyroscope (radians a second) on the screen's axes as it is turned:
// x right, y up, z out of the glass. As a controller lying flat: x right, y out of the face, z toward the player.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePhoneMotion(
    JNIEnv*, jclass, jfloat ax, jfloat ay, jfloat az, jfloat gx, jfloat gy, jfloat gz, jlong time_ns) {
    namespace sw = galaxy::input::switch_hid;
    static sw::SwitchMotion s_motion;
    static std::int64_t s_last_ns = 0;
    const float dt = s_last_ns != 0 ? std::min(0.1f, static_cast<float>(time_ns - s_last_ns) * 1e-9f) : 0.0f;
    s_last_ns = time_ns;
    sw::ImuSample imu{};
    imu.gyro[0] = gx;
    imu.gyro[1] = gz;
    imu.gyro[2] = -gy;
    imu.accel[0] = ax / SDL_STANDARD_GRAVITY;
    imu.accel[1] = az / SDL_STANDARD_GRAVITY;
    imu.accel[2] = -ay / SDL_STANDARD_GRAVITY;
    sw::MotionSettings settings{};
    settings.gyro_pointer = false;
    const std::uint64_t now_us = galaxy::steady_us();
    const sw::SwitchMotion::Output out = s_motion.feed(imu, dt, now_us, settings);
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    galaxy::sdl::PadMotion& motion = galaxy::sdl::g_input.device_motion;
    motion.imu_live = true;
    motion.gravity = s_motion.gravity();
    motion.gravity_valid = s_motion.gravity_ready();
    if (out.shake_started) {
        motion.shake_until_us = now_us + sw::SwitchMotion::kShakeHoldUs;
    }
}

// PadSensors.java (Java's sensor thread): a controller's motion sensors as Android lists them, gone, and each sample.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePadSensorDevice(
    JNIEnv* env, jclass, jint id, jstring name, jstring descriptor, jint vendor, jint product, jboolean gyro, jboolean accel) {
    using namespace galaxy::sdl;
    std::uint16_t crc = 0u;
    if (const char* const chars = descriptor != nullptr ? env->GetStringUTFChars(descriptor, nullptr) : nullptr) {
        crc = SDL_crc16(0u, chars, std::strlen(chars));
        env->ReleaseStringUTFChars(descriptor, chars);
    }
    const char* const label = name != nullptr ? env->GetStringUTFChars(name, nullptr) : nullptr;
    std::fprintf(stderr, "[input-sensors] \"%s\" %04x:%04x: gyro %d, accelerometer %d%s\n", label != nullptr ? label : "?",
                 static_cast<unsigned>(vendor) & 0xFFFFu, static_cast<unsigned>(product) & 0xFFFFu, gyro ? 1 : 0, accel ? 1 : 0,
                 gyro || accel ? "" : " (Android lists no motion sensors for it)");
    if (label != nullptr) {
        env->ReleaseStringUTFChars(name, label);
    }
    std::lock_guard<std::mutex> lock(g_android_pad_mutex);
    auto device = std::find_if(g_android_pads.begin(), g_android_pads.end(), [id](const AndroidPadDevice& d) { return d.id == id; });
    if (device == g_android_pads.end()) {
        g_android_pads.push_back(AndroidPadDevice{});
        device = g_android_pads.end() - 1;
    }
    device->id = id;
    device->crc = crc;
    device->vendor = static_cast<std::uint16_t>(vendor);
    device->product = static_cast<std::uint16_t>(product);
    device->gyro = gyro;
    device->accel = accel;
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePadSensorRemoved(JNIEnv*, jclass, jint id) {
    using namespace galaxy::sdl;
    std::lock_guard<std::mutex> lock(g_android_pad_mutex);
    g_android_pads.erase(std::remove_if(g_android_pads.begin(), g_android_pads.end(), [id](const AndroidPadDevice& d) { return d.id == id; }),
                         g_android_pads.end());
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePadSensor(JNIEnv*, jclass, jint id, jint type, jlong time_ns,
                                                                                        jfloat x, jfloat y, jfloat z) {
    using namespace galaxy::sdl;
    std::lock_guard<std::mutex> lock(g_android_pad_mutex);
    if (g_android_samples.size() >= kAndroidSampleLimit) {
        return;  // SDL's thread is not pumping (the game is out of sight): the newest are dropped
    }
    g_android_samples.push_back(AndroidPadSample{id, type, static_cast<std::uint64_t>(time_ns), {x, y, z}});
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchControls(
    JNIEnv*, jclass, jint buttons, jfloat stick_x, jfloat stick_y) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    const auto held = static_cast<std::uint32_t>(buttons);
    if ((held & ~galaxy::sdl::g_input.touch_buttons) != 0u) {
        galaxy::sdl::g_last_input.store(galaxy::sdl::kLastNone, std::memory_order_relaxed);
    }
    galaxy::sdl::g_touch_pressed |= held & ~galaxy::sdl::g_input.touch_buttons;
    galaxy::sdl::g_input.touch_buttons = held;
    galaxy::sdl::g_input.touch_stick_x = std::clamp(stick_x, -1.0f, 1.0f);
    galaxy::sdl::g_input.touch_stick_y = std::clamp(stick_y, -1.0f, 1.0f);
}

// A finger on the picture that is not on a control (the activity's controls take every touch): action 0 down, 1 moved,
// 2 up or cancelled; x, y in the picture's pixels.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePointer(
    JNIEnv*, jclass, jint action, jint id, jfloat x, jfloat y) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    if (action == 0) {
        galaxy::sdl::g_last_input.store(galaxy::sdl::kLastNone, std::memory_order_relaxed);
    }
    galaxy::sdl::finger_event(static_cast<std::uint64_t>(id), x, y, action == 0, action == 2);
}

extern "C" JNIEXPORT jboolean JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTitleScreen(JNIEnv*, jclass) {
    return galaxy::sdl::g_title_screen.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

// A finger there grabs something: it is the pointer, not the stick.
extern "C" JNIEXPORT jboolean JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchTargetAt(JNIEnv*, jclass, jfloat x, jfloat y) {
    return galaxy::host::touch_target_at(x, y) ? JNI_TRUE : JNI_FALSE;
}

// How far down the game's own buttons in the top corners reach, in the picture's pixels; -1 while there are none.
extern "C" JNIEXPORT jfloat JNICALL Java_dev_nebula_galaxy_GameActivity_nativeCornerButtonsFoot(JNIEnv*, jclass) {
    return galaxy::host::corner_buttons_foot();
}

// Whether the game is on a pointer screen (set_menu_context), for the controls deciding what a touch is.
extern "C" JNIEXPORT jboolean JNICALL Java_dev_nebula_galaxy_GameActivity_nativeMenuContext(JNIEnv*, jclass) {
    return galaxy::sdl::g_menu_context.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

// The activity in sight (onStart) or not (onStop): the host stops the game's clocks while it is not, and nothing
// stays held meanwhile.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSetVisible(JNIEnv*, jclass, jboolean visible) {
    std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    if (!visible) {
        galaxy::sdl::g_input.keys = {};
        galaxy::sdl::g_input.touch_buttons = 0;
        galaxy::sdl::g_back_held = false;
        galaxy::sdl::g_touch_pressed = 0u;
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
    galaxy::render::host_window::surface_withdrawn.store(true, std::memory_order_release);
    if (galaxy::render::host_window::surface_use.try_lock_for(std::chrono::seconds(2))) {
        galaxy::render::host_window::surface_use.unlock();
    } else {
        std::fprintf(stderr, "[window] the renderer kept the surface past 2 s\n");
    }
}

// A new surface is ready (after SDL took it in surfaceChanged): the renderer makes its own for it.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSurfaceReturned(JNIEnv*, jclass) {
    galaxy::render::host_window::surface_withdrawn.store(false, std::memory_order_release);
}

// The panel's fastest refresh rate at its resolution (on start and whenever the display changes).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativePanelRate(JNIEnv*, jclass, jfloat hz) {
    galaxy::sdl::g_panel_fastest_hz.store(hz > 1.0f ? hz : 0.0f, std::memory_order_relaxed);
}

// The display's touch slop in window pixels (ViewConfiguration.getScaledTouchSlop).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchSlop(JNIEnv*, jclass, jfloat px) {
    galaxy::sdl::g_touch_slop_px.store(px > 0.0f ? px : 0.0f, std::memory_order_relaxed);
}

// The frame-rate setting (max_fps; 0: the display's own) for the activity's display mode.
extern "C" JNIEXPORT jfloat JNICALL Java_dev_nebula_galaxy_GameActivity_nativeWantedFrameRate(JNIEnv*, jclass) {
    return galaxy::render::get_render_config().max_fps;
}
#endif

// The renderer's window (host_window.h).

namespace galaxy::render::host_window {

unsigned backbuffer_width = 0;
unsigned backbuffer_height = 0;
std::atomic<bool> borderless_active{true};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0u};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    const bool hidden = hidden_window_mode_enabled();
#if defined(NEBULA_GPU_METAL)
    SDL_WindowFlags flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
#if defined(__ANDROID__)
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    // The system's launch screen stands until the first picture.
    constexpr bool kShownWithFirstPicture = false;
#else
    // Shown with its first picture (show_window): the start draws no empty window.
    constexpr bool kShownWithFirstPicture = true;
#endif
    if (hidden || kShownWithFirstPicture) {
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
        galaxy::luma_input::set_text_entry_available(true);
        galaxy::sdl::update_window_size();
        backbuffer_width = static_cast<unsigned>(galaxy::sdl::g_input.window_width);
        backbuffer_height = static_cast<unsigned>(galaxy::sdl::g_input.window_height);
    }
    owner.window = window;
    // LumaCore's clipboard and links on SDL, and the first display snapshot (services-system 4 to 6).
    galaxy::luma_system::set_platform(galaxy::luma_system::sdl_platform());
    galaxy::luma_system::sdl_window_changed(window);
    return {window, !hidden && !kShownWithFirstPicture};
}

void close_game_window(WindowOwner& owner) noexcept {
    if (owner.window != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(owner.window));
        owner.window = nullptr;
    }
    galaxy::sdl::g_window = nullptr;
}

void show_window(NativeWindow window) noexcept {
    // On the main thread, as SDL asks for a window's changes; it takes the focus as a new window does.
    (void)SDL_RunOnMainThread(
        [](void* shown) {
            SDL_ShowWindow(static_cast<SDL_Window*>(shown));
            SDL_RaiseWindow(static_cast<SDL_Window*>(shown));
        },
        window, false);
}

bool window_unseen(NativeWindow window) noexcept {
    if (window == nullptr) {
        return true;
    }
    return (SDL_GetWindowFlags(static_cast<SDL_Window*>(window)) &
            (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) != 0u;
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

void set_state_wake(void*) noexcept {}

bool vblank_grid_known(std::int64_t& vblank, std::int64_t& period) noexcept {
    return vblank_grid(nullptr, vblank, period);
}

bool vblank_grid(NativeWindow, std::int64_t& vblank, std::int64_t& period) noexcept {
    return galaxy::sdl::vsync_grid(vblank, period);  // qpc_now() is CLOCK_MONOTONIC nanoseconds off Windows
}

bool present_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now_ns) noexcept {
    return galaxy::sdl::display_grid(slot_ns, period_ns, vsync_ns, now_ns);
}

// What the settings menu asks of its platform (host_window.h).
std::vector<SettingsMonitor> settings_monitors() {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    // The desktop's displays as SDL's thread last read them (reconcile_display), each with its own sizes.
    {
        std::vector<SettingsMonitor> out;
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_display_mutex);
        for (const galaxy::sdl::SdlDisplay& display : galaxy::sdl::g_displays) {
            SettingsMonitor entry;
            entry.id = display.key;
            entry.name = display.key;
            entry.width = display.width;
            entry.height = display.height;
            entry.refresh_hz = display.refresh_hz;
            entry.max_client_width = display.max_client_width;
            entry.max_client_height = display.max_client_height;
            entry.primary = display.primary;
            entry.resolutions = display.resolutions;
            out.push_back(std::move(entry));
        }
        if (!out.empty()) {
            return out;
        }
    }
#endif
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    SettingsMonitor monitor;
    monitor.id = "display";
    monitor.name = "Display";
    monitor.width = static_cast<unsigned>(state.window_width);
    monitor.height = static_cast<unsigned>(state.window_height);
    monitor.refresh_hz = galaxy::sdl::g_window != nullptr ? static_cast<unsigned>(std::lround(display_refresh_hz(galaxy::sdl::g_window))) : 0u;
#if defined(__ANDROID__)
    monitor.refresh_hz = std::max(monitor.refresh_hz, static_cast<unsigned>(std::lround(
                                                          galaxy::sdl::g_panel_fastest_hz.load(std::memory_order_relaxed))));
#endif
    monitor.max_client_width = state.window_width;
    monitor.max_client_height = state.window_height;
    monitor.primary = true;
    monitor.resolutions.emplace_back(monitor.width, monitor.height);
    return {std::move(monitor)};
}

bool window_modal_active() noexcept {
    return false;
}

void resize_window_client(unsigned width, unsigned height) noexcept {
    // SDL's window calls belong to its main thread; the size is in the window's points.
    const auto packed = static_cast<std::uintptr_t>(width) << 16u | static_cast<std::uintptr_t>(height & 0xFFFFu);
    (void)SDL_RunOnMainThread(
        [](void* size) {
            SDL_Window* const window = galaxy::sdl::g_window;
            if (window == nullptr) {
                return;
            }
            const auto value = reinterpret_cast<std::uintptr_t>(size);
            const float density = std::max(SDL_GetWindowPixelDensity(window), 1.0f);
            (void)SDL_SetWindowSize(window, static_cast<int>(std::lround(static_cast<float>(value >> 16u) / density)),
                                    static_cast<int>(std::lround(static_cast<float>(value & 0xFFFFu) / density)));
        },
        reinterpret_cast<void*>(packed), false);
}

#if defined(__ANDROID__)
// The document picker (GameActivity.pickFiles): Android hands back copies of the chosen files the game reads by path
// (nativeFilesPicked). One picker at a time; a second ask while one is open answers nothing.
namespace {
std::mutex g_pick_mutex;
PickDone g_pick_done;
}  // namespace

void pick_files(FilePick kind, PickDone done) {
    if (kind == FilePick::ModFolders) {
        done({}, false);
        return;
    }
    bool open = false;
    {
        const std::lock_guard<std::mutex> lock(g_pick_mutex);
        open = static_cast<bool>(g_pick_done);
        if (!open) g_pick_done = std::move(done);
    }
    if (open) {
        done({}, false);
        return;
    }
    galaxy::sdl::call_activity(
        galaxy::sdl::ActivityCall{"pickFiles", true, kind == FilePick::Miis ? 0 : kind == FilePick::Saves ? 2 : 1});
}

// The save dialog (GameActivity.saveCopy: a document the player creates, the file copied into it). One at a time.
namespace {
std::mutex g_save_copy_mutex;
std::function<void(SaveCopy)> g_save_copy_done;
std::string g_save_copy_file;
}  // namespace

void save_copy(const std::filesystem::path& file, std::function<void(SaveCopy)> done) {
    bool busy = false;
    {
        const std::lock_guard<std::mutex> lock(g_save_copy_mutex);
        busy = static_cast<bool>(g_save_copy_done);
        if (!busy) {
            g_save_copy_done = std::move(done);
            g_save_copy_file = galaxy::path_text(file);
        }
    }
    if (busy) {
        done(SaveCopy::Failed);
        return;
    }
    galaxy::sdl::call_activity("saveCopy");
}

// The file saveCopy offers, as UTF-8 bytes; empty when none is asked for.
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSaveCopyFile(JNIEnv* env, jclass) {
    std::string file;
    {
        const std::lock_guard<std::mutex> lock(g_save_copy_mutex);
        if (g_save_copy_done) file = g_save_copy_file;
    }
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(file.size()));
    if (bytes != nullptr && !file.empty()) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(file.size()), reinterpret_cast<const jbyte*>(file.data()));
    }
    return bytes;
}

// saveCopy's answer: 0 saved, 1 cancelled, 2 failed.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeCopySaved(JNIEnv*, jclass, jint result) {
    std::function<void(SaveCopy)> done;
    {
        const std::lock_guard<std::mutex> lock(g_save_copy_mutex);
        done = std::move(g_save_copy_done);
        g_save_copy_done = nullptr;
        g_save_copy_file.clear();
    }
    if (done) done(result == 0 ? SaveCopy::Saved : result == 1 ? SaveCopy::Cancelled : SaveCopy::Failed);
}

// The picker's answer: the copies' paths as UTF-8 bytes (JNI's own strings are modified UTF-8), none when cancelled.
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeFilesPicked(JNIEnv* env, jclass,
                                                                                       jobjectArray paths) {
    std::vector<std::filesystem::path> files;
    const jsize count = paths != nullptr ? env->GetArrayLength(paths) : 0;
    for (jsize i = 0; i < count; ++i) {
        auto* bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(paths, i));
        if (bytes == nullptr) continue;
        const jsize size = env->GetArrayLength(bytes);
        std::string text(static_cast<std::size_t>(size), '\0');
        env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
        env->DeleteLocalRef(bytes);
        files.emplace_back(std::move(text));
    }
    PickDone done;
    {
        const std::lock_guard<std::mutex> lock(g_pick_mutex);
        done = std::move(g_pick_done);
        g_pick_done = nullptr;
    }
    if (done) done(std::move(files), false);
}

void open_folder(const std::filesystem::path&) {}

bool folders_open() noexcept {
    return false;
}
#else
// SDL's own dialogs: the desktop's (a portal or Zenity on Linux, the Cocoa panel on macOS), answered on a thread of
// SDL's or of the window's.
namespace {
struct PickRequest {
    PickDone done;
    // The filters SDL reads until it answers; their names are the host's words (host_text), static storage.
    std::array<SDL_DialogFileFilter, 2> filters{};
};

// One of SDL's dialogs titled in the host's language (host_text, P-106).
void show_dialog(SDL_FileDialogType type, SDL_DialogFileCallback callback, void* request, const char* title,
                 const SDL_DialogFileFilter* filters, int count, const char* location, bool many) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, galaxy::sdl::g_window);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    if (count > 0) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, count);
    }
    if (location != nullptr) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, many);
    SDL_ShowFileDialogWithProperties(type, callback, request, props);
    SDL_DestroyProperties(props);  // what the dialog keeps of them it has copied, as SDL_ShowOpenFileDialog does
}

void SDLCALL picked(void* userdata, const char* const* files, int) {
    std::unique_ptr<PickRequest> request(static_cast<PickRequest*>(userdata));
    // No list at all: no dialog could be shown (on Linux, neither a desktop portal nor Zenity is there).
    if (files == nullptr) {
        std::fprintf(stderr, "[sdl] the file picker did not open: %s\n", SDL_GetError());
        request->done({}, true);
        return;
    }
    std::vector<std::filesystem::path> chosen;
    for (const char* const* file = files; *file != nullptr; ++file) {
        chosen.emplace_back(*file);
    }
    request->done(std::move(chosen), false);
}
}  // namespace

void pick_files(FilePick kind, PickDone done) {
    namespace text = galaxy::host_text;
    auto* request = new PickRequest{std::move(done)};
    const char* const language = text::language();
    const auto say = [language](const char* english) { return text::translated(english, language).data(); };
    if (kind == FilePick::ModFolders) {
        show_dialog(SDL_FILEDIALOG_OPENFOLDER, picked, request, say(text::kImportModFolders), nullptr, 0, nullptr, true);
        return;
    }
    // As Windows' picker lists them (host_window_win32.cpp).
    const char* const patterns = kind == FilePick::Miis ? "mii;rcd;rsd;mae;miigx;ncd;nsd;cfcd;cfsd;ffcd;ffsd;charinfo;nfsd;"
                                                          "nfcd;mnms;emnms;miic;dat;bin;txt"
                                 : kind == FilePick::Saves ? "zip;sav;bin"
                                                           : "luma;zip;7z;rar;tar;tgz;gz;txz;xz;tbz2;bz2;tzst;zst";
    const char* const name = say(kind == FilePick::Miis    ? text::kMiiFiles
                                 : kind == FilePick::Saves ? text::kSaveFiles
                                                           : text::kModFiles);
    request->filters = {{{name, patterns}, {say(text::kAllFiles), "*"}}};
    const char* const title = say(kind == FilePick::Miis    ? text::kImportMiis
                                  : kind == FilePick::Saves ? text::kImportSaves
                                                            : text::kImportMods);
    show_dialog(SDL_FILEDIALOG_OPENFILE, picked, request, title, request->filters.data(), 2, nullptr,
                kind != FilePick::Saves);
}

// SDL's save dialog; the file is copied where the player chose (a ".zip" added when the name has no extension).
namespace {
struct SaveRequest {
    std::filesystem::path file;
    std::function<void(SaveCopy)> done;
    SDL_DialogFileFilter zip{};  // read by SDL until it answers
};

void SDLCALL save_chosen(void* userdata, const char* const* files, int) {
    std::unique_ptr<SaveRequest> request(static_cast<SaveRequest*>(userdata));
    if (files == nullptr) {
        request->done(SaveCopy::Failed);
        return;
    }
    if (files[0] == nullptr) {
        request->done(SaveCopy::Cancelled);
        return;
    }
    std::filesystem::path target(files[0]);
    if (!target.has_extension()) target += ".zip";
    std::error_code error;
    std::filesystem::copy_file(request->file, target, std::filesystem::copy_options::overwrite_existing, error);
    request->done(error ? SaveCopy::Failed : SaveCopy::Saved);
}
}  // namespace

void save_copy(const std::filesystem::path& file, std::function<void(SaveCopy)> done) {
    namespace text = galaxy::host_text;
    const char* const language = text::language();
    auto* request = new SaveRequest{file, std::move(done)};
    request->zip = {text::translated(text::kZipArchive, language).data(), "zip"};
    const std::string name = galaxy::path_text(file.filename());
    show_dialog(SDL_FILEDIALOG_SAVEFILE, save_chosen, request, text::translated(text::kExportSaves, language).data(),
                &request->zip, 1, name.c_str(), false);
}

// The file manager at the folder (made first, as on Windows).
void open_folder(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::string url = "file://" + galaxy::path_text(std::filesystem::absolute(folder, error));
    if (!SDL_OpenURL(url.c_str())) {
        std::fprintf(stderr, "[sdl] open folder failed: %s\n", SDL_GetError());
    }
}

bool folders_open() noexcept {
    return true;
}
#endif

// AMD's FSR 3 frame generation where the device runs it: built in, its two queues given, and FSR 3's compute features
// (before the device, what the build holds).
UpscalerCaps upscaler_caps() noexcept {
    UpscalerCaps caps;
#if !defined(NEBULA_GPU_METAL)
    const vk::Context& context = vk::Context::get();
    caps.fsr_frame_generation =
        fsr_frame_generation_compiled() &&
        (context.device() == VK_NULL_HANDLE ||
         (context.generation_present_queue() != VK_NULL_HANDLE && context.generation_acquire_queue() != VK_NULL_HANDLE &&
          context.temporal_upscaling()));
#endif
    if (get_render_config().frame_generation == 2u) {
        caps.frames_presented = frame_generation_running() && frame_generation_allowed() ? 2u : 1u;
    }
    return caps;
}

unsigned dlss_min_width(unsigned, unsigned, unsigned) noexcept {
    return 0u;
}

bool dlss_files_present() {
    return false;
}

bool frame_generation_files_present() {
    return false;
}

bool xess_files_present() {
    return false;
}

unsigned vendor_megabytes(unsigned) {
    return 0u;
}

bool start_vendor_install(unsigned) {
    return false;
}

void cancel_vendor_install() {}

VendorInstall poll_vendor_install(unsigned& percent) {
    percent = 0u;
    return VendorInstall::Failed;
}

void fetch_vendor_license(unsigned) {}

VendorLicense vendor_license(std::string&) {
    return VendorLicense::Failed;
}

void quit_game() noexcept {
    quit_request.store(true, std::memory_order_release);
}

#if defined(__APPLE__) && TARGET_OS_IPHONE
// An iOS app cannot start itself again (host_main kHostStartsAgain).
bool restart_game(Restart) {
    return false;
}

int restart_wanted() noexcept {
    return 0;
}

bool can_restart() noexcept {
    return false;
}
#else
namespace {
std::atomic<int> restart_request{0};  // restart_wanted: 0 none, else a Restart's value
}  // namespace

// The game ends as for Quit, and the host starts it again as it leaves (host_main finish_and_exit: an exec of this
// executable with the arguments of the start `kind` asks for; on Android the activity's new process, start_again).
bool restart_game(Restart kind) {
    restart_request.store(static_cast<int>(kind), std::memory_order_release);
    quit_request.store(true, std::memory_order_release);
    return true;
}

int restart_wanted() noexcept {
    return restart_request.load(std::memory_order_acquire);
}

bool can_restart() noexcept {
    return true;
}

#if defined(__ANDROID__)
void start_again(Restart kind) {
    galaxy::sdl::call_activity(galaxy::sdl::ActivityCall{"restartGame", true, static_cast<int>(kind)});
}
#endif
#endif

#if defined(NEBULA_GPU_METAL)
// The Metal renderer on this host (a development build: Apple's own host is platform/apple/host): SDL's Metal view.
void* metal_layer(NativeWindow window) {
    static SDL_MetalView view = nullptr;
    if (view == nullptr) {
        view = SDL_Metal_CreateView(static_cast<SDL_Window*>(window));
    }
    return view != nullptr ? SDL_Metal_GetLayer(view) : nullptr;
}
#else
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
    // A new surface has no vote: it gets the setting's (sync_display_rate's rule).
    if (auto* native = static_cast<ANativeWindow*>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(window)), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,
            nullptr))) {
        galaxy::sdl::vote_frame_rate(native, galaxy::render::get_render_config().max_fps);
    }
#endif
    return surface;
}
#endif

}  // namespace galaxy::render::host_window

// The window functions the host and the backend call (render_api.h).

namespace galaxy::render {

HostClientExtent current_host_client_extent() noexcept {
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    return {state.window_width, state.window_height};
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

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const std::lock_guard<std::mutex> lock(galaxy::sdl::g_menu_keys_mutex);
    galaxy::sdl::g_menu_keys.push_back(virtual_key);
}

void native_settings_post_keyboard_key(std::uintptr_t virtual_key) noexcept {
    const std::lock_guard<std::mutex> lock(galaxy::sdl::g_menu_keys_mutex);
    galaxy::sdl::g_menu_keys.push_back(virtual_key | kMenuKeyKeyboard);
}

}  // namespace galaxy::render

namespace galaxy::host {

// What the settings menu's CONTROLS page asks of the host.
std::size_t connected_controllers(std::span<input::ControllerInfo> out) noexcept {
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    std::size_t count = 0u;
    for (std::size_t i = 0; i < galaxy::sdl::kMaxPads && count < out.size(); ++i) {
        if (state.pads[i].connected) {
            input::ControllerInfo info;
            info.scheme = state.pads[i].scheme;
            info.slot = static_cast<std::uint8_t>(i);
            info.icons = state.pad_icons[i];
            // The system's own name for it, as Windows shows the device's (P-106); a translated word without one.
            info.model = state.pad_names[i] != nullptr
                             ? state.pad_names[i]
                             : galaxy::host_text::translated("Controller", galaxy::host_text::language()).data();
            info.gyro = state.motion[i].has_gyro;
            info.sensors = state.motion[i].has_gyro || state.motion[i].has_accel;
            info.touch = state.motion[i].has_touch;
            out[count++] = info;
        }
    }
    return count;
}

std::optional<input::DeviceKey> last_input_device() noexcept {
    const std::uint32_t last = galaxy::sdl::g_last_input.load(std::memory_order_relaxed);
    if (last == galaxy::sdl::kLastKeyboard) {
        return input::DeviceKey{input::ControlScheme::Keyboard, input::kKeyboardSlot};
    }
    if (last >= galaxy::sdl::kLastPad) {
        return input::DeviceKey{static_cast<input::ControlScheme>(last - galaxy::sdl::kLastPad), 0u};
    }
    return std::nullopt;
}

void arm_controller_binding_capture(input::ControlScheme scheme, std::uint32_t slot) noexcept {
    const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    galaxy::sdl::g_capture_result.reset();
    galaxy::sdl::g_capture_device = input::DeviceKey{scheme, slot};
}

void cancel_controller_binding_capture() noexcept {
    const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    galaxy::sdl::g_capture_device.reset();
    galaxy::sdl::g_capture_result.reset();
}

std::optional<input::InputSource> take_controller_binding_capture() noexcept {
    const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
    std::optional<input::InputSource> result = galaxy::sdl::g_capture_result;
    if (result.has_value()) {
        galaxy::sdl::g_capture_device.reset();
        galaxy::sdl::g_capture_result.reset();
    }
    return result;
}

// The style Auto shows now (button_icons sets it, the settings menu reads it).
std::atomic<input::ButtonIconStyle> g_auto_icon_style{input::ButtonIconStyle::Game};

input::ButtonIconStyle button_icons_auto_style() noexcept {
    return g_auto_icon_style.load(std::memory_order_relaxed);
}

void note_keyboard_input() noexcept {
    galaxy::sdl::g_last_input.store(galaxy::sdl::kLastKeyboard, std::memory_order_relaxed);
}

namespace {
std::atomic<bool> g_input_missing_shown{false};
}  // namespace

void set_input_missing_shown(bool shown) noexcept {
    g_input_missing_shown.store(shown, std::memory_order_relaxed);
}

bool input_missing_shown() noexcept {
    return g_input_missing_shown.load(std::memory_order_relaxed);
}

void real_wiimote_taken(bool taken) noexcept {
#if defined(__ANDROID__)
    galaxy::sdl::call_activity(taken ? "realWiimoteTaken" : "realWiimoteLetGo");
#else
    (void)taken;
#endif
}

#if defined(__ANDROID__)
// As GameActivity.reportInputDevices sees them: real external devices. SDL's count takes a phone's own buttons for a
// keyboard.
namespace {
std::atomic<bool> g_external_keyboard{false};
std::atomic<bool> g_external_mouse{false};
std::atomic<unsigned> g_touch_size{0};
std::atomic<unsigned> g_touch_opacity{60};
std::atomic<bool> g_touch_vibration{true};
std::atomic<bool> g_touch_arranging{false};  // the on-screen controls' editor is open
}  // namespace

void arrange_touch_controls() noexcept {
    g_touch_arranging.store(true, std::memory_order_release);
    galaxy::sdl::call_activity("arrangeControls");
}

bool touch_arranging() noexcept {
    return g_touch_arranging.load(std::memory_order_acquire);
}

// The editor answers nativeArrangingDone when it has kept or put back the places.
void finish_arranging_touch_controls(bool save) noexcept {
    galaxy::sdl::call_activity(save ? "saveArrangedControls" : "cancelArrangedControls");
}

void reset_arranged_touch_controls() noexcept {
    galaxy::sdl::call_activity("resetArrangedControls");
}

void input_mode_changed() noexcept {
    galaxy::sdl::call_activity("inputModeChanged");
}

bool touch_controls_present() noexcept {
    return true;
}

TouchControlsSettings touch_controls_settings() noexcept {
    return {g_touch_size.load(std::memory_order_relaxed), g_touch_opacity.load(std::memory_order_relaxed),
            g_touch_vibration.load(std::memory_order_relaxed)};
}

void set_touch_controls_settings(const TouchControlsSettings& settings) noexcept {
    g_touch_size.store(settings.size, std::memory_order_relaxed);
    g_touch_opacity.store(settings.opacity, std::memory_order_relaxed);
    g_touch_vibration.store(settings.vibration, std::memory_order_relaxed);
    galaxy::sdl::call_activity("touchSettingsChanged");
}

void reset_touch_controls_places() noexcept {
    galaxy::sdl::call_activity("resetControlPlaces");
}
#else
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

// No real Wii Remote on this host (wiimote_hid.cpp is Windows' and Android's).
WiiRemoteState wii_remote_state() noexcept {
    return {};
}

TouchControlsSettings touch_controls_settings() noexcept {
    return {};
}

void set_touch_controls_settings(const TouchControlsSettings&) noexcept {}

void reset_touch_controls_places() noexcept {}
#endif

unsigned window_width_dp() noexcept {
    int width = 0;
    int height = 0;
    if (galaxy::sdl::g_window == nullptr || !SDL_GetWindowSizeInPixels(galaxy::sdl::g_window, &width, &height)) {
        return 0u;
    }
    const float density = SDL_GetWindowDisplayScale(galaxy::sdl::g_window);
    return static_cast<unsigned>(static_cast<float>(width) / (density > 0.0f ? density : 1.0f));
}

bool keyboard_or_mouse_attached() noexcept {
#if defined(__ANDROID__)
    return g_external_keyboard.load(std::memory_order_relaxed) || g_external_mouse.load(std::memory_order_relaxed);
#else
    return SDL_HasKeyboard() || SDL_HasMouse();
#endif
}

JoyConStatus joycon_status() noexcept {
    return {};
}

void set_joycon_pairing(bool) noexcept {}

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
    ButtonIconStyle style = ButtonIconStyle::Game;
    if (keyboard) {
        style = ButtonIconStyle::Keyboard;
    } else if (pad.has_value()) {
        style = input::button_icon_style(*pad);
    }
    g_auto_icon_style.store(style, std::memory_order_relaxed);
    switch (get_runtime_settings().button_icons) {
    case ButtonIcons::Auto: break;
    case ButtonIcons::Game: style = ButtonIconStyle::Game; break;
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
    static ButtonIconStyle s_style = ButtonIconStyle::Game;
    static ControlScheme s_scheme = ControlScheme::Keyboard;
    static input::ButtonIconModel s_model = input::ButtonIconModel::Standard;
    static bool s_menu = false;
    static input::ButtonIconSpec s_spec;
    const input::ButtonIconModel model =
        pad.has_value() ? static_cast<input::ButtonIconModel>(galaxy::sdl::g_last_pad_icons.load(std::memory_order_relaxed))
                        : input::ButtonIconModel::Standard;
    const std::uint64_t generation = input::controls_config_generation();
    if (generation != s_generation || style != s_style || scheme != s_scheme || model != s_model || menu != s_menu) {
        s_generation = generation;
        s_style = style;
        s_scheme = scheme;
        s_model = model;
        s_menu = menu;
        s_spec = input::button_icon_spec(style, input::get_controls_config().table(scheme), menu, model);
    }
    return s_spec;
}

}  // namespace galaxy::host

#if !defined(NEBULA_MOD_IMPORT)
// A build without the importer (mod_import.cpp, iOS): mods come in through the app there.
namespace galaxy::mods {

bool is_mod_archive(const std::filesystem::path&) {
    return false;
}

ImportResult import_mod(const std::filesystem::path&, const std::filesystem::path&, const std::function<void(const ImportProgress&)>&,
                        const std::atomic<bool>*) {
    ImportResult result;
    result.message = "MODS ARE ADDED FROM THE APP ON THIS SYSTEM";
    return result;
}

}  // namespace galaxy::mods
#endif

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeInputDevices(JNIEnv*, jclass, jboolean keyboard, jboolean mouse) {
    galaxy::host::g_external_keyboard.store(keyboard != JNI_FALSE, std::memory_order_relaxed);
    galaxy::host::g_external_mouse.store(mouse != JNI_FALSE, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchSettings(JNIEnv*, jclass, jint size, jint opacity, jboolean vibration) {
    galaxy::host::g_touch_size.store(static_cast<unsigned>(size), std::memory_order_relaxed);
    galaxy::host::g_touch_opacity.store(static_cast<unsigned>(opacity), std::memory_order_relaxed);
    galaxy::host::g_touch_vibration.store(vibration != JNI_FALSE, std::memory_order_relaxed);
}

// The on-screen controls' editor has closed (saved, cancelled or the phone's Back).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeArrangingDone(JNIEnv*, jclass) {
    galaxy::host::g_touch_arranging.store(false, std::memory_order_release);
}

extern "C" JNIEXPORT jint JNICALL Java_dev_nebula_galaxy_GameActivity_nativeTouchWanted(JNIEnv*, jclass, jint which) {
    const galaxy::host::TouchControlsSettings settings = galaxy::host::touch_controls_settings();
    return which == 0 ? static_cast<jint>(settings.size) : which == 1 ? static_cast<jint>(settings.opacity) : (settings.vibration ? 1 : 0);
}

extern "C" JNIEXPORT jint JNICALL Java_dev_nebula_galaxy_GameActivity_nativeInputMode(JNIEnv*, jclass) {
    return static_cast<jint>(galaxy::get_runtime_settings().input_mode);
}
#endif
