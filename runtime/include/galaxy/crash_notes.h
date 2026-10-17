// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What a crash log needs that is known before the crash: a few fixed lines (build, CPU, GPU) and
// the last notable events (a skipped graphics command, a recovered error). Writing costs a copy
// into a static buffer; nothing is read until the process fails (native_runtime.cpp's crash log).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace galaxy::crash_notes {

enum class Fixed : std::size_t { Build, Cpu, Gpu, Count };

inline constexpr std::size_t kLineBytes = 320;
inline constexpr std::size_t kRecentCount = 16;

using Line = std::array<char, kLineBytes>;

inline std::array<Line, static_cast<std::size_t>(Fixed::Count)> g_fixed{};
inline std::array<Line, kRecentCount> g_recent{};
inline std::atomic<std::uint32_t> g_recent_written{0};

inline void copy_line(Line& line, const char* text) noexcept {
    if (text == nullptr) text = "";
    std::size_t length = std::strlen(text);
    while (length != 0u && (text[length - 1u] == '\n' || text[length - 1u] == '\r')) --length;
    length = length < kLineBytes - 1u ? length : kLineBytes - 1u;
    std::memcpy(line.data(), text, length);
    line[length] = '\0';
}

// Set once, at startup, from the thread that owns the value.
inline void set(Fixed which, const char* text) noexcept {
    copy_line(g_fixed[static_cast<std::size_t>(which)], text);
}

// A notable event. The newest kRecentCount are kept.
inline void note(const char* text) noexcept {
    const std::uint32_t index = g_recent_written.fetch_add(1u, std::memory_order_relaxed);
    copy_line(g_recent[index % kRecentCount], text);
}

// The kept events, oldest first.
template <class Visit>
void for_each_recent(Visit visit) {
    const std::uint32_t written = g_recent_written.load(std::memory_order_relaxed);
    const std::uint32_t kept = written < kRecentCount ? written : static_cast<std::uint32_t>(kRecentCount);
    for (std::uint32_t i = written - kept; i != written; ++i) visit(g_recent[i % kRecentCount].data());
}

}  // namespace galaxy::crash_notes
