// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/input/controls_engine.h"
#include "galaxy/controller_roster.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/input_bindings.h"
#include "galaxy/pointer_control.h"
#include <optional>
#include <span>

namespace galaxy::host::controls {
// One controller this sample: unified pad sample plus its motion output.
struct LivePad {
    galaxy::input::PadSample sample{};
    bool shake_remote = false;
    bool shake_nunchuk = false;
    bool imu_live = false;
    bool gyro_missing = false;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    // The touch surface's finger travel this sample (its width and height 1 each) and whether the controller has one.
    bool touch_live = false;
    float touch_dx = 0.0f;
    float touch_dy = 0.0f;
    // Controller up (gravity reaction, SDL frame) when the IMU knows it.
    bool gravity_valid = false;
    galaxy::input::tilt::Vec3 gravity{0.0f, 1.0f, 0.0f};
    // XInput user index or Switch worker slot (with the scheme: identity).
    std::uint32_t device_slot = galaxy::input::kKeyboardSlot;
};


bool pad_has_input(const galaxy::input::PadSample& sample);
struct PointerSample {
    bool polled = false;
    bool window_focused = false;
    bool absolute_valid = false;
    bool inside_client = false;
    bool left_button = false;
    bool right_button = false;
    bool deliver_buttons = false;
    bool has_transition_coordinates = false;
    std::uint64_t absolute_sequence = 0;
    std::uint64_t transition_sequence = 0;
    galaxy::input::NativeMousePointerProjection projected{};
    bool raw_motion = false;
};
class LiveControlsHost {
public:
    virtual ~LiveControlsHost() = default;
    virtual std::uint64_t now_ms() = 0;
    virtual std::uint64_t now_us() = 0;
    virtual bool injected(bool button) = 0;
    virtual PointerSample pointer(bool buttons_allowed, bool check_hover) = 0;
    virtual galaxy::input::NativeMousePointerProjection cursor() = 0;
};
struct LiveControlsFrame {
    const galaxy::input::ControlsConfig& config;
    std::span<const LivePad> pads;
    galaxy::input::KeyboardMouseLevels keyboard_levels{};
    NativeInputScriptOverlay script{};
    std::uint64_t game_frame = 0;
    std::uint64_t script_pointer_grace_ms = 250;
    bool window_focused = false;
    bool use_keyboard = false;
    bool use_pads = false;
    bool allow_background_device_input = false;
    bool allow_absolute_pointer = false;
    bool drop_injected = false;
    bool script_pointer_force = false;
    // A menu of the host is open (Settings, the HOME menu): the mods' actions read up (services-input).
    bool host_menu = false;
    galaxy::input::PlayContext context = galaxy::input::PlayContext::Play;
    galaxy::input::tilt::Section tilt_section = galaxy::input::tilt::Section::None;
    // The host's own controls with no binding table (a phone's on-screen buttons and stick), merged with the bindings'.
    galaxy::input::LogicalInput extra{};
    // The device's own motion sensors (a phone) when no controller is in hand: its up steers the tilt sections and its
    // shake spins.
    const galaxy::input::tilt::Vec3* device_up = nullptr;
    bool device_shake = false;
};
struct RumbleSelection {
    std::optional<galaxy::input::DeviceKey> device;
    std::uint8_t strength = 0;
    bool focused = false;
};
struct LiveControlsResult {
    galaxy::input::PlayerInput input{};
    RumbleSelection rumble{};
    galaxy::input::PointerSource pointer_owner = galaxy::input::PointerSource::None;
};
class LiveControlsEngine {
public:
    LiveControlsResult sample(const LiveControlsFrame& frame, LiveControlsHost& host);
private:
    // A pad's key is its scheme in the high 32 bits and its slot in the low 32.
    static constexpr std::uint64_t kDrivingNone = ~std::uint64_t{0};
    static constexpr std::uint64_t kDrivingKeyboard = ~std::uint64_t{0} - 1u;
    struct PadHistory {
        galaxy::input::DeviceKey device;
        std::uint32_t pressed = 0;
        bool seen = false;
    };
    std::vector<PadHistory> s_pad_history;
    std::uint64_t s_driving_pad = kDrivingNone;
    galaxy::input::JumpBackLatch s_jump_back{};
    float s_pointer_x = 0.0f;
    float s_pointer_y = 0.0f;
    float s_mouse_x = 0.0f;
    float s_mouse_y = 0.0f;
    std::uint64_t s_last_mouse_inside_tick = 0;
    std::uint64_t s_last_live_mouse_tick = 0;
    std::uint64_t s_last_host_pointer_absolute_sequence = 0;
    std::uint64_t s_last_host_pointer_transition_sequence = 0;
    bool s_mouse_pointer_seen_inside = false;
    galaxy::input::PointerOwner s_pointer_owner{};
    std::uint64_t s_pointer_clock_us = 0u;
    galaxy::input::RecenterHold s_recenter{};
    galaxy::input::PointerRestHide s_rest_hide{};
    galaxy::input::MouseRestHide s_mouse_rest_hide{};
    galaxy::input::tilt::Tilt s_tilt{};
    bool s_previous_shake = false;
    bool s_device_input_suspended = false;
};
}  // namespace galaxy::host::controls
