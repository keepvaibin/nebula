#pragma once
// The timing stamp of a pointer response.
#include <cstdint>

namespace galaxy::render {
struct PointerResponseStamp {
    std::uint64_t serial{}, generation{}, selected_ns{}, processed_ns{}, drawn_ns{};
    // GetTickCount64 milliseconds: the age of the pointer sample, not input latency.
    std::uint64_t acquisition_age_ms{};
    bool acquisition_age_known{};
};
}
