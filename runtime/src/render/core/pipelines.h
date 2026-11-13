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
    gpu::Pipeline* get(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
        const std::uint32_t key = key_of(variant, aux);
        const Page* page = page_of(id, key / kKeys);
        if (page != nullptr) {
            gpu::Pipeline* found = page->rows[id & kPageMask][key & (kKeys - 1u)].load(std::memory_order_acquire);
            if (found != nullptr) {
                return found;
            }
        }
        return create_now(id, entry, variant, aux);
    }

    // Queues `id` for every target kind and aux set of the settings, for the background threads: with `motion`, aux 1 and
    // either 4 (`one_pass`: the scene's draws share the motion pass) or 2 and, with `layer`, 3.
    // With `traced` (ray tracing on), also the prepass of an opaque entry and a lit program's traced forms.
    void prepare(std::uint32_t id, const PipelineEntry& entry, const std::vector<unsigned>& variants, bool motion, bool layer,
                 bool one_pass, bool traced = false);
    [[nodiscard]] std::uint32_t pending() const noexcept { return pending_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t created() const noexcept { return created_.load(std::memory_order_relaxed); }
    // Pipelines a draw wanted before the background had them: the cook's list missed them.
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_; }
    [[nodiscard]] std::uint64_t failures() const noexcept { return failures_.load(std::memory_order_relaxed); }

private:
    static constexpr unsigned kPageBits = 8u;
    static constexpr std::uint32_t kPageMask = (1u << kPageBits) - 1u;
    static constexpr std::uint32_t kKeys = 128u;
    static constexpr unsigned kDirectoryBits = 12u;
    static constexpr unsigned kDirectorySize = 1u << kDirectoryBits;
    static constexpr unsigned kRootSize = 1u << (33u - kPageBits - kDirectoryBits);
    // Below kKeys: aux 0-4. From kKeys: the tracer's forms (aux 5 and up), in the second pages; from 2 kKeys their
    // exchange forms (aux bit 16, program_table.h kAuxExchange; FB b94), in the third. Every key is under 512 (refused_).
    static constexpr unsigned kExchangeBit = 16u;
    [[nodiscard]] static constexpr std::uint32_t key_of(unsigned variant, unsigned aux) noexcept {
        return aux >= 5u ? ((aux & kExchangeBit) != 0u ? 2u * kKeys : kKeys) | (variant & 15u) | ((aux - 5u) & 7u) << 4u
                         : (variant & 15u) | (aux & 7u) << 4u;
    }
    struct Page {
        std::array<std::array<std::atomic<gpu::Pipeline*>, kKeys>, 1u << kPageBits> rows{};
    };
    struct Directory {
        std::array<std::atomic<Page*>, kDirectorySize> pages{};
        std::array<std::atomic<Page*>, kDirectorySize> traced_pages{};
        std::array<std::atomic<Page*>, kDirectorySize> exchange_pages{};  // FB b94
        [[nodiscard]] std::array<std::atomic<Page*>, kDirectorySize>& set(unsigned which) noexcept {
            return which == 0u ? pages : which == 1u ? traced_pages : exchange_pages;
        }
        [[nodiscard]] const std::array<std::atomic<Page*>, kDirectorySize>& set(unsigned which) const noexcept {
            return which == 0u ? pages : which == 1u ? traced_pages : exchange_pages;
        }
    };
    // `which`: the key's page set, key / kKeys (0 the game's forms, 1 the tracer's, 2 their exchange forms).
    [[nodiscard]] Page* page_of(std::uint64_t id, unsigned which) const noexcept {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { return nullptr; }
        const Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) { return nullptr; }
        const std::size_t at = (id >> kPageBits) & (kDirectorySize - 1u);
        return directory->set(which)[at].load(std::memory_order_acquire);
    }
    struct Job {
        std::uint32_t id;
        PipelineEntry entry;
        unsigned variant, aux;
        bool optimize = false;  // a draw's quickly linked pipeline, to be replaced by the optimised link
    };
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
    void drained() noexcept;
    void worker();

    gpu::Device* device_ = nullptr;
    RootLayout root_{};
    void* backend_ = nullptr;
    std::array<std::atomic<Directory*>, kRootSize> directories_{};
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
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
// A state the second prepass takes when its surface is a glass row's (FB b94: an added clear shell, drawn as an
// exchange): added to the picture (dst factor one) or tinting it and adding (dst source colour, b94b), depth tested,
// no stencil.
[[nodiscard]] bool added_prepass_draws(std::uint32_t state) noexcept;

// Writes `bytes` beside `file` and renames it over, so a crash mid-write leaves the old cache whole.
bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept;

}  // namespace galaxy::render::core
