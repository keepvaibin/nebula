// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string_view>

namespace galaxy::render {
struct RenderConfig;
}

// Launcher mode (docs/COMMAND_LINE.md `--home`): the boot holds the title on a home screen (Play, Settings, Benchmark,
// Quit, and an update's notice when one is offered) in the menus' own look. The host keeps it (launcher_model.h: its keys and
// controllers' buttons); the game opens it at the title's hold and draws it as the HOME Menu's screen
// (NEBULA_HOME_MENU_LAUNCHER_*), and goes on to the title after Play.
namespace galaxy::ui {

// Before the game starts: whether the game's request opens the home screen.
void launcher_enable(bool on) noexcept;
// Before the game starts: whether the rows offer the benchmark (the host can start itself again with it).
void launcher_offer_benchmark(bool on) noexcept;
// Before the game starts: settings the settings menu shows as its unapplied draft when the home screen opens (a
// benchmark's recommendation, --benchmark-result); its Apply keeps them.
void launcher_show_draft(const galaxy::render::RenderConfig& draft);
// The game's request at the title's hold: true once, when launcher mode is on.
[[nodiscard]] bool launcher_open() noexcept;
[[nodiscard]] bool launcher_visible() noexcept;
// LauncherModel::view(): -1 while closed.
[[nodiscard]] int launcher_view() noexcept;
// -1 nothing chosen yet, LauncherModel::kChoicePlay or kChoiceQuit.
[[nodiscard]] int launcher_poll() noexcept;
void launcher_point(unsigned answer) noexcept;
void launcher_choose(unsigned answer) noexcept;
// A key while the settings menu is closed; `accept` a controller's A. True when the home screen took it.
[[nodiscard]] bool launcher_key(std::uintptr_t key, bool was_down, bool accept) noexcept;
// An update was found (any thread): its version and notes (UTF-8) for the notice's page.
void launcher_offer_update(std::string_view version, std::string_view notes);
// The notice's texts for the game: `which` 0 the version, 1 the notes; the UTF-16 unit at `index`, 0 past the end.
[[nodiscard]] unsigned launcher_text(unsigned which, unsigned index) noexcept;

}  // namespace galaxy::ui
