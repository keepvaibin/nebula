// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "galaxy/cpu_count.h"
#if defined(_WIN32)
#include "galaxy/windows_power.h"
#endif

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#if defined(NEBULA_DEV)
#include "galaxy/render/render_config.h"

#include <cinttypes>
#include <cstdlib>
#include <sstream>
#include <string>
#endif

namespace galaxy::render::core {

#if defined(NEBULA_DEV)
namespace {

// The use order of the pipelines (see pipelines.h): this run's, by first draw, and the file the last runs left.
struct UseOrder {
    std::mutex lock;
    bool loaded = false;
    std::unordered_map<std::uint64_t, std::uint32_t> file_rank;  // key -> place in the file
    std::vector<std::uint64_t> file_keys;                         // the file's keys, in order
    std::vector<std::uint32_t> slot_of_id;                        // pipeline id -> 1 + index in `drawn`, 0 none
    struct Drawn {
        std::uint64_t key;
        std::uint64_t draws;
    };
    std::vector<Drawn> drawn;  // this run's pipelines, in first-draw order
};
UseOrder gUse;

std::filesystem::path use_file() {
    return shader_cache_directory() / "pipeline_use.tsv";
}

// A pipeline's identity, field by field as the registry's.
std::uint64_t use_key(const PipelineEntry& entry) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash = (hash ^ value) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29u;
    };
    mix(entry.desc.vertex);
    mix(entry.desc.pixel);
    mix((std::uint64_t{entry.desc.state} << 32u) | entry.desc.input_count);
    mix(entry.desc.position_stride);
    for (const NebulaLayoutInput& input : entry.inputs) {
        mix((std::uint64_t{input.semantic} << 16u) | (std::uint64_t{input.stream} << 8u) | input.format);
        mix((std::uint64_t{input.offset} << 32u) | input.stride);
    }
    return hash;
}

void load_use_file_locked() {
    if (gUse.loaded) {
        return;
    }
    gUse.loaded = true;
    std::ifstream in(use_file());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::uint64_t key = std::strtoull(line.c_str(), nullptr, 16);
        if (gUse.file_rank.emplace(key, static_cast<std::uint32_t>(gUse.file_keys.size())).second) {
            gUse.file_keys.push_back(key);
        }
    }
}

// Writes this run's order (and the file's unused pipelines after it) whole through a temporary file.
void save_use_file() {
    std::lock_guard guard(gUse.lock);
    if (gUse.drawn.empty()) {
        return;  // a run that drew nothing keeps the file it has
    }
    std::string text = "# pipeline key, first draw order, draws; read by the next run's warm-up (pipelines.h)";
    text += '\n';
    char row[96];
    std::unordered_set<std::uint64_t> written;
    for (const UseOrder::Drawn& item : gUse.drawn) {
        std::snprintf(row, sizeof(row), "%016" PRIx64 "\t%" PRIu64 "\n", item.key, item.draws);
        text += row;
        written.insert(item.key);
    }
    for (const std::uint64_t key : gUse.file_keys) {
        if (written.insert(key).second) {
            std::snprintf(row, sizeof(row), "%016" PRIx64 "\t0\n", key);
            text += row;
        }
    }
    static_cast<void>(replace_file(use_file(), text.data(), text.size()));
}

}  // namespace

void note_pipeline_use(std::uint32_t id, const PipelineEntry& entry) {
    std::lock_guard guard(gUse.lock);
    if (id >= gUse.slot_of_id.size()) {
        gUse.slot_of_id.resize(std::size_t{id} + 1024u, 0u);
    }
    std::uint32_t& slot = gUse.slot_of_id[id];
    if (slot == 0u) {
        gUse.drawn.push_back({use_key(entry), 0u});
        slot = static_cast<std::uint32_t>(gUse.drawn.size());
    }
    ++gUse.drawn[slot - 1u].draws;
}

std::uint32_t pipeline_use_rank(const PipelineEntry& entry) {
    std::lock_guard guard(gUse.lock);
    load_use_file_locked();
    const auto found = gUse.file_rank.find(use_key(entry));
    return found != gUse.file_rank.end() ? found->second : kPipelineUnranked;
}
#endif

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
#if defined(NEBULA_DEV)
    try {
        save_use_file();
    } catch (...) {
        // a test aid: a failed write loses the order, nothing else
    }
#endif
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
#if defined(_WIN32)
    // Lowest priority; started on battery, it also asks for efficiency (windows_power.h).
    galaxy::host::lower_current_thread_priority();
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
        std::atomic<gpu::Pipeline*>* at = cell(job.id, job.variant, job.aux);
        if (job.optimize) {
            // The draw's pipeline was linked quickly: the optimised link takes its place for the draws after it (frames in flight
            // keep the first, which lives until shutdown).
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
