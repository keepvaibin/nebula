// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark (Nebula --benchmark 1, docs/using-nebula/command-line-arguments.md, or the settings menu's Run benchmark
// in the running game): real stages entered through the game's own stage change (the settings' galaxy select path,
// svc_galaxy_travel), measured from the sources the host already has, then a probe of each optional pass and the
// recommended settings (benchmark_recommend.h), into one JSON document in the data folder's diagnostics. It runs on a
// blank save held in memory and never writes a setting, a save, a counter or an achievement: a crash or a quit at any
// point leaves the player's files as they were. A start with --benchmark 1 applies no mod; a run in the running game
// keeps the mods it started with and its result says so (mods_loaded).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy::bench {

// True from start() until the run ends (Options::here), or to the end of the process (--benchmark 1). The settings
// writers, the save service, the counters and the input read test it (one relaxed load).
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

// The player stops the run (Escape, a controller's B, Android's back; native_settings.cpp): any thread. The result is
// written as stopped (status "aborted", failure "stopped") with the steps that finished: a run in the running game ends
// at the game's next scene frame and the game goes back to its title (Options::here); --benchmark 1 ends the game at
// its next picture.
inline std::atomic<bool> g_stop_asked{false};
inline void stop() noexcept {
    if (running()) g_stop_asked.store(true, std::memory_order_relaxed);
}
[[nodiscard]] inline bool stopping() noexcept {
    return g_stop_asked.load(std::memory_order_relaxed);
}

// The renderers, as one frame's pass timestamps are read back (render thread): each pass's milliseconds in that frame,
// `names[i]` static text; `game_frame`: the frame drew the scene for a game step, not a picture between steps
// (interpolation) or one without the scene. Set only while a window measures.
using GpuFrameHook = void (*)(const char* const* names, const double* ms, std::size_t count, bool game_frame);
inline std::atomic<GpuFrameHook> g_gpu_frame_hook{nullptr};
inline void note_gpu_frame(const char* const* names, const double* ms, std::size_t count, bool game_frame) noexcept {
    if (const GpuFrameHook hook = g_gpu_frame_hook.load(std::memory_order_acquire)) {
        hook(names, ms, count, game_frame);
    }
}

// The game's disc reads (svc_asset_read), counted while a benchmark runs: a slow frame that read is a loading hitch.
inline std::atomic<std::uint64_t> g_read_bytes{0u};
inline void note_read(std::uint64_t bytes) noexcept {
    if (running()) g_read_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

// The work the game hands the renderer while a benchmark runs: each frame list's draw items (RenderCore::render_frame)
// and the texture images made or rewritten (TextureStore), with their bytes.
inline std::atomic<std::uint64_t> g_draw_items{0u};
inline std::atomic<std::uint64_t> g_texture_uploads{0u};
inline std::atomic<std::uint64_t> g_texture_bytes{0u};
inline void note_frame_list(std::uint32_t items) noexcept {
    if (running()) g_draw_items.fetch_add(items, std::memory_order_relaxed);
}
inline void note_texture(std::uint64_t bytes) noexcept {
    if (running()) {
        g_texture_uploads.fetch_add(1u, std::memory_order_relaxed);
        g_texture_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

// The render thread's time by part while a benchmark runs, in wall time so that a wait counts as much as work: what a
// slow frame's render thread was doing (benchmark.cpp: each stage's render_parts, and each slow frame's). Each renderer
// marks its own calls (the frame wait, the swap chain's image, the submission, the present, freeing what finished
// frames held) and the texture store its texture work. A span opened inside another on the same thread is left out:
// the outer one holds that time.
enum class RenderPart : unsigned {
    FrameWait,      // waiting for the GPU to finish a frame whose resources are written next
    Acquire,        // the swap chain's next image, or the display's signal that a frame may be queued
    Submit,         // handing the frame's commands to the GPU
    Present,        // the present call
    ResourceFree,   // freeing what finished frames held (images, buffers, descriptor pools)
    TextureCreate,  // making a texture (its image and memory)
    TextureUpload,  // writing a texture's texels, or recording their copy
    TextureFree,    // freeing textures no frame reads any more
    UploadWait,     // waiting for the copy queue's uploads before their staging memory is written again
    RingResize,     // making the per-frame upload rings again at another size
    Count
};
inline constexpr std::size_t kRenderParts = static_cast<std::size_t>(RenderPart::Count);
inline constexpr const char* kRenderPartNames[kRenderParts] = {
    "frame wait", "acquire", "submit", "present", "resource free", "texture create", "texture upload", "texture free",
    "upload wait", "ring resize"};
inline std::atomic<std::uint64_t> g_render_part_ns[kRenderParts]{};
inline thread_local bool t_render_span_open = false;

class RenderSpan {
public:
    explicit RenderSpan(RenderPart part) noexcept {
        if (running() && !t_render_span_open) {
            t_render_span_open = true;
            part_ = part;
            start_ns_ = now_ns();
            open_ = true;
        }
    }
    ~RenderSpan() {
        if (open_) {
            g_render_part_ns[static_cast<std::size_t>(part_)].fetch_add(now_ns() - start_ns_, std::memory_order_relaxed);
            t_render_span_open = false;
        }
    }
    RenderSpan(const RenderSpan&) = delete;
    RenderSpan& operator=(const RenderSpan&) = delete;

private:
    static std::uint64_t now_ns() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    RenderPart part_ = RenderPart::FrameWait;
    std::uint64_t start_ns_ = 0u;
    bool open_ = false;
};

struct Options {
    std::filesystem::path pak_folder;  // benchmark.tsv beside game.pak replaces the built-in stage list
    std::filesystem::path out_file;    // --benchmark-out; empty: a new file in results_folder() (benchmark_results.h)
    // A run in the running game (the settings menu's Run benchmark, the home screen's Benchmark): the game goes back to
    // its title through its own reset, its saves read again from the run's blank ones, and does so again at the end
    // with the player's saves; then `ended` gets the result file (empty when none was written). Else the run ends the
    // game (--benchmark 1).
    bool here = false;
    bool mods_loaded = false;  // mods are loaded in the game the run measures
    void (*ended)(const std::filesystem::path& result) = nullptr;
};

// The run as the game's run panel shows it (live_state).
enum class Part : unsigned { Yours, Probe, Again };
struct LiveState {
    std::uint32_t step = 0u, steps = 0u;  // the step running, from 1, of the run's steps
    Part part = Part::Yours;              // what the step measures: your settings, one optional pass, or the
                                          // first stage again (the device warmed up)
    bool showing = false;                 // the step's stage is on screen: under the cover until play, warming up or measured
                                          // (not loading)
    std::uint32_t cover = 0u;             // black over the game, under the panel, 0 to 255: settings change under it
    std::uint32_t fps_tenths = 0u;        // pictures shown a second over the last half second, in tenths; 0 not known
    std::uint32_t progress = 0u;          // the whole run, 0 to 10000, never going back
    bool stopping = false;                // the player stopped it: the game is about to start again
    std::string stage;                    // the step's stage (the game's name: CosmosGardenGalaxy)
};
// Game thread: the run now; false when no benchmark runs or it has ended.
[[nodiscard]] bool live_state(LiveState& out);

// The main thread before the game module attaches (--benchmark 1), or any thread while the game runs (Options::here).
// False while a run is going.
bool start(const Options& options);
// The renderer this build runs: d3d12, d3d11, vulkan or metal, as a result's "backend" names it.
[[nodiscard]] const char* running_backend() noexcept;
// The game, each frame no reset of its own runs (NEBULA_HOME_MENU_RESET): true once for each reset a run in the running
// game asks for (its start and its end).
[[nodiscard]] bool take_reset() noexcept;
// The game's reset is done and its saves read (NEBULA_HOME_MENU_RESET_DONE): the start's begins the run at the title,
// the end's hands the result to Options::ended.
void reset_done() noexcept;

// Game thread, once a scene frame (svc_galaxy_travel): the travel to ask of the scene now (NEBULA_GALAXY_TRAVEL_*), with
// `stage` and `star` filled for one. `current_stage` is the stage the game last reported (svc_game_stage).
[[nodiscard]] int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star);
// Game thread, at each scene's start (svc_presence from GameScene::start; not the file select's): a travel to the
// stage the game is on has arrived when its new scene starts.
void scene_started() noexcept;
// NEBULA_CONTEXT_SCENE_PLAY: the scene shows play (its opening and Mario's arrival are over); a window measures play.
void note_play(bool play) noexcept;

// The game's save files while the benchmark runs (svc_save_request): in memory only. `op` is NEBULA_SAVE_READ, _WRITE or
// _DELETE; the result a NEBULA_SAVE_* value (galaxy::saves::Result).
[[nodiscard]] std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size,
                                        std::uint32_t* length) noexcept;

// The host's exit: a run that did not finish writes its result as "aborted".
void at_exit() noexcept;


}  // namespace galaxy::bench
