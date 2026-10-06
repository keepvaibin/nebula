// SPDX-License-Identifier: GPL-3.0-only
// Diagnostic host-input lease. No guest-memory or Windows input mutation.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace galaxy::input {

struct DiagnosticStickCommand {
    std::uint64_t sequence{};
    std::uint64_t duration_ms{};
    float x{};
    float y{};
    bool a{};
};

// One atomic file publication is "sequence:duration_ms:x:y[:a]", where the
// optional A level is exactly 0 or 1 and shares the same finite lease. An optional
// final LF/CRLF is accepted; extra lines/fields, spaces, signed integers,
// nonfinite floats, out-of-range axes, and durations above 2s are rejected.
[[nodiscard]] inline std::optional<DiagnosticStickCommand>
parse_diagnostic_stick_command(std::string_view text) noexcept {
    if (text.size() > 128u) return std::nullopt;
    if (!text.empty() && text.back() == '\n') {
        text.remove_suffix(1u);
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1u);
    }
    std::array<std::string_view, 4> fields{};
    bool a = false;
    const auto last_separator = text.rfind(':');
    if (std::count(text.begin(), text.end(), ':') == 4u) {
        const auto level = text.substr(last_separator + 1u);
        if (level != "0" && level != "1") return std::nullopt;
        // Retain the ordinary four-field parser and every validation below.
        a = level == "1";
        text = text.substr(0u, last_separator);
    }
    for (std::size_t index = 0; index != fields.size(); ++index) {
        const auto separator = text.find(':');
        if ((index + 1u != fields.size() && separator == text.npos) ||
            (index + 1u == fields.size() && separator != text.npos)) {
            return std::nullopt;
        }
        fields[index] = text.substr(0u, separator);
        if (fields[index].empty()) return std::nullopt;
        if (separator != text.npos) text.remove_prefix(separator + 1u);
    }
    DiagnosticStickCommand command{};
    command.a = a;
    const auto decimal = [](std::string_view field, std::uint64_t& value) {
        for (const char ch : field) {
            if (ch < '0' || ch > '9') return false;
        }
        const auto result = std::from_chars(
            field.data(), field.data() + field.size(), value);
        return result.ec == std::errc{} &&
               result.ptr == field.data() + field.size();
    };
    const auto axis = [](std::string_view field, float& value) {
        const auto result = std::from_chars(
            field.data(), field.data() + field.size(), value,
            std::chars_format::general);
        return result.ec == std::errc{} &&
               result.ptr == field.data() + field.size() &&
               std::isfinite(value) && value >= -1.0f && value <= 1.0f;
    };
    if (!decimal(fields[0], command.sequence) || command.sequence == 0u ||
        !decimal(fields[1], command.duration_ms) || command.duration_ms > 2000u ||
        !axis(fields[2], command.x) || !axis(fields[3], command.y)) {
        return std::nullopt;
    }
    return command;
}

// Owned only by the HID snapshot consumer thread. Clock samples are
// monotonic host milliseconds, not VI counts or gameplay/simulation time.
class DiagnosticStickLease {
public:
    void cancel() noexcept { command_.duration_ms = 0u; }

    void observe(std::string_view text, std::uint64_t now_ms) noexcept {
        const auto parsed = parse_diagnostic_stick_command(text);
        if (!parsed.has_value()) {
            cancel();
            return;
        }
        // A terminal file remains on disk. It cannot extend an existing
        // deadline, resurrect an expired/cancelled lease, or rewind identity.
        if (parsed->sequence <= last_sequence_) return;
        last_sequence_ = parsed->sequence;
        command_ = *parsed;
        first_observed_ms_ = now_ms;
    }

    [[nodiscard]] std::optional<DiagnosticStickCommand>
    active(std::uint64_t now_ms) const noexcept {
        if (command_.duration_ms == 0u || now_ms < first_observed_ms_ ||
            now_ms - first_observed_ms_ >= command_.duration_ms) {
            return std::nullopt;
        }
        return command_;
    }

private:
    std::uint64_t last_sequence_{};
    std::uint64_t first_observed_ms_{};
    DiagnosticStickCommand command_{};
};

} // namespace galaxy::input
