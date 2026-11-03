// SPDX-License-Identifier: GPL-3.0-only
// Scripted and replayed input for test runs. Not in a release: controls_engine.cpp answers with nothing.
#if !defined(NEBULA_RELEASE)
#include "galaxy/input/controls_engine.h"
#include "switches.h"
#include "galaxy/host_env.h"
#include "galaxy/input_bindings.h"
#include "galaxy/pointer_control.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <iostream>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared controls engine must not include Windows.h
#endif

namespace galaxy::host::controls {
struct NativeInputReplayColumns {
    std::size_t elapsed_ms = 0;
    std::size_t game_focused = 0;
    std::size_t mouse_client_x = 0;
    std::size_t mouse_client_y = 0;
    std::size_t client_w = 0;
    std::size_t client_h = 0;
    std::size_t keys_hold_hex = 0;
    std::size_t xinput_connected = 0;
    std::size_t xinput_buttons_hex = 0;
    std::size_t left_trigger = 0;
    std::size_t right_trigger = 0;
    std::size_t thumb_lx = 0;
    std::size_t thumb_ly = 0;
    std::size_t thumb_rx = 0;
    std::size_t thumb_ry = 0;
};

std::optional<std::uint64_t> read_env_u64_optional(const char* name) {
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0u;
    if (!galaxy::input::parse_native_input_script_u64(*value, parsed)) {
        throw std::runtime_error(
            std::string("invalid unsigned environment value for ") + name);
    }
    return parsed;
}

std::uint64_t parse_script_u64(
    const std::string& field,
    const char* script_name) {
    std::uint64_t parsed = 0u;
    if (!galaxy::input::parse_native_input_script_u64(field, parsed)) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    return parsed;
}

float parse_script_float(const std::string& field, const char* script_name) {
    if (field.empty()) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    char* end = nullptr;
    const float parsed = std::strtof(field.c_str(), &end);
    if (end == field.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    return std::clamp(parsed, -1.0f, 1.0f);
}

std::optional<float> read_env_float_optional(const char* name) {
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return parse_script_float(*value, name);
}

std::vector<std::string> split_script_fields(
    const std::string& text,
    char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    if (!text.empty() && text.back() == delimiter) {
        fields.emplace_back();
    }
    return fields;
}

constexpr std::string_view kNativeInputReplayKeyOrder =
    "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
    "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
    "Ctrl,LCtrl,RCtrl";

std::size_t require_replay_column(
    const std::vector<std::string>& header,
    const char* name,
    const std::string& path) {
    const auto it = std::find(header.begin(), header.end(), name);
    if (it == header.end()) {
        throw std::runtime_error(
            "native input replay log is missing column '" +
            std::string(name) + "': " + path);
    }
    return static_cast<std::size_t>(std::distance(header.begin(), it));
}

NativeInputReplayColumns parse_replay_columns(
    const std::vector<std::string>& header,
    const std::string& path) {
    NativeInputReplayColumns columns{};
    columns.elapsed_ms = require_replay_column(header, "elapsed_ms", path);
    columns.game_focused =
        require_replay_column(header, "game_focused", path);
    columns.mouse_client_x =
        require_replay_column(header, "mouse_client_x", path);
    columns.mouse_client_y =
        require_replay_column(header, "mouse_client_y", path);
    columns.client_w = require_replay_column(header, "client_w", path);
    columns.client_h = require_replay_column(header, "client_h", path);
    columns.keys_hold_hex =
        require_replay_column(header, "keys_hold_hex", path);
    columns.xinput_connected =
        require_replay_column(header, "xinput_connected", path);
    columns.xinput_buttons_hex =
        require_replay_column(header, "xinput_buttons_hex", path);
    columns.left_trigger =
        require_replay_column(header, "left_trigger", path);
    columns.right_trigger =
        require_replay_column(header, "right_trigger", path);
    columns.thumb_lx = require_replay_column(header, "thumb_lx", path);
    columns.thumb_ly = require_replay_column(header, "thumb_ly", path);
    columns.thumb_rx = require_replay_column(header, "thumb_rx", path);
    columns.thumb_ry = require_replay_column(header, "thumb_ry", path);
    return columns;
}

void require_replay_field(
    const std::vector<std::string>& fields,
    std::size_t index,
    const std::string& path,
    std::size_t line_number) {
    if (index >= fields.size()) {
        std::ostringstream message;
        message << "native input replay log row " << line_number
                << " is missing fields: " << path;
        throw std::runtime_error(message.str());
    }
}

std::uint64_t parse_replay_u64(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number,
    int base = 10) {
    if (field.empty() || field.find_first_not_of(" \t\r\n\v\f") == std::string::npos ||
        field[field.find_first_not_of(" \t\r\n\v\f")] == '-') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(field.c_str(), &end, base);
    if (errno == ERANGE || end == field.c_str() || *end != '\0') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::uint64_t>(parsed);
}

std::int64_t parse_replay_i64(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    if (field.empty()) {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(field.c_str(), &end, 10);
    if (errno == ERANGE || end == field.c_str() || *end != '\0') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::int64_t>(parsed);
}

int parse_replay_int(
    const std::string& field, const std::string& name,
    const std::string& path, std::size_t line_number) {
    const auto value = parse_replay_i64(field, name, path, line_number);
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("native input replay " + name +
            " is out of range at " + path + ':' + std::to_string(line_number));
    }
    return static_cast<int>(value);
}

std::uint8_t parse_replay_u8(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::uint64_t parsed =
        parse_replay_u64(field, name, path, line_number);
    if (parsed > 0xffu) {
        std::ostringstream message;
        message << "native input replay " << name << " is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::uint8_t>(parsed);
}

std::int16_t parse_replay_i16(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::int64_t parsed =
        parse_replay_i64(field, name, path, line_number);
    if (parsed < std::numeric_limits<std::int16_t>::min() ||
        parsed > std::numeric_limits<std::int16_t>::max()) {
        std::ostringstream message;
        message << "native input replay " << name << " is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::int16_t>(parsed);
}

bool parse_replay_bool(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::uint64_t parsed =
        parse_replay_u64(field, name, path, line_number);
    if (parsed > 1u) {
        std::ostringstream message;
        message << "native input replay " << name << " must be 0 or 1 at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return parsed != 0;
}

std::string replay_field(
    const std::vector<std::string>& fields,
    std::size_t index,
    const std::string& path,
    std::size_t line_number) {
    require_replay_field(fields, index, path, line_number);
    return fields[index];
}

NativeInputReplaySample parse_replay_sample(
    const std::vector<std::string>& fields,
    const NativeInputReplayColumns& columns,
    const std::string& path,
    std::size_t line_number) {
    NativeInputReplaySample sample{};
    sample.elapsed_ms = parse_replay_u64(
        replay_field(fields, columns.elapsed_ms, path, line_number),
        "elapsed_ms",
        path,
        line_number);
    sample.game_focused = parse_replay_bool(
        replay_field(fields, columns.game_focused, path, line_number),
        "game_focused",
        path,
        line_number);
    sample.mouse_client_x = parse_replay_int(
        replay_field(fields, columns.mouse_client_x, path, line_number),
        "mouse_client_x",
        path,
        line_number);
    sample.mouse_client_y = parse_replay_int(
        replay_field(fields, columns.mouse_client_y, path, line_number),
        "mouse_client_y",
        path,
        line_number);
    sample.client_w = parse_replay_int(
        replay_field(fields, columns.client_w, path, line_number),
        "client_w",
        path,
        line_number);
    sample.client_h = parse_replay_int(
        replay_field(fields, columns.client_h, path, line_number),
        "client_h",
        path,
        line_number);
    sample.keys_hold = parse_replay_u64(
        replay_field(fields, columns.keys_hold_hex, path, line_number),
        "keys_hold_hex",
        path,
        line_number,
        0);
    sample.xinput_connected = parse_replay_bool(
        replay_field(fields, columns.xinput_connected, path, line_number),
        "xinput_connected",
        path,
        line_number);
    const std::uint64_t buttons = parse_replay_u64(
        replay_field(fields, columns.xinput_buttons_hex, path, line_number),
        "xinput_buttons_hex",
        path,
        line_number,
        0);
    if (buttons > 0xffffu) {
        std::ostringstream message;
        message << "native input replay xinput_buttons_hex is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    sample.xinput_buttons = static_cast<std::uint16_t>(buttons);
    sample.left_trigger = parse_replay_u8(
        replay_field(fields, columns.left_trigger, path, line_number),
        "left_trigger",
        path,
        line_number);
    sample.right_trigger = parse_replay_u8(
        replay_field(fields, columns.right_trigger, path, line_number),
        "right_trigger",
        path,
        line_number);
    sample.thumb_lx = parse_replay_i16(
        replay_field(fields, columns.thumb_lx, path, line_number),
        "thumb_lx",
        path,
        line_number);
    sample.thumb_ly = parse_replay_i16(
        replay_field(fields, columns.thumb_ly, path, line_number),
        "thumb_ly",
        path,
        line_number);
    sample.thumb_rx = parse_replay_i16(
        replay_field(fields, columns.thumb_rx, path, line_number),
        "thumb_rx",
        path,
        line_number);
    sample.thumb_ry = parse_replay_i16(
        replay_field(fields, columns.thumb_ry, path, line_number),
        "thumb_ry",
        path,
        line_number);
    return sample;
}

NativeInputReplayLog load_native_input_replay_log(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error(
            "failed to open native input replay log: " + path);
    }

    NativeInputReplayLog log{};
    log.path = path;
    bool saw_key_order = false;
    bool saw_header = false;
    NativeInputReplayColumns columns{};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line_number == 1 && line.size() >= 3u &&
            static_cast<unsigned char>(line[0]) == 0xefu &&
            static_cast<unsigned char>(line[1]) == 0xbbu &&
            static_cast<unsigned char>(line[2]) == 0xbfu) {
            line.erase(0, 3);
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split_script_fields(line, '\t');
        if (!fields.empty() && fields[0] == "meta") {
            if (fields.size() >= 3u && fields[1] == "key_order") {
                if (fields[2] != kNativeInputReplayKeyOrder) {
                    throw std::runtime_error(
                        "native input replay log key_order is unsupported: " +
                        path);
                }
                saw_key_order = true;
            }
            continue;
        }
        if (!saw_header) {
            columns = parse_replay_columns(fields, path);
            saw_header = true;
            continue;
        }
        NativeInputReplaySample sample =
            parse_replay_sample(fields, columns, path, line_number);
        if (!log.samples.empty() &&
            sample.elapsed_ms < log.samples.back().elapsed_ms) {
            std::ostringstream message;
            message << "native input replay log elapsed_ms regressed at "
                    << path << ':' << line_number;
            throw std::runtime_error(message.str());
        }
        log.samples.push_back(sample);
    }
    if (!saw_key_order) {
        throw std::runtime_error(
            "native input replay log is missing key_order metadata: " + path);
    }
    if (!saw_header) {
        throw std::runtime_error(
            "native input replay log is missing a header row: " + path);
    }
    if (log.samples.empty()) {
        throw std::runtime_error(
            "native input replay log has no samples: " + path);
    }
    return log;
}

const NativeInputReplayLog* active_native_input_replay_log() {
    const std::optional<std::string> path =
        read_env_string(nebula_switch::input_replay_log_name());
    static std::optional<std::string> s_loaded_path;
    static std::unique_ptr<NativeInputReplayLog> s_log;
    if (!path.has_value()) {
        s_loaded_path.reset();
        s_log.reset();
        return nullptr;
    }
    if (!s_loaded_path.has_value() || *s_loaded_path != *path) {
        NativeInputReplayLog loaded = load_native_input_replay_log(*path);
        s_log = std::make_unique<NativeInputReplayLog>(std::move(loaded));
        s_loaded_path = *path;
    }
    return s_log.get();
}

const NativeInputReplaySample* native_input_replay_sample_for_vi(
    std::uint64_t current_frame) {
    const NativeInputReplayLog* log = active_native_input_replay_log();
    if (log == nullptr) {
        return nullptr;
    }
    const auto seconds = current_frame / 60u;
    const auto fraction = (current_frame % 60u) * 1000u / 60u;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t target_ms = seconds > (maximum - fraction) / 1000u
        ? maximum : seconds * 1000u + fraction;
    const auto it = std::upper_bound(
        log->samples.begin(),
        log->samples.end(),
        target_ms,
        [](std::uint64_t value, const NativeInputReplaySample& sample) {
            return value < sample.elapsed_ms;
        });
    if (it == log->samples.begin()) {
        return &log->samples.front();
    }
    return &*(it - 1);
}

bool replay_key_held(const NativeInputReplaySample& sample, int index) {
    return (sample.keys_hold & (std::uint64_t{1} << index)) != 0;
}

bool replay_pad_held(
    const NativeInputReplaySample& sample,
    std::uint32_t mask) {
    return sample.xinput_connected && (sample.xinput_buttons & mask) != 0u;
}

float replay_key_axis(
    const NativeInputReplaySample& sample,
    int positive,
    int negative) {
    float value = 0.0f;
    if (sample.game_focused && replay_key_held(sample, positive)) value += 1.0f;
    if (sample.game_focused && replay_key_held(sample, negative)) value -= 1.0f;
    return value;
}

bool replay_xinput_has_input(const NativeInputReplaySample& sample) {
    if (!sample.xinput_connected) {
        return false;
    }
    return sample.xinput_buttons != 0u ||
           sample.left_trigger >= kXInputTriggerThreshold ||
           sample.right_trigger >= kXInputTriggerThreshold ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_lx, kReplayLeftDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ly, kReplayLeftDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_rx, kReplayPointerDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ry, kReplayPointerDeadzone)) >
               0.001f;
}

void apply_script_buttons_to_input(
    galaxy::input::PlayerInput& input,
    const NativeInputScriptOverlay& script) {
    galaxy::input::ButtonState& b = input.buttons;
    const galaxy::input::ButtonState& s = script.buttons;
    b.left = b.left || s.left;
    b.right = b.right || s.right;
    b.down = b.down || s.down;
    b.up = b.up || s.up;
    b.plus = b.plus || s.plus;
    b.minus = b.minus || s.minus;
    b.one = b.one || s.one;
    b.two = b.two || s.two;
    b.home = b.home || s.home;
    b.a = b.a || s.a;
    b.b = b.b || s.b;
    b.c = b.c || s.c;
    b.z = b.z || s.z;
}

std::string uppercase_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool frame_in_script_window(
    std::uint64_t frame,
    std::uint64_t start,
    std::uint64_t duration) {
    return duration != 0 && frame >= start && frame - start < duration;
}

void apply_script_button_token(
    NativeInputScriptOverlay& overlay,
    const std::string& token,
    const char* script_name) {
    const std::string name = uppercase_ascii(token);
    if (name == "A") {
        overlay.buttons.a = true;
    } else if (name == "B") {
        overlay.buttons.b = true;
    } else if (name == "PLUS" || name == "START") {
        overlay.buttons.plus = true;
    } else if (name == "MINUS" || name == "SELECT" || name == "BACK") {
        overlay.buttons.minus = true;
    } else if (name == "ONE" || name == "1") {
        overlay.buttons.one = true;
    } else if (name == "TWO" || name == "2") {
        overlay.buttons.two = true;
    } else if (name == "HOME") {
        overlay.buttons.home = true;
    } else if (name == "LEFT") {
        overlay.buttons.left = true;
    } else if (name == "RIGHT") {
        overlay.buttons.right = true;
    } else if (name == "UP") {
        overlay.buttons.up = true;
    } else if (name == "DOWN") {
        overlay.buttons.down = true;
    } else if (name == "C") {
        overlay.buttons.c = true;
    } else if (name == "Z") {
        overlay.buttons.z = true;
    } else if (name == "SHAKE") {
        overlay.shake = true;
    } else {
        throw std::runtime_error(
            std::string("unsupported native input script button '") + token +
            "' in " + script_name);
    }
}

void apply_script_buttons(
    NativeInputScriptOverlay& overlay,
    const std::string& text,
    const char* script_name) {
    const std::vector<std::string> tokens = split_script_fields(text, '+');
    if (tokens.empty()) {
        throw std::runtime_error(
            std::string("empty native input script button list in ") +
            script_name);
    }
    for (const std::string& token : tokens) {
        if (token.empty()) {
            throw std::runtime_error(
                std::string("empty native input script button in ") +
                script_name);
        }
        apply_script_button_token(overlay, token, script_name);
    }
}

void apply_button_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    const char* env_name,
    std::int64_t (*clock_ticks)() noexcept) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 3u) {
            throw std::runtime_error(
                std::string("invalid native input button script segment in ") +
                env_name);
        }
        const std::uint64_t start = parse_script_u64(fields[0], env_name);
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (frame_in_script_window(current_frame, start, duration)) {
            apply_script_buttons(overlay, fields[2], env_name);
            // A step's first sample is logged with its time, as a pointer jump is, for input-to-present measurements.
            static std::string logged;  // under the sample lock (host_main.cpp), as every sample is built
            const std::string step = fields[0] + ':' + fields[2];
            if (current_frame == start && logged != step) {
                logged = step;
                std::cerr << "[latency] buttons " << fields[2] << " frame=" << current_frame << " qpc=" << (clock_ticks ? clock_ticks() : 0)
                          << '\n';
            }
        }
    }
}

void apply_stick_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    const std::uint64_t anchor = 0;
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 4u) {
            throw std::runtime_error(
                std::string("invalid native input stick script segment in ") +
                env_name);
        }
        std::uint64_t start = 0u;
        if (!galaxy::input::resolve_native_input_script_start_vi(
                anchor, fields[0], start)) {
            throw std::runtime_error(
                std::string(
                    "invalid or overflowing native input stick script start in ") +
                env_name);
        }
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (frame_in_script_window(current_frame, start, duration)) {
            overlay.has_stick = true;
            overlay.stick_x = parse_script_float(fields[2], env_name);
            overlay.stick_y = parse_script_float(fields[3], env_name);
        }
    }
}

void apply_shake_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 2u) {
            throw std::runtime_error(
                std::string("invalid native input shake script segment in ") +
                env_name);
        }
        const std::uint64_t start = parse_script_u64(fields[0], env_name);
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (frame_in_script_window(current_frame, start, duration)) {
            overlay.shake = true;
        }
    }
}

void apply_pointer_pair_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    const char* x_env,
    const char* y_env,
    const char* frame_env,
    const char* duration_env = nullptr) {
    const std::optional<float> x = read_env_float_optional(x_env);
    const std::optional<float> y = read_env_float_optional(y_env);
    if (x.has_value() != y.has_value()) {
        throw std::runtime_error(
            std::string("native input pointer env pair is incomplete: ") +
            x_env + "/" + y_env);
    }
    if (!x.has_value()) {
        return;
    }
    if (frame_env != nullptr) {
        const std::optional<std::uint64_t> frame = read_env_u64_optional(frame_env);
        if (!frame.has_value()) {
            throw std::runtime_error(
                std::string("native input pointer waypoint is missing ") +
                frame_env);
        }
        if (current_frame < *frame) {
            return;
        }
        if (duration_env != nullptr) {
            const std::optional<std::uint64_t> duration =
                read_env_u64_optional(duration_env);
            if (duration.has_value() &&
                !frame_in_script_window(current_frame, *frame, *duration)) {
                return;
            }
        }
    }
    overlay.has_pointer = true;
    overlay.pointer_x = *x;
    overlay.pointer_y = *y;
}

struct PointerPathPoint {
    std::uint64_t frame;
    float x;
    float y;
};

// GALAXY_INPUT_POINTER_PATH "frame:x:y;frame:x:y;...": the pointer glides between those points (frames ascending), on screen
// from the first to the last. Parsed again only when the value changes.
void apply_pointer_path_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    std::int64_t (*clock_ticks)() noexcept) {
    const std::optional<std::string> script = read_env_string(nebula_switch::input_pointer_path_name());
    if (!script.has_value()) {
        return;
    }
    thread_local std::string parsed;
    thread_local std::vector<PointerPathPoint> points;
    if (*script != parsed) {
        std::vector<PointerPathPoint> next;
        for (const std::string& segment : split_script_fields(*script, ';')) {
            if (segment.empty()) {
                continue;
            }
            const std::vector<std::string> fields = split_script_fields(segment, ':');
            if (fields.size() != 3u) {
                throw std::runtime_error("invalid native input pointer path segment");
            }
            const PointerPathPoint point{parse_script_u64(fields[0], nebula_switch::input_pointer_path_name()),
                                         parse_script_float(fields[1], nebula_switch::input_pointer_path_name()),
                                         parse_script_float(fields[2], nebula_switch::input_pointer_path_name())};
            if (!next.empty() && point.frame < next.back().frame) {
                throw std::runtime_error("native input pointer path VIs must ascend");
            }
            next.push_back(point);
        }
        points = std::move(next);
        parsed = *script;
    }
    if (points.empty() || current_frame < points.front().frame || current_frame > points.back().frame) {
        return;
    }
    // The first point at or after this frame, and the one before it.
    const auto after = std::lower_bound(points.begin(), points.end(), current_frame,
                                        [](const PointerPathPoint& point, std::uint64_t frame) { return point.frame < frame; });
    float x = after->x;
    float y = after->y;
    if (after != points.begin() && after->frame != current_frame) {
        const PointerPathPoint& before = *(after - 1);
        const float t = static_cast<float>(current_frame - before.frame) / static_cast<float>(after->frame - before.frame);
        x = before.x + (after->x - before.x) * t;
        y = before.y + (after->y - before.y) * t;
    }
    overlay.has_pointer = true;
    overlay.pointer_x = std::clamp(x, -1.0f, 1.0f);
    overlay.pointer_y = std::clamp(y, -1.0f, 1.0f);
    // A jump (a quarter of the picture or more in one step) is logged with its time: the first sample that carries it
    // is built now, for input-to-present measurements against the present snapshots' times.
    static float last_x = x;  // under the sample lock, as every sample is built
    static float last_y = y;
    if (std::fabs(x - last_x) + std::fabs(y - last_y) >= 0.5f) {
        std::cerr << "[latency] pointer jump frame=" << current_frame << " x=" << x << " y=" << y << " qpc=" << (clock_ticks ? clock_ticks() : 0)
                  << '\n';
    }
    last_x = x;
    last_y = y;
}

void apply_pointer_script_envs(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_frame,
    std::int64_t (*clock_ticks)() noexcept) {
    apply_pointer_pair_env(
        overlay,
        current_frame,
        nebula_switch::input_pointer_x_name(),
        nebula_switch::input_pointer_y_name(),
        nullptr);
    // A second position, read at every sample, so a test can change it while the game runs.
    apply_pointer_pair_env(
        overlay, current_frame, nebula_switch::input_pointer2_x_name(), nebula_switch::input_pointer2_y_name(),
        nebula_switch::input_pointer2_frame_name(), nebula_switch::input_pointer2_duration_name());
    apply_pointer_path_env(overlay, current_frame, clock_ticks);
}

NativeInputScriptOverlay build_native_input_script_overlay(std::uint64_t current_frame, const ScriptServices& services) {
    NativeInputScriptOverlay overlay{};
    apply_button_script_env(overlay, current_frame, nebula_switch::input_autopress_script_name(), services.clock_ticks);
    apply_stick_script_env(overlay, current_frame, nebula_switch::input_stick_script_name());
    apply_shake_script_env(
        overlay, current_frame, nebula_switch::input_shake_script_name());
    apply_pointer_script_envs(overlay, current_frame, services.clock_ticks);
    return overlay;
}

// GALAXY_INPUT_REPLAY_LOG: one recorded sample of window, keyboard, mouse and XInput state as the player's input.
galaxy::input::PlayerInput build_replayed_input(
    std::uint64_t current_frame,
    const NativeInputReplaySample& replay,
    const NativeInputScriptOverlay& script,
    galaxy::RuntimeInputMode input_mode,
    galaxy::input::tilt::Section tilt_section,
    const galaxy::input::NativeMousePointerProjection& replay_pointer,
    const void* log_identity) {
    galaxy::input::PlayerInput snapshot{};
    const bool replay_controller_active = replay_xinput_has_input(replay);
    const bool force_controller =
        input_mode == galaxy::RuntimeInputMode::Controller;
    const bool use_controller = force_controller ||
        (input_mode == galaxy::RuntimeInputMode::Auto &&
         replay_controller_active);
    const bool allow_absolute_pointer =
        input_mode != galaxy::RuntimeInputMode::RealWiimote;

    const auto pad = [&](std::uint32_t mask) {
        return replay_pad_held(replay, mask);
    };
    const auto key = [&](int index) {
        return replay.game_focused && replay_key_held(replay, index);
    };

    snapshot.buttons.left =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kDpadLeftBit)) : key(7);
    snapshot.buttons.right =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kDpadRightBit)) : key(9);
    snapshot.buttons.down =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kDpadDownBit)) : key(10);
    snapshot.buttons.up =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kDpadUpBit)) : key(8);
    snapshot.buttons.plus =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kStartBit)) : false;
    snapshot.buttons.minus =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kBackBit)) : false;
    snapshot.buttons.one = use_controller ? false : key(11);
    snapshot.buttons.two = use_controller ? false : key(12);
    snapshot.buttons.home = false;
    snapshot.buttons.a =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kSouthBit)) : (key(0) || key(2) || key(3));
    snapshot.buttons.b =
        use_controller
            ? (pad(galaxy::input::pad::bit(galaxy::input::pad::kEastBit)) ||
               replay.right_trigger >= kXInputTriggerThreshold)
            : (key(1) || key(5) || key(6));

    float stick_x =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_lx, kReplayLeftDeadzone)
            : replay_key_axis(replay, 16, 13);
    float stick_y =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_ly, kReplayLeftDeadzone)
            : replay_key_axis(replay, 25, 24);
    if (script.has_stick) {
        stick_x = script.stick_x;
        stick_y = script.stick_y;
    }
    const galaxy::input::StickValue nunchuk_stick = galaxy::input::nunchuk_stick(stick_x, stick_y);
    snapshot.stick_x = nunchuk_stick.x;
    snapshot.stick_y = nunchuk_stick.y;
    snapshot.buttons.c =
        use_controller ? pad(galaxy::input::pad::bit(galaxy::input::pad::kLeftStickBit)) : key(15);
    snapshot.buttons.z =
        use_controller
            ? (pad(galaxy::input::pad::bit(galaxy::input::pad::kLeftShoulderBit)) ||
               replay.left_trigger >= kXInputTriggerThreshold)
            : (key(26) || key(27) || key(28) || key(29));

    apply_script_buttons_to_input(snapshot, script);

    // The live pointer model (pointer_control.h) on the replay timeline: the
    // last moved source owns the pointer and dt comes from the frame clock, so a
    // recorded stick moves the pointer at the same speed at any sample rate.
    static float s_replay_pointer_x = 0.0f;
    static float s_replay_pointer_y = 0.0f;
    static galaxy::input::PointerOwner s_replay_owner{};
    static std::uint64_t s_replay_pointer_vi = 0u;
    static int s_replay_mouse_x = -1;
    static int s_replay_mouse_y = -1;
    static galaxy::input::RecenterHold s_replay_recenter{};
    // Each replay log starts from a fresh pointer.
    static const void* s_replay_log_identity = nullptr;
    if (const void* log = log_identity; log != s_replay_log_identity) {
        s_replay_log_identity = log;
        s_replay_pointer_x = s_replay_pointer_y = 0.0f;
        s_replay_owner = {};
        s_replay_pointer_vi = 0u;
        s_replay_mouse_x = s_replay_mouse_y = -1;
        s_replay_recenter = {};
    }
    const float dt = s_replay_pointer_vi == 0u || current_frame <= s_replay_pointer_vi
        ? 0.0f
        : std::min(static_cast<float>(current_frame - s_replay_pointer_vi) / 60.0f, 0.05f);
    s_replay_pointer_vi = current_frame;
    const std::uint64_t now_us = current_frame * 1'000'000ull / 60u;
    const bool keys_allowed = !force_controller;
    const bool pad_allowed = replay.xinput_connected &&
                             input_mode != galaxy::RuntimeInputMode::KeyboardMouse &&
                             input_mode != galaxy::RuntimeInputMode::RealWiimote;
    bool pointer_active = false;
    // A recorded mouse is replayed in every mode, as recorded.
    const bool mouse_visible = allow_absolute_pointer && replay_pointer.visible;
    const bool mouse_moved = mouse_visible &&
        (replay.mouse_client_x != s_replay_mouse_x || replay.mouse_client_y != s_replay_mouse_y);
    s_replay_mouse_x = replay.mouse_client_x;
    s_replay_mouse_y = replay.mouse_client_y;
    const galaxy::input::StickVector pointer_axes = pad_allowed
        ? galaxy::input::apply_radial_deadzone(
              galaxy::input::normalize_xinput_axis(replay.thumb_rx, 0),
              galaxy::input::normalize_xinput_axis(replay.thumb_ry, 0),
              static_cast<float>(kReplayPointerDeadzone) / 32767.0f)
        : galaxy::input::StickVector{};
    const galaxy::input::StickVector stick_velocity =
        galaxy::input::stick_pointer_velocity(pointer_axes, kReplayStickPointerSpeed);
    const float key_x = keys_allowed ? replay_key_axis(replay, 21, 19) : 0.0f;
    const float key_y = keys_allowed ? replay_key_axis(replay, 18, 20) : 0.0f;
    galaxy::input::PointerActivity activity{};
    activity.mouse = mouse_moved;
    activity.keys = key_x != 0.0f || key_y != 0.0f;
    activity.stick = stick_velocity.x != 0.0f || stick_velocity.y != 0.0f;
    const bool controller_recenter =
        pad_allowed && replay_pad_held(replay, galaxy::input::pad::bit(galaxy::input::pad::kRightStickBit));
    const bool recenter_held = (keys_allowed && key(23)) || controller_recenter;
    const galaxy::input::RecenterHold::Output recenter = s_replay_recenter.update(recenter_held);
    if (recenter.pressed) {
        s_replay_owner.owner = controller_recenter ? galaxy::input::PointerSource::Stick
                                                   : galaxy::input::PointerSource::Keys;
    }
    if (recenter.pinned) {
        s_replay_owner.last_active_us = now_us;
    }
    switch (recenter.pinned ? s_replay_owner.owner : s_replay_owner.update(activity, now_us)) {
    case galaxy::input::PointerSource::Mouse:
        if (mouse_visible) {
            s_replay_pointer_x = replay_pointer.x;
            s_replay_pointer_y = replay_pointer.y;
            pointer_active = true;
        }
        break;
    case galaxy::input::PointerSource::Keys:
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + key_x * kReplayKeyboardPointerSpeed * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - key_y * kReplayKeyboardPointerSpeed * dt, -1.0f, 1.0f);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Stick:
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + stick_velocity.x * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - stick_velocity.y * dt, -1.0f, 1.0f);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Gyro:
    case galaxy::input::PointerSource::None:
        if (pad_allowed) {
            pointer_active = true;
        }
        break;
    }
    if (recenter.pinned) {
        // Held recenter: the pointer stays at the centre (pointer_control.h).
        s_replay_pointer_x = 0.0f;
        s_replay_pointer_y = 0.0f;
        pointer_active = true;
    }
    if (script.has_pointer) {
        s_replay_pointer_x = script.pointer_x;
        s_replay_pointer_y = script.pointer_y;
        pointer_active = true;
    }
    galaxy::input::set_pointer(snapshot, s_replay_pointer_x, s_replay_pointer_y, pointer_active);

    // Tilt sections steer with the replayed stick (tilt.h).
    static galaxy::input::tilt::Tilt s_replay_tilt{};
    apply_tilt_accel(snapshot, s_replay_tilt, tilt_section, nullptr, stick_x, stick_y, recenter.pressed);

    const bool shake_pressed =
        script.shake ||
        (use_controller
             ? (pad(galaxy::input::pad::bit(galaxy::input::pad::kWestBit)) || pad(galaxy::input::pad::bit(galaxy::input::pad::kNorthBit)) ||
                pad(galaxy::input::pad::bit(galaxy::input::pad::kRightShoulderBit)))
             : (key(17) || key(22)));
    snapshot.shake = shake_pressed;
    return snapshot;
}


}  // namespace galaxy::host::controls

#endif
