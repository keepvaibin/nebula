// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/live_controls_engine.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Live controls tests must not include Windows.h
#endif

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace galaxy::input;
using namespace galaxy::host::controls;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }
struct Host final : LiveControlsHost {
    std::uint64_t clock = 1'000'000;
    PointerSample state{};
    NativeMousePointerProjection fallback{};
    bool injected_move = false, injected_button = false;
    std::uint64_t now_ms() override { return clock / 1000; }
    std::uint64_t now_us() override { return clock; }
    bool injected(bool button) override { return button ? injected_button : injected_move; }
    PointerSample pointer(bool buttons_allowed, bool) override {
        auto sample = state;
        if (!buttons_allowed) { sample.left_button = sample.right_button = sample.deliver_buttons = false; }
        return sample;
    }
    NativeMousePointerProjection cursor() override { return fallback; }
    void advance(std::uint64_t us = 16'667) { clock += us; }
};

void families_and_keyboard() {
    auto config = default_controls_config();
    for (const auto scheme : {ControlScheme::Xbox, ControlScheme::SwitchPro, ControlScheme::SwitchPro2,
                              ControlScheme::PlayStation, ControlScheme::Steam, ControlScheme::Generic}) {
        LiveControlsEngine engine; Host host;
        LivePad pad; pad.sample.connected = true; pad.sample.scheme = scheme; pad.device_slot = 2;
        const bool nintendo = scheme == ControlScheme::SwitchPro || scheme == ControlScheme::SwitchPro2;
        pad.sample.buttons = pad::bit(nintendo ? pad::kEastBit : pad::kSouthBit);
        pad.sample.axes[0] = 0.6f;
        LiveControlsFrame frame{config}; frame.pads = std::span(&pad, 1);
        frame.use_pads = true; frame.window_focused = true;
        auto result = engine.sample(frame, host);
        check(result.input.buttons.a && result.input.stick_x == nunchuk_stick(0.6f, 0).x, "family buttons and stick");
        check(result.rumble.device == DeviceKey{scheme, 2} && result.rumble.focused, "rumble follows active family/slot");
        frame.use_keyboard = true; frame.keyboard_levels.set_position('D', true);
        frame.keyboard_levels.set_key(0x20, true);
        host.advance(); result = engine.sample(frame, host);
        check(result.input.buttons.a && result.input.stick_x == 1.0f && !result.rumble.device,
              "keyboard merges controls and takes motor ownership");
        frame.script.has_stick = true; frame.script.stick_x = -0.5f;
        frame.script.buttons.z = true; frame.script.shake = true;
        host.advance(); result = engine.sample(frame, host);
        check(result.input.buttons.z && result.input.shake && result.input.stick_x == nunchuk_stick(-0.5f, 0).x,
              "script override after live mapping");
    }
}

void mouse_samples() {
    auto config = default_controls_config();
    LiveControlsEngine engine; Host host;
    LiveControlsFrame frame{config}; frame.use_keyboard = true; frame.allow_absolute_pointer = true;
    host.state.polled = true; host.state.absolute_valid = true; host.state.inside_client = true;
    host.state.left_button = true; host.state.deliver_buttons = true; host.state.absolute_sequence = 1;
    host.state.projected.visible = true; host.state.projected.x = 0.3f; host.state.projected.y = -0.4f;
    auto result = engine.sample(frame, host);
    check(result.input.buttons.a && result.input.pointer_on && result.input.pointer_x == 0.3f,
          "acquired client mouse can click without focus");
    host.state.left_button = host.state.deliver_buttons = false;
    host.state.absolute_valid = false; host.state.projected.visible = false;
    host.advance(100'000); result = engine.sample(frame, host);
    check(result.input.pointer_on && result.input.pointer_y == -0.4f, "bounded invalid sample hold");
    host.advance(500'000); result = engine.sample(frame, host);
    check(!result.input.pointer_on && !result.input.buttons.a, "invalid sample hold expires and button releases");
    host.state.absolute_valid = true; host.state.projected.visible = true;
    host.state.has_transition_coordinates = true; host.state.transition_sequence = 1;
    host.advance(); result = engine.sample(frame, host);
    check(result.input.pointer_on, "queued button coordinates renew mouse acquisition");
    host.state.inside_client = false; host.state.projected.visible = false;
    host.advance(); result = engine.sample(frame, host);
    check(!result.input.pointer_on, "known outside never holds invalid-sample visibility");

    LiveControlsEngine priority;
    host.state.inside_client = true; host.state.projected.visible = true;
    host.state.has_transition_coordinates = false; ++host.state.absolute_sequence;
    frame.script.has_pointer = true; frame.script.pointer_x = -0.8f;
    result = priority.sample(frame, host);
    check(result.input.pointer_x == 0.3f, "fresh physical mouse wins over nonforced script");
    frame.script_pointer_force = true;
    host.advance(); result = priority.sample(frame, host);
    check(result.input.pointer_x == -0.8f, "forced script takes pointer");

    LiveControlsEngine raw_priority;
    frame.script_pointer_force = false;
    host.state.raw_motion = false;
    (void)raw_priority.sample(frame, host);
    host.advance(500'000); result = raw_priority.sample(frame, host);
    check(result.input.pointer_x == -0.8f, "stationary absolute sample expires script grace");
    host.state.raw_motion = true;
    host.advance(); result = raw_priority.sample(frame, host);
    check(result.input.pointer_x == 0.3f && result.input.pointer_y == -0.4f,
          "opposing raw movement renews activity without integrating pointer coordinates");
    host.state.raw_motion = false;
    host.advance(500'000); result = raw_priority.sample(frame, host);
    check(result.input.pointer_x == -0.8f, "consumed raw activity cannot latch permanently");
}

void motion_context_and_instances() {
    auto config = default_controls_config();
    const auto scheme = ControlScheme::SwitchPro;
    config.tune(scheme).gyro_pointer = true;
    config.tune(scheme).pointer_hide_seconds = 0;
    config.table(scheme)[GameInput::PointerRecenter] = {InputSource{SourceKind::Button, pad::kRightShoulderBit}};
    LivePad pad; pad.sample.connected = true; pad.sample.scheme = scheme; pad.device_slot = 0;
    pad.imu_live = true; pad.gyro_x = 0.1f;
    LiveControlsFrame frame{config}; frame.use_pads = true; frame.window_focused = true;
    frame.pads = std::span(&pad, 1);
    LiveControlsEngine first, second; Host host;
    (void)first.sample(frame, host); host.advance();
    auto result = first.sample(frame, host);
    check(result.input.pointer_on && result.input.pointer_x > 0.05f, "native gyro moves pointer");
    const auto separate = second.sample(frame, host);
    check(near(separate.input.pointer_x, 0), "engine instances have independent pointer history");
    pad.sample.buttons = pad::bit(pad::kRightShoulderBit);
    host.advance(); result = first.sample(frame, host);
    check(result.input.pointer_on && near(result.input.pointer_x, 0), "recenter holds against gyro movement");
    pad.gravity_valid = true; frame.tilt_section = tilt::Section::StarBall;
    host.advance(); result = first.sample(frame, host);
    check(!result.input.pointer_on && result.input.gravity[1] < -0.8f, "native gravity drives upright tilt section");

    config.table(scheme)[GameInput::A] = {};
    config.table(scheme)[GameInput::B] = {};
    config.table(scheme)[GameInput::JumpBack] = {InputSource{SourceKind::Button, pad::kEastBit}};
    pad.sample.buttons = pad::bit(pad::kEastBit); frame.tilt_section = tilt::Section::None;
    LiveControlsEngine context;
    frame.context = PlayContext::Play; result = context.sample(frame, host);
    check(result.input.buttons.a && !result.input.buttons.b, "jump/back press in play jumps");
    frame.context = PlayContext::Menu; host.advance(); result = context.sample(frame, host);
    check(result.input.buttons.a && !result.input.buttons.b, "held jump does not become menu back");
    pad.sample.buttons = 0; host.advance(); (void)context.sample(frame, host);
    pad.sample.buttons = pad::bit(pad::kEastBit); host.advance(); result = context.sample(frame, host);
    check(!result.input.buttons.a && result.input.buttons.b, "fresh menu press goes back");
}

// The SDL hosts' inputs: on-screen controls, the device's own sensors, the finger as the mouse, the default R recenter.
void host_controls_and_device_motion() {
    auto config = default_controls_config();
    LiveControlsEngine engine; Host host;
    LiveControlsFrame frame{config}; frame.window_focused = true;
    frame.extra.pressed = game_input_bit(GameInput::A); frame.extra.stick_x = 0.5f;
    auto result = engine.sample(frame, host);
    check(result.input.buttons.a && result.input.stick_x == nunchuk_stick(0.5f, 0).x, "on-screen controls merge");
    frame.extra = {};
    frame.device_shake = true;
    const tilt::Vec3 up{0.0f, 1.0f, 0.0f};
    frame.device_up = &up; frame.tilt_section = tilt::Section::StarBall;
    host.advance(); result = engine.sample(frame, host);
    check(result.input.shake && result.input.gravity[1] < -0.8f, "device sensors shake and steer without a controller");

    const auto scheme = ControlScheme::SwitchPro;
    config.tune(scheme).gyro_pointer = true;
    config.tune(scheme).pointer_hide_seconds = 0;
    LiveControlsEngine shared; Host finger;
    LivePad pad; pad.sample.connected = true; pad.sample.scheme = scheme; pad.device_slot = 0; pad.imu_live = true;
    LiveControlsFrame pads{config}; pads.use_pads = true; pads.window_focused = true; pads.allow_absolute_pointer = true;
    pads.pads = std::span(&pad, 1);
    finger.state.polled = true; finger.state.absolute_valid = true; finger.state.inside_client = true;
    finger.state.raw_motion = true; finger.state.projected = {0.4f, 0.2f, true};
    result = shared.sample(pads, finger);
    check(result.pointer_owner == PointerSource::Mouse && result.input.pointer_x == 0.4f, "finger owns the pointer");
    finger.state.raw_motion = false; finger.state.inside_client = false; finger.state.projected.visible = false;
    pad.gyro_x = 0.1f;
    for (int i = 0; i < 20; ++i) { finger.advance(); result = shared.sample(pads, finger); }
    check(result.pointer_owner == PointerSource::Gyro && result.input.pointer_x > 0.4f,
          "gyro takes the pointer from where the finger left it");
    pad.sample.buttons = pad::bit(pad::kRightShoulderBit);
    finger.advance(); result = shared.sample(pads, finger);
    check(result.input.pointer_on && near(result.input.pointer_x, 0) && near(result.input.pointer_y, 0),
          "R recenters by default");

    // A controller held while the mouse aims: its hand tremor (about 10 degrees a second) never takes the pointer.
    LiveControlsEngine aim; Host mouse;
    pad.sample.buttons = 0;
    pad.gyro_x = 0.5f * 0.016667f;
    mouse.state = finger.state;
    mouse.state.raw_motion = true; mouse.state.inside_client = true; mouse.state.projected = {-0.3f, 0.1f, true};
    result = aim.sample(pads, mouse);
    mouse.state.raw_motion = false;
    for (int i = 0; i < 60; ++i) { mouse.advance(); result = aim.sample(pads, mouse); }
    check(result.pointer_owner == PointerSource::Mouse && result.input.pointer_x == -0.3f,
          "a resting mouse keeps the pointer from a hand-held gyro");

    // Turned half a picture past the top and back, the pointer comes back where it was: the hand and the pointer stay
    // together.
    LiveControlsEngine turn; Host clock;
    pad.gyro_x = 0.0f;
    pad.gyro_y = 0.0f;
    (void)turn.sample(pads, clock);
    pad.gyro_y = 0.1f;
    for (int i = 0; i < 15; ++i) { clock.advance(); result = turn.sample(pads, clock); }
    check(!result.input.pointer_on, "pointed past the top the pointer hides");
    pad.gyro_y = -0.1f;
    for (int i = 0; i < 15; ++i) { clock.advance(); result = turn.sample(pads, clock); }
    check(result.input.pointer_on && std::fabs(result.input.pointer_y) < 0.001f,
          "turned back, the pointer is where the controller points");
}
}
int main() {
    try {
        families_and_keyboard(); mouse_samples(); motion_context_and_instances(); host_controls_and_device_motion();
        std::cout << "live controls families, mouse, motion, context, host controls and independent state passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
