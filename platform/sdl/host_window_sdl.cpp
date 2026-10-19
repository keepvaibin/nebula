// SPDX-License-Identifier: GPL-3.0-only
// The game window and input on SDL3 platforms (sdl_host.h, host_window.h and the window functions of gx_d3d12.h):
// the SDL window and its Vulkan surface; its events (size, focus, quit, the finger and the mouse as the pointer, keys,
// gamepads); on Android the activity's touch controls and HOME Menu through JNI.

#include "sdl_host.h"

#include "galaxy/gx/display_modes.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_host.h"
#include "galaxy/platform_time.h"
#include "galaxy/virtual_keys.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <jni.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace galaxy::sdl {
namespace {

std::mutex g_input_mutex;
InputState g_input;                 // guarded by g_input_mutex
SDL_Window* g_window = nullptr;
std::atomic<int> g_home_choice{-1};
std::atomic<bool> g_home_visible{false};
std::array<SDL_Gamepad*, kMaxPads> g_gamepads{};

// A touch that ends within this time, having moved less than this fraction of the window's shorter side, is a tap.
constexpr std::uint64_t kTapMs = 280;
constexpr float kTapTravel = 0.03f;
SDL_FingerID g_pointer_finger = 0;
bool g_pointer_finger_valid = false;
std::uint64_t g_finger_down_ms = 0;
float g_finger_down_x = 0.0f;
float g_finger_down_y = 0.0f;
float g_finger_travel = 0.0f;

#if defined(__ANDROID__)
// stdout and stderr to logcat, line by line (the host and the renderer log there).
void forward_output_to_logcat() {
    static int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    std::thread([] {
        char line[1024];
        std::size_t used = 0;
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
                    __android_log_write(ANDROID_LOG_INFO, "Nebula", line + start);
                    start = i + 1;
                }
            }
            if (start == 0 && used == sizeof(line) - 1) {
                line[used] = '\0';
                __android_log_write(ANDROID_LOG_INFO, "Nebula", line);
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
    // The runtime's bindings name the generic modifier keys too.
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) g_input.keys.set_key(VK_SHIFT, down);
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) g_input.keys.set_key(VK_CONTROL, down);
    if (vk == VK_LMENU || vk == VK_RMENU) g_input.keys.set_key(VK_MENU, down);
}

// The unified layout's scheme for an SDL gamepad: Nintendo's positional layout for Switch controllers.
galaxy::input::ControlScheme scheme_of(SDL_Gamepad* pad) noexcept {
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return galaxy::input::ControlScheme::SwitchPro;
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
}

void finger_event(const SDL_TouchFingerEvent& finger, bool down, bool up) {
    const float x = finger.x * static_cast<float>(g_input.window_width);
    const float y = finger.y * static_cast<float>(g_input.window_height);
    const std::uint64_t now = galaxy::steady_ms();
    if (down && !g_pointer_finger_valid) {
        g_pointer_finger = finger.fingerID;
        g_pointer_finger_valid = true;
        g_finger_down_ms = now;
        g_finger_down_x = x;
        g_finger_down_y = y;
        g_finger_travel = 0.0f;
    }
    if (!g_pointer_finger_valid || finger.fingerID != g_pointer_finger) {
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
#if defined(__ANDROID__)
    forward_output_to_logcat();
#endif
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR)) {
        std::fprintf(stderr, "[sdl] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void pump_events(int timeout_ms) noexcept {
    SDL_Event event;
    bool any = SDL_WaitEventTimeout(&event, timeout_ms);
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
            finger_event(event.tfinger, true, false);
            break;
        case SDL_EVENT_FINGER_MOTION:
            finger_event(event.tfinger, false, false);
            break;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            finger_event(event.tfinger, false, true);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            g_input.finger_x = event.motion.x;
            g_input.finger_y = event.motion.y;
            g_input.finger_ms = galaxy::steady_ms();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            const unsigned bit = event.button.button == SDL_BUTTON_LEFT ? 0u
                : event.button.button == SDL_BUTTON_RIGHT ? 1u
                : event.button.button == SDL_BUTTON_MIDDLE ? 2u
                : event.button.button == SDL_BUTTON_X1 ? 3u : 4u;
            g_input.keys.mouse = static_cast<std::uint8_t>(
                down ? (g_input.keys.mouse | (1u << bit)) : (g_input.keys.mouse & ~(1u << bit)));
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

InputState input_state() noexcept {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    return g_input;
}

void rumble(std::uint32_t pad_id, float strength) noexcept {
    const auto level = static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
    for (SDL_Gamepad* gamepad : g_gamepads) {
        if (gamepad != nullptr && SDL_GetGamepadID(gamepad) == pad_id) {
            // Renewed with every report (100 a second); 50 ms outlasts a late one.
            (void)SDL_RumbleGamepad(gamepad, level, level, level != 0 ? 50 : 0);
        }
    }
}

void home_menu_show() noexcept {
    g_home_choice.store(-1, std::memory_order_relaxed);
    g_home_visible.store(true, std::memory_order_release);
#if defined(__ANDROID__)
    call_activity("showHomeMenu");
#endif
}

void home_menu_hide() noexcept {
    g_home_visible.store(false, std::memory_order_release);
#if defined(__ANDROID__)
    call_activity("hideHomeMenu");
#endif
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
    galaxy::sdl::g_input.touch_buttons = static_cast<std::uint32_t>(buttons) | back;
    galaxy::sdl::g_input.touch_stick_x = std::clamp(stick_x, -1.0f, 1.0f);
    galaxy::sdl::g_input.touch_stick_y = std::clamp(stick_y, -1.0f, 1.0f);
}

// The HOME Menu's choice (0 return, 1 quit, 2 reset).
extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_GameActivity_nativeHomeChoice(JNIEnv*, jclass, jint choice) {
    galaxy::sdl::g_home_choice.store(static_cast<int>(choice), std::memory_order_release);
}
#endif

// ---------------------------------------------------------------------------------------------------------------------
// The renderer's window (host_window.h)
// ---------------------------------------------------------------------------------------------------------------------

namespace galaxy::gx::host_window {

unsigned backbuffer_width = 0;
unsigned backbuffer_height = 0;
std::atomic<double> measured_present_hz{0.0};
std::string native_settings_content_root;
std::atomic<bool> borderless_active{true};
std::atomic<bool> exclusive_fullscreen_active{false};
std::atomic<unsigned> efb_scale_rejected{0u};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0u};
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

double display_refresh_hz(NativeWindow window) noexcept {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(static_cast<SDL_Window*>(window)));
    return mode != nullptr && mode->refresh_rate > 1.0f ? static_cast<double>(mode->refresh_rate) : 0.0;
}

// The overlay, the HOME Menu and the hint are the platform's own screens on SDL platforms (Android: the activity's).
void draw_native_settings_overlay(UiCanvas&) {}
void draw_home_menu(UiCanvas&) {}
void draw_native_settings_hint(UiCanvas&) {}

std::vector<const char*> vulkan_instance_extensions() {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    return names != nullptr ? std::vector<const char*>(names, names + count) : std::vector<const char*>{};
}

VkSurfaceKHR create_vulkan_surface(VkInstance instance, NativeWindow window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window), instance, nullptr, &surface)) {
        std::fprintf(stderr, "[window] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return VK_NULL_HANDLE;
    }
    return surface;
}

}  // namespace galaxy::gx::host_window

// ---------------------------------------------------------------------------------------------------------------------
// The window functions the host and the backend call (gx_d3d12.h)
// ---------------------------------------------------------------------------------------------------------------------

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

bool native_settings_overlay_visible() noexcept {
    return false;
}

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

void native_settings_post_key(std::uintptr_t) noexcept {}

}  // namespace galaxy::gx
