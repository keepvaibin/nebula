// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark's results after a run (benchmark.h writes them): where they are kept, a result read back for the
// results screen, and the frame-time graph drawn from a result's frame lists (benchmark_stats.h).
//
// Results live in the data folder's diagnostics/benchmarks folder as benchmark-<UTC>.json with
// benchmark-<UTC>.frames beside it (and benchmark-<UTC>.json.sent once sent, diag_report.h); the newest kKeptResults
// are kept and older ones deleted.

#include "galaxy/benchmark_stats.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace galaxy::bench {

inline constexpr std::size_t kKeptResults = 5u;

// The results folder; empty when there is no data folder.
[[nodiscard]] std::filesystem::path results_folder();
// A new result's file in `folder`, named by the UTC time now.
[[nodiscard]] std::filesystem::path new_result_file(const std::filesystem::path& folder);
// The frame lists beside a result.
[[nodiscard]] std::filesystem::path frames_file(const std::filesystem::path& result);
// The results in `folder`, newest first.
[[nodiscard]] std::vector<std::filesystem::path> list_results(const std::filesystem::path& folder);
// Deletes all but the newest `count` results in `folder`, each with its frame lists and sent marker.
void keep_newest(const std::filesystem::path& folder, std::size_t count);

// The settings a run measured with, as the results screen names them.
struct SettingsView {
    unsigned internal_height = 0u;  // 0: the display's
    unsigned upscaler = 0u;
    unsigned msaa_samples = 1u;
    unsigned aa = 0u;               // 0 off, 1 SMAA
    unsigned smaa_quality = 0u;
    unsigned bloom = 0u;
    unsigned aspect_mode = 0u;
    bool ambient_occlusion = false;
    bool enhanced_lighting = false;
};

struct StageView {
    std::string stage;  // the game's stage name (CosmosGardenGalaxy)
    double average_fps = 0.0, low_10_fps = 0.0, low_1_fps = 0.0, p99_ms = 0.0;
    double steps_per_s = 0.0, steps_low_1 = 0.0;  // the game's own steps a second and their 1% low
    double gpu_ms = -1.0, game_ms = -1.0, render_ms = -1.0;  // per frame, < 0 when not known
    double load_ms = 0.0;
    std::uint32_t stutters = 0u;
    bool settings_held = true;
};

// The run's part at the player's settings.
struct PartView {
    bool present = false;
    double average_fps = 0.0, low_10_fps = 0.0, low_1_fps = 0.0, low_01_fps = 0.0, average_ms = 0.0, p99_ms = 0.0;
    double steps_per_s = 0.0, steps_low_1 = 0.0;  // the game's own steps a second and their 1% low
    std::uint32_t stutters = 0u;
    std::string limiter;            // "gpu", "render thread", "game thread", "display", "game frames", "waiting"
    bool display_limited = false;   // the frame rate stood at a rate it cannot pass (benchmark.cpp ceiling_of)
    std::vector<StageView> stages;
    std::vector<std::pair<std::string, double>> gpu_passes;  // each pass's average milliseconds, over the stages
    double game_ms = -1.0, render_ms = -1.0, audio_ms = -1.0;
    SettingsView settings;
    unsigned output_width = 0u, output_height = 0u;
};

struct RecommendationRow {
    std::string key;  // the result file's key (internal_height, msaa_samples, ...)
    unsigned current = 0u, recommended = 0u;
};

struct ResultView {
    std::string created_utc;  // 2026-11-15T07:00:00Z
    std::string status;       // complete, aborted, failed
    std::string failure;
    std::string backend;      // d3d12, d3d11, vulkan, metal
    std::string gpu, driver, cpu, os;
    unsigned ram_mb = 0u, gpu_memory_peak_mb = 0u, logical_cores = 0u;
    unsigned output_width = 0u, output_height = 0u;  // the display's pixels
    double refresh_hz = 0.0;
    std::string verdict;      // fits, cpu_bound, below_target, none
    double spare_percent = 0.0;
    PartView yours;
    std::vector<RecommendationRow> recommendation;
    bool thermal_judged = false;
    double thermal_slowdown = 1.0;  // the GPU's time at the end over its time at the start
};
// The result in `file` (schema 3); false for a file that cannot be read or is not one.
[[nodiscard]] bool read_result(const std::filesystem::path& file, ResultView& out);
// The same from the file's text.
[[nodiscard]] bool parse_result(std::string_view text, ResultView& out);

// An RGBA8 image, rows top first, straight alpha.
struct Image {
    unsigned width = 0u, height = 0u;
    std::vector<std::uint8_t> rgba;
};
// The frame rate graph of one part's stages (FrameList::part), side by side in the run's order, in the settings menu's
// palette: each column's frames per second (its frames over their time) as a line, the band under it down to the
// column's slowest frame, red where that frame was over kStutterMs, the 60 and 30 FPS lines, and a line where each
// stage after the first starts. The picture holds no text: the game writes the scale's values, names the two lines and
// numbers the stages over it in its own font and language, from the numbers here. 0 FPS is at the plot's foot
// (`bottom`) and `top_fps` at its top (`top`); the scale's lines are every `step_fps`. The image is empty when the part
// has no frames or the size is outside kGraphMin* to kGraphMaxSide.
inline constexpr unsigned kGraphMinWidth = 160u;
inline constexpr unsigned kGraphMinHeight = 60u;
inline constexpr unsigned kGraphMaxSide = 1024u;  // the menu's largest picture (NebulaSettingsMenu cModPictureSide)
struct FrameGraph {
    Image image;
    double top_fps = 0.0;
    double step_fps = 0.0;
    unsigned left = 0u, top = 0u, right = 0u, bottom = 0u;  // the plot inside the picture, in texels
    std::vector<unsigned> stage_starts;  // the column where each stage starts, in texels, the first at `left`
};
[[nodiscard]] FrameGraph draw_frame_graph(const std::vector<FrameList>& lists, unsigned part, unsigned width, unsigned height);
// The graph's scale in frames per second: the step between its lines for a top (the smallest of 1, 2, 2.5 and 5 times
// a power of ten that divides it into at most six), and the top for the fastest column (a whole number of steps at or
// over it, and over kGraphLeastTopFps so that the 60 FPS line has room).
inline constexpr double kGraphLeastTopFps = 75.0;
[[nodiscard]] double graph_step(double top) noexcept;
[[nodiscard]] double graph_top(double fps) noexcept;

}  // namespace galaxy::bench
