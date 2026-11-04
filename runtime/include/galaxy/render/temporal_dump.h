// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// GALAXY_TEST_MOTION_DUMP=<dir>:<first>:<every>: the frames whose colour and motion inputs a test reads: two in a row from
// <first>, every <every> frames (the motion of the second maps it onto the first). Files <dir>/<name>_<frame>_<w>x<h>.bin,
// rows packed.

#include "galaxy/run_options.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace galaxy::render::temporal_dump {

struct Plan {
    std::string directory;
    std::uint64_t first = 0u;
    std::uint64_t every = 0u;
    [[nodiscard]] bool wanted(std::uint64_t frame) const noexcept {
        return every != 0u && frame >= first && (frame - first) % every < 2u;
    }
};

[[nodiscard]] inline Plan read_plan() {
    Plan plan;
    const char* const value = ::galaxy::run_options().test_motion_dump;
    if (value == nullptr) {
        return plan;
    }
    const std::string text(value);
    const std::size_t last = text.rfind(':');
    const std::size_t middle = last != std::string::npos && last > 0u ? text.rfind(':', last - 1u) : std::string::npos;
    if (middle != std::string::npos) {
        plan.directory = text.substr(0u, middle);
        plan.first = std::strtoull(text.c_str() + middle + 1u, nullptr, 10);
        plan.every = std::strtoull(text.c_str() + last + 1u, nullptr, 10);
    }
    return plan;
}

inline void write(const Plan& plan, const char* name, std::uint64_t frame, unsigned width, unsigned height, unsigned pixel_bytes,
                  const void* data, std::size_t row_pitch) {
    std::ofstream out(plan.directory + "/" + name + "_" + std::to_string(frame) + "_" + std::to_string(width) + "x" + std::to_string(height) +
                          ".bin",
                      std::ios::binary);
    const auto* bytes = static_cast<const char*>(data);
    for (unsigned y = 0u; y < height; ++y) {
        out.write(bytes + y * row_pitch, static_cast<std::streamsize>(width * pixel_bytes));
    }
}

}  // namespace galaxy::render::temporal_dump
