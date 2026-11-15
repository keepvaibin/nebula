// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The HDR calibration's scene (ui/hdr_calibration.h). Its paper white step shows the game's own picture, and at the
// title or the save select that picture has no Mario in it. There the calibration plays a reference stage under its first
// two steps, which cover the screen in black: the game goes back to its title through its own reset with blank saves
// held in memory, travels to the stage and skips the stage's opening camera tour. The paper white step then shows Mario
// there, live and at rest. When the calibration ends, the game goes back to its title the same way with the player's
// saves. In play the calibration shows the player's own scene and nothing is visited.
// The host routes the game's resets, travels, scene starts, saves and controls here as it does a benchmark's
// (benchmark.h).

#include <cstdint>
#include <string_view>

namespace galaxy::calibration_scene {

// The stage and its star (the game's names).
inline constexpr const char* kStage = "EggStarGalaxy";
inline constexpr std::int32_t kStar = 1;

// Whether a visit may start now (the host's: the title has shown, no file is in play, no benchmark runs, no home screen
// holds the game). None installed: never.
void set_gate(bool (*may_visit)()) noexcept;

// The calibration opened: true when a visit starts.
bool start() noexcept;
// The calibration ended: a visit goes back to the title with the player's saves.
void stop() noexcept;

// From start() until stop(): the game is given no controls, and it unlocks and counts nothing.
[[nodiscard]] bool active() noexcept;
// From the game's taking the start's reset until its taking the end's (both on the game thread, so no save the game has
// begun is split between the player's files and the visit's): its saves are in memory.
[[nodiscard]] bool saves_in_memory() noexcept;
// From start() until the title shows again after stop(): the game plays under the settings menu
// (NebulaSettingsView::scene_live).
[[nodiscard]] bool live() noexcept;
// A visit runs and its stage is not yet ready to look at: the paper white step keeps the screen black.
[[nodiscard]] bool waiting() noexcept;

// NEBULA_HOME_MENU_RESET: true once for each reset a visit asks for (its start and its end).
[[nodiscard]] bool take_reset() noexcept;
// NEBULA_HOME_MENU_RESET_DONE: the game's reset is done and its saves read.
void reset_done() noexcept;
// Game thread, once a scene frame (svc_galaxy_travel): the travel to ask of the scene now (NEBULA_GALAXY_TRAVEL_*), with
// `stage` and `star` filled for one. `current_stage` is the stage the game last reported (svc_game_stage).
[[nodiscard]] int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star);
// Game thread, at each scene's start (svc_presence from GameScene::start).
void scene_started() noexcept;
// NEBULA_CONTEXT_SCENE_PLAY: the scene shows play (its opening and Mario's arrival are over).
void note_play(bool play) noexcept;
// svc_save_request while saves_in_memory(). As MemorySaves::request.
[[nodiscard]] std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size,
                                        std::uint32_t* length) noexcept;

// Tests: the clock the waits are timed on, in nanoseconds (nullptr: the steady clock).
void set_clock_for_tests(std::int64_t (*now_ns)()) noexcept;

}  // namespace galaxy::calibration_scene
