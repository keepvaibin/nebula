// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The settings menu's benchmark (the SYSTEM page's BENCHMARK rows, settings_system_page.h): the kept results
// (benchmark_results.h), the questions before a run and before a send, and the results window
// (settings_benchmark_page.h). native_settings.cpp calls in under its own lock; sending, which only the host can do
// (diag_report.h), is handed in as Reports.

#include "galaxy/render/render_config.h"
#include "galaxy/render_api.h"
#include "galaxy/ui/settings_system_page.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace galaxy::ui::benchmark_menu {

// The host's report sending, where the build can send reports (NEBULA_DIAG_REPORTS and an endpoint): `send` starts
// sending `result` on a thread of its own and answers with report_sent; `show`, where the system has a viewer for it
// (desktops), opens exactly what a send seals. Set once, before the menu opens.
struct Reports {
    void (*send)(const std::filesystem::path& result) = nullptr;
    void (*show)(const std::filesystem::path& result) = nullptr;
};
void set_reports(const Reports& reports) noexcept;
// The host's run in the running game (bench::Options::here): `start` starts it (false when it did not), `in_play` says
// whether a file is in play, whose unsaved progress the run's reset to the title loses. Set once, before the menu
// opens; without `start` there is no Run benchmark.
struct Runner {
    bool (*start)() = nullptr;
    bool (*in_play)() = nullptr;
};
void set_runner(const Runner& runner) noexcept;
// How a send ended (nebula_game_api.h NEBULA_BENCHMARK_SEND_*, checked alike in host_main.cpp).
enum class SendOutcome : int {
    Sent = 0,          // it reached the developer, or was sent before
    NoConnection = 1,  // the server did not answer
    TooMany = 2,       // the server's daily limit for one network (HTTP 429)
    ServerBusy = 3,    // the server takes no reports now (HTTP 503)
    Refused = 4,       // the server answered with another error
    NotMade = 5,       // no report could be made from the result
};
// A send ended (any thread). `detail` is the error as the sending reported it, shown under the reason.
void report_sent(SendOutcome outcome, std::string_view detail) noexcept;

// The kept results read again (the SYSTEM page as it opens; after a send).
void refresh();

// The BENCHMARK rows' state. `draft` the settings the menu shows (the changes not yet applied included), `adapter` the
// graphics card in use (RenderFeatureCaps::adapter_description); `set_draft` puts the recommended settings in the
// draft.
[[nodiscard]] BenchmarkView rows(const render::RenderConfig& draft, std::string_view adapter,
                                 std::function<void(const render::RenderConfig&)> set_draft);

// The results window for `result` (a kept result's file, or a results folder for its newest), as the game starts after a
// run; false when there is none to show.
bool open_results(const std::filesystem::path& result);

// A question or the results window is up: the page under it waits.
[[nodiscard]] bool up() noexcept;
// The results window is up with no question over it.
[[nodiscard]] bool results_up() noexcept;
// A key while up(): Enter or Space (a controller's Y) answers yes or turns to the next page, Backspace (X) shows the
// report or turns back, Escape (B) answers no or leaves the window; Right, Page Down (A, RB) and Left, Page Up (LB)
// turn the pages too. While the window offers Send to developer, Space (A: settings_pad::kKeyAccept) asks it instead.
// After a yes to the send the screen shows the send until it ends (no key answers it), then how it ended until any of
// Enter, Space, Escape or Backspace (or A) goes back. True when taken. `message` shows a line under the page (a
// restart that could not start).
bool key(std::uintptr_t key, bool was_down, const std::function<void(const char*)>& message);
// The view's status while up(): false when nothing is up.
bool fill(render::NativeSettingsView& view);
// The menu closed: whatever was up goes.
void close() noexcept;

}  // namespace galaxy::ui::benchmark_menu
