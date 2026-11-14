// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "galaxy/atomic_file.h"
#include "galaxy/cpu_count.h"
#include "galaxy/windows_power.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "galaxy/render/program_table.h"
#include "render/core/pipeline_cache_file.h"

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
    // The forms stored before the cache's last write are the ones it holds.
    std::vector<std::uint64_t> stored;
    try {
        stored = stored_forms();
    } catch (...) {
    }
    destroy_all();
    save_targets(stored);
    refused_.clear();
    for (auto& root : directories_) {
        if (Directory* directory = root.exchange(nullptr)) {
            for (auto& page : directory->pages) { delete page.load(); }
            for (auto& page : directory->traced_pages) { delete page.load(); }
            delete directory;
        }
    }
}

std::atomic<gpu::Pipeline*>* Pipelines::cell(std::uint64_t id, unsigned variant, unsigned aux) {
    const std::uint32_t key = key_of(variant, aux);
    const bool traced = key >= kKeys;
    Page* page = page_of(id, traced);
    if (page == nullptr) {
        const auto root = static_cast<std::size_t>(id >> (kPageBits + kDirectoryBits));
        if (root >= directories_.size()) { throw std::out_of_range("pipeline ID outside the native namespaces"); }
        std::lock_guard guard(lock_);
        Directory* directory = directories_[root].load(std::memory_order_acquire);
        if (directory == nullptr) {
            directory = new Directory();
            directories_[root].store(directory, std::memory_order_release);
        }
        auto& at = (traced ? directory->traced_pages : directory->pages)[(id >> kPageBits) & (kDirectorySize - 1u)];
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
    if (!linked && target_form(variant, aux)) {
        try {
            if (targets_open()) {
                note_target_stored(form_key(entry, variant, aux));
            }
        } catch (...) {
        }
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
        const bool prepass = traced && (prepass_draws(entry.desc.state) || blended_prepass_draws(entry.desc.state));
        const bool lit = prepass && program_traced(entry.desc.pixel);
        const bool blended = blended_prepass_draws(entry.desc.state);
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
                        add(variant, kAuxTraced | (writes_motion ? 1u : 0u));
                    } else if (one_pass) {
                        add(variant, kAuxTraced | 4u);
                    } else {
                        add(variant, kAuxTraced | 2u);
                        if (layer) {
                            add(variant, kAuxTraced | 3u);
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
    // Below the frame's threads on every platform (windows_power.h); on Windows, started on battery, it also asks for
    // efficiency, and on Apple what the compiler does in the system's service for this thread runs at the same class.
    galaxy::host::lower_current_thread_priority();
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
            // A texture target's form, where the targets file is kept: its key in the file's sets (0, prepared as any other,
            // when there is none or memory ran out finding it).
            std::uint64_t form = 0u;
            if (target_form(job.variant, job.aux)) {
                try {
                    form = targets_open() ? form_key(job.entry, job.variant, job.aux) : 0u;
                } catch (...) {
                }
            }
            if (job.optimize) {
                // The draw's pipeline was linked quickly: the optimised link takes its place for the draws after it (frames in
                // flight keep the first, which lives until shutdown).
                if (at != nullptr && !pair_refused(job.entry)) {
                    if (gpu::Pipeline* better = build(job.entry, job.variant, job.aux); better != nullptr) {
                        at->store(better, std::memory_order_release);
                        note_target_stored(form);
                    }
                }
            } else if (at != nullptr && at->load(std::memory_order_acquire) == nullptr && !pair_refused(job.entry)) {
                if (left_to_cache(form)) {
                    left_.fetch_add(1u, std::memory_order_relaxed);
                } else if (gpu::Pipeline* built = build(job.entry, job.variant, job.aux); built != nullptr) {
                    note_target_stored(form);
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
    // The forms stored before the cache's write are the ones it holds.
    std::vector<std::uint64_t> stored;
    try {
        stored = stored_forms();
    } catch (...) {
    }
    const std::size_t written = unsaved_.exchange(false, std::memory_order_acq_rel) ? save_cache() : 0u;
    save_targets(stored);
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
        std::fprintf(stderr, "[pipelines] %llu prepared by %u workers at %lld ms (%llu made by draws first, %llu refused, %llu texture "
                     "target forms left to the cache), cache %zu bytes written\n",
                     static_cast<unsigned long long>(created_.load(std::memory_order_relaxed)), workers_, static_cast<long long>(ms),
                     static_cast<unsigned long long>(misses_.load(std::memory_order_relaxed)), static_cast<unsigned long long>(failures_.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(left_.load(std::memory_order_relaxed)), written);
    }
}

// ---- The texture targets' forms (pipelines.h, targets_open).

namespace {

// The targets file: the magic, the hash of the cache data it goes with, the counts of the two sets, then the stored forms' keys
// and the drawn forms' keys, eight bytes each.
constexpr char kTargetsMagic[8] = {'N', 'B', 'T', 'G', 'T', 'S', '0', '1'};
constexpr std::size_t kTargetsHeader = 8u + 8u + 4u + 4u;
// More keys than the game has pipelines is a file from something else.
constexpr std::uint32_t kTargetsMost = 1u << 20u;

std::uint64_t mix(std::uint64_t hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

}  // namespace

bool Pipelines::targets_open() {
    {
        std::lock_guard guard(targets_lock_);
        if (targets_tried_) {
            return targets_on_;
        }
    }
    // Opening the driver cache takes the backend's own locks: outside this one.
    std::filesystem::path file;
    std::uint64_t accepted = 0u;
    const bool kept = cache_file_state(file, accepted);
    std::lock_guard guard(targets_lock_);
    if (targets_tried_) {
        return targets_on_;
    }
    targets_tried_ = true;
    if (!kept || file.empty()) {
        return false;
    }
    targets_file_ = file;
    targets_file_ += ".targets";
    targets_on_ = true;
    std::ifstream in(targets_file_, std::ios::binary | std::ios::ate);
    if (!in) {
        return true;
    }
    const auto length = static_cast<std::size_t>(in.tellg());
    if (length < kTargetsHeader) {
        return true;
    }
    std::vector<char> bytes(length);
    in.seekg(0);
    in.read(bytes.data(), static_cast<std::streamsize>(length));
    if (!in || std::memcmp(bytes.data(), kTargetsMagic, sizeof(kTargetsMagic)) != 0) {
        return true;
    }
    std::uint64_t hash = 0u;
    std::uint32_t stored = 0u, drawn = 0u;
    std::memcpy(&hash, bytes.data() + 8u, 8u);
    std::memcpy(&stored, bytes.data() + 16u, 4u);
    std::memcpy(&drawn, bytes.data() + 20u, 4u);
    targets_hash_ = hash;
    if (stored > kTargetsMost || drawn > kTargetsMost ||
        length != kTargetsHeader + (std::size_t{stored} + std::size_t{drawn}) * 8u) {
        return true;
    }
    const auto key_at = [&](std::size_t i) {
        std::uint64_t key;
        std::memcpy(&key, bytes.data() + kTargetsHeader + i * 8u, 8u);
        return key;
    };
    // What a target pass drew stands whatever the cache; what the cache holds only if it is the data the driver took.
    for (std::size_t i = 0; i < drawn; ++i) {
        target_drawn_.insert(key_at(std::size_t{stored} + i));
    }
    if (accepted != 0u && hash == accepted) {
        for (std::size_t i = 0; i < stored; ++i) {
            target_cached_.insert(key_at(i));
            target_stored_.insert(key_at(i));
        }
    }
    return true;
}

// Every field of the description and inputs, the target and aux, and the programs' bytes, so a program cooked again under the
// same id (a game update) is another form, which no cache of an earlier build holds. 0: no programs.
std::uint64_t Pipelines::form_key(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    const NebulaPipelineDesc& desc = entry.desc;
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program =
        program_variant(desc.pixel, aux, (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    const ProgramBytes vs = program_bytes(desc.vertex, aux == kAuxPrepassMotion ? unsigned{kProgramTracedMotion} : program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        return 0u;
    }
    // A program's bytes are hashed once a run: many pipelines share one.
    const auto program_hash = [this](const ProgramBytes& bytes) {
        {
            std::lock_guard guard(targets_lock_);
            if (const auto found = program_hashes_.find(bytes.data); found != program_hashes_.end()) {
                return found->second;
            }
        }
        const std::uint64_t hash = cache_bytes_hash(bytes.data, bytes.size);
        std::lock_guard guard(targets_lock_);
        program_hashes_.emplace(bytes.data, hash);
        return hash;
    };
    std::uint64_t hash = 14695981039346656037ull;
    hash = mix(hash, &desc.vertex, sizeof(desc.vertex));
    hash = mix(hash, &desc.pixel, sizeof(desc.pixel));
    hash = mix(hash, &desc.state, sizeof(desc.state));
    hash = mix(hash, &desc.input_count, sizeof(desc.input_count));
    hash = mix(hash, &desc.position_stride, sizeof(desc.position_stride));
    for (const NebulaLayoutInput& input : entry.inputs) {
        hash = mix(hash, &input.semantic, sizeof(input.semantic));
        hash = mix(hash, &input.stream, sizeof(input.stream));
        hash = mix(hash, &input.format, sizeof(input.format));
        hash = mix(hash, &input.offset, sizeof(input.offset));
        hash = mix(hash, &input.stride, sizeof(input.stride));
    }
    const std::uint64_t values[] = {program, variant, aux, program_hash(vs), program_hash(ps)};
    hash = mix(hash, values, sizeof(values));
    return hash != 0u ? hash : 1u;
}

bool Pipelines::left_to_cache(std::uint64_t form) {
    if (form == 0u) {
        return false;
    }
    std::lock_guard guard(targets_lock_);
    return target_cached_.count(form) != 0u && target_drawn_.count(form) == 0u;
}

void Pipelines::note_target_drawn(std::uint64_t id, const PipelineEntry& entry, unsigned variant, unsigned aux) {
    Page* page = page_of(id, key_of(variant, aux) >= kKeys);
    if (page == nullptr || page->drawn[id & kPageMask].exchange(1u, std::memory_order_relaxed) != 0u) {
        return;
    }
    try {
        if (!targets_open()) {
            return;
        }
        const std::uint64_t form = form_key(entry, variant, aux);
        if (form == 0u) {
            return;
        }
        std::lock_guard guard(targets_lock_);
        if (target_drawn_.insert(form).second) {
            targets_unsaved_ = true;
        }
    } catch (...) {
        // Memory ran out: the form is prepared again next run, as before.
    }
}

void Pipelines::note_target_stored(std::uint64_t form) noexcept {
    if (form == 0u) {
        return;
    }
    try {
        std::lock_guard guard(targets_lock_);
        if (target_stored_.insert(form).second) {
            targets_unsaved_ = true;
        }
    } catch (...) {
        // Memory ran out: the form is not named in the file, and is prepared next run.
    }
}

std::vector<std::uint64_t> Pipelines::stored_forms() {
    std::lock_guard guard(targets_lock_);
    if (!targets_on_) {
        return {};
    }
    return {target_stored_.begin(), target_stored_.end()};
}

// `stored` was taken before the cache's last write, so the data now on disk holds every one of them.
void Pipelines::save_targets(const std::vector<std::uint64_t>& stored) noexcept {
    try {
        std::vector<char> bytes;
        std::filesystem::path file;
        {
            std::lock_guard guard(targets_lock_);
            // Written when a set grew or the cache file was written since: the file always names the data on disk.
            const std::uint64_t hash = targets_on_ ? cache_file_hash() : 0u;
            if (hash == 0u || (!targets_unsaved_ && hash == targets_hash_)) {
                return;
            }
            const auto stored_count = static_cast<std::uint32_t>(stored.size());
            const auto drawn_count = static_cast<std::uint32_t>(target_drawn_.size());
            bytes.resize(kTargetsHeader + (std::size_t{stored_count} + std::size_t{drawn_count}) * 8u);
            char* at = bytes.data();
            std::memcpy(at, kTargetsMagic, sizeof(kTargetsMagic));
            std::memcpy(at + 8u, &hash, 8u);
            std::memcpy(at + 16u, &stored_count, 4u);
            std::memcpy(at + 20u, &drawn_count, 4u);
            at += kTargetsHeader;
            for (const std::uint64_t key : stored) {
                std::memcpy(at, &key, 8u);
                at += 8u;
            }
            for (const std::uint64_t key : target_drawn_) {
                std::memcpy(at, &key, 8u);
                at += 8u;
            }
            // Forms stored since `stored` was taken wait for the next write (the set only grows).
            targets_unsaved_ = target_stored_.size() != stored.size();
            targets_hash_ = hash;
            file = targets_file_;
        }
        if (!replace_file(file, bytes.data(), bytes.size())) {
            std::lock_guard guard(targets_lock_);
            targets_unsaved_ = true;
            targets_hash_ = 0u;
        }
    } catch (...) {
    }
}

bool replace_file(const std::filesystem::path& file, const char* bytes, std::size_t size) noexcept {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return io::write_file_atomically(file, std::string_view(bytes, size));
}

}  // namespace galaxy::render::core
