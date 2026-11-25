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
#include "galaxy/game_step.h"
#include "galaxy/host_wait.h"
#include "galaxy/input/host_input.h"
#include "galaxy/input/key_positions.h"
#include "galaxy/mod_import.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/render_api.h"
#include "galaxy/render/host_window.h"
#include "galaxy/luma_system_platform.h"
#include "galaxy/luma_input_service.h"
#include "galaxy/ui/launcher.h"
#include <deque>
#include "sdl_host.h"

#include "galaxy/render/desktop_display.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/present_clock.h"
#include "galaxy/render/variable_refresh.h"
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
#include "galaxy/runtime_settings.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/virtual_keys.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
// The desktop hosts (Linux, and the SDL build on a Mac): the window follows the settings' display mode and display, the
// settings list the real displays, and the system's pointer is hidden over the game. Android's and iOS's window is the
// system's full screen one.
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define NEBULA_SDL_DESKTOP 1
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
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
// after each pump; leaving one wakes the pump (wake_pump).
std::thread::id g_sdl_thread;
// The pump's wake (P-281): an event of this type, pushed by a thread that leaves a request, ends SDL's event wait at
// once (0: SDL gave no type; the request waits for the pump's bound). On Android, where the pump blocks on
// g_pump_wake rather than in SDL's wait, an event watch wakes it for every event pushed, this one and the system's.
std::atomic<Uint32> g_wake_event{0u};
#if defined(__ANDROID__)
galaxy::WakeWait g_pump_wake;
#endif

void wake_pump() noexcept {
    if (const Uint32 type = g_wake_event.load(std::memory_order_acquire); type != 0u) {
        SDL_Event wake{};
        wake.type = type;
        (void)SDL_PushEvent(&wake);
    }
}

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
        {
            const std::lock_guard<std::mutex> lock(g_activity_calls_mutex);
            g_activity_calls.push_back(call);
        }
        wake_pump();
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
// What Android says of the display (G2.3.5 point 6): its API level and Display.hasArrSupport() (asked only at API 36
// and above); 0 until GameActivity reports them.
std::atomic<int> g_android_sdk{0};
std::atomic<bool> g_display_arr{false};

// The surface's frame-rate vote for the frame-rate setting (max_fps; 0: the display's own; unlimited: the panel's
// fastest). The game's own frames (galaxy::own_frame_rate: up to the step rate) are a fixed source, so a variable-rate
// panel shows each for the same time; interpolated rates are the app's own rate, switched to at once (the player chose
// it) rather than at the next seamless moment.
void vote_frame_rate(ANativeWindow* native, float max_fps) {
    if (galaxy::own_frame_rate(max_fps)) {
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
    // A scancode is a physical place, so virtual_key's letter is also the letter at that place on a US keyboard, which is
    // what the default layout binds movement, Z, C, the shake and the pointer keys to (input_bindings.h KeyPosition: W A S D
    // are where the hand rests, whatever the keyboard prints on them). Without it those keys did nothing on these hosts.
    galaxy::input::apply_physical_key_event(g_input.keys, vk, down);
    if (down) {
        g_last_input.store(kLastKeyboard, std::memory_order_relaxed);
    }
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

void sample_gamepads() {
    namespace pad = galaxy::input::pad;
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
        // Back buttons: SDL's upper pair is the Edge's paddles (the Steam controllers' L4 R4), its lower pair the Edge's
        // Fn buttons (L5 R5).
        if (sample.scheme == galaxy::input::ControlScheme::PlayStation || sample.scheme == galaxy::input::ControlScheme::Steam) {
            const bool steam = sample.scheme == galaxy::input::ControlScheme::Steam;
            if (button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)) bits |= pad::bit(pad::kGripLeftBit);
            if (button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1)) bits |= pad::bit(pad::kGripRightBit);
            if (button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2)) bits |= pad::bit(steam ? pad::kGripLeft2Bit : pad::kFnLeftBit);
            if (button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2)) bits |= pad::bit(steam ? pad::kGripRight2Bit : pad::kFnRightBit);
        }
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

#if defined(NEBULA_SDL_DESKTOP)
namespace desktop = galaxy::render::desktop_display;

// The displays as SDL lists them, in pixels, and the ids the settings keep for them. Read on SDL's thread (its display and
// window calls are its own); handed to the other threads through g_displays.
struct DesktopDisplays {
    std::vector<desktop::Display> list;  // in the settings' order: the primary display first
    std::vector<std::string> ids;        // list's ids (desktop::display_ids)
    long frame_width = 0;                // the window's frame in pixels; 0 where the system draws none or has not said
    long frame_height = 0;
};
std::mutex g_displays_mutex;
DesktopDisplays g_displays;                      // guarded by g_displays_mutex
std::atomic<bool> g_displays_dirty{true};        // a display was added, removed or changed: read them again (SDL's events)
std::atomic<double> g_window_display_hz{0.0};    // the refresh rate of the display the window is on, from SDL's thread
std::optional<desktop::Choice> g_placed;         // what the window was last placed for (SDL's thread)
desktop::Pixels g_fullscreen_pixels{};           // the display's own pixels while the window is, or is about to be, full screen
bool g_cursor_hidden = false;                    // the system's pointer is hidden over the window (SDL's thread)
bool g_logged_window = false;
bool g_logged_full = false;
int g_logged_width = 0;
int g_logged_height = 0;

DesktopDisplays read_displays() {
    DesktopDisplays out;
    int count = 0;
    SDL_DisplayID* const found = SDL_GetDisplays(&count);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; found != nullptr && i < count; ++i) {
        const SDL_DisplayID id = found[i];
        const SDL_DisplayMode* const desktop_mode = SDL_GetDesktopDisplayMode(id);
        if (desktop_mode == nullptr) {
            continue;
        }
        const SDL_DisplayMode* const current = SDL_GetCurrentDisplayMode(id);
        // A mode's size is in the system's units, and its density turns them into pixels (Wayland at 150 %: 1707 x 960 at 1.4997 is
        // the display's 2560 x 1440).
        const float density = desktop_mode->pixel_density > 0.0f ? desktop_mode->pixel_density : 1.0f;
        desktop::Display display;
        display.handle = id;
        if (const char* const name = SDL_GetDisplayName(id)) {
            display.name = name;
        }
        SDL_Rect bounds{};
        SDL_Rect usable{};
        (void)SDL_GetDisplayBounds(id, &bounds);
        if (!SDL_GetDisplayUsableBounds(id, &usable) || usable.w <= 0 || usable.h <= 0) {
            usable = bounds;
        }
        display.x = bounds.x;
        display.y = bounds.y;
        display.width = desktop::pixels_of(desktop_mode->w, density);
        display.height = desktop::pixels_of(desktop_mode->h, density);
        display.refresh_hz = static_cast<double>((current != nullptr ? current : desktop_mode)->refresh_rate);
        display.usable_width = desktop::pixels_of(usable.w, density);
        display.usable_height = desktop::pixels_of(usable.h, density);
        display.primary = id == primary;
        std::vector<desktop::RawMode> raw;
        int mode_count = 0;
        SDL_DisplayMode** const modes = SDL_GetFullscreenDisplayModes(id, &mode_count);
        for (int m = 0; modes != nullptr && m < mode_count; ++m) {
            raw.push_back({modes[m]->w, modes[m]->h, modes[m]->pixel_density, modes[m]->refresh_rate});
        }
        SDL_free(modes);
        display.modes = desktop::modes_of(raw, {display.width, display.height, desktop::whole_hz(display.refresh_hz)});
        out.list.push_back(std::move(display));
    }
    SDL_free(found);
    desktop::order_displays(out.list);
    out.ids = desktop::display_ids(out.list);
    if (g_window != nullptr) {
        int top = 0;
        int left = 0;
        int bottom = 0;
        int right = 0;
        if (SDL_GetWindowBordersSize(g_window, &top, &left, &bottom, &right)) {
            const double density = std::max(1.0f, SDL_GetWindowPixelDensity(g_window));
            out.frame_width = std::lround(static_cast<double>(left + right) * density);
            out.frame_height = std::lround(static_cast<double>(top + bottom) * density);
        }
    }
    return out;
}

// Reads the displays again and hands them over; true when the list is not the one before (a display added, removed or
// changed: the window is placed again).
bool refresh_displays(DesktopDisplays& now) {
    now = read_displays();
    const std::lock_guard<std::mutex> lock(g_displays_mutex);
    const bool changed = g_displays.list != now.list;
    g_displays = now;
    return changed;
}

DesktopDisplays shared_displays() {
    const std::lock_guard<std::mutex> lock(g_displays_mutex);
    return g_displays;
}

// Puts the window where the settings say, on SDL's thread. SDL's rules (SDL_video.c, SDL_waylandwindow.c, SDL_x11window.c):
// a full screen window without a display mode of its own is the desktop's borderless one, and it goes to the display its
// position names (SDL_SetWindowPosition with SDL_WINDOWPOS_CENTERED_DISPLAY, which also tells Wayland the output, where a
// window cannot place itself); a window already full screen on another display leaves full screen first; on a window that
// is not shown yet the calls only note the state, which is applied when it is shown. The calls return before the system has
// finished (X11 and Wayland answer later): SDL_SyncWindow waits for it, and the size events (SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
// SDL_EVENT_WINDOW_ENTER_FULLSCREEN) are what the swap chain follows.
void place_on_display(const desktop::Placement& placement, const DesktopDisplays& displays) {
    SDL_Window* const window = g_window;
    if (window == nullptr || placement.display >= displays.list.size()) {
        return;
    }
    const desktop::Display& display = displays.list[placement.display];
    const SDL_DisplayID target = display.handle;
    const bool full_now = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0u;
    if (placement.fullscreen) {
        if (full_now && SDL_GetDisplayForWindow(window) != target) {
            (void)SDL_SetWindowFullscreen(window, false);
            (void)SDL_SyncWindow(window);
        }
        (void)SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
        (void)SDL_SetWindowFullscreen(window, true);
        g_fullscreen_pixels = {display.width, display.height};
    } else {
        if (full_now) {
            (void)SDL_SetWindowFullscreen(window, false);
            (void)SDL_SyncWindow(window);
        }
        g_fullscreen_pixels = {};
        // The window's size is in the system's units: the configured pixels over the density the display's windows have.
        const SDL_DisplayMode* const mode = SDL_GetDesktopDisplayMode(target);
        const double density = mode != nullptr && mode->pixel_density > 0.0f
                                   ? static_cast<double>(mode->pixel_density)
                                   : static_cast<double>(std::max(1.0f, SDL_GetWindowPixelDensity(window)));
        (void)SDL_SetWindowSize(window, static_cast<int>(desktop::window_points(placement.client_width, density)),
                                static_cast<int>(desktop::window_points(placement.client_height, density)));
        (void)SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
    }
    (void)SDL_SyncWindow(window);
    const std::string& id = displays.ids[placement.display];
    if (placement.fullscreen) {
        std::fprintf(stderr, "[window] asked for borderless full screen on %s (%u x %u pixels)\n", id.c_str(), display.width,
                     display.height);
    } else {
        std::fprintf(stderr, "[window] asked for a window of %u x %u pixels on %s\n", placement.client_width,
                     placement.client_height, id.c_str());
    }
}

// The window follows the settings (a change of display mode, display or window size, or a display added, removed or changed),
// at most every 50 ms, as RendererVk::reconcile_display_state does at a frame boundary on Windows. SDL's thread.
void follow_display_settings() {
    if (g_window == nullptr || galaxy::render::host_window::hidden_window_mode_enabled()) {
        return;
    }
    static std::uint64_t s_last_ms = 0;
    const std::uint64_t now_ms = galaxy::steady_ms();
    if (now_ms - s_last_ms < 50u && !g_displays_dirty.load(std::memory_order_relaxed)) {
        return;
    }
    s_last_ms = now_ms;
    DesktopDisplays displays;
    bool changed = false;
    if (g_displays_dirty.exchange(false, std::memory_order_acq_rel)) {
        changed = refresh_displays(displays);
    } else {
        displays = shared_displays();
    }
    // The rate of the display the window is on, for the frame rate rows and the pacing (read here: SDL's calls are its thread's).
    if (const SDL_DisplayMode* const mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window)); mode != nullptr) {
        g_window_display_hz.store(static_cast<double>(mode->refresh_rate), std::memory_order_relaxed);
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(g_window);
    const bool full = (flags & SDL_WINDOW_FULLSCREEN) != 0u;
    galaxy::render::host_window::borderless_active.store(full, std::memory_order_release);
    int width = 0;
    int height = 0;
    {
        const std::lock_guard<std::mutex> lock(g_input_mutex);
        width = g_input.window_width;
        height = g_input.window_height;
    }
    // The window's state is told when it is shown (before, it only has what it will be) and each time it changes.
    if ((flags & SDL_WINDOW_HIDDEN) == 0u &&
        (!g_logged_window || full != g_logged_full || width != g_logged_width || height != g_logged_height)) {
        g_logged_window = true;
        g_logged_full = full;
        g_logged_width = width;
        g_logged_height = height;
        std::fprintf(stderr, "[window] %s %d x %d pixels\n", full ? "borderless full screen" : "windowed", width, height);
    }
    const desktop::Choice wanted = desktop::choice_of(galaxy::render::get_render_config(), false);
    if (!desktop::must_place(g_placed, wanted, changed)) {
        return;
    }
    const std::optional<desktop::Placement> placement =
        desktop::place_window(wanted, displays.ids, displays.list, displays.frame_width, displays.frame_height);
    g_placed = wanted;
    if (placement) {
        place_on_display(*placement, displays);
    }
}

// The system's pointer is hidden while the game's window has the focus (the game draws its own pointer); SDL hides it over
// the window's picture only, so the frame, another window and the desktop show the system's.
void update_os_cursor() {
    if (g_window == nullptr || galaxy::render::host_window::hidden_window_mode_enabled()) {
        return;
    }
    bool focused = false;
    {
        const std::lock_guard<std::mutex> lock(g_input_mutex);
        focused = g_input.window_focused;
    }
    const bool hidden = desktop::os_cursor_hidden(focused, g_background.load(std::memory_order_acquire));
    if (hidden != g_cursor_hidden) {
        g_cursor_hidden = hidden;
        (void)(hidden ? SDL_HideCursor() : SDL_ShowCursor());
        std::fprintf(stderr, "[window] the system's pointer is %s\n", hidden ? "hidden" : "shown");
    }
}
#endif

void update_window_size() {
    int width = 0;
    int height = 0;
    if (g_window != nullptr && SDL_GetWindowSizeInPixels(g_window, &width, &height) && width > 0 && height > 0) {
#if defined(NEBULA_SDL_DESKTOP)
        if (g_fullscreen_pixels.width != 0u) {
            // Full screen is the display's own pixels. Not yet shown, the window has no full screen size to report; then it
            // is the system's size, which a scale rounds to whole points and so can be a pixel off the display's.
            const desktop::Pixels picture =
                (SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN) != 0u
                    ? g_fullscreen_pixels
                    : desktop::fullscreen_pixels(g_fullscreen_pixels,
                                                 {static_cast<unsigned>(width), static_cast<unsigned>(height)},
                                                 static_cast<double>(SDL_GetWindowPixelDensity(g_window)));
            width = static_cast<int>(picture.width);
            height = static_cast<int>(picture.height);
        }
#endif
        g_input.window_width = width;
        g_input.window_height = height;
    }
}

}  // namespace

bool start() noexcept {
    g_sdl_thread = std::this_thread::get_id();
#if defined(__ANDROID__)
    forward_output_to_logcat();
#else
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
    g_wake_event.store(SDL_RegisterEvents(1), std::memory_order_release);
#if defined(__ANDROID__)
    // Every event pushed (touch, keys and pads from the activity's threads, the wake above) ends the pump's block.
    if (!SDL_AddEventWatch([](void*, SDL_Event*) -> bool { g_pump_wake.wake(); return true; }, nullptr)) {
        std::fprintf(stderr, "[sdl] no event watch (%s): events wait for the pump's bound\n", SDL_GetError());
    }
#endif
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
#if defined(NEBULA_SDL_DESKTOP)
    follow_display_settings();
    update_os_cursor();
#endif
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
#if defined(__ANDROID__)
    sync_display_rate();
    // SDL's timed event wait polls without sleeping on Android, keeping a core busy: the events are pumped here and
    // the thread blocks between pumps until an event is pushed or a request left (g_pump_wake, P-281), `timeout_ms` at
    // most (what only a pump reads: sensors, pads SDL polls). A wake from before this look is dropped: the look sees
    // what it was for; one after it ends the block at once.
    g_pump_wake.clear();
    SDL_PumpEvents();
    bool any = SDL_PollEvent(&event);
    if (!any && timeout_ms > 0) {
        (void)g_pump_wake.wait_for(std::chrono::milliseconds(timeout_ms));
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
#endif
#if defined(NEBULA_SDL_DESKTOP)
        // A display added, removed or changed (its mode, place, scale or usable area), or the window shown (its frame is known
        // then): the displays are read again and the window is placed again if they differ.
        case SDL_EVENT_DISPLAY_ORIENTATION:
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_MOVED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED:
        case SDL_EVENT_WINDOW_SHOWN:
            g_displays_dirty.store(true, std::memory_order_release);
            // The window's way to the display changed: the renderer asks again whether the display is variable
            // (G2.3.5 point 5 (c), (d)).
            galaxy::render::g_display_recheck.store(true, std::memory_order_release);
            break;
        // Full screen entered or left, or the window on another display (point 5 (a), (c)).
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            galaxy::render::g_display_recheck.store(true, std::memory_order_release);
            break;
#endif
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            g_input.mouse_inside = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            g_input.mouse_inside = false;
            break;
        // Focus gained or lost (point 5 (b)).
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_input.window_focused = true;
            galaxy::render::g_display_recheck.store(true, std::memory_order_release);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            galaxy::render::g_display_recheck.store(true, std::memory_order_release);
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
    const std::uint32_t level = static_cast<std::uint32_t>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
    const bool changed = g_rumble_level.exchange(level, std::memory_order_relaxed) != level;
    if (g_rumble_pad.exchange(pad_id, std::memory_order_relaxed) != pad_id || changed) {
        wake_pump();  // a new rumble is sent at once (run_requests), not at the pump's bound
    }
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
// it (host_window.h). Two seconds at most, so a stalled render thread cannot hold the UI thread: past them the surface
// goes with the renderer still holding it, which then presents nothing into it and makes its device again
// (surface_overdue, P-281).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeSurfaceWithdrawn(JNIEnv*, jclass) {
    galaxy::render::host_window::surface_withdrawn.store(true, std::memory_order_release);
    if (galaxy::render::host_window::surface_use.try_lock_for(std::chrono::seconds(2))) {
        galaxy::render::host_window::surface_use.unlock();
    } else {
        galaxy::render::host_window::surface_overdue.store(true, std::memory_order_release);
        std::fprintf(stderr, "[window] the renderer kept the surface past 2 s: it goes, and the renderer's device is made again\n");
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

// The game's step rate (galaxy::kGameStepRate), the one number the activity's fall-back setting and display mode read.
extern "C" JNIEXPORT jfloat JNICALL Java_dev_nebula_galaxy_GameActivity_nativeStepRate(JNIEnv*, jclass) {
    return static_cast<jfloat>(galaxy::kGameStepRate);
}

// The API level and the display's adaptive refresh (Display.hasArrSupport, API 36; false below it), with the panel's
// rate. A change asks the variable-refresh question again (G2.3.5 point 6).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeDisplayVaries(JNIEnv*, jclass, jint sdk,
                                                                                         jboolean has_arr) {
    const bool arr = has_arr == JNI_TRUE;
    const int was_sdk = galaxy::sdl::g_android_sdk.exchange(static_cast<int>(sdk), std::memory_order_relaxed);
    const bool was_arr = galaxy::sdl::g_display_arr.exchange(arr, std::memory_order_relaxed);
    if (was_sdk != static_cast<int>(sdk) || was_arr != arr) {
        galaxy::render::g_display_recheck.store(true, std::memory_order_release);
    }
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
// Whether the window is borderless full screen: Android's and iOS's is from the start, a desktop window says so as it is
// (follow_display_settings).
#if defined(NEBULA_SDL_DESKTOP)
std::atomic<bool> borderless_active{false};
#else
std::atomic<bool> borderless_active{true};
#endif
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0u};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;
std::atomic<bool> surface_overdue{false};

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    const bool hidden = hidden_window_mode_enabled();
#if defined(NEBULA_SDL_DESKTOP)
    // The displays, the mode and the display the settings name: the window is made at the configured size on that display,
    // and made borderless full screen on it before it is shown (a start with the default setting is full screen, as on
    // Windows). A hidden test window is made as before.
    galaxy::sdl::DesktopDisplays displays;
    galaxy::sdl::desktop::Choice wanted;
    std::optional<galaxy::sdl::desktop::Placement> placement;
    if (!hidden) {
        (void)galaxy::sdl::refresh_displays(displays);
        galaxy::sdl::g_displays_dirty.store(false, std::memory_order_release);
        // Which session SDL is in (x11 or wayland), for a report.
        const char* const driver = SDL_GetCurrentVideoDriver();
        std::fprintf(stderr, "[display] SDL video driver %s, %zu display%s\n", driver != nullptr ? driver : "unknown", displays.list.size(),
                     displays.list.size() == 1u ? "" : "s");
        for (std::size_t i = 0; i < displays.list.size(); ++i) {
            const galaxy::sdl::desktop::Display& display = displays.list[i];
            std::fprintf(stderr, "[display] %s: %u x %u pixels, %u Hz%s\n", displays.ids[i].c_str(), display.width, display.height,
                         galaxy::sdl::desktop::whole_hz(display.refresh_hz), display.primary ? ", primary" : "");
        }
        wanted = galaxy::sdl::desktop::choice_of(get_render_config(), false);
        // The window as a window first: its size when it leaves full screen is the configured one.
        galaxy::sdl::desktop::Choice as_window = wanted;
        as_window.mode = kDisplayWindowed;
        if (const auto windowed = galaxy::sdl::desktop::place_window(as_window, displays.ids, displays.list, 0, 0)) {
            const SDL_DisplayMode* const mode = SDL_GetDesktopDisplayMode(displays.list[windowed->display].handle);
            const double density = mode != nullptr && mode->pixel_density > 0.0f ? static_cast<double>(mode->pixel_density) : 1.0;
            window_width = static_cast<int>(galaxy::sdl::desktop::window_points(windowed->client_width, density));
            window_height = static_cast<int>(galaxy::sdl::desktop::window_points(windowed->client_height, density));
        }
        placement = galaxy::sdl::desktop::place_window(wanted, displays.ids, displays.list, 0, 0);
    }
#endif
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
#if defined(NEBULA_SDL_DESKTOP)
    const bool sized_by_settings = !hidden && placement.has_value();
#else
    constexpr bool sized_by_settings = false;
#endif
    SDL_Window* window = SDL_CreateWindow("Nebula", sized_by_settings ? std::max(window_width, 1) : std::max(window_width, 320),
                                          sized_by_settings ? std::max(window_height, 1) : std::max(window_height, 240), flags);
    if (window == nullptr) {
        std::fprintf(stderr, "[window] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return {};
    }
    {
        std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_window = window;
    }
#if defined(NEBULA_SDL_DESKTOP)
    if (placement) {
        galaxy::sdl::place_on_display(*placement, displays);
        galaxy::sdl::g_placed = wanted;
    }
#endif
    {
        std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
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
#if defined(NEBULA_SDL_DESKTOP)
    if (galaxy::sdl::g_cursor_hidden) {
        (void)SDL_ShowCursor();
        galaxy::sdl::g_cursor_hidden = false;
    }
    galaxy::sdl::g_placed.reset();
    galaxy::sdl::g_fullscreen_pixels = {};
#endif
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
#if defined(NEBULA_SDL_DESKTOP)
    // The rate of the display the window is on, read on SDL's thread (its display calls are not for the render thread).
    if (const double hz = galaxy::sdl::g_window_display_hz.load(std::memory_order_relaxed); hz > 1.0) {
        return hz;
    }
#endif
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(static_cast<SDL_Window*>(window)));
    return mode != nullptr && mode->refresh_rate > 1.0f ? static_cast<double>(mode->refresh_rate) : 0.0;
}

double display_mode_hz(NativeWindow window) noexcept {
    return display_refresh_hz(window);
}

// Android: hasArrSupport as GameActivity reported it (fixed until it has); Apple: the screen's refresh-interval range
// (display_clock_apple.mm); Linux: no call says it (the presents prove it, G2.3.5 point 7).
galaxy::render::RefreshKind system_refresh_kind(NativeWindow, double* shortest_s) noexcept {
    if (shortest_s != nullptr) {
        *shortest_s = 0.0;
    }
#if defined(__ANDROID__)
    return galaxy::render::android_refresh_kind(galaxy::sdl::g_android_sdk.load(std::memory_order_relaxed),
                                                galaxy::sdl::g_display_arr.load(std::memory_order_relaxed));
#elif defined(__APPLE__)
    return galaxy::sdl::apple_refresh_kind(shortest_s);
#else
    return galaxy::render::RefreshKind::kUnknown;
#endif
}

void set_state_wake(void*) noexcept {}

bool vblank_grid_known(std::int64_t& vblank, std::int64_t& period) noexcept {
    return vblank_grid(nullptr, vblank, period);
}

// Linux's display clock is fitted from the presents, on the mode of the display the window is on (read on SDL's
// thread); Android's Choreographer gives the vsyncs and takes nothing.
void note_present_shown(std::int64_t stamp, bool exact) noexcept {
#if defined(NEBULA_SDL_DESKTOP) && defined(__linux__)
    const double hz = galaxy::sdl::g_window_display_hz.load(std::memory_order_relaxed);
    galaxy::sdl::note_present_shown(stamp, hz > 1.0 ? std::llround(1e9 / hz) : 0, exact);
#else
    (void)stamp;
    (void)exact;
#endif
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
#if defined(NEBULA_SDL_DESKTOP)
    // The real displays, as the Windows host lists them: each one's id (what config.ini keeps), name, pixels of its desktop mode,
    // refresh rate, the largest window it takes and the resolutions it offers.
    if (const galaxy::sdl::DesktopDisplays displays = galaxy::sdl::shared_displays(); !displays.list.empty()) {
        std::vector<SettingsMonitor> monitors;
        for (std::size_t i = 0; i < displays.list.size(); ++i) {
            const galaxy::sdl::desktop::Display& display = displays.list[i];
            SettingsMonitor monitor;
            monitor.id = displays.ids[i];
            monitor.name = display.name;
            monitor.width = display.width;
            monitor.height = display.height;
            monitor.refresh_hz = galaxy::sdl::desktop::whole_hz(display.refresh_hz);
            const auto largest = galaxy::sdl::desktop::max_client_size(display.usable_width, display.usable_height,
                                                                       displays.frame_width, displays.frame_height);
            monitor.max_client_width = largest.first;
            monitor.max_client_height = largest.second;
            monitor.primary = display.primary;
            monitor.resolutions = distinct_resolutions(display.modes);
            monitors.push_back(std::move(monitor));
        }
        return monitors;
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
std::function<void(std::vector<std::filesystem::path>)> g_pick_done;
}  // namespace

void pick_files(FilePick kind, std::function<void(std::vector<std::filesystem::path>)> done) {
    if (kind == FilePick::ModFolders) {
        done({});
        return;
    }
    bool open = false;
    {
        const std::lock_guard<std::mutex> lock(g_pick_mutex);
        open = static_cast<bool>(g_pick_done);
        if (!open) g_pick_done = std::move(done);
    }
    if (open) {
        done({});
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
    std::function<void(std::vector<std::filesystem::path>)> done;
    {
        const std::lock_guard<std::mutex> lock(g_pick_mutex);
        done = std::move(g_pick_done);
        g_pick_done = nullptr;
    }
    if (done) done(std::move(files));
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
    std::function<void(std::vector<std::filesystem::path>)> done;
};

void SDLCALL picked(void* userdata, const char* const* files, int) {
    std::unique_ptr<PickRequest> request(static_cast<PickRequest*>(userdata));
    std::vector<std::filesystem::path> chosen;
    for (const char* const* file = files; file != nullptr && *file != nullptr; ++file) {
        chosen.emplace_back(*file);
    }
    request->done(std::move(chosen));
}
}  // namespace

void pick_files(FilePick kind, std::function<void(std::vector<std::filesystem::path>)> done) {
    auto* request = new PickRequest{std::move(done)};
    if (kind == FilePick::ModFolders) {
        SDL_ShowOpenFolderDialog(picked, request, galaxy::sdl::g_window, nullptr, true);
        return;
    }
    // As Windows' picker lists them (host_window_win32.cpp).
    static constexpr SDL_DialogFileFilter kMiis[] = {
        {"Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)",
         "mii;rcd;rsd;mae;miigx;ncd;nsd;cfcd;cfsd;ffcd;ffsd;charinfo;nfsd;nfcd;mnms;emnms;miic;dat;bin;txt"},
        {"All files", "*"}};
    static constexpr SDL_DialogFileFilter kMods[] = {
        {"Mods and texture packs (luma, zip, 7z, rar, tar)", "luma;zip;7z;rar;tar;tgz;gz;txz;xz;tbz2;bz2;tzst;zst"},
        {"All files", "*"}};
    static constexpr SDL_DialogFileFilter kSaves[] = {
        {"Saves (zip, Nebula.sav, GameData.bin, Wii SD data.bin)", "zip;sav;bin"},
        {"All files", "*"}};
    const SDL_DialogFileFilter* filters = kind == FilePick::Miis ? kMiis : kind == FilePick::Saves ? kSaves : kMods;
    SDL_ShowOpenFileDialog(picked, request, galaxy::sdl::g_window, filters, 2, nullptr, kind != FilePick::Saves);
}

// SDL's save dialog; the file is copied where the player chose (a ".zip" added when the name has no extension).
namespace {
struct SaveRequest {
    std::filesystem::path file;
    std::function<void(SaveCopy)> done;
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
    static constexpr SDL_DialogFileFilter kZip[] = {{"Zip archive", "zip"}};
    auto* request = new SaveRequest{file, std::move(done)};
    const std::string name = galaxy::path_text(file.filename());
    SDL_ShowSaveFileDialog(save_chosen, request, galaxy::sdl::g_window, kZip, 1, name.c_str());
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
void metal_link_layer(void* layer) noexcept {
    galaxy::sdl::metal_link_layer(layer);
}
bool metal_link_drawable(void*& drawable, double& shown_at, std::int64_t wait_ns) noexcept {
    return galaxy::sdl::metal_link_drawable(drawable, shown_at, wait_ns);
}
void set_display_fills(bool fills) noexcept {
    galaxy::sdl::set_display_fills(fills);
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
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_menu_keys_mutex);
        galaxy::sdl::g_menu_keys.push_back(virtual_key);
    }
    galaxy::sdl::wake_pump();
}

void native_settings_post_keyboard_key(std::uintptr_t virtual_key) noexcept {
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_menu_keys_mutex);
        galaxy::sdl::g_menu_keys.push_back(virtual_key | kMenuKeyKeyboard);
    }
    galaxy::sdl::wake_pump();
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
            info.model = state.pads[i].scheme == input::ControlScheme::PlayStation ? "PlayStation controller"
                         : state.pads[i].scheme == input::ControlScheme::SwitchPro  ? "Switch controller"
                                                                                    : "Controller";
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
