// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/live_controls_engine.h"
#include <array>
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
    config.table(ControlScheme::Xbox)[WiiTarget::A] = {InputSource{SourceKind::AxisPositive, 0}};
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
    config.table(ControlScheme::Xbox)[WiiTarget::A] = {InputSource{SourceKind::AxisPositive, 0}};
    std::array<LivePad, 2> devices;
    devices[0].sample.connected = true; devices[0].sample.scheme = ControlScheme::Xbox;
    devices[0].device_slot = 0; devices[0].sample.axes[0] = 0.9f;
    devices[1].sample.connected = true; devices[1].sample.scheme = ControlScheme::SwitchPro;
    devices[1].device_slot = 0;
    LiveControlsFrame frame{config}; frame.pads = devices; frame.use_pads = true;
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
}
int main() {
    const int failures = replacement_hysteresis() + rumble_fallback() + reorder_and_reconnect();
    return failures ? 1 : 0;
}
