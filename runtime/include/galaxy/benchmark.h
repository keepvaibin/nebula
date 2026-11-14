// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark (NebulaNative --benchmark 1, docs/COMMAND_LINE.md): real stages from the player's game data, entered
// through the game's own stage change (the settings' galaxy select path, svc_galaxy_travel), measured from the sources
// the host already has, then a probe of each optional pass and the recommended settings (benchmark_recommend.h), into
// one JSON document in the data folder's diagnostics. It runs on a blank save held in memory, never applies mods, and
// never writes a setting, a save, a counter or an achievement: a crash or a quit at any point leaves the player's files
// as they were.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy::render {
struct RenderConfig;
}

namespace galaxy::bench {

// True from start() to the end of the process. The settings writers, the save service, the counters and the input read
// test it (one relaxed load).
inline std::atomic<bool> g_running{false};
[[nodiscard]] inline bool running() noexcept {
    return g_running.load(std::memory_order_relaxed);
}

// The render thread at each new picture shown (RenderCore::note_picture_shown), with the frame-tails cadence sample's
// time, and the mixing thread at each mix (AudioMixer::mix): set only while a benchmark runs, so the call sites cost one
// load and link nothing.
inline std::atomic<void (*)(std::uint64_t)> g_picture_hook{nullptr};
inline std::atomic<void (*)()> g_audio_hook{nullptr};
inline void note_new_picture(std::uint64_t steady_ns) noexcept {
    if (void (*hook)(std::uint64_t) = g_picture_hook.load(std::memory_order_acquire)) {
        hook(steady_ns);
    }
}
inline void note_audio_thread() noexcept {
    if (void (*hook)() = g_audio_hook.load(std::memory_order_acquire)) {
        hook();
    }
}

struct Options {
    std::filesystem::path pak_folder;  // benchmark.tsv beside game.pak replaces the built-in stage list
    std::filesystem::path out_file;    // --benchmark-out; empty: diagnostics/benchmark-<utc>.json in the data folder
    bool return_home = false;          // --benchmark-return 1: started from the home screen, which it starts again after
};

// Main thread, before the game module attaches.
void start(const Options& options);

// Game thread, once a scene frame (svc_galaxy_travel): the travel to ask of the scene now (NEBULA_GALAXY_TRAVEL_*), with
// `stage` and `star` filled for one. `current_stage` is the stage the game last reported (svc_game_stage).
[[nodiscard]] int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star);
// Game thread, at each scene's start (svc_presence from GameScene::start; not the file select's): a travel to the
// stage the game is on has arrived when its new scene starts.
void scene_started() noexcept;

// The game's save files while the benchmark runs (svc_save_request): in memory only. `op` is NEBULA_SAVE_READ, _WRITE or
// _DELETE; the result a NEBULA_SAVE_* value (galaxy::saves::Result).
[[nodiscard]] std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size,
                                        std::uint32_t* length) noexcept;

// The host's exit: a run that did not finish writes its result as "aborted".
void at_exit() noexcept;

// The file this run writes, fixed by start(); empty when there is no data folder and no --benchmark-out.
[[nodiscard]] std::filesystem::path result_file();
// Started from the home screen and ended (complete or failed): the host starts the home screen again.
[[nodiscard]] bool returns_home() noexcept;

// A finished run's result with a recommendation (status complete): its recommended settings put over `config`; false,
// `config` as it was, for any other file. Reads only what write_result writes.
[[nodiscard]] bool read_recommendation(const std::filesystem::path& file, render::RenderConfig& config);

}  // namespace galaxy::bench
