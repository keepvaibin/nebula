// SPDX-License-Identifier: GPL-3.0-only
// The Mods screens a mod's request opens over the game: mods.install's (services-mods 9, J-SM1) and save.game's offer
// (services-save 8, J-SV4). Each opens the menu on MODS with the game paused as by Start, and answers once, on the
// game thread at a frame's start (run_screen_answers), never inside the call that opened it. The services call these;
// the Mods page (runtime/src/ui/native_settings.cpp) gives the screens through set_screen_hooks. A host without the
// menu (the headless host) has none: the asks answer LUMA_E_ABSENT.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace galaxy::mods {

// Whether a screen waiting on the player is up: the consent question, an import's result, an import or the picker
// running, a request's screen. A request asked meanwhile is LUMA_E_BUSY (J-SM3, J-SV8). Any thread.
[[nodiscard]] bool player_screen_up();

// mods.install's screen for `file` (a .luma in the asker's data folder): "{asker} asks to install this.", the file's
// name (never its path), and INSTALL, NOT NOW, DON'T ASK AGAIN. INSTALL imports it as a drop does (the mod's file stays
// where it is) and the import's result page follows. `done(status)`: LUMA_OK imported or updated (it starts at the
// next start, and the page says so), LUMA_E_DENIED for NOT NOW, Escape, B, the menu closed or DON'T ASK AGAIN,
// LUMA_E_FORMAT refused by the validator or failed, LUMA_E_EXISTS already installed, LUMA_E_BUSY an import began
// between the screen and INSTALL. Returns LUMA_PENDING (the screen is up), LUMA_E_BUSY (a screen is up: nothing
// opened) or LUMA_E_ABSENT (no menu in this host); `done` is called only after LUMA_PENDING. Game thread.
[[nodiscard]] std::int32_t ask_install(const std::filesystem::path& file, std::string_view asker,
                                       std::function<void(std::int32_t status)> done);
// False after the player's DON'T ASK AGAIN for `id` (mods.ini install.ask=no, consent's store; the mod's page offers
// "May ask to install mods" to undo it). Any thread.
[[nodiscard]] bool install_ask_allowed(std::string_view id);

// One user file as the offer screen shows it, from the module's reading of that file (services-save 8).
struct OfferFile {
    bool exists = false;   // false: an empty file
    bool in_play = false;  // the file loaded now: shown, never pickable
    bool luigi = false;    // its active character
    std::uint32_t stars = 0;
    std::uint32_t play_seconds = 0;
};
struct OfferFacts {
    OfferFile offered;               // the mod's file, as parsed before the screen
    std::array<OfferFile, 6> files;  // user files 1-6, from the files in memory
};
// save.game's offer screen: "{asker} offers this file" over the offered file's summary, the six files to pick from
// (the one in play shown, not pickable), then "Replace file {n}? Its progress goes." REPLACE / KEEP, its help saying
// the old file is kept in the save folder. `done(status, file)`: LUMA_OK and the file (1-6) to replace, or
// LUMA_E_DENIED and 0 for NO, Escape, B or the menu closed. Returns as ask_install does. Game thread.
[[nodiscard]] std::int32_t ask_offer(const OfferFacts& facts, std::string_view asker,
                                     std::function<void(std::int32_t status, std::uint32_t file)> done);

// The service's cancel, or its mod stopped: `asker`'s open screen closes with no answer (an install whose import
// already runs finishes, and still answers). Game thread.
void close_asks(std::string_view asker);

// Game thread, at each frame's start (the host's frame_begun): each answer given since, its `done` called once.
void run_screen_answers();

// ---- For the Mods page and the host ----

struct ScreenHooks {
    bool (*screen_up)();
    std::int32_t (*ask_install)(const std::filesystem::path& file, std::string_view asker,
                                std::function<void(std::int32_t)> done);
    bool (*install_ask_allowed)(std::string_view id);
    std::int32_t (*ask_offer)(const OfferFacts& facts, std::string_view asker,
                              std::function<void(std::int32_t, std::uint32_t)> done);
    void (*close_asks)(std::string_view asker);
};
// The Mods page's screens (static; set once, before any mod starts).
void set_screen_hooks(const ScreenHooks* hooks);
// An answer to run at the game's next frame start (any thread).
void post_screen_answer(std::function<void()> answer);
// The host's pause, as Start pauses the game: asked as a request's screen opens (set once by the host).
void set_screen_pause(void (*pause)());
void pause_for_screen();

}  // namespace galaxy::mods
