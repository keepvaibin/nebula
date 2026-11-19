// SPDX-License-Identifier: GPL-3.0-only
// The GPU's time by pass, one way for every renderer (P-022): the passes they mark, the time each frame's marks give
// each pass, the settings page's running averages, the logs' window and the benchmark's hook. A renderer keeps only
// its API's part: where it marks and how it reads its timestamps back.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace galaxy::render {

struct GpuPassTime;

// The passes a renderer can mark; each marks those it has. Count is no pass: a mark of it ends the one before.
enum class GpuPass : std::uint8_t {
    Scene, Resolve, FramePass, DisplayCopy, Bloom, SceneAa, SceneAo, Upscale, Present, Camera,
    AoDistance, AoOcclusion, AoDenoise, AoUpsample, AoApply,
    Uploads,  // texture uploads ahead of the frame (Metal times them in a command buffer of their own)
    Count
};
inline constexpr std::size_t kGpuPassCount = static_cast<std::size_t>(GpuPass::Count);

// The settings page's names ("frame read") and the logs' ("frame-read"), by pass.
extern const char* const kGpuPassPageNames[kGpuPassCount];
extern const char* const kGpuPassLogNames[kGpuPassCount];

using GpuPassMs = std::array<double, kGpuPassCount>;

// One frame's time by pass from its marks: interval i, from ticks[i] to ticks[i + 1], is the work that fills it, so it
// goes to marks[i], the pass that began at its first timestamp (a scope's end marks the outer pass, so work after it is
// the outer pass's). `ticks` holds interval_count + 1 timestamps; an interval that goes backwards is skipped, and a
// mark of Count bills nothing.
GpuPassMs gpu_pass_ms(const std::uint64_t* ticks, const GpuPass* marks, unsigned interval_count, double ms_per_tick) noexcept;

class GpuPassTimes {
public:
    // Whether the frame about to begin times its passes (the diagnostics' gpu-passes or the settings page). Timing
    // turned off clears the page's averages, so the page shows none until it times again.
    bool begin_frame(bool timed) noexcept;
    [[nodiscard]] bool timed() const noexcept { return timed_; }

    // A timed frame's passes, read back once its fence passed (render thread): each pass the frame ran moves its page
    // average a tenth of the way (the first is taken as it is), the window adds them, and the benchmark sees them.
    // `game_frame`: the frame drew the scene for a game step (not an in-between picture, not a frame without the scene).
    void add_frame(const GpuPassMs& frame_ms, bool game_frame) noexcept;

    // The page's averages, the passes with one (any thread).
    std::size_t page_times(GpuPassTime* out, std::size_t capacity) const noexcept;

    // The window's milliseconds a timed frame for each pass that ran in it, then the window starts again; "" when no
    // frame was timed. As a list, " <log name>=<ms>" each; as the present statistics' field,
    // " gpu-pass-ms=<log name>:<ms>,...".
    std::string take_window_list();
    std::string take_window_field();
    [[nodiscard]] unsigned window_frames() const noexcept { return window_frames_; }

private:
    std::string take_window(bool field);

    std::array<std::atomic<float>, kGpuPassCount> average_{};
    GpuPassMs window_{};
    unsigned window_frames_ = 0u;
    bool timed_ = false;
};

}  // namespace galaxy::render
