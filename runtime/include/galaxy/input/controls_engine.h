// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/native_input.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/tilt.h"
#include <string>
#include <vector>

namespace galaxy::host::controls {
struct NativeInputScriptOverlay {
    galaxy::input::ButtonState buttons{};
    bool has_stick = false;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    bool has_pointer = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool shake = false;
};

struct NativeInputReplaySample {
    std::uint64_t elapsed_ms = 0;
    bool game_focused = false;
    int mouse_client_x = 0;
    int mouse_client_y = 0;
    int client_w = 0;
    int client_h = 0;
    std::uint64_t keys_hold = 0;
    bool xinput_connected = false;
    std::uint16_t xinput_buttons = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
    std::int16_t thumb_lx = 0;
    std::int16_t thumb_ly = 0;
    std::int16_t thumb_rx = 0;
    std::int16_t thumb_ry = 0;
};

struct NativeInputReplayLog {
    std::string path;
    std::vector<NativeInputReplaySample> samples;
};

// A replayed XInput pad uses Windows' recommended thumbstick dead zones, a trigger threshold of 96 and the default
// pointer speeds.
inline constexpr std::int16_t kReplayLeftDeadzone = 7849;
inline constexpr std::int16_t kReplayPointerDeadzone = 8689;
inline constexpr std::uint8_t kXInputTriggerThreshold = 96u;
inline constexpr float kReplayStickPointerSpeed = 2.4f;
inline constexpr float kReplayKeyboardPointerSpeed = 1.6f;


struct ScriptServices {
    std::int64_t (*clock_ticks)() noexcept = nullptr;
};
NativeInputScriptOverlay build_native_input_script_overlay(std::uint64_t current_vi, const ScriptServices& services);
NativeInputReplayLog load_native_input_replay_log(const std::string& path);
const NativeInputReplayLog* active_native_input_replay_log();
const NativeInputReplaySample* native_input_replay_sample_for_vi(std::uint64_t current_vi);
void apply_tilt_accel(galaxy::input::PlayerInput& input, galaxy::input::tilt::Tilt& state,
                      galaxy::input::tilt::Section section, const galaxy::input::tilt::Vec3* controller_up,
                      float stick_x, float stick_y, bool recenter);
galaxy::input::PlayerInput build_replayed_input(std::uint64_t current_vi, const NativeInputReplaySample& replay,
    const NativeInputScriptOverlay& script, galaxy::RuntimeInputMode input_mode,
    galaxy::input::tilt::Section tilt_section, const galaxy::input::NativeMousePointerProjection& replay_pointer,
    const void* log_identity);
}  // namespace galaxy::host::controls
