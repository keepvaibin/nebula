// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "galaxy/cpu_count.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

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
    // The title waits for the list once; every core but the game's prepares it. Idle workers cost nothing.
    const unsigned hardware = std::max(2u, logical_cores());
    const unsigned count = hardware - 1u;
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
            delete directory;
        }
    }
}

std::atomic<gpu::Pipeline*>* Pipelines::cell(std::uint64_t id, unsigned variant, unsigned aux) {
    Page* page = page_of(id);
    if (page == nullptr) {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { throw std::out_of_range("pipeline ID outside the native namespaces"); }
        std::lock_guard guard(lock_);
        Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) {
            directory = new Directory();
            directories_[root].store(directory, std::memory_order_release);
        }
        auto& at = directory->pages[(id >> kPageBits) & (kDirectorySize - 1u)];
        page = at.load(std::memory_order_acquire);
        if (page == nullptr) { page = new Page(); at.store(page, std::memory_order_release); }
    }
    return &page->rows[id & kPageMask][key_of(variant, aux)];
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
    refused_.insert(std::uint64_t{id} << 8u | key_of(variant, aux));
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
        if (refused_.count(std::uint64_t{id} << 8u | key_of(variant, aux)) != 0u) {
            return nullptr;
        }
    }
    if (pair_refused(entry)) {
        return nullptr;
    }
    ++misses_;
    gpu::Pipeline* built = build(entry, variant, aux);
    if (built == nullptr) {
        refuse(id, entry, variant, aux);
        return nullptr;
    }
    return publish(*at, built);
}

void Pipelines::prepare(std::uint32_t id, const PipelineEntry& entry, const std::vector<unsigned>& variants, bool motion, bool layer,
                        bool one_pass) {
    const auto add = [&](unsigned variant, unsigned aux) {
        pending_.fetch_add(1u, std::memory_order_relaxed);
        jobs_.push_back(Job{id, entry, variant, aux});
    };
    {
        std::lock_guard guard(lock_);
        if (quit_) {
            return;
        }
        for (const unsigned variant : variants) {
            add(variant, 0u);
            if (!motion || (variant & kSceneVariantTarget) != 0u || (variant & kSceneVariantSamplesMask) != 0u) {
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
        std::atomic<gpu::Pipeline*>* at = cell(job.id, job.variant, job.aux);
        if (at != nullptr && at->load(std::memory_order_acquire) == nullptr && !pair_refused(job.entry)) {
            gpu::Pipeline* built = build(job.entry, job.variant, job.aux);
            if (built != nullptr) {
                publish(*at, built);
            } else {
                refuse(job.id, job.entry, job.variant, job.aux);
            }
        }
        if (pending_.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            drained();
        }
    }
}

// Every queued pipeline is made: the driver's cache goes to disk now, off the render thread, so the next boot loads it
// whatever way this one ends. Pipelines a draw made itself are written with the next list.
void Pipelines::drained() noexcept {
    const std::size_t written = unsaved_.exchange(false, std::memory_order_acq_rel) ? save_cache() : 0u;
    if (!reported_.exchange(true, std::memory_order_relaxed)) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
        std::fprintf(stderr, "[pipelines] %llu prepared at %lld ms, cache %zu bytes written\n",
                     static_cast<unsigned long long>(created_.load(std::memory_order_relaxed)), static_cast<long long>(ms), written);
    }
}

bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(bytes, static_cast<std::streamsize>(size));
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(temp, file, error);
    return !error;
}

}  // namespace galaxy::render::core
