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
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    using RootLayout = void*;  // Vulkan: a vk::RenderLayout; Metal: an mtl::RenderLayout
#else
    using RootLayout = ID3D12RootSignature*;
#endif
    void initialize(gpu::Device* device, RootLayout root, void* backend = nullptr);
    void shutdown() noexcept;

    // The pipeline of `id` for a target of `variant` (samples, HDR, a texture target) with `aux` beside the colour: 0 none,
    // 1 the motion, 2 the reactive mask, 3 the reactive mask and the effects layer, 4 the motion target there but not
    // written (a draw sharing the motion draws' pass); from 5 the tracer's forms (program_table.h kAux*), kept in pages
    // of their own that exist only once one is asked for.
    // A texture target's form is noted the first time a run draws it (note_target_drawn): the forms a target pass has drawn
    // are the ones prepared ahead (the targets file).
    gpu::Pipeline* get(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
        const std::uint32_t key = key_of(variant, aux);
        const bool target = target_form(variant, aux);
        Page* page = page_of(id, key >= kKeys);
        if (page != nullptr) {
            gpu::Pipeline* found = page->rows[id & kPageMask][key & (kKeys - 1u)].load(std::memory_order_acquire);
            if (found != nullptr) {
                if (target && page->drawn[id & kPageMask].load(std::memory_order_relaxed) == 0u) {
                    note_target_drawn(id, entry, variant, aux);
                }
                return found;
            }
        }
        gpu::Pipeline* made = create_now(id, entry, variant, aux);
        if (made != nullptr && target) {
            note_target_drawn(id, entry, variant, aux);
        }
        return made;
    }

    // Queues `id` for every target kind and aux set of the settings, for the background threads: with `motion`, the aux a
    // scene draw of its state can take (scene_aux; 4 for every other with `one_pass`, 3 only with `layer`), and while the
    // wide variant is listed the narrow variant's left for after the first scene (defer). With `traced` (ray tracing on),
    // aux 1 and either 4 or 2 and 3 whatever the state, the prepass of an opaque entry and a lit program's traced forms.
    void prepare(std::uint32_t id, const PipelineEntry& entry, const std::vector<unsigned>& variants, bool motion, bool layer,
                 bool one_pass, bool traced = false);
    [[nodiscard]] std::uint32_t pending() const noexcept { return pending_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t created() const noexcept { return created_.load(std::memory_order_relaxed); }
    // Pipelines a draw wanted before the background had them: the cook's list missed them.
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_; }
    [[nodiscard]] std::uint64_t failures() const noexcept { return failures_.load(std::memory_order_relaxed); }
    // The texture target forms left to the cache at the start (left_to_cache) are made after the first picture with a scene in
    // it is shown, behind every other job, so the first target pass that draws one finds it made: scene_drawn from the walk
    // that draws a scene, picture_shown from each present.
    void scene_drawn() noexcept { scene_drawn_.store(true, std::memory_order_relaxed); }
    void picture_shown() noexcept {
        if (scene_drawn_.load(std::memory_order_relaxed) && !scene_shown_.load(std::memory_order_relaxed)) {
            release_left();
        }
    }

private:
    static constexpr unsigned kPageBits = 8u;
    static constexpr std::uint32_t kPageMask = (1u << kPageBits) - 1u;
    static constexpr std::uint32_t kKeys = 128u;
    static constexpr unsigned kDirectoryBits = 12u;
    static constexpr unsigned kDirectorySize = 1u << kDirectoryBits;
    static constexpr unsigned kRootSize = 1u << (33u - kPageBits - kDirectoryBits);
    // Below kKeys: aux 0-4. From kKeys: the tracer's forms (aux 5 and up), in the second pages.
    [[nodiscard]] static constexpr std::uint32_t key_of(unsigned variant, unsigned aux) noexcept {
        return aux >= 5u ? kKeys | (variant & 15u) | ((aux - 5u) & 7u) << 4u : (variant & 15u) | (aux & 7u) << 4u;
    }
    struct Page {
        std::array<std::array<std::atomic<gpu::Pipeline*>, kKeys>, 1u << kPageBits> rows{};
        std::array<std::atomic<std::uint8_t>, 1u << kPageBits> drawn{};  // the id's texture target form has been drawn this run
    };
    // A texture target's form: the one kind of pipeline prepared ahead only when the targets file says it is used.
    [[nodiscard]] static constexpr bool target_form(unsigned variant, unsigned aux) noexcept {
        return variant == kSceneVariantTarget && aux == 0u;
    }
    struct Directory {
        std::array<std::atomic<Page*>, kDirectorySize> pages{};
        std::array<std::atomic<Page*>, kDirectorySize> traced_pages{};
    };
    [[nodiscard]] Page* page_of(std::uint64_t id, bool traced) const noexcept {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { return nullptr; }
        const Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) { return nullptr; }
        const std::size_t at = (id >> kPageBits) & (kDirectorySize - 1u);
        return traced ? directory->traced_pages[at].load(std::memory_order_acquire) : directory->pages[at].load(std::memory_order_acquire);
    }
    struct Job {
        std::uint32_t id;
        PipelineEntry entry;
        unsigned variant, aux;
        bool optimize = false;  // a draw's quickly linked pipeline, to be replaced by the optimised link
        bool deferred = false;  // a form left out of the start's set, made after the first scene is shown (defer)
    };
    // A form the start's set leaves out (a texture target form no run has drawn; any other kind a caller decides the same way):
    // queued in left_jobs_, made by a worker once jobs_ is empty and a scene has been shown, or by the draw that first needs it if
    // that comes sooner (the deferred job then finds its cell filled). A deferred job is not deferred again.
    void defer(const Job& job) noexcept;
    void release_left() noexcept;
    gpu::Pipeline* create_now(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
    std::atomic<gpu::Pipeline*>* cell(std::uint64_t id, unsigned variant, unsigned aux);
    void refuse(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
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
    // loads; a form no target pass has drawn is then left out of the start's set and made after the first scene is shown
    // (left_jobs_), or by the draw that first needs it if that comes sooner, a load from the cache, not a compile. The file beside the cache keeps both sets and the hash of the cache data they go with: a cache
    // the driver refused, written by another run, or holding other programs names nothing, and every form is prepared.
    bool targets_open();
    [[nodiscard]] std::uint64_t form_key(const PipelineEntry& entry, unsigned variant, unsigned aux);
    [[nodiscard]] bool left_to_cache(std::uint64_t form);
    void note_target_drawn(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux);
    void note_target_stored(std::uint64_t form) noexcept;
    [[nodiscard]] std::vector<std::uint64_t> stored_forms();
    void save_targets(const std::vector<std::uint64_t>& stored) noexcept;
    std::mutex targets_lock_;
    bool targets_tried_ = false, targets_on_ = false, targets_unsaved_ = false;
    std::uint64_t targets_hash_ = 0u;  // the cache data the targets file names: written again when the cache file moves
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
    // The forms left to the cache, taken by a worker only when jobs_ is empty and scene_shown_ is set (under lock_). Not counted
    // in pending_: the first drain (drained) is the start's set alone.
    std::deque<Job> left_jobs_;
    std::atomic<bool> scene_drawn_{false}, scene_shown_{false};
    std::unordered_set<std::uint64_t> refused_;  // the driver said no: asked once
    // Program pairs (vertex and pixel ids mixed) the driver refused, and how often: a compiler that cannot take a pair's
    // code refuses it in every state, and each try is a whole compile (hundreds of milliseconds on a phone's draw).
    std::unordered_map<std::uint64_t, unsigned> refused_pairs_;
    std::vector<std::thread> threads_;
    std::vector<gpu::PipelineRef> owned_;
    std::atomic<std::uint32_t> pending_{0u};
    std::atomic<std::uint64_t> created_{0u}, failures_{0u};
    std::atomic<std::uint64_t> misses_{0u};  // the render thread counts, a worker reports
    unsigned workers_ = 0u;
    std::atomic<bool> unsaved_{false};  // a pipeline was made since the cache was last written
    std::atomic<bool> reported_{false};
    std::chrono::steady_clock::time_point started_{};
    bool quit_ = false;
};


// Whether the tracer's prepass draws an entry of this state (and the scene pass then finds its depth laid).
[[nodiscard]] bool prepass_draws(std::uint32_t state) noexcept;
// Whether the tracer's second prepass draws this entry: a surface blended over the scene by its alpha (water, glass,
// crystal). Not what adds its own light to the picture, nor a stencil's marks.
[[nodiscard]] bool blended_prepass_draws(std::uint32_t state) noexcept;
// Which targets a draw of the 3D scene writes beside the colour: an opaque surface the motion, a blended draw the reactive
// mask, and in a layer window a blended draw whose factors never read the scene the effects layer too.
[[nodiscard]] std::uint8_t scene_aux(std::uint32_t state, bool layer_window) noexcept;

// Writes `bytes` beside `file` and renames it over, so a crash mid-write leaves the old cache whole.
bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept;

}  // namespace galaxy::render::core
