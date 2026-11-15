// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark's results window (nebula_game_api.h NEBULA_SETTINGS_STATUS_BENCHMARK_RESULTS): its four pages as the
// host hands them to the game, which draws each page's table in its own font and language.
//
// A page's table is lines split by '\n' and cells by '\t'. A cell starts with any of these, in this order: '*' the
// shown run's line (the game's blue), '#' a heading (the help's grey), '<' left aligned (column 0 always is), then '@'
// a stage the game names in its own words or '"' a name shown as it is. Any other cell is host capitals, translated as
// the rows are (numbers kept: "143 FPS" is S.#_FPS). A cell followed by an empty cell, or by the line's end before its
// group's last column, spans on over them, left aligned; it widens the last column it covers only when it is wider
// than all it covers. A line of four cells whose third is left aligned is two groups side by side (columns 0-1 and
// 2-3); a narrow window shows the second group's lines under the first's.

#include "galaxy/benchmark_results.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace galaxy::ui {

struct BenchmarkPage {
    std::string title;   // host capitals
    std::string table;
    bool graph = false;  // the frame-time graph over the table (the summary)
};

// The frame-time graph's picture size the host draws for the summary (bench::draw_frame_graph).
inline constexpr unsigned kBenchmarkGraphWidth = 1024u;
inline constexpr unsigned kBenchmarkGraphHeight = 300u;
// The results' status number while the shown run may be sent (nebula_game_api.h NEBULA_BENCHMARK_RESULTS_SENDABLE),
// or was sent or is being sent (NEBULA_BENCHMARK_RESULTS_SENT: Send to developer greyed).
inline constexpr int kBenchmarkResultsSendable = 1 << 16;
inline constexpr int kBenchmarkResultsSent = 1 << 17;
// The send's question and screens opened from the results window (NEBULA_BENCHMARK_FROM_RESULTS).
inline constexpr int kBenchmarkFromResults = 1 << 18;

// A result's time as the pages show it: the player's local time, "2026-11-15 01:43"; empty for a time that is not
// UTC as benchmark.cpp writes it.
using BenchmarkClock = std::function<std::string(const std::string& created_utc)>;
[[nodiscard]] std::string benchmark_local_time(const std::string& created_utc);

// What a run came to, as the pages and the LAST RESULT row say it (host capitals).
[[nodiscard]] std::string benchmark_outcome(const bench::ResultView& result);

// The pages for `runs[shown]`, `runs` the kept results newest first (bench::kKeptResults at most): summary, stages,
// where the time goes and the hardware, earlier runs.
[[nodiscard]] std::vector<BenchmarkPage> benchmark_pages(const std::vector<bench::ResultView>& runs, std::size_t shown,
                                                         const BenchmarkClock& clock);

}  // namespace galaxy::ui
