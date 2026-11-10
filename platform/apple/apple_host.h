// SPDX-License-Identifier: GPL-3.0-only
// What the host asks of Apple's systems (macOS, iOS, iPadOS) outside SDL: docs/APPLE.md.
#pragma once

#include <string>

namespace galaxy::apple {

// An app opened from the Finder or the home screen has no command line. Fills in what it leaves out from the app's own
// layout (--data, --pak, --saves, --module; a command line's own values stay), sends the output to logs/game.log when
// nothing reads it, and names the system's language for the game (LANG). Called first in main; the argument list it
// leaves lives for the process.
void launch_arguments(int& argc, char**& argv);

// The system's audio answered when it was asked at the start (launch_arguments): a Mac's can hang in a driver, and a
// program that then opens a device waits for ever. False: this run plays no sound.
[[nodiscard]] bool audio_answers() noexcept;

// Brings up the Mac app (host_window_apple.mm): its delegate, menus and launch, once, before any window. Everything
// that shows a window calls it first: the delegate must be the app's before AppKit does anything else.
void start_app() noexcept;

// The game's data at `pak` opens as the game opens it (its store version, its index, a release the game plays).
[[nodiscard]] bool game_data_opens(const std::string& pak);

// A Mac's first start (setup_apple.mm): with no game.pak at --pak, asks the player for their disc image and builds it.
// False: the player left without setting up, and the app ends. After launch_arguments, on the main thread.
[[nodiscard]] bool ensure_game_data(int argc, char** argv);

}  // namespace galaxy::apple
