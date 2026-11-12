// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "galaxy/atomic_file.h"
#include "galaxy/cpu_count.h"
#if defined(_WIN32)
#include "galaxy/windows_power.h"
#endif

#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "galaxy/render/program_table.h"

namespace galaxy::render::core {


void Pipelines::initialize(gpu::Device* device, RootLayout root, void* backend) {
    device_ = device;
    root_ = root;
    backend_ = backend;
    quit_ = false;
    started_ = std::chrono::steady_clock::now();
    created_.store(0u, std::memory_order_relaxed);  // counts are this device's
    failures_.store(0u, std::memory_order_relaxed);
    misses_ = 0u;
    reported_.store(false, std::memory_order_relaxed);
    // One worker for every logical core, at the lowest priority (worker()): the list is prepared by whatever the game and render
    // threads leave idle, and no frame waits for it. A draw whose pipeline is not made yet makes that one itself (create_now);
    // idle workers cost nothing.
    const unsigned count = std::max(1u, logical_cores());
    workers_ = count;
    for (unsigned i = 0; i < count; ++i) {
        threads_.emplace_back([this] { worker(); });
    }
}

void Pipelines::shutdown() noexcept {
    {
        std::lock_guard guard(lock_);
        quit_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    for (std::thread& thread : threads_) {
        thread.join();
    }
    threads_.clear();
    destroy_all();
    refused_.clear();
    for (auto& root : directories_) {
        if (Directory* directory = root.exchange(nullptr)) {
            for (auto& page : directory->pages) { delete page.load(); }
            for (auto& page : directory->traced_pages) { delete page.load(); }
            for (auto& page : directory->exchange_pages) { delete page.load(); }
            delete directory;
        }
    }
}

std::atomic<gpu::Pipeline*>* Pipelines::cell(std::uint64_t id, unsigned variant, unsigned aux) {
    static_assert(kExchangeBit == kAuxExchange && kExchangeBit > kAuxTraced + 4u, "the exchange's aux bit (FB b94)");
    const std::uint32_t key = key_of(variant, aux);
    const unsigned which = key / kKeys;
    Page* page = page_of(id, which);
    if (page == nullptr) {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { throw std::out_of_range("pipeline ID outside the native namespaces"); }
        std::lock_guard guard(lock_);
        Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) {
            directory = new Directory();
            directories_[root].store(directory, std::memory_order_release);
        }
        auto& at = directory->set(which)[(id >> kPageBits) & (kDirectorySize - 1u)];
        page = at.load(std::memory_order_acquire);
        if (page == nullptr) { page = new Page(); at.store(page, std::memory_order_release); }
    }
    return &page->rows[id & kPageMask][key & (kKeys - 1u)];
}

gpu::Pipeline* Pipelines::publish(std::atomic<gpu::Pipeline*>& at, gpu::Pipeline* built) noexcept {
    gpu::Pipeline* expected = nullptr;
    if (at.compare_exchange_strong(expected, built, std::memory_order_acq_rel)) {
        created_.fetch_add(1u, std::memory_order_relaxed);
        unsaved_.store(true, std::memory_order_release);
        return built;
    }
    return expected;
}

namespace {

// Two refusals in different states: the pair's code is what the driver cannot take.
constexpr unsigned kPairRefusals = 2u;

std::uint64_t pair_key(const PipelineEntry& entry) noexcept {
    return entry.desc.vertex * 0x9E3779B97F4A7C15ull ^ entry.desc.pixel;
}

}  // namespace

void Pipelines::refuse(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
    failures_.fetch_add(1u, std::memory_order_relaxed);
    std::lock_guard guard(lock_);
    refused_.insert(std::uint64_t{id} << 9u | key_of(variant, aux));
    if (++refused_pairs_[pair_key(entry)] == kPairRefusals) {
        std::fprintf(stderr, "[pipelines] programs %llx %llx refused twice: not asked for again\n",
                     static_cast<unsigned long long>(entry.desc.vertex), static_cast<unsigned long long>(entry.desc.pixel));
    }
}

bool Pipelines::pair_refused(const PipelineEntry& entry) {
    std::lock_guard guard(lock_);
    const auto found = refused_pairs_.find(pair_key(entry));
    return found != refused_pairs_.end() && found->second >= kPairRefusals;
}

gpu::Pipeline* Pipelines::create_now(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
    std::atomic<gpu::Pipeline*>* at = cell(id, variant, aux);
    if (at == nullptr) {
        return nullptr;
    }
    if (gpu::Pipeline* existing = at->load(std::memory_order_acquire); existing != nullptr) {
        return existing;
    }
    {
        std::lock_guard guard(lock_);
        if (refused_.count(std::uint64_t{id} << 9u | key_of(variant, aux)) != 0u) {
            return nullptr;
        }
    }
    if (pair_refused(entry)) {
        return nullptr;
    }
    ++misses_;
    // Where pipelines link from parts, the draw gets a quick link and a worker replaces it with the optimised one.
    gpu::Pipeline* built = build_fast(entry, variant, aux);
    const bool linked = built != nullptr;
    if (built == nullptr) {
        built = build(entry, variant, aux);
    }
    if (built == nullptr) {
        refuse(id, entry, variant, aux);
        return nullptr;
    }
    gpu::Pipeline* const published = publish(*at, built);
    if (linked && published == built) {
        {
            std::lock_guard guard(lock_);
            if (!quit_) {
                pending_.fetch_add(1u, std::memory_order_relaxed);
                jobs_.push_back(Job{static_cast<std::uint32_t>(id), entry, variant, aux, true});
            }
        }
        wake_.notify_one();
    }
    return published;
}

// Whether the tracer's prepass draws this entry: an opaque surface that writes depth through a test that passes an equal
// depth (the pass after it finds its own depth there), and no stencil (its marks are the scene pass's).
bool prepass_draws(std::uint32_t state) noexcept {
    const std::uint32_t test = (state >> 11u) & 7u;  // the game's: 3 less or equal, 6 greater or equal, 7 always
    return (state & NEBULA_STATE_COLOR_WRITE) != 0u && (state & 3u) == NEBULA_BLEND_REPLACE && (state & (1u << 10u)) != 0u &&
           (state & (1u << 14u)) != 0u && test == 3u && ((state >> 20u) & 3u) == 0u && ((state >> 15u) & 3u) != NEBULA_CULL_ALL;
}

bool blended_prepass_draws(std::uint32_t state) noexcept {
    return (state & NEBULA_STATE_COLOR_WRITE) != 0u && (state & 3u) == NEBULA_BLEND_ADD && ((state >> 2u) & 15u) == NEBULA_FACTOR_SRC_ALPHA &&
           ((state >> 6u) & 15u) == NEBULA_FACTOR_INV_SRC_ALPHA && (state & (1u << 10u)) != 0u && ((state >> 20u) & 3u) == 0u &&
           ((state >> 15u) & 3u) != NEBULA_CULL_ALL;
}

bool added_prepass_draws(std::uint32_t state) noexcept {
    return (state & NEBULA_STATE_COLOR_WRITE) != 0u && (state & 3u) == NEBULA_BLEND_ADD && ((state >> 6u) & 15u) == NEBULA_FACTOR_ONE &&
           (state & (1u << 10u)) != 0u && ((state >> 20u) & 3u) == 0u && ((state >> 15u) & 3u) != NEBULA_CULL_ALL;
}

void Pipelines::prepare(std::uint32_t id, const PipelineEntry& entry, const std::vector<unsigned>& variants, bool motion, bool layer,
                        bool one_pass, bool traced) {
    const auto add = [&](unsigned variant, unsigned aux) {
        pending_.fetch_add(1u, std::memory_order_relaxed);
        jobs_.push_back(Job{id, entry, variant, aux});
    };
    {
        std::lock_guard guard(lock_);
        if (quit_) {
            return;
        }
        // The tracer's forms of a scene draw (no target, one sample): its prepass, and a lit program's traced forms under
        // the aux the scene pass gives an opaque draw.
        // An added state (FB b94): its prepass and exchange forms, for the glass rows' shells among its draws.
        const bool added = traced && added_prepass_draws(entry.desc.state);
        const bool prepass = traced && (prepass_draws(entry.desc.state) || blended_prepass_draws(entry.desc.state) || added);
        const bool lit = prepass && program_traced(entry.desc.pixel);
        const bool blended = blended_prepass_draws(entry.desc.state) || added;
        const unsigned exchange = added ? kAuxExchange : 0u;
        for (const unsigned variant : variants) {
            add(variant, 0u);
            const bool scene = (variant & kSceneVariantTarget) == 0u && (variant & kSceneVariantSamplesMask) == 0u;
            if (scene && prepass) {
                // The aux the scene pass gives it (draw_walker scene_aux): with motion written, an opaque draw the
                // motion (1), a blended one the reactive mask (2; 3 in a layer window too), or with one pass for the
                // scene the motion target left alone (4); without motion, none. The prepass runs the same vertex form.
                const bool writes_motion = motion && !blended;
                add(variant, writes_motion ? kAuxPrepassMotion : kAuxPrepass);
                if (lit) {
                    if (!motion || !blended) {
                        add(variant, kAuxTraced | exchange | (writes_motion ? 1u : 0u));
                    } else if (one_pass) {
                        add(variant, kAuxTraced | exchange | 4u);
                    } else {
                        add(variant, kAuxTraced | exchange | 2u);
                        if (layer) {
                            add(variant, kAuxTraced | exchange | 3u);
                        }
                    }
                }
            }
            if (!motion || !scene) {
                continue;
            }
            add(variant, 1u);
            if (one_pass) {
                add(variant, 4u);
                continue;
            }
            add(variant, 2u);
            if (layer) {
                add(variant, 3u);
            }
        }
    }
    wake_.notify_all();
}

void Pipelines::worker() {
#if defined(_WIN32)
    // Lowest priority; started on battery, it also asks for efficiency (windows_power.h).
    galaxy::host::lower_current_thread_priority();
#elif defined(__APPLE__)
    // The class for long work nobody waits on: the frame's threads (perf_hint.cpp: user-interactive) always run first,
    // and what the compiler does in the system's service for this thread runs at the same class.
    (void)pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
    for (;;) {
        Job job;
        {
            std::unique_lock guard(lock_);
            wake_.wait(guard, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) {
                return;
            }
            job = jobs_.front();
            jobs_.pop_front();
        }
        try {
            std::atomic<gpu::Pipeline*>* at = cell(job.id, job.variant, job.aux);
            if (job.optimize) {
                // The draw's pipeline was linked quickly: the optimised link takes its place for the draws after it (frames in
                // flight keep the first, which lives until shutdown).
                if (at != nullptr && !pair_refused(job.entry)) {
                    if (gpu::Pipeline* better = build(job.entry, job.variant, job.aux); better != nullptr) {
                        at->store(better, std::memory_order_release);
                    }
                }
            } else if (at != nullptr && at->load(std::memory_order_acquire) == nullptr && !pair_refused(job.entry)) {
                gpu::Pipeline* built = build(job.entry, job.variant, job.aux);
                if (built != nullptr) {
                    publish(*at, built);
                } else {
                    refuse(job.id, job.entry, job.variant, job.aux);
                }
            }
        } catch (...) {
            // A build that throws (out of memory) is a refusal of that pipeline; an exception out of a worker would end the process.
            try {
                if (!job.optimize) {
                    refuse(job.id, job.entry, job.variant, job.aux);
                }
            } catch (...) {
            }
        }
        if (pending_.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            drained();
        }
    }
}

// The cache files (the driver's pipeline library or cache, both back ends) are written twice at most in a run, never on the
// render thread and never in the middle of a frame's work: when the first queue drains (warm-up done, here, on the worker that
// finished it), so the next boot loads it whatever way this one ends, and at exit (destroy_all). Each write is whole or absent:
// a temporary file renamed over (replace_file), one writer at a time. Pipelines made later in the run, by what a scene change
// registers or by a draw, join the file at exit.
void Pipelines::drained() noexcept {
    if (reported_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    const std::size_t written = unsaved_.exchange(false, std::memory_order_acq_rel) ? save_cache() : 0u;
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
        std::fprintf(stderr, "[pipelines] %llu prepared by %u workers at %lld ms (%llu made by draws first, %llu refused), cache %zu bytes written\n",
                     static_cast<unsigned long long>(created_.load(std::memory_order_relaxed)), workers_, static_cast<long long>(ms),
                     static_cast<unsigned long long>(misses_.load(std::memory_order_relaxed)), static_cast<unsigned long long>(failures_.load(std::memory_order_relaxed)),
                     written);
    }
}

bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return io::write_file_atomically(file, std::string_view(bytes, size));
}

}  // namespace galaxy::render::core
