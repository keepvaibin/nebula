// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/live_controls_engine.h"
#include <array>
#include <cmath>
#include <iostream>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Hotplug policy must be testable without Windows.h
#endif

namespace {
using namespace galaxy::input;
using namespace galaxy::host::controls;
struct Host final : LiveControlsHost {
    std::uint64_t clock = 1'000'000;
    std::uint64_t now_ms() override { return clock / 1000; }
    std::uint64_t now_us() override { return clock; }
    bool injected(bool) override { return false; }
    PointerSample pointer(bool, bool) override { return {}; }
    NativeMousePointerProjection cursor() override { return {}; }
};
int replacement_hysteresis() {
    auto config = default_controls_config();
    config.table(ControlScheme::Xbox) = {};
    config.table(ControlScheme::Xbox)[GameInput::A] = {InputSource{SourceKind::AxisPositive, 0}};
    LivePad device;
    device.sample.connected = true; device.sample.scheme = ControlScheme::Xbox;
    device.device_slot = 0; device.sample.axes[0] = 0.9f;
    LiveControlsFrame frame{config}; frame.pads = std::span(&device, 1);
    frame.use_pads = true; frame.window_focused = true;
    LiveControlsEngine engine; Host host;
    const auto first = engine.sample(frame, host);
    device.device_slot = 1;
    device.sample.axes[0] = (kStickDirectionPress + kStickDirectionRelease) * 0.5f;
    host.clock += 16'667;
    const auto next = engine.sample(frame, host);
    std::cout << "replacement_hysteresis\tfirst=" << first.input.buttons.a
              << "\treplacement=" << next.input.buttons.a << "\texpected=0\n";
    return first.input.buttons.a && !next.input.buttons.a ? 0 : 1;
}
int rumble_fallback() {
    auto config = default_controls_config();
    LivePad device;
    device.sample.connected = true; device.sample.scheme = ControlScheme::Xbox;
    device.device_slot = 0; device.sample.buttons = pad::bit(pad::kSouthBit);
    LiveControlsFrame frame{config}; frame.pads = std::span(&device, 1);
    frame.use_pads = true; frame.window_focused = true;
    LiveControlsEngine engine; Host host;
    (void)engine.sample(frame, host);
    device.sample = {}; device.sample.connected = true; device.sample.scheme = ControlScheme::SwitchPro;
    device.device_slot = 1; host.clock += 16'667;
    const auto next = engine.sample(frame, host);
    const DeviceKey expected{ControlScheme::SwitchPro, 1};
    const auto actual = next.rumble.device.value_or(DeviceKey{});
    std::cout << "rumble_fallback\tscheme=" << unsigned(actual.scheme) << "\tslot=" << unsigned(actual.slot)
              << "\texpected_scheme=2\texpected_slot=1\n";
    return next.rumble.device == expected ? 0 : 1;
}
int reorder_and_reconnect() {
    auto config = default_controls_config();
    config.table(ControlScheme::Xbox) = {};
    config.table(ControlScheme::Xbox)[GameInput::A] = {InputSource{SourceKind::AxisPositive, 0}};
    std::array<LivePad, 2> devices;
    devices[0].sample.connected = true; devices[0].sample.scheme = ControlScheme::Xbox;
    devices[0].device_slot = 0; devices[0].sample.axes[0] = 0.9f;
    devices[1].sample.connected = true; devices[1].sample.scheme = ControlScheme::SwitchPro;
    devices[1].device_slot = 0;
    LiveControlsFrame frame{config}; frame.pads = devices; frame.use_pads = true; frame.window_focused = true;
    LiveControlsEngine engine; Host host;
    (void)engine.sample(frame, host);
    std::swap(devices[0], devices[1]);
    devices[1].sample.axes[0] = (kStickDirectionPress + kStickDirectionRelease) * 0.5f;
    host.clock += 16'667;
    const bool retained = engine.sample(frame, host).input.buttons.a;
    frame.pads = std::span(devices.data(), 1); host.clock += 16'667;
    (void)engine.sample(frame, host);
    frame.pads = devices; host.clock += 16'667;
    const bool reconnected = engine.sample(frame, host).input.buttons.a;
    std::cout << "reorder_and_reconnect\tretained=" << retained << "\treconnected=" << reconnected
              << "\texpected_retained=1\texpected_reconnected=0\n";
    return retained && !reconnected ? 0 : 1;
}
int focus_release_and_reacquire() {
    auto config = default_controls_config();
    config.table(ControlScheme::Xbox) = {};
    config.table(ControlScheme::Xbox)[GameInput::A] = {InputSource{SourceKind::AxisPositive, 0}};
    LivePad pad; pad.sample.connected = true; pad.sample.scheme = ControlScheme::Xbox;
    pad.device_slot = 0; pad.sample.axes[0] = 0.9f;
    pad.sample.axes[1] = 0.8f;
    LiveControlsFrame frame{config}; frame.pads = std::span(&pad, 1);
    frame.use_pads = true; frame.window_focused = true;
    LiveControlsEngine engine; Host host;
    const bool first = engine.sample(frame, host).input.buttons.a;
    frame.window_focused = false; host.clock += 16'667;
    const auto lost = engine.sample(frame, host);
    pad.sample.axes[0] = (kStickDirectionPress + kStickDirectionRelease) * 0.5f;
    frame.window_focused = true; host.clock += 16'667;
    const auto resumed = engine.sample(frame, host);
    pad.sample.axes[0] = 0.9f; frame.window_focused = false;
    frame.allow_background_device_input = true; host.clock += 16'667;
    const auto explicit_background = engine.sample(frame, host);
    std::cout << "focus_release\tfirst=" << first << "\tlost=" << lost.input.buttons.a
              << "\tlost_motor=" << lost.rumble.device.has_value() << "\tresumed_midpoint=" << resumed.input.buttons.a
              << "\tbackground_opt_in=" << explicit_background.input.buttons.a
              << "\tbackground_motor_focus=" << explicit_background.rumble.focused << '\n';
    return first && !lost.input.buttons.a && !lost.rumble.device && !lost.input.pointer_on &&
        !resumed.input.buttons.a && explicit_background.input.buttons.a && !explicit_background.rumble.focused ? 0 : 1;
}
int motion_after_suspension() {
    int failures = 0;
    for (const bool touch : {false,true}) {
        auto config = default_controls_config();
        const auto scheme = ControlScheme::SwitchPro;
        config.tune(scheme).gyro_pointer = !touch;
        config.tune(scheme).touch_pointer = touch;
        LivePad pad; pad.sample.connected = true; pad.sample.scheme = scheme; pad.device_slot = 0;
        pad.imu_live = !touch; pad.touch_live = touch;
        LiveControlsFrame frame{config}; frame.pads = std::span(&pad,1);
        frame.use_pads = true; frame.window_focused = true;
        LiveControlsEngine engine; Host host;
        (void)engine.sample(frame,host);
        pad.gyro_x = touch ? 0.0f : 0.1f; pad.touch_dx = touch ? 0.05f : 0.0f;
        host.clock += 16'667;
        const auto warm = engine.sample(frame,host).input;
        frame.window_focused = false; host.clock += 16'667;
        const auto lost = engine.sample(frame,host).input;
        // The native worker kept accumulating motion until the next consuming read.
        pad.gyro_x = touch ? 0.0f : 3.0f; pad.touch_dx = touch ? 0.8f : 0.0f;
        pad.shake_remote = true;
        frame.window_focused = true; host.clock += 16'667;
        const auto resumed = engine.sample(frame,host).input;
        pad.gyro_x = touch ? 0.0f : 0.02f; pad.touch_dx = touch ? 0.01f : 0.0f;
        pad.shake_remote = false; host.clock += 16'667;
        const auto fresh = engine.sample(frame,host).input;
        const bool retained = std::fabs(resumed.pointer_x-warm.pointer_x) < 0.00001f;
        const bool advanced = fresh.pointer_x > resumed.pointer_x;
        std::cout << (touch ? "touch_resume" : "gyro_resume") << "\twarm=" << warm.pointer_x
                  << "\tlost_visible=" << lost.pointer_on << "\tresumed=" << resumed.pointer_x
                  << "\tstale_shake=" << resumed.shake << "\tfresh=" << fresh.pointer_x << '\n';
        if (!warm.pointer_on || lost.pointer_on || !resumed.pointer_on || !retained || resumed.shake || !advanced) ++failures;
    }
    return failures;
}
}
int main() {
    const int failures = replacement_hysteresis() + rumble_fallback() + reorder_and_reconnect() +
        focus_release_and_reacquire() + motion_after_suspension();
    return failures ? 1 : 0;
}
