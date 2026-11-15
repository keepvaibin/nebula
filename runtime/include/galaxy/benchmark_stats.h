// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark's arithmetic (benchmark.h), kept apart from the run so it is tested on its own: frame-time statistics,
// the cause given to a slow frame, and the frame-list file that keeps every frame time of a run for the results
// screen's graph.
//
// The statistics follow the frame-analysis tools' definitions (PresentMon, CapFrameX, OCAT):
//   average FPS    frames / the time they took (never the average of per-frame FPS)
//   percentiles    nearest rank over the frame times
//   1% low         1000 / the average time of the slowest 1% of frames (at least one frame); 0.1% low likewise
//   stutter        a frame that took longer than kStutterMs

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::bench {

// The result file's schema (benchmark.cpp write_result; benchmark_results.h, benchmark_recommend.h and diag_report.cpp
// read it).
inline constexpr unsigned kResultSchema = 2u;

inline constexpr double kStutterMs = 25.0;

struct FrameStats {
    std::size_t frames = 0u;      // frame times
    double seconds = 0.0;         // their sum
    double average_fps = 0.0;
    double average_ms = 0.0;
    double p50_ms = 0.0, p95_ms = 0.0, p99_ms = 0.0, p999_ms = 0.0, worst_ms = 0.0;
    double low_10_fps = 0.0;  // the FPS over the slowest 10% of frames, 1% and 0.1% likewise
    double low_1_fps = 0.0;
    double low_01_fps = 0.0;
    std::uint32_t stutters = 0u;
};
// Frame times in milliseconds (any order; zero and negative ones left out).
[[nodiscard]] FrameStats frame_stats(const std::vector<double>& frame_ms);

// The nearest-rank percentile `p` (0 to 1) of `sorted` (ascending); 0 when empty.
[[nodiscard]] double percentile(const std::vector<double>& sorted, double p) noexcept;

// A per-frame cost's average, 99th percentile and largest (GPU time, a thread's CPU time).
struct Spread {
    double average = 0.0, p99 = 0.0, worst = 0.0;
    std::size_t count = 0u;
};
[[nodiscard]] Spread spread(std::vector<double> values);

// What made a slow frame slow. Work that came with the frame names it first (a pipeline built, the disc read, a
// texture made), then the busiest of the GPU and the two threads when it took at least half the frame; Waiting when
// none did (the system or the driver held the frame).
enum class Cause : unsigned { Pipeline, DiscRead, Texture, Gpu, RenderThread, GameThread, Waiting };
[[nodiscard]] const char* cause_name(Cause cause) noexcept;
struct FrameFacts {
    double frame_ms = 0.0;
    double gpu_ms = -1.0;     // < 0: not known
    double render_ms = -1.0;
    double game_ms = -1.0;
    std::uint32_t pipelines = 0u;    // built during the frame
    std::uint64_t read_bytes = 0u;   // read from the disc during the frame
    std::uint32_t textures = 0u;     // texture images made or rewritten during the frame
};
[[nodiscard]] Cause cause_of(const FrameFacts& facts) noexcept;

// The frame-list file beside a result (<result>.frames): each measured stage's frame times. Little-endian:
//   "NBFR", u32 version 1, u32 lists; per list: u8 part (0 the player's settings, 1 the standard), u8 stage name
//   length, its bytes (printable ASCII), u32 frames, then each frame time as u16 in 10 microsecond units (655.35 ms
//   at most: a longer frame is stored as that).
inline constexpr double kFrameUnitMs = 0.01;
inline constexpr std::uint32_t kMostFramesPerList = 1u << 20;
struct FrameList {
    unsigned part = 0u;
    std::string stage;
    std::vector<double> frame_ms;
};
[[nodiscard]] std::string encode_frame_lists(const std::vector<FrameList>& lists);
// False (and `lists` empty) for anything that is not such a file, whole.
[[nodiscard]] bool decode_frame_lists(std::string_view bytes, std::vector<FrameList>& lists);

}  // namespace galaxy::bench
