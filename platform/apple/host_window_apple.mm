// SPDX-License-Identifier: GPL-3.0-only
// The game window and input on macOS, in the system's own terms (docs/APPLE.md, "Host"): an AppKit window whose view
// is a Metal layer, its events (size, focus, quit, keys, the mouse as the pointer), controllers through the Game
// Controller framework with their motion sensors, touch surface and haptics, the system's file panels. It gives the
// host and the renderer what the SDL host gives on other systems (sdl_host.h, host_window.h and the window functions
// of render_api.h); nothing of SDL is here.
//
// AppKit is the main thread's: the host's main loop pumps the events here (pump_events), and what other threads ask
// of the window is done on the main queue.
#import <AppKit/AppKit.h>
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#import <QuartzCore/CAMetalLayer.h>

#include "galaxy/content_viewport.h"
#include "galaxy/controller_hid.h"
#include "galaxy/game_paths.h"
#include "galaxy/input/host_input.h"
#include "galaxy/input_bindings.h"
#include "galaxy/mod_import.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/playstation_controller.h"
#include "galaxy/render/display_modes.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render_api.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/steam_controller.h"
#include "galaxy/switch_controller.h"
#include "galaxy/virtual_keys.h"
#include "apple_host.h"
#include "sdl_host.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

@class NebulaView;

namespace galaxy::sdl {

// The device that gave input last (button_icons' Auto): none, the keyboard and mouse, or a controller: kLastPad plus
// its scheme.
constexpr std::uint32_t kLastNone = 0u;
constexpr std::uint32_t kLastKeyboard = 1u;
constexpr std::uint32_t kLastPad = 2u;
std::atomic<std::uint32_t> g_last_input{kLastNone};
// The icon model (input::ButtonIconModel) of the pad that gave input last, and that pad's id.
std::atomic<std::uint8_t> g_last_pad_icons{0};
std::atomic<std::uint32_t> g_last_pad{0};

namespace {

std::mutex g_input_mutex;
InputState g_input;  // guarded by g_input_mutex
NSWindow* g_window = nil;       // the main thread's
NebulaView* g_view = nil;
id g_delegate = nil;
std::vector<std::filesystem::path> g_opened_early;  // files opened before the window (the main thread's)
std::atomic<void*> g_layer{nullptr};  // the view's CAMetalLayer, for the renderer's thread
std::atomic<bool> g_background{false};
std::atomic<bool> g_menu_context{false};
std::atomic<bool> g_title_screen{false};
std::atomic<bool> g_direct_touch{false};
std::thread::id g_main_thread;
// A system panel (the file picker, an alert) is up: the pointer is the system's meanwhile.
std::atomic<int> g_panels{0};
bool g_cursor_hidden = false;
// The display mode the window was last put in (RenderConfig::display_mode; ~0: none yet).
unsigned g_applied_mode = ~0u;
// The system refused the last change to full screen (the main thread's).
bool g_full_screen_owed = false;

// Keys the settings menu takes from other threads and from the pads, in order, on the main thread.
std::mutex g_menu_keys_mutex;
std::deque<std::uintptr_t> g_menu_keys;
galaxy::input::NativeSettingsPadState g_menu_pad{};
std::optional<galaxy::input::DeviceKey> g_capture_device;
std::optional<galaxy::input::InputSource> g_capture_result;
std::uint32_t g_capture_previous = 0u;

// A controller in a slot: the system's object, the id the game knows it by, and its haptics.
struct Pad {
    GCController* controller = nil;
    std::uint32_t number = 0u;  // what the game knows it by
    galaxy::input::ControlScheme scheme = galaxy::input::ControlScheme::Xbox;
    galaxy::input::ButtonIconModel icons = galaxy::input::ButtonIconModel::Standard;
    bool nintendo = false;  // its face buttons are named by their labels: A is the right one
    bool has_gyro = false;
    bool has_accel = false;
    bool has_touch = false;
    std::uint64_t last_feed_us = 0u;
    std::uint64_t reported_us = 0u;
    std::uint64_t samples = 0u;
    bool touch_down = false;
    float touch_x = 0.0f, touch_y = 0.0f;
    CHHapticEngine* engine API_AVAILABLE(macos(11.0)) = nil;
    id<CHHapticPatternPlayer> player API_AVAILABLE(macos(11.0)) = nil;
    bool rumbling = false;
};
std::array<Pad, kMaxPads> g_pads{};
std::uint32_t g_next_pad_id = 1u;
std::array<galaxy::input::switch_hid::SwitchMotion, kMaxPads> g_motion{};
std::atomic<std::uint32_t> g_rumble_pad{0};
std::atomic<std::uint32_t> g_rumble_level{0};
std::uint32_t g_rumble_sent_pad = 0;
std::uint32_t g_rumble_sent_level = 0;

// A Mac keyboard's key codes (the hardware's, whatever the layout: NSEvent.keyCode) to the runtime's key numbering
// (virtual_keys.h): letters, digits, function keys and the keys the default keyboard layout binds.
int virtual_key(unsigned short code) noexcept {
    static constexpr struct {
        unsigned short code;
        int key;
    } kKeys[] = {
        {0x00, 'A'}, {0x0B, 'B'}, {0x08, 'C'}, {0x02, 'D'}, {0x0E, 'E'}, {0x03, 'F'}, {0x05, 'G'}, {0x04, 'H'}, {0x22, 'I'},
        {0x26, 'J'}, {0x28, 'K'}, {0x25, 'L'}, {0x2E, 'M'}, {0x2D, 'N'}, {0x1F, 'O'}, {0x23, 'P'}, {0x0C, 'Q'}, {0x0F, 'R'},
        {0x01, 'S'}, {0x11, 'T'}, {0x20, 'U'}, {0x09, 'V'}, {0x0D, 'W'}, {0x07, 'X'}, {0x10, 'Y'}, {0x06, 'Z'},
        {0x12, '1'}, {0x13, '2'}, {0x14, '3'}, {0x15, '4'}, {0x17, '5'}, {0x16, '6'}, {0x1A, '7'}, {0x1C, '8'}, {0x19, '9'},
        {0x1D, '0'},
        {0x7A, VK_F1}, {0x78, VK_F1 + 1}, {0x63, VK_F1 + 2}, {0x76, VK_F1 + 3}, {0x60, VK_F1 + 4}, {0x61, VK_F1 + 5},
        {0x62, VK_F1 + 6}, {0x64, VK_F1 + 7}, {0x65, VK_F1 + 8}, {0x6D, VK_F1 + 9}, {0x67, VK_F1 + 10}, {0x6F, VK_F1 + 11},
        {0x52, VK_NUMPAD0}, {0x53, VK_NUMPAD1}, {0x54, VK_NUMPAD1 + 1}, {0x55, VK_NUMPAD1 + 2}, {0x56, VK_NUMPAD1 + 3},
        {0x57, VK_NUMPAD1 + 4}, {0x58, VK_NUMPAD1 + 5}, {0x59, VK_NUMPAD1 + 6}, {0x5B, VK_NUMPAD1 + 7}, {0x5C, VK_NUMPAD1 + 8},
        {0x24, VK_RETURN}, {0x4C, VK_RETURN}, {0x35, VK_ESCAPE}, {0x33, VK_BACK}, {0x30, VK_TAB}, {0x31, VK_SPACE},
        {0x1B, VK_OEM_MINUS}, {0x18, VK_OEM_PLUS}, {0x21, VK_OEM_4}, {0x1E, VK_OEM_6}, {0x2A, VK_OEM_5}, {0x29, VK_OEM_1},
        {0x27, VK_OEM_7}, {0x32, VK_OEM_3}, {0x2B, VK_OEM_COMMA}, {0x2F, VK_OEM_PERIOD}, {0x2C, VK_OEM_2},
        {0x39, VK_CAPITAL}, {0x72, VK_INSERT}, {0x73, VK_HOME}, {0x74, VK_PRIOR}, {0x75, VK_DELETE}, {0x77, VK_END},
        {0x79, VK_NEXT}, {0x7C, VK_RIGHT}, {0x7B, VK_LEFT}, {0x7D, VK_DOWN}, {0x7E, VK_UP},
        {0x4B, VK_DIVIDE}, {0x43, VK_MULTIPLY}, {0x4E, VK_SUBTRACT}, {0x45, VK_ADD}, {0x41, VK_DECIMAL},
        {0x3B, VK_LCONTROL}, {0x38, VK_LSHIFT}, {0x3A, VK_LMENU}, {0x3E, VK_RCONTROL}, {0x3C, VK_RSHIFT}, {0x3D, VK_RMENU},
    };
    for (const auto& entry : kKeys) {
        if (entry.code == code) {
            return entry.key;
        }
    }
    return 0;
}

// Called with g_input_mutex held. `position`: the letter at the key's place on a US keyboard (0: not a letter key), which
// is what the default layout binds movement to (input_bindings.h KeyPosition: W A S D are where the hand rests, whatever
// the keyboard's layout prints on them).
void set_key(int vk, int position, bool down) {
    if (position >= 'A' && position <= 'Z') {
        g_input.keys.set_position(static_cast<std::uint8_t>(position), down);
    }
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

// The window's picture in pixels, and its place for the renderer (host_window.h).
void update_window_size() {
    if (g_view == nil) {
        return;
    }
    const NSSize pixels = [(NSView*)g_view convertSizeToBacking:((NSView*)g_view).bounds.size];
    const int width = static_cast<int>(std::lround(pixels.width));
    const int height = static_cast<int>(std::lround(pixels.height));
    if (width <= 0 || height <= 0) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input.window_width = width;
        g_input.window_height = height;
    }
    galaxy::render::host_window::backbuffer_width = static_cast<unsigned>(width);
    galaxy::render::host_window::backbuffer_height = static_cast<unsigned>(height);
}

// The pointer's place from an event: the view's pixels, the origin at its top left.
void mouse_at(NSEvent* event) {
    if (g_view == nil || event.window != g_window) {
        return;
    }
    NSView* const view = (NSView*)g_view;
    const NSPoint place = [view convertPointToBacking:[view convertPoint:event.locationInWindow fromView:nil]];
    const NSSize pixels = [view convertSizeToBacking:view.bounds.size];
    const bool inside = NSPointInRect([view convertPoint:event.locationInWindow fromView:nil], view.bounds);
    const std::lock_guard<std::mutex> lock(g_input_mutex);
    g_input.finger_x = static_cast<float>(place.x);
    g_input.finger_y = static_cast<float>(pixels.height - place.y);
    g_input.finger_ms = galaxy::steady_ms();
    g_input.mouse_pointer = true;
    g_input.mouse_inside = inside;
}

// The system's pointer is hidden while it is over the game's picture and the game has the keyboard: the game draws
// its own (the star pointer, the menus' hand), as on Windows.
void update_cursor() {
    bool inside = false;
    bool focused = false;
    {
        const std::lock_guard<std::mutex> lock(g_input_mutex);
        inside = g_input.mouse_inside;
        focused = g_input.window_focused;
    }
    const bool hide = inside && focused && NSApp.active && g_panels.load(std::memory_order_relaxed) == 0 &&
                      g_window != nil && g_window.keyWindow;
    if (hide != g_cursor_hidden) {
        g_cursor_hidden = hide;
        if (hide) {
            [NSCursor hide];
        } else {
            [NSCursor unhide];
        }
    }
}

// ---- controllers ----

galaxy::input::ControlScheme scheme_of(GCController* controller, bool& nintendo) noexcept {
    nintendo = false;
    GCExtendedGamepad* const pad = controller.extendedGamepad;
    NSString* const category = controller.productCategory ?: @"";
    if (@available(macOS 11.3, *)) {
        if ([pad isKindOfClass:GCDualSenseGamepad.class]) {
            return galaxy::input::ControlScheme::PlayStation;
        }
    }
    if (@available(macOS 11.0, *)) {
        if ([pad isKindOfClass:GCDualShockGamepad.class]) {
            return galaxy::input::ControlScheme::PlayStation;
        }
    }
    if ([category containsString:@"DualSense"] || [category containsString:@"DualShock"]) {
        return galaxy::input::ControlScheme::PlayStation;
    }
    if ([category containsString:@"Switch"] || [category containsString:@"Joy-Con"] || [category containsString:@"Nintendo"]) {
        nintendo = true;
        return galaxy::input::ControlScheme::SwitchPro;
    }
    return galaxy::input::ControlScheme::Xbox;
}

// One motion sample of pad `index` (gyro in radians a second, accelerometer in g) over dt seconds into its integrator
// and g_input.motion, as every host feeds its controllers' reports. Called with g_input_mutex held.
void feed_pad_motion(std::size_t index, const float (&gyro)[3], const float (&accel)[3], float dt, std::uint64_t now_us) {
    namespace sw = galaxy::input::switch_hid;
    sw::ImuSample imu{};
    for (int axis = 0; axis < 3; ++axis) {
        imu.gyro[axis] = gyro[axis];
        imu.accel[axis] = accel[axis];
    }
    const galaxy::input::SchemeTuning& tuning = galaxy::input::get_controls_config().tune(g_pads[index].scheme);
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
    g_pads[index].last_feed_us = now_us;
}

// A pad's sensors and touch surface, as they stand at this pump. The sensors' axes are turned into the frame the
// motion code takes on every system (x to the right, y up through the face, z towards the player), as SDL turns the
// same framework's. Called with g_input_mutex held.
void sample_pad_motion(std::size_t index) {
    Pad& pad = g_pads[index];
    PadMotion& motion = g_input.motion[index];
    motion.has_gyro = pad.has_gyro || motion.imu_live;
    motion.has_accel = pad.has_accel || motion.imu_live;
    motion.has_touch = pad.has_touch;
    if (@available(macOS 11.0, *)) {
        GCMotion* const sensors = pad.controller.motion;
        if (sensors != nil && pad.has_gyro && (!sensors.sensorsRequireManualActivation || sensors.sensorsActive)) {
            const GCRotationRate rate = sensors.rotationRate;
            const GCAcceleration pull = sensors.acceleration;
            const float gyro[3] = {static_cast<float>(rate.x), static_cast<float>(rate.z), static_cast<float>(-rate.y)};
            const float accel[3] = {static_cast<float>(-pull.x), static_cast<float>(-pull.y), static_cast<float>(-pull.z)};
            // A pad at rest always reads gravity: all zeros is no sample yet.
            if (!pad.has_accel || accel[0] != 0.0f || accel[1] != 0.0f || accel[2] != 0.0f) {
                const std::uint64_t now_us = galaxy::steady_us();
                const float dt = pad.last_feed_us != 0u && now_us > pad.last_feed_us
                                     ? std::min(0.1f, static_cast<float>(now_us - pad.last_feed_us) * 1e-6f)
                                     : 0.0f;
                feed_pad_motion(index, gyro, accel, dt, now_us);
                ++pad.samples;
            }
        }
        GCControllerDirectionPad* surface = nil;
        GCExtendedGamepad* const gamepad = pad.controller.extendedGamepad;
        if (@available(macOS 11.3, *)) {
            if ([gamepad isKindOfClass:GCDualSenseGamepad.class]) {
                surface = ((GCDualSenseGamepad*)gamepad).touchpadPrimary;
            }
        }
        if (surface == nil && [gamepad isKindOfClass:GCDualShockGamepad.class]) {
            surface = ((GCDualShockGamepad*)gamepad).touchpadPrimary;
        }
        if (surface != nil) {
            // The surface from -1 to 1 each way, both zero with no finger on it; the game takes its width and height
            // as 1 each, y downward.
            const float x = (surface.xAxis.value + 1.0f) * 0.5f;
            const float y = (1.0f - surface.yAxis.value) * 0.5f;
            const bool down = surface.xAxis.value != 0.0f || surface.yAxis.value != 0.0f;
            motion.touch_live = true;
            if (down && pad.touch_down) {
                motion.touch_dx = std::clamp(motion.touch_dx + x - pad.touch_x, -2.0f, 2.0f);
                motion.touch_dy = std::clamp(motion.touch_dy + y - pad.touch_y, -2.0f, 2.0f);
            }
            pad.touch_down = down;
            pad.touch_x = x;
            pad.touch_y = y;
        }
    }
    // Every 10 s while a pad has sensors: whether its motion reaches the game.
    const std::uint64_t now_us = galaxy::steady_us();
    if ((pad.has_gyro || pad.has_accel) && (pad.reported_us == 0u || now_us - pad.reported_us >= 10'000'000u)) {
        pad.reported_us = now_us;
        std::fprintf(stderr, "[input-motion] pad %zu: %llu samples, live %d\n", index, static_cast<unsigned long long>(pad.samples),
                     motion.imu_live ? 1 : 0);
    }
}

// Called with g_input_mutex held.
void sample_gamepads() {
    namespace pad_bits = galaxy::input::pad;
    for (std::size_t i = 0; i < kMaxPads; ++i) {
        Pad& pad = g_pads[i];
        galaxy::input::PadSample& sample = g_input.pads[i];
        sample = {};
        g_input.pad_ids[i] = 0;
        g_input.pad_icons[i] = galaxy::input::ButtonIconModel::Standard;
        GCExtendedGamepad* const gamepad = pad.controller != nil ? pad.controller.extendedGamepad : nil;
        if (gamepad == nil) {
            continue;
        }
        g_input.pad_ids[i] = pad.number;
        sample.connected = true;
        sample.scheme = pad.scheme;
        g_input.pad_icons[i] = pad.icons;
        std::uint32_t bits = 0;
        const auto set = [&bits](GCControllerButtonInput* button, unsigned bit) {
            if (button != nil && button.pressed) bits |= pad_bits::bit(bit);
        };
        set(gamepad.dpad.up, pad_bits::kDpadUpBit);
        set(gamepad.dpad.down, pad_bits::kDpadDownBit);
        set(gamepad.dpad.left, pad_bits::kDpadLeftBit);
        set(gamepad.dpad.right, pad_bits::kDpadRightBit);
        set(gamepad.buttonMenu, pad_bits::kStartBit);
        set(gamepad.buttonOptions, pad_bits::kBackBit);
        set(gamepad.leftThumbstickButton, pad_bits::kLeftStickBit);
        set(gamepad.rightThumbstickButton, pad_bits::kRightStickBit);
        set(gamepad.leftShoulder, pad_bits::kLeftShoulderBit);
        set(gamepad.rightShoulder, pad_bits::kRightShoulderBit);
        if (@available(macOS 11.0, *)) {
            set(gamepad.buttonHome, pad_bits::kHomeBit);
        }
        if (@available(macOS 12.0, *)) {
            if ([gamepad isKindOfClass:GCXboxGamepad.class]) {
                set(((GCXboxGamepad*)gamepad).buttonShare, pad_bits::kCaptureBit);
            }
        }
        // The unified layout names the face buttons by their places. The system names a Nintendo controller's by
        // their labels (its A is the right one), every other's by their places (A is the lower one).
        set(pad.nintendo ? gamepad.buttonB : gamepad.buttonA, pad_bits::kSouthBit);
        set(pad.nintendo ? gamepad.buttonA : gamepad.buttonB, pad_bits::kEastBit);
        set(pad.nintendo ? gamepad.buttonY : gamepad.buttonX, pad_bits::kWestBit);
        set(pad.nintendo ? gamepad.buttonX : gamepad.buttonY, pad_bits::kNorthBit);
        sample.buttons = bits;
        // The system's sticks grow upward, as the unified layout's.
        sample.axes[0] = std::clamp(gamepad.leftThumbstick.xAxis.value, -1.0f, 1.0f);
        sample.axes[1] = std::clamp(gamepad.leftThumbstick.yAxis.value, -1.0f, 1.0f);
        sample.axes[2] = std::clamp(gamepad.rightThumbstick.xAxis.value, -1.0f, 1.0f);
        sample.axes[3] = std::clamp(gamepad.rightThumbstick.yAxis.value, -1.0f, 1.0f);
        sample.axes[4] = std::clamp(gamepad.leftTrigger.value, 0.0f, 1.0f);
        sample.axes[5] = std::clamp(gamepad.rightTrigger.value, 0.0f, 1.0f);
        if (bits != 0u || std::any_of(std::begin(sample.axes), std::end(sample.axes), [](float a) { return std::abs(a) > 0.5f; })) {
            g_last_input.store(kLastPad + static_cast<std::uint32_t>(sample.scheme), std::memory_order_relaxed);
            g_last_pad_icons.store(static_cast<std::uint8_t>(g_input.pad_icons[i]), std::memory_order_relaxed);
            g_last_pad.store(pad.number, std::memory_order_relaxed);
        }
        sample_pad_motion(i);
        if (g_capture_device.has_value() && g_capture_device->scheme == sample.scheme && !g_capture_result.has_value()) {
            const std::uint32_t fresh = bits & ~g_capture_previous;
            if (fresh != 0u) {
                g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::Button,
                                                              static_cast<std::uint8_t>(std::countr_zero(fresh))};
            } else {
                for (std::size_t axis = 0; axis < std::size(sample.axes); ++axis) {
                    if (sample.axes[axis] >= 0.6f) {
                        g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::AxisPositive,
                                                                      static_cast<std::uint8_t>(axis)};
                        break;
                    }
                    if (sample.axes[axis] <= -0.6f) {
                        g_capture_result = galaxy::input::InputSource{galaxy::input::SourceKind::AxisNegative,
                                                                      static_cast<std::uint8_t>(axis)};
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

void open_gamepad(GCController* controller) {
    if (controller.extendedGamepad == nil) {
        return;  // a remote or a micro gamepad: not a controller the game plays with
    }
    const std::lock_guard<std::mutex> lock(g_input_mutex);
    for (const Pad& pad : g_pads) {
        if (pad.controller == controller) {
            return;
        }
    }
    for (std::size_t index = 0; index < kMaxPads; ++index) {
        Pad& pad = g_pads[index];
        if (pad.controller != nil) {
            continue;
        }
        pad = Pad{};
        pad.controller = controller;
        pad.number = g_next_pad_id++;
        pad.scheme = scheme_of(controller, pad.nintendo);
        NSString* const category = controller.productCategory ?: @"";
        pad.icons = [category containsString:@"Edge"] && pad.scheme == galaxy::input::ControlScheme::PlayStation
                        ? galaxy::input::ButtonIconModel::DualSenseEdge
                        : galaxy::input::ButtonIconModel::Standard;
        // A new controller's motion starts from rest, never from the last one in this slot.
        g_motion[index] = {};
        g_input.motion[index] = {};
        if (@available(macOS 11.0, *)) {
            GCMotion* const sensors = controller.motion;
            if (sensors != nil) {
                if (sensors.sensorsRequireManualActivation) {
                    sensors.sensorsActive = YES;
                }
                pad.has_gyro = sensors.hasRotationRate;
                pad.has_accel = sensors.hasGravityAndUserAcceleration;
            }
            GCExtendedGamepad* const gamepad = controller.extendedGamepad;
            pad.has_touch = [gamepad isKindOfClass:GCDualShockGamepad.class];
            if (@available(macOS 11.3, *)) {
                pad.has_touch = pad.has_touch || [gamepad isKindOfClass:GCDualSenseGamepad.class];
            }
        }
        std::fprintf(stderr, "[input] controller \"%s\" (%s) scheme %u connected; gyro %d, accelerometer %d, touch surface %d\n",
                     (controller.vendorName ?: @"").UTF8String, category.UTF8String, static_cast<unsigned>(pad.scheme),
                     pad.has_gyro ? 1 : 0, pad.has_accel ? 1 : 0, pad.has_touch ? 1 : 0);
        return;
    }
}

void close_gamepad(GCController* controller) {
    const std::lock_guard<std::mutex> lock(g_input_mutex);
    for (Pad& pad : g_pads) {
        if (pad.controller != controller) {
            continue;
        }
        if (g_last_pad.load(std::memory_order_relaxed) == pad.number && g_last_input.load(std::memory_order_relaxed) >= kLastPad) {
            g_last_input.store(kLastNone, std::memory_order_relaxed);
        }
        if (@available(macOS 11.0, *)) {
            [pad.player stopAtTime:0 error:nil];
            [pad.engine stopWithCompletionHandler:nil];
        }
        pad = Pad{};
    }
}

// The pad's motor at `level` of 65535 (0: still): one continuous haptic event whose intensity follows the game.
void set_rumble(Pad& pad, std::uint32_t level) {
    if (@available(macOS 11.0, *)) {
        if (level == 0u) {
            if (pad.rumbling) {
                [pad.player stopAtTime:0 error:nil];
                pad.rumbling = false;
            }
            return;
        }
        NSError* error = nil;
        if (pad.engine == nil) {
            GCDeviceHaptics* const haptics = pad.controller.haptics;
            if (haptics == nil) {
                return;
            }
            pad.engine = [haptics createEngineWithLocality:GCHapticsLocalityDefault];
            // Rumble only: the engine starts no audio of its own on the controller (a controller with a speaker and
            // a microphone is an audio device too, and audio started on it is what has the system ask for the
            // microphone).
            pad.engine.playsHapticsOnly = YES;
            if (pad.engine == nil || ![pad.engine startAndReturnError:&error]) {
                pad.engine = nil;
                return;
            }
            Pad* const stopped = &pad;
            pad.engine.resetHandler = ^{
                [stopped->engine startAndReturnError:nil];
                stopped->player = nil;
                stopped->rumbling = false;
            };
        }
        if (pad.player == nil) {
            CHHapticEventParameter* const full =
                [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1.0f];
            CHHapticEvent* const event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                                       parameters:@[full]
                                                                     relativeTime:0
                                                                         duration:GCHapticDurationInfinite];
            CHHapticPattern* const pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
            pad.player = pattern != nil ? [pad.engine createPlayerWithPattern:pattern error:&error] : nil;
            if (pad.player == nil) {
                return;
            }
        }
        CHHapticDynamicParameter* const strength =
            [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                            value:static_cast<float>(level) / 65535.0f
                                                     relativeTime:0];
        [pad.player sendParameters:@[strength] atTime:0 error:&error];
        if (!pad.rumbling) {
            pad.rumbling = [pad.player startAtTime:0 error:&error];
        }
    }
}

// Requests from other threads (menu keys, rumble) and the settings the window follows, on the main thread.
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
        (void)galaxy::render::native_settings_pad_key(key);
    }
    const std::uint32_t pad_id = g_rumble_pad.load(std::memory_order_relaxed);
    const std::uint32_t level = g_rumble_level.load(std::memory_order_relaxed);
    if (pad_id != g_rumble_sent_pad || level != g_rumble_sent_level) {
        for (Pad& pad : g_pads) {
            if (pad.controller != nil && (pad.number == pad_id || pad.number == g_rumble_sent_pad)) {
                set_rumble(pad, pad.number == pad_id ? level : 0u);
            }
        }
        g_rumble_sent_pad = pad_id;
        g_rumble_sent_level = level;
    }
    // DISPLAY MODE: windowed, or the window on a space of its own (the system's full screen) for the other two.
    if (g_window != nil) {
        const unsigned mode = galaxy::render::get_render_config().display_mode;
        if (mode != g_applied_mode && g_window.visible) {
            const bool wanted = mode != 0u;
            const bool full = (g_window.styleMask & NSWindowStyleMaskFullScreen) != 0u;
            std::fprintf(stderr, "[window] display mode %u: %s\n", mode,
                         wanted == full ? "as the window is" : wanted ? "to full screen" : "to a window");
            if (wanted != full) {
                [g_window toggleFullScreen:nil];
            }
            g_applied_mode = mode;
        }
    }
    update_cursor();
}

// What `block` does to the window, on the main thread: now when this is it, else queued for its next pump.
void on_main(dispatch_block_t block) {
    if (std::this_thread::get_id() == g_main_thread) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

}  // namespace
// Files dropped on the window or opened with Nebula (the main thread): to the importer once the window is up.
void files_opened(NSArray<NSURL*>* urls) {
    std::vector<std::filesystem::path> files;
    for (NSURL* url in urls) {
        if (url.isFileURL && url.fileSystemRepresentation != nullptr) {
            files.emplace_back(url.fileSystemRepresentation);
        }
    }
    if (files.empty()) {
        return;
    }
    if (g_window == nil) {
        g_opened_early.insert(g_opened_early.end(), files.begin(), files.end());
        return;
    }
    galaxy::render::native_settings_files_dropped(std::move(files));
}

}  // namespace galaxy::sdl

// The window's view: a Metal layer the size of the window in pixels.
@interface NebulaView : NSView
@end

@implementation NebulaView
- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self != nil) {
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        [self addTrackingArea:[[NSTrackingArea alloc]
                                  initWithRect:NSZeroRect
                                       options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways |
                                               NSTrackingInVisibleRect
                                         owner:self
                                      userInfo:nil]];
    }
    return self;
}
- (CALayer*)makeBackingLayer {
    CAMetalLayer* const layer = [CAMetalLayer layer];
    layer.opaque = YES;
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    return layer;
}
- (BOOL)wantsUpdateLayer {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}
- (void)mouseEntered:(NSEvent*)event {
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_input.mouse_inside = true;
    }
    galaxy::sdl::update_cursor();
}
- (void)mouseExited:(NSEvent*)event {
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_input.mouse_inside = false;
    }
    galaxy::sdl::update_cursor();
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    self.layer.contentsScale = self.window != nil ? self.window.backingScaleFactor : 1.0;
}
// Files dropped on the window: mods to the importer, Mii files to the Mii import (native_settings_files_dropped).
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* const urls = [sender.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    galaxy::sdl::files_opened(urls);
    return urls.count != 0u;
}
// Keys are the game's: none is passed on to beep.
- (void)keyDown:(NSEvent*)event {
}
- (void)keyUp:(NSEvent*)event {
}
@end

// The window's and the app's delegate.
@interface NebulaDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@end

@implementation NebulaDelegate
- (void)sizeChanged {
    using namespace galaxy::sdl;
    update_window_size();
    const InputState state = input_state();
    galaxy::render::host_window::pending_resize_extent.store(
        (static_cast<std::uint64_t>(state.window_width) << 32u) | static_cast<std::uint32_t>(state.window_height),
        std::memory_order_release);
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    galaxy::render::host_window::quit_request.store(true, std::memory_order_release);
    return NO;  // the host closes it as the game ends
}
// "Open With" Nebula, or a .luma opened in Finder (Info.plist's document type).
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    galaxy::sdl::files_opened(urls);
}
- (void)windowDidResize:(NSNotification*)notification {
    [self sizeChanged];
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    [self sizeChanged];
}
- (void)windowDidChangeScreen:(NSNotification*)notification {
    [self sizeChanged];
}
- (void)windowWillStartLiveResize:(NSNotification*)notification {
    galaxy::render::host_window::move_size_active.store(true, std::memory_order_release);
}
- (void)windowDidEndLiveResize:(NSNotification*)notification {
    galaxy::render::host_window::move_size_active.store(false, std::memory_order_release);
    [self sizeChanged];
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    galaxy::render::host_window::borderless_active.store(true, std::memory_order_relaxed);
    std::fprintf(stderr, "[window] full screen\n");
    [self sizeChanged];
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
    galaxy::render::host_window::borderless_active.store(false, std::memory_order_relaxed);
    std::fprintf(stderr, "[window] left full screen\n");
    [self sizeChanged];
}
- (void)windowDidFailToEnterFullScreen:(NSWindow*)window {
    // The system takes no window to full screen while the screen is locked or another such change is under way. The
    // mode is still the one asked for: it is put in again when the window next comes to the front.
    std::fprintf(stderr, "[window] the system did not take the window to full screen\n");
    galaxy::sdl::g_full_screen_owed = true;
    [self sizeChanged];
}
// A minimised window: the host stops the game's clocks and sound as for an app out of sight.
- (void)windowDidMiniaturize:(NSNotification*)notification {
    galaxy::sdl::g_background.store(true, std::memory_order_release);
}
- (void)windowDidDeminiaturize:(NSNotification*)notification {
    galaxy::sdl::g_background.store(false, std::memory_order_release);
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_input.window_focused = true;
    }
    if (std::exchange(galaxy::sdl::g_full_screen_owed, false)) {
        galaxy::sdl::g_applied_mode = ~0u;  // the next pump puts the display mode in again
    }
    galaxy::sdl::update_cursor();
}
- (void)windowDidResignKey:(NSNotification*)notification {
    {
        const std::lock_guard<std::mutex> lock(galaxy::sdl::g_input_mutex);
        galaxy::sdl::g_input.window_focused = false;
        galaxy::sdl::g_input.keys = {};
    }
    galaxy::sdl::update_cursor();
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    // Quit from the menu, the Dock or the system. While the first start's setup window is up it is that window's
    // Quit (it stops an import under way first); else the host ends the game and leaves by itself.
    if (NSWindow* const modal = NSApp.modalWindow; modal != nil) {
        [modal performClose:nil];
        return NSTerminateCancel;
    }
    galaxy::render::host_window::quit_request.store(true, std::memory_order_release);
    return NSTerminateCancel;
}
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
    return YES;
}
- (void)controllerConnected:(NSNotification*)notification {
    galaxy::sdl::open_gamepad((GCController*)notification.object);
}
- (void)controllerDisconnected:(NSNotification*)notification {
    galaxy::sdl::close_gamepad((GCController*)notification.object);
}
@end

namespace galaxy::sdl {

namespace {

// The app's menus: its own (About, Hide, Quit) and Window (Minimise, Zoom, Enter Full Screen).
void make_menus() {
    NSMenu* const bar = [NSMenu new];
    NSMenuItem* const app_item = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu* const app_menu = [NSMenu new];
    [app_menu addItemWithTitle:@"About Nebula" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Hide Nebula" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* const others = [app_menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app_menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Quit Nebula" action:@selector(terminate:) keyEquivalent:@"q"];
    app_item.submenu = app_menu;
    NSMenuItem* const window_item = [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""];
    NSMenu* const window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
    [window_menu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [window_menu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    NSMenuItem* const full = [window_menu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
    full.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    window_item.submenu = window_menu;
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = window_menu;
}

// A key's event: the game's, unless Command is held (the menus' shortcuts).
// A key event's key in the runtime's numbering. A letter key is named by the letter the keyboard's layout gives it (an
// AZERTY keyboard's A is the key a US keyboard calls Q), so a binding by name follows what is printed on the key; a
// layout without Latin letters names it by its place. The name a key went down with is the one it comes up with.
int layout_key(NSEvent* event, int position) {
    static std::array<std::uint8_t, 128> s_down{};
    const unsigned short code = event.keyCode;
    if (position < 'A' || position > 'Z' || code >= s_down.size()) {
        return position;
    }
    if (event.type == NSEventTypeKeyUp && s_down[code] != 0u) {
        return std::exchange(s_down[code], std::uint8_t{0});
    }
    int key = position;
    if (@available(macOS 10.15, *)) {
        NSString* const plain = [event charactersByApplyingModifiers:0];
        if (plain.length == 1u) {
            const unichar letter = [plain characterAtIndex:0];
            if (letter >= 'a' && letter <= 'z') {
                key = letter - 'a' + 'A';
            } else if (letter >= 'A' && letter <= 'Z') {
                key = letter;
            }
        }
    }
    if (event.type == NSEventTypeKeyDown) {
        s_down[code] = static_cast<std::uint8_t>(key);
    }
    return key;
}

bool key_event(NSEvent* event, std::vector<std::tuple<int, bool, bool, int>>& menu_keys) {
    if (event.type == NSEventTypeFlagsChanged) {
        // A modifier key went down or came up: which one by its code, which way by its own bit of the flags.
        static constexpr struct {
            unsigned short code;
            NSUInteger bit;
        } kModifiers[] = {{0x38, 0x2}, {0x3C, 0x4}, {0x3B, 0x1}, {0x3E, 0x2000}, {0x3A, 0x20}, {0x3D, 0x40}};
        for (const auto& modifier : kModifiers) {
            if (modifier.code == event.keyCode) {
                menu_keys.emplace_back(virtual_key(event.keyCode), (event.modifierFlags & modifier.bit) != 0u, false, 0);
            }
        }
        if (event.keyCode == 0x39) {  // Caps Lock: a press each time it changes
            menu_keys.emplace_back(VK_CAPITAL, (event.modifierFlags & NSEventModifierFlagCapsLock) != 0u, false, 0);
        }
        return false;
    }
    if ((event.modifierFlags & NSEventModifierFlagCommand) != 0u) {
        return false;  // the app's menus take it
    }
    // The table's letter is the key's place (a Mac's key codes are the hardware's).
    const int place = virtual_key(event.keyCode);
    const int position = place >= 'A' && place <= 'Z' ? place : 0;
    menu_keys.emplace_back(position != 0 ? layout_key(event, position) : place, event.type == NSEventTypeKeyDown,
                           event.type == NSEventTypeKeyDown && event.ARepeat, position);
    return true;
}

}  // namespace

bool start() noexcept {
    g_main_thread = std::this_thread::get_id();
    galaxy::apple::start_app();
    @autoreleasepool {
        NSNotificationCenter* const center = NSNotificationCenter.defaultCenter;
        [center addObserver:g_delegate selector:@selector(controllerConnected:) name:GCControllerDidConnectNotification object:nil];
        [center addObserver:g_delegate
                   selector:@selector(controllerDisconnected:)
                       name:GCControllerDidDisconnectNotification
                     object:nil];
        for (GCController* controller in GCController.controllers) {
            open_gamepad(controller);
        }
    }
    return true;
}

}  // namespace galaxy::sdl

namespace galaxy::apple {

void start_app() noexcept {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    @autoreleasepool {
        // The delegate is the app's before anything of AppKit runs: it is asked at the app's first moments whether
        // the windows' saved state is kept by secure coding, and AppKit stops a process whose answer changes after it
        // has started without one (as when the first start's setup window came up before the game's delegate).
        // Nothing of a window is restored at the next start either: the game opens its own as its settings say.
        [NSUserDefaults.standardUserDefaults registerDefaults:@{@"NSQuitAlwaysKeepsWindows" : @NO}];
        [NSApplication sharedApplication];
        galaxy::sdl::g_delegate = [NebulaDelegate new];
        NSApp.delegate = galaxy::sdl::g_delegate;
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        galaxy::sdl::make_menus();
        [NSApp finishLaunching];
    }
}

}  // namespace galaxy::apple

namespace galaxy::sdl {


void pump_events(int timeout_ms) noexcept {
    std::vector<std::tuple<int, bool, bool, int>> menu_keys;  // key, down, repeat, the letter at its place
    int menu_wheel = 0;
    @autoreleasepool {
        NSDate* const until = [NSDate dateWithTimeIntervalSinceNow:static_cast<double>(std::max(timeout_ms, 0)) / 1000.0];
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
        while (event != nil) {
            bool taken = false;
            switch (event.type) {
            case NSEventTypeKeyDown:
            case NSEventTypeKeyUp:
            case NSEventTypeFlagsChanged:
                taken = key_event(event, menu_keys);
                break;
            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
            case NSEventTypeOtherMouseDragged:
                mouse_at(event);
                break;
            case NSEventTypeLeftMouseDown:
            case NSEventTypeLeftMouseUp:
            case NSEventTypeRightMouseDown:
            case NSEventTypeRightMouseUp:
            case NSEventTypeOtherMouseDown:
            case NSEventTypeOtherMouseUp: {
                mouse_at(event);
                const bool down = event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown ||
                                  event.type == NSEventTypeOtherMouseDown;
                const NSInteger number = event.buttonNumber;
                const unsigned bit = number == 0 ? 0u : number == 1 ? 1u : number == 2 ? 2u : number == 3 ? 3u : 4u;
                const std::lock_guard<std::mutex> lock(g_input_mutex);
                // A press outside the picture (the title bar, another window) is the system's; a release always counts.
                if (!down || (event.window == g_window && g_input.mouse_inside)) {
                    g_input.keys.mouse = static_cast<std::uint8_t>(down ? (g_input.keys.mouse | (1u << bit))
                                                                        : (g_input.keys.mouse & ~(1u << bit)));
                    if (down) {
                        g_last_input.store(kLastKeyboard, std::memory_order_relaxed);
                    }
                }
                break;
            }
            case NSEventTypeScrollWheel: {
                // In 120ths of a notch, positive away from the player (Windows' WHEEL_DELTA), as the game takes it: a
                // wheel's lines, or a trackpad's points at ten to a notch.
                const double turned = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10.0 : event.scrollingDeltaY;
                const double notches = event.directionInvertedFromDevice ? -turned : turned;
                menu_wheel += static_cast<int>(std::lround(notches * 120.0));
                break;
            }
            default:
                break;
            }
            if (!taken) {
                [NSApp sendEvent:event];
            }
            event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES];
        }
        {
            const std::lock_guard<std::mutex> lock(g_input_mutex);
            sample_gamepads();
        }
    }
    // Pad 0 of the connected ones drives the menu as a controller does on Windows.
    {
        const InputState state = input_state();
        for (const galaxy::input::PadSample& sample : state.pads) {
            if (!sample.connected) {
                continue;
            }
            // The unified buttons are the menu's, by their labels (Nintendo's A is the right face button).
            const std::uint16_t buttons = galaxy::input::settings_pad_buttons(sample.scheme, sample.buttons);
            const galaxy::input::NativeSettingsPadStep step = galaxy::input::native_settings_pad_step(
                g_menu_pad, buttons, sample.axes[0], sample.axes[1], galaxy::render::native_settings_overlay_visible(),
                galaxy::steady_ms());
            for (std::size_t i = 0; i < step.key_count; ++i) {
                galaxy::render::native_settings_post_key(step.keys[i]);
            }
            break;
        }
    }
    if (menu_wheel != 0 && !galaxy::render::native_settings_wheel(menu_wheel)) {
        galaxy::render::host_window::pointer_wheel.fetch_add(menu_wheel, std::memory_order_relaxed);
    }
    for (const auto& [key, down, repeat, position] : menu_keys) {
        const bool taken = down && galaxy::render::native_settings_handle_window_key(static_cast<std::uintptr_t>(key), repeat,
                                                                                    static_cast<std::uint32_t>(key));
        // A release always reaches the game: a key the menu took going down is not left held when the menu closes on it.
        if (!taken || !down) {
            const std::lock_guard<std::mutex> lock(g_input_mutex);
            set_key(key, position, down && !taken);
        }
    }
    @autoreleasepool {
        run_requests();
    }
}

void show_fatal(const char* message) noexcept {
    if (const std::filesystem::path data = galaxy::nebula_data_directory(); !data.empty()) {
        if (std::FILE* file = galaxy::open_path(data / "last_error.txt", "w")) {
            std::fputs(message, file);
            std::fclose(file);
        }
    }
    std::fprintf(stderr, "[fatal] %s\n", message);
    // An alert is the main thread's; from another the log and the file say it.
    if (std::this_thread::get_id() == g_main_thread && NSApp != nil) {
        @autoreleasepool {
            g_panels.fetch_add(1, std::memory_order_relaxed);
            update_cursor();
            NSAlert* const alert = [NSAlert new];
            alert.alertStyle = NSAlertStyleCritical;
            alert.messageText = @"Nebula";
            alert.informativeText = @(message);
            [NSApp activateIgnoringOtherApps:YES];
            [alert runModal];
            g_panels.fetch_sub(1, std::memory_order_relaxed);
        }
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

InputState input_state() noexcept {
    const std::lock_guard<std::mutex> lock(g_input_mutex);
    return g_input;
}

InputState take_input_state() noexcept {
    const std::lock_guard<std::mutex> lock(g_input_mutex);
    const InputState state = g_input;
    for (PadMotion& motion : g_input.motion) {
        motion.gyro_x = motion.gyro_y = 0.0f;
        motion.touch_dx = motion.touch_dy = 0.0f;
    }
    return state;
}

void rumble(std::uint32_t pad_id, float strength) noexcept {
    g_rumble_level.store(static_cast<std::uint32_t>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f), std::memory_order_relaxed);
    g_rumble_pad.store(pad_id, std::memory_order_relaxed);
}

void start_thread_sampler() noexcept {}

}  // namespace galaxy::sdl

// The window as the renderer and the settings menu ask for it (host_window.h).
namespace galaxy::render::host_window {

unsigned backbuffer_width = 0;
unsigned backbuffer_height = 0;
std::atomic<bool> borderless_active{false};
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0u};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;

namespace {

// The window, `width` x `height` points of picture, on the screen the settings name (else the main one). Main thread.
void make_window(int width, int height) {
    using namespace galaxy::sdl;
    NSScreen* screen = NSScreen.mainScreen;
    const std::string wanted = get_render_config().monitor.data();
    for (NSScreen* candidate in NSScreen.screens) {
        if (!wanted.empty() && wanted == candidate.localizedName.UTF8String) {
            screen = candidate;
        }
    }
    const NSRect visible = screen.visibleFrame;
    const NSRect content = NSMakeRect(0, 0, std::min<double>(std::max(width, 320), visible.size.width),
                                      std::min<double>(std::max(height, 240), visible.size.height));
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                    NSWindowStyleMaskResizable;
    NSWindow* const window = [[NSWindow alloc] initWithContentRect:content styleMask:style backing:NSBackingStoreBuffered defer:NO
                                                            screen:screen];
    window.releasedWhenClosed = NO;
    window.restorable = NO;  // the settings say how the game's window opens, not the last run's
    window.title = @"Nebula";
    window.contentMinSize = NSMakeSize(320, 240);
    window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    window.acceptsMouseMovedEvents = YES;
    window.backgroundColor = NSColor.blackColor;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    NebulaView* const view = [[NebulaView alloc] initWithFrame:content];
    window.contentView = (NSView*)view;
    window.delegate = (id<NSWindowDelegate>)g_delegate;
    [window makeFirstResponder:(NSView*)view];
    // In the middle of the screen's free area.
    const NSRect frame = window.frame;
    [window setFrameOrigin:NSMakePoint(NSMidX(visible) - frame.size.width / 2.0, NSMidY(visible) - frame.size.height / 2.0)];
    ((NSView*)view).layer.contentsScale = window.backingScaleFactor;
    g_window = window;
    g_view = view;
    g_layer.store((__bridge void*)((NSView*)view).layer, std::memory_order_release);
    update_window_size();
    // Files opened before the window was up (Nebula started by opening a .luma): imported now.
    if (!g_opened_early.empty()) {
        galaxy::render::native_settings_files_dropped(std::move(g_opened_early));
        g_opened_early.clear();
    }
}

}  // namespace

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    using namespace galaxy::sdl;
    const bool hidden = hidden_window_mode_enabled();
    const auto make = [&] {
        @autoreleasepool {
            make_window(window_width, window_height);
            if (!hidden) {
                [g_window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            }
        }
    };
    if (std::this_thread::get_id() == g_main_thread) {
        make();
    } else {
        // The renderer starts on a thread of its own: the main thread, which waits for it, runs its queue meanwhile.
        dispatch_sync(dispatch_get_main_queue(), make);
    }
    if (g_window == nil) {
        std::fprintf(stderr, "[window] the window could not be made\n");
        return {};
    }
    owner.window = (__bridge void*)g_window;
    return {(__bridge void*)g_window, !hidden};
}

void close_game_window(WindowOwner& owner) noexcept {
    using namespace galaxy::sdl;
    owner.window = nullptr;
    g_layer.store(nullptr, std::memory_order_release);
    on_main(^{
        if (g_cursor_hidden) {
            [NSCursor unhide];
            g_cursor_hidden = false;
        }
        [g_window orderOut:nil];
        g_window.delegate = nil;
        g_window = nil;
        g_view = nil;
    });
}

void show_window(NativeWindow) noexcept {
    using namespace galaxy::sdl;
    on_main(^{
        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    });
}

// The display link's rate where it is known (display_clock_apple.mm), to the whole Hz; else the screen's own.
double display_refresh_hz(NativeWindow) noexcept {
    if (const double vsync = galaxy::sdl::display_refresh_hz(); vsync > 1.0) {
        return std::round(vsync);
    }
    return 0.0;
}

void set_state_wake(void*) noexcept {}

bool vblank_grid_known(std::int64_t& vblank, std::int64_t& period) noexcept {
    return vblank_grid(nullptr, vblank, period);
}

bool vblank_grid(NativeWindow, std::int64_t& vblank, std::int64_t& period) noexcept {
    return galaxy::sdl::vsync_grid(vblank, period);
}

bool present_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns, std::int64_t& now_ns) noexcept {
    return galaxy::sdl::display_grid(slot_ns, period_ns, vsync_ns, now_ns);
}

// The screens, for the settings menu's DISPLAY page: each one's pixels and fastest rate.
std::vector<SettingsMonitor> settings_monitors() {
    __block std::vector<SettingsMonitor> monitors;
    const auto list = ^{
        @autoreleasepool {
            bool first = true;
            for (NSScreen* screen in NSScreen.screens) {
                const NSSize pixels = [screen convertRectToBacking:screen.frame].size;
                SettingsMonitor monitor;
                monitor.id = screen.localizedName.UTF8String ?: "display";
                monitor.name = monitor.id;
                monitor.width = static_cast<unsigned>(std::lround(pixels.width));
                monitor.height = static_cast<unsigned>(std::lround(pixels.height));
                monitor.refresh_hz = static_cast<unsigned>(screen.maximumFramesPerSecond);
                monitor.max_client_width = static_cast<int>(monitor.width);
                monitor.max_client_height = static_cast<int>(monitor.height);
                monitor.primary = first;
                monitor.resolutions.emplace_back(monitor.width, monitor.height);
                monitors.push_back(std::move(monitor));
                first = false;
            }
        }
    };
    if (std::this_thread::get_id() == galaxy::sdl::g_main_thread) {
        list();
    } else {
        // Screens are read-only facts: any thread may ask, with no wait on the main one.
        list();
    }
    if (monitors.empty()) {
        const galaxy::sdl::InputState state = galaxy::sdl::input_state();
        SettingsMonitor monitor;
        monitor.id = "display";
        monitor.name = "Display";
        monitor.width = static_cast<unsigned>(state.window_width);
        monitor.height = static_cast<unsigned>(state.window_height);
        monitor.primary = true;
        monitor.resolutions.emplace_back(monitor.width, monitor.height);
        monitors.push_back(std::move(monitor));
    }
    return monitors;
}

bool window_modal_active() noexcept {
    return galaxy::sdl::g_panels.load(std::memory_order_relaxed) != 0;
}

void resize_window_client(unsigned width, unsigned height) noexcept {
    using namespace galaxy::sdl;
    on_main(^{
        if (g_window != nil && g_view != nil) {
            const CGFloat scale = g_window.backingScaleFactor > 0.0 ? g_window.backingScaleFactor : 1.0;
            [g_window setContentSize:NSMakeSize(static_cast<CGFloat>(width) / scale, static_cast<CGFloat>(height) / scale)];
        }
    });
}

// The system's open panel, answered on the main thread.
void pick_files(FilePick kind, std::function<void(std::vector<std::filesystem::path>)> done) {
    using namespace galaxy::sdl;
    auto shared = std::make_shared<std::function<void(std::vector<std::filesystem::path>)>>(std::move(done));
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            NSOpenPanel* const panel = [NSOpenPanel openPanel];
            panel.allowsMultipleSelection = YES;
            panel.canChooseDirectories = kind == FilePick::ModFolders;
            panel.canChooseFiles = kind != FilePick::ModFolders;
            panel.message = kind == FilePick::Miis ? @"Choose Mii files" : kind == FilePick::ModFolders ? @"Choose mod folders" : @"Choose mods or texture packs";
            g_panels.fetch_add(1, std::memory_order_relaxed);
            update_cursor();
            const auto finished = ^(NSModalResponse result) {
                std::vector<std::filesystem::path> chosen;
                if (result == NSModalResponseOK) {
                    for (NSURL* url in panel.URLs) {
                        if (url.fileSystemRepresentation != nullptr) {
                            chosen.emplace_back(url.fileSystemRepresentation);
                        }
                    }
                }
                g_panels.fetch_sub(1, std::memory_order_relaxed);
                (*shared)(std::move(chosen));
            };
            if (g_window != nil) {
                [panel beginSheetModalForWindow:g_window completionHandler:finished];
            } else {
                [panel beginWithCompletionHandler:finished];
            }
        }
    });
}

// The Finder at the folder (made first, as on Windows).
void open_folder(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::string path = galaxy::path_text(std::filesystem::absolute(folder, error));
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(path.c_str()) isDirectory:YES]];
        }
    });
}

bool folders_open() noexcept {
    return true;
}

// The other systems' vendor upscalers and frame generation are not here: MetalFX is the system's own (always there).
UpscalerCaps upscaler_caps() noexcept {
    return UpscalerCaps{};
}

unsigned dlss_min_width(unsigned, unsigned, unsigned) noexcept {
    return 0u;
}

bool launcher_available() {
    return false;
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

bool restart_game(bool) {
    return false;
}

void* metal_layer(NativeWindow) {
    return galaxy::sdl::g_layer.load(std::memory_order_acquire);
}

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
    pointer.inside_client = state.mouse_inside;
    pointer.absolute_valid = state.mouse_pointer;
    pointer.left_button = (state.keys.mouse & 1u) != 0u;
    pointer.right_button = (state.keys.mouse & 2u) != 0u;
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

input::ButtonIconStyle button_icons_auto_style() noexcept {
    return input::ButtonIconStyle::Game;
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

void real_wiimote_taken(bool) noexcept {}

// A Mac has no on-screen controls: a keyboard, a mouse or a controller plays.
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

// No real Wii Remote on this host.
WiiRemoteState wii_remote_state() noexcept {
    return {};
}

TouchControlsSettings touch_controls_settings() noexcept {
    return {};
}

void set_touch_controls_settings(const TouchControlsSettings&) noexcept {}

void reset_touch_controls_places() noexcept {}

// The picture's width in points, as the system counts a window.
unsigned window_width_dp() noexcept {
    const galaxy::sdl::InputState state = galaxy::sdl::input_state();
    const unsigned pixels = static_cast<unsigned>(std::max(state.window_width, 0));
    const unsigned backing = galaxy::render::host_window::backbuffer_width;
    (void)backing;
    // Two pixels a point on a Retina display: the layer's scale is the window's.
    CAMetalLayer* const layer = (__bridge CAMetalLayer*)galaxy::sdl::g_layer.load(std::memory_order_acquire);
    const double scale = layer != nil && layer.contentsScale > 0.0 ? layer.contentsScale : 1.0;
    return static_cast<unsigned>(static_cast<double>(pixels) / scale);
}

bool keyboard_or_mouse_attached() noexcept {
    return true;
}

// A second player on this system: not yet.
bool native_game_co_star(galaxy::input::PlayerInput*) noexcept {
    return false;
}

JoyConStatus joycon_status() noexcept {
    return {};
}

void set_joycon_pairing(bool) noexcept {}

// The game's button icons (the Windows host's rules): the ButtonIcons setting; Auto's the keyboard's after a key or
// mouse button (always in the keyboard and mouse input mode, never in the controller one), a controller's family
// after its input while it stays attached, else the Wii's.
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
// A build without the importer: mods come in through the app there.
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
