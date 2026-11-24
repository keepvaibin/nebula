// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A pipeline is a registered pair of programs and a state word, for one kind of target and one set of aux targets. All of
// them are prepared in the background ahead of play; a draw only reads a table.

#include "render/core/frame_core.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace galaxy::render::core {

class Pipelines {
public:
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL) || defined(NEBULA_GPU_D3D11)
    using RootLayout = void*;  // Vulkan: a vk::RenderLayout; Metal: an mtl::RenderLayout; Direct3D 11: a d3d11::RenderLayout
#else
    using RootLayout = ID3D12RootSignature*;
#endif
    void initialize(gpu::Device* device, RootLayout root, void* backend = nullptr);
    void shutdown() noexcept;

    // The pipeline of `id` for a target of `variant` (samples, HDR, a texture target) with `aux` beside the colour: 0 none,
    // 1 the motion, 2 the reactive mask, 3 the reactive mask and the effects layer, 4 the motion target there but not
    // written (a draw sharing the motion draws' pass).
    // A texture target's form is noted the first time a run draws it (note_target_drawn): the forms a target pass has drawn
    // are the ones prepared ahead (the targets file). A pipeline not made yet is never built here (missing): null, and the
    // draw is left out of this picture.
    gpu::Pipeline* get(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
        const std::uint32_t key = key_of(variant, aux);
        const bool target = target_form(variant, aux);
        const Page* page = page_of(id);
        if (page != nullptr) {
            gpu::Pipeline* found = page->rows[id & kPageMask][key].load(std::memory_order_acquire);
            if (found != nullptr) {
                if (target && page->drawn[id & kPageMask].load(std::memory_order_relaxed) == 0u) {
                    note_target_drawn(id, entry, variant, aux);
                }
                return found;
            }
        }
        gpu::Pipeline* made = missing(id, entry, variant, aux);
        if (made != nullptr && target) {
            note_target_drawn(id, entry, variant, aux);
        }
        return made;
    }

    // An entry to prepare: its id and a copy its jobs share.
    struct Prepared {
        std::uint32_t id = 0u;
        std::shared_ptr<const PipelineEntry> entry;
    };
    // Queues each entry for every target kind and aux set of the settings, for the background threads: with `motion`, the
    // aux a scene draw of its state can take (scene_aux; 4 for every other with `one_pass`, 3 only with `layer`), and while
    // the wide variant is listed the narrow variant's left for a load's wait (defer). The jobs are made before
    // the queue's lock is taken, and the queue takes them all at once.
    void prepare(const std::vector<Prepared>& entries, const std::vector<unsigned>& variants, bool motion, bool layer,
                 bool one_pass);
    [[nodiscard]] std::uint32_t pending() const noexcept { return pending_.load(std::memory_order_relaxed); }
    // The forms left for a load's wait (left_jobs_): queued, and taken by a worker that has not finished them yet.
    [[nodiscard]] std::uint32_t later() const noexcept { return later_.load(std::memory_order_acquire); }
    // Whether the cache the driver took holds the start's whole set, made by an earlier run of these programs at these
    // settings (the targets file, decide_warm): each of its pipelines is then a load from the cache, not a compile, and
    // nothing need wait for the workers. False until the first worker has looked, and where the renderer keeps no cache
    // file of its own (Direct3D 11, Metal: the driver's or the system's own cache, which no file here describes).
    [[nodiscard]] bool warm() const noexcept { return warm_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t created() const noexcept { return created_.load(std::memory_order_relaxed); }
    // Pipelines a draw wanted before the workers had made them (missing): each form once, a timeline event each.
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_.load(std::memory_order_relaxed); }
    // While the game's load waits for its pipelines (the host's NEBULA_OPTION_SCENE_PIPELINES): the workers at the normal
    // priority, not the lowest, and the forms left for a load (left_jobs_) taken, behind the queue; at no other time. Any
    // thread. A worker that took a left form before the flag fell finishes it: the load waits for later() too.
    void set_hurried(bool hurried) noexcept;
    // Runs of fixed steps (NEBULA_EXT_FIXED_STEPS): a draw whose pipeline is missing waits for the worker that makes it, so a
    // picture holds every draw whatever the workers' pace and two runs draw alike. The render thread, each frame.
    void set_wait_for_missing(bool wait) noexcept { wait_for_missing_.store(wait, std::memory_order_relaxed); }
    // The workers hurried until every queued job (pending()) and every left form (later()) is made, or the workers stop
    // (the render thread, drain_pipelines).
    void drain();
    // A pipeline the game registered, handed over once when the render thread first queues it. The cook's list
    // (NebulaData/Pipelines.bin) is registered at boot: one registered after the first picture with a scene is not in it, and
    // its record is logged for crates/nebula-import/data/site_pipelines.txt.
    void note_registered(const PipelineEntry& entry);
    [[nodiscard]] std::uint64_t failures() const noexcept { return failures_.load(std::memory_order_relaxed); }
    // Play begins at the first picture with a scene in it (scene_drawn from the walk that draws a scene, picture_shown from
    // each present): from then a registration or a miss is logged (note_registered, missing), and a Present that waits for a
    // creation (present_gate.h) while no load waits.
    void scene_drawn() noexcept { scene_drawn_.store(true, std::memory_order_relaxed); }
    void picture_shown() noexcept {
        if (scene_drawn_.load(std::memory_order_relaxed) && !scene_shown_.load(std::memory_order_relaxed)) {
            enter_play();
        }
    }

private:
    static constexpr unsigned kPageBits = 8u;
    static constexpr std::uint32_t kPageMask = (1u << kPageBits) - 1u;
    static constexpr std::uint32_t kKeys = 128u;
    static constexpr unsigned kDirectoryBits = 12u;
    static constexpr unsigned kDirectorySize = 1u << kDirectoryBits;
    static constexpr unsigned kRootSize = 1u << (33u - kPageBits - kDirectoryBits);
    // aux 0-4 by the variant: every key is under kKeys.
    [[nodiscard]] static constexpr std::uint32_t key_of(unsigned variant, unsigned aux) noexcept {
        return (variant & 15u) | (aux & 7u) << 4u;
    }
    struct Page {
        std::array<std::array<std::atomic<gpu::Pipeline*>, kKeys>, 1u << kPageBits> rows{};
        std::array<std::atomic<std::uint8_t>, 1u << kPageBits> drawn{};  // the id's texture target form has been drawn this run
        // A draw has found the cell empty (missing): one bit for each key, so a form is counted and queued once.
        std::array<std::array<std::atomic<std::uint32_t>, kKeys / 32u>, 1u << kPageBits> missed{};
    };
    // A texture target's form: the one kind of pipeline prepared ahead only when the targets file says it is used.
    [[nodiscard]] static constexpr bool target_form(unsigned variant, unsigned aux) noexcept {
        return variant == kSceneVariantTarget && aux == 0u;
    }
    struct Directory {
        std::array<std::atomic<Page*>, kDirectorySize> pages{};
    };
    [[nodiscard]] Page* page_of(std::uint64_t id) const noexcept {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { return nullptr; }
        const Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) { return nullptr; }
        const std::size_t at = (id >> kPageBits) & (kDirectorySize - 1u);
        return directory->pages[at].load(std::memory_order_acquire);
    }
    struct Job {
        std::uint32_t id;
        std::shared_ptr<const PipelineEntry> entry;  // shared by the entry's jobs
        unsigned variant, aux;
        bool optimize = false;  // a draw's quickly linked pipeline, to be replaced by the optimised link
        bool deferred = false;  // a form left out of the start's set, made while a load waits (defer)
        bool urgent = false;    // a draw's miss: at the queue's front, built at the normal priority (missing)
    };
    // A form the start's set leaves out (a texture target form no run has drawn; any other kind a caller decides the same way):
    // queued in left_jobs_, made by a worker while a load waits (hurried) once jobs_ is empty, or at the queue's front when a
    // draw needs it first (missing; the deferred job then finds its cell filled). A deferred job is not deferred again.
    void defer(const Job& job) noexcept;
    // The first picture with a scene (picture_shown): scene_shown_, and the present gate's play (present_gate.h).
    void enter_play() noexcept;
    // A draw's pipeline that is not made: never built on the draw's thread. The first time a form is missed it is counted and
    // recorded (timeline PipelineMissed), and either linked at once from parts (build_fast: Vulkan with graphics pipeline
    // libraries, the optimised link queued behind) or queued at the front of the workers' queue as an urgent job. Null then,
    // and the draw is left out until a later picture finds the cell filled; with wait_for_missing_, and before play (no
    // picture with a scene yet), the draw waits for the urgent job instead. Refused forms are null at once.
    gpu::Pipeline* missing(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
    // The first draw that missed `form` (its urgent_ entry made under lock_ with the bit): counted, recorded, logged once a
    // scene has been shown, then linked and published, or queued at the front. Erases the urgent_ entry when nothing is queued.
    void note_missed(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux, std::uint64_t form,
                     std::atomic<gpu::Pipeline*>& at);
    std::atomic<gpu::Pipeline*>* cell(std::uint64_t id, unsigned variant, unsigned aux);
    void refuse(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
    // Why a build returned null: the driver refused the pipeline, the device ran out of memory, or the device was lost.
    enum class Failure : unsigned char { refused, memory, device_lost };
    // A build that returned null: refused; for lack of memory (pipeline_workers.h) the workers' wait and, below
    // kPipelineMemoryTries, a later try (`job`: a worker's, queued again behind the others); with the device lost,
    // nothing: the pipeline is not at fault, and the device made after the loss is asked for it (N-4024).
    void failed(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux, Failure why, const Job* job);
    // build(), and why it failed (note_pipeline_out_of_memory, note_pipeline_device_lost on this thread).
    gpu::Pipeline* build_noting(const PipelineEntry& entry, unsigned variant, unsigned aux, Failure& why);
    // Whether the form failed for memory and the wait after it is not over: its draw is left out until then.
    [[nodiscard]] bool waiting_for_memory(std::uint64_t key);
    // Whether the driver has refused this entry's program pair often enough that no state of it is asked for again.
    [[nodiscard]] bool pair_refused(const PipelineEntry& entry);
    gpu::Pipeline* publish(std::atomic<gpu::Pipeline*>& at, gpu::Pipeline* built) noexcept;
    gpu::Pipeline* build(const PipelineEntry& entry, unsigned variant, unsigned aux);  // per backend
    // A pipeline for a draw that cannot wait: linked at once from parts that exist (Vulkan with graphics pipeline libraries), or
    // null where the backend has no such thing (the caller then builds the whole pipeline). The workers replace it by build()'s.
    gpu::Pipeline* build_fast(const PipelineEntry& entry, unsigned variant, unsigned aux);  // per backend
#if defined(NEBULA_GPU_VULKAN)
    gpu::Pipeline* build_vk(const PipelineEntry& entry, unsigned variant, unsigned aux, bool fast);
#endif
    void destroy_all() noexcept;  // per backend
    std::size_t save_cache() noexcept;  // per backend: the driver's pipelines to disk when they changed; bytes written
    // Per backend: the driver cache's file, opening the cache if it is not yet, and the hash of the data the driver took from
    // it at the start (0: none, refused, or no file of the renderer's own). False where the renderer keeps no file (Metal: the
    // system's own cache; a driver whose file is not written).
    bool cache_file_state(std::filesystem::path& file, std::uint64_t& accepted);
    // Per backend: the hash of the data the cache file now holds (0 when none), kept after destroy_all.
    std::uint64_t cache_file_hash() noexcept;
    void drained() noexcept;
    void worker();

    // The texture targets' forms (target_form). Every one prepared and published is in the driver cache, which a later run
    // loads; a form no target pass has drawn is then left out of the start's set and made while a load waits
    // (left_jobs_), or at the front of the queue when a draw needs it first, a load from the cache, not a compile. The file beside the cache keeps both sets and the hash of the cache data they go with: a cache
    // the driver refused, written by another run, or holding other programs names nothing, and every form is prepared.
    bool targets_open();
    // warm(): the targets file names the cache data the driver took, was written after the start's set of a run drained,
    // and by a run of these programs (program_set_identity) at the start's settings (start_settings_). Decided once, by
    // the first worker to take a job after the first list (prepare) set the settings.
    void decide_warm() noexcept;
    [[nodiscard]] std::uint64_t form_key(const PipelineEntry& entry, unsigned variant, unsigned aux);
    [[nodiscard]] bool left_to_cache(std::uint64_t form);
    void note_target_drawn(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
    void note_target_stored(std::uint64_t form) noexcept;
    [[nodiscard]] std::vector<std::uint64_t> stored_forms();
    void save_targets(const std::vector<std::uint64_t>& stored) noexcept;
    std::mutex targets_lock_;
    bool targets_tried_ = false, targets_on_ = false, targets_unsaved_ = false;
    std::uint64_t targets_hash_ = 0u;  // the cache data the targets file names: written again when the cache file moves
    // The targets file's run (targets_open): its programs, its start's settings, whether its start's set had drained when
    // it was written, and whether the cache data it names is the data the driver took.
    std::uint64_t file_programs_ = 0u, file_settings_ = 0u;
    bool file_complete_ = false, file_names_cache_ = false;
    bool start_drained_ = false;  // this run's start's set drained (drained): the cache written from then holds it
    std::atomic<std::uint64_t> start_settings_{0u};  // the first list's targets and passes (prepare); 0 before it
    std::atomic<bool> warm_{false}, warm_decided_{false};
    std::filesystem::path targets_file_;
    std::unordered_set<std::uint64_t> target_cached_;  // in the cache the driver took at the start
    std::unordered_set<std::uint64_t> target_stored_;  // put in the cache by this run, or there from the start
    std::unordered_set<std::uint64_t> target_drawn_;   // drawn by a target pass, this run or an earlier one
    std::unordered_map<const void*, std::uint64_t> program_hashes_;  // a program's bytes' hash, by its bytes' address
    std::atomic<std::uint64_t> left_{0u};  // forms left to the cache this run

    gpu::Device* device_ = nullptr;
    RootLayout root_{};
    void* backend_ = nullptr;
    std::array<std::atomic<Directory*>, kRootSize> directories_{};
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    // The forms left for a load, taken by a worker only when jobs_ is empty and hurried_ is set (under lock_). Not counted
    // in pending_: the first drain (drained) is the start's set alone. later_: queued there or taken and not finished (raised
    // under lock_ with each push, lowered when a deferred job ends).
    std::deque<Job> left_jobs_;
    std::atomic<std::uint32_t> later_{0u};
    std::atomic<bool> scene_drawn_{false}, scene_shown_{false};
    std::atomic<bool> hurried_{false}, wait_for_missing_{false};
    // The forms (id << 9 | key) of the urgent jobs queued and not finished (under lock_); made_ is told when one finishes.
    std::unordered_set<std::uint64_t> urgent_;
    std::condition_variable made_;
    std::unordered_set<std::uint64_t> refused_;  // the driver said no: asked once
    // Program pairs (vertex and pixel ids mixed) the driver refused, and how often: a compiler that cannot take a pair's
    // code refuses it in every state, and each try is a whole compile (hundreds of milliseconds on a phone's draw).
    std::unordered_map<std::uint64_t, unsigned> refused_pairs_;
    // Forms whose builds failed for lack of memory, and how often (under lock_); the end of the workers' wait after the
    // latest such failure (steady_clock ticks); the failures this run.
    std::unordered_map<std::uint64_t, unsigned> memory_failed_;
    std::atomic<std::int64_t> memory_wait_until_{0};
    std::atomic<std::uint64_t> memory_failures_{0u};
    std::vector<std::thread> threads_;
    std::vector<gpu::PipelineRef> owned_;
    std::atomic<std::uint32_t> pending_{0u};
    std::atomic<std::uint64_t> created_{0u}, failures_{0u};
    std::atomic<std::uint64_t> misses_{0u};  // the draw's thread counts (missing), a worker reports (drained)
    unsigned workers_ = 0u;
    std::atomic<bool> unsaved_{false};  // a pipeline was made since the cache was last written
    std::atomic<bool> reported_{false};
    std::chrono::steady_clock::time_point started_{};
    bool quit_ = false;
};


// Which targets a draw of the 3D scene writes beside the colour: an opaque surface the motion, a blended draw the reactive
// mask, and in a layer window a blended draw whose factors never read the scene the effects layer too.
[[nodiscard]] std::uint8_t scene_aux(std::uint32_t state, bool layer_window) noexcept;

// For a back end's build: the driver refused a pipeline for lack of memory (pipeline_workers.h), on this thread.
void note_pipeline_out_of_memory() noexcept;
// For a back end's build: the device was lost (removed, reset, hung) while it built, on this thread: no refusal.
void note_pipeline_device_lost() noexcept;

// Writes `bytes` beside `file` and renames it over, so a crash mid-write leaves the old cache whole.
bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept;

}  // namespace galaxy::render::core
