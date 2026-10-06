#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace galaxy::telemetry {

// Windows local-session IPC, little endian, 32 uint64 words (256 bytes).
// Mapping: Local\Nebula.FrameTelemetry.v1.<PID>; mutex: same + .Guard.
// Readers take the mutex with a zero timeout and validate process creation
// FILETIME, schema, active flag and QPC freshness. No render-thread IPC writes.
// 19=worker CPU100ns (OS granularity),20=max atomic sample us,24=worker cycles.
// Display fields21..23 are startup settings, not live resize dimensions.
inline constexpr std::uint64_t kMagic = 0x47414c5854463031ull;
inline constexpr std::size_t kWords = 32;
using Packet = std::array<std::uint64_t, kWords>;
// copies, first game-timed serial Present returns, game-timed returns, repeats,
// last serial/stamp/address, last production/first-return steady nanoseconds.
using Counts = std::array<std::uint64_t, 9>;

class Publisher {
public:
    Publisher(std::function<Counts()> sample, std::uint64_t initial_width,
              std::uint64_t initial_height, std::uint64_t internal_scale) noexcept;
    ~Publisher();
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace galaxy::telemetry
