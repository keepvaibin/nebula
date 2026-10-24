// Frame counts published once a second in shared memory for the monitoring tools (tools/telemetry).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace galaxy::telemetry {

// A local-session mapping Local\Nebula.FrameTelemetry.v1.<pid> of 32 little-endian uint64 words, guarded by the
// mutex <mapping name>.Guard. Readers take the mutex with a zero timeout and check the process creation FILETIME, the
// schema, the active flag and the QPC stamp's age. Words: 0 magic, 1 schema, 2 size, 3 pid, 4 creation FILETIME,
// 5 serial, 6 QPC stamp, 7 QPC frequency, 8 active, 9..17 Counts, 18 steady-clock ns, 19 publisher thread CPU time
// (100 ns), 20 longest publication (us), 21..23 the startup width, height and internal scale (not live), 24 publisher
// thread cycles. The publisher runs on its own thread, never the render thread.
inline constexpr std::uint64_t kMagic = 0x47414c5854463031ull;
inline constexpr std::size_t kWords = 32;
using Packet = std::array<std::uint64_t, kWords>;
// Frame-buffer copies, first Present returns of a new game-timed serial, game-timed returns, repeats, the last
// serial, stamp and address, and the steady-clock ns of the last production and of its first return.
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
