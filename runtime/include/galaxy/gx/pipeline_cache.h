#pragma once

// PipelineCache: GX shader/PSO cache with synchronous first-use compilation.
//
// Threading contract (binding, per architecture review):
//   - get() and drain_completions() are RENDER-THREAD-ONLY.  The published
//     lookup map is never touched by any other thread.
//   - Worker threads compile specialized shaders and build PSOs, then push
//     the finished pipeline onto the lock-free MPSC PsoCompletionQueue.
//     They never mutate the published map.
//   - GxBackend calls drain_completions() from RendererD3D12::begin_frame(),
//     before any command-list recording — the sole publication point, so a
//     PSO pointer can never change while a list is recording.
//
// Lookup policy: if the specialized PSO for the key is published, return it;
// otherwise compile it immediately. Returning a missing/uber pipeline before
// M8 has real ubershaders causes visible missing geometry, so first-use hitches
// are preferred over black/flickering frames. Disk DXBC caching keeps repeat
// runs out of FXC; optional prewarm/pipeline-library paths can reduce driver
// PSO hitches when explicitly enabled.
//
// Disk cache, %LOCALAPPDATA%\Nebula\shadercache\RMGE01\:
//   - shaders.bin   {magic, version, vs/ps hash, vs/ps DXBC} records;
//                   loaded at initialize; valid previously saved stages avoid
//                   FXC. New configurations and rejected/unsaved stages may
//                   still require compilation on subsequent runs.
//   - psos.bin      successful PsoKey records, used by optional
//                   startup prewarm.
//   - pipelines.bin optional ID3D12PipelineLibrary1 blob, invalidated when the
//                   stored adapter LUID or shader-cache version doesn't match,
//                   or the driver rejects its serialized data.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <exception>
#include <mutex>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace galaxy::gx {

// Shared production PSO descriptor builder. Point/line topology selects the
// matching mandatory geometry shader; triangles deliberately select none.
// Exposed so focused WARP tests validate the exact production assembly path.
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    ID3DBlob* vertex_shader,
    ID3DBlob* pixel_shader,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader);

// Lock-free multi-producer single-consumer queue (Vyukov intrusive list).
// Producers: compile workers.  Consumer: the render thread, exclusively.
class PsoCompletionQueue {
public:
    struct Item {
        PsoKey key{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };

    PsoCompletionQueue();
    ~PsoCompletionQueue();
    PsoCompletionQueue(const PsoCompletionQueue&) = delete;
    PsoCompletionQueue& operator=(const PsoCompletionQueue&) = delete;

    // Any thread; lock-free (one atomic exchange + one store).
    void push(Item item);

    // Render thread only.  Returns false when the queue is empty.
    bool try_pop(Item& out);

private:
    struct Node;
    std::atomic<Node*> head_;       // producers swing this
    Node* tail_;                    // consumer-owned cursor
};

class PipelineCache {
public:
    ~PipelineCache() noexcept;
    // `root_signature` is the single shared GX root signature; `cache_dir`
    // is created if missing.  Spawns `worker_count` compile threads (1-2 on
    // low-end CPUs) and warms PSOs from the disk cache.
    bool initialize(
        ID3D12Device* device,
        ID3D12RootSignature* root_signature,
        const std::filesystem::path& cache_dir,
        unsigned worker_count);

    void shutdown();                // joins workers, flushes disk

    // RENDER THREAD ONLY.  May block on first use of a new material/state;
    // see lookup policy above.
    [[nodiscard]] ID3D12PipelineState* get(
        const PsoKey& key,
        const PixelShaderKey& ps_key,
        const VertexShaderKey& vs_key);

    // RENDER THREAD ONLY, called from begin_frame(): publishes every
    // completed specialization into the lookup map.
    void drain_completions();

    // Preserves valid records and atomically replaces shader/key files. Called
    // periodically (e.g. every N frames) and from shutdown().
    void flush_disk();

    // RENDER THREAD ONLY. Snapshot CPU cache records and persist them without
    // holding up frame production. At most one batch is outstanding. This
    // never serializes or borrows a live driver pipeline library.
    void flush_disk_async();
    void poll_disk_flush();

    // RENDER THREAD ONLY. True when shader pairs or PSO keys learned this run
    // are not yet on disk (an abnormal exit would lose them).
    [[nodiscard]] bool has_unsaved_records() const {
        return !unsaved_blobs_.empty() || !unsaved_pso_keys_.empty();
    }

    // Stats for the on-screen debug overlay / logs.
    [[nodiscard]] std::uint64_t specialized_hits() const;
    [[nodiscard]] std::uint64_t uber_fallbacks() const;

    // Routine, clock-free compile accounting. Every first-use pipeline this
    // process had to build is counted here, including the ones far below
    // `GALAXY_TRACE_GX_STALL_US` that the per-miss log line cannot show: the
    // render thread's `flush-pso-us` long tail is the sum of these, and a run
    // that reports few logged misses can still have built hundreds. Cache
    // coverage is the whole question for this path, so these are the numbers
    // that falsify a coverage change without enabling intrusive diagnostics.
    struct CompileStats {
        std::uint64_t pso_compiles = 0;      // create_or_load_pso builds
        std::uint64_t pso_library_loads = 0; // served by the driver library
        std::uint64_t shader_pair_compiles = 0; // FXC-generated pairs
        std::uint64_t blobs_from_cache = 0;  // pairs whose DXBC came off disk
        // Cumulative microseconds of FXC shader compilation this session. The
        // counts above say how many pairs were generated; this says what they
        // cost. Without it the only time figures are the per-request `blob-us=`
        // fields on `[gx-pso-miss]` lines, which print only when a SINGLE
        // request crosses the stall threshold -- so a frame that compiles four
        // shaders at 9 ms each reports nothing, and the logged count is a floor
        // rather than the count.
        std::uint64_t blob_compile_us = 0;
        std::size_t shader_records = 0;      // in-memory (vs,ps) pairs
        std::size_t shader_records_on_disk = 0;
        std::size_t pso_keys = 0;
        std::size_t pso_keys_on_disk = 0;
        bool shader_cache_at_capacity = false;
        bool pso_cache_at_capacity = false;
    };
    [[nodiscard]] CompileStats compile_stats() const;

private:
    friend struct PipelineCachePersistenceTestAccess;

    struct CompileJob {
        PsoKey key;
        PixelShaderKey ps_key;
        VertexShaderKey vs_key;
        Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
        bool use_cached_blobs = false;
    };

    // Render-thread-only published map.
    std::unordered_map<MemoizedPsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>,
        MemoizedPsoKeyHasher> published_;
    // One-entry memo of the last successful resolution, sitting in front of the
    // unordered_map probe. A busy frame resolves the same handful of PsoKeys
    // tens of thousands of times in runs (one material alternating with a few
    // others), and `flush_draw_state`'s own key comparison only catches a repeat
    // of the immediately preceding key. This catches the alternation.
    //
    // Safe because `published_` is never erased from for the life of a session
    // (only cleared in shutdown()), so an entry that resolved once stays
    // resolved to the same pipeline. `last_hit_owner_` keeps that pipeline alive
    // and the `key`/`pipeline` pair is only ever written together, so the memo
    // can never name a destroyed PSO. Any path that can observe a new
    // completion (drain_completions, reached from get() or begin_frame) clears
    // the memo, so it cannot mask a key that became resolvable later.
    MemoizedPsoKey last_hit_key_{};
    ID3D12PipelineState* last_hit_pipeline_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> last_hit_owner_;
    void clear_last_hit_memo() noexcept {
        last_hit_pipeline_ = nullptr;
        last_hit_owner_.Reset();
    }
    // Keys with a specialization already in flight (render-thread-only).
    std::unordered_set<MemoizedPsoKey, MemoizedPsoKeyHasher> in_flight_;
    // Uber PSOs per RenderStateKey hash, created lazily from embedded DXBC.
    std::unordered_map<std::uint64_t,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>> uber_;

    PsoCompletionQueue completions_;

    // Queue draining stays lock-free on the render owner. Only an actual wait
    // or worker completion takes this mutex to make notification persistent.
    std::mutex job_mutex_;
    std::condition_variable job_cv_;
    std::condition_variable completion_cv_;
    // Protected by job_mutex_; captured before draining, advanced only after
    // completion publication. This closes the drain-to-sleep notification gap.
    std::uint64_t completion_sequence_{};
    [[nodiscard]] std::uint64_t completion_snapshot();
    void wait_for_worker_completion(std::uint64_t observed,
                                    std::chrono::milliseconds timeout);
    std::exception_ptr worker_error_;
    // Fast, lock-free gate for worker_error_. `worker_error_` is std::exception_ptr
    // (non-trivially-copyable), so it cannot itself be atomic; this flag is set
    // under job_mutex_ before worker_error_ is published and cleared under the
    // same lock. The render thread's hot path only pays one relaxed load and
    // takes job_mutex_ solely when a worker actually failed, instead of
    // acquiring a process-wide mutex on every pipeline request.
    std::atomic<bool> worker_failed_{false};
    std::deque<CompileJob> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3DBlob> line_geometry_shader_;
    Microsoft::WRL::ComPtr<ID3DBlob> point_geometry_shader_;
    // CreatePipelineLibrary borrows these bytes until the library is released.
    // Declared before the COM owner so implicit destruction also releases it first.
    std::vector<std::uint8_t> pipeline_library_blob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> pipeline_library_;
    std::filesystem::path cache_dir_;
    ShaderGenerator generator_;

    // Render-thread-only counters, exactly like `published_` and the
    // `last_hit_*` memo above: `get()` and `wait_for_in_flight_pso()` are the
    // only writers and both run only on the render thread, so a locked
    // read-modify-write per resolution would buy nothing. At tens of thousands
    // of resolutions per frame that was tens of thousands of `lock` operations
    // on the render thread's hottest path.
    std::uint64_t specialized_hits_ = 0;
    std::uint64_t uber_fallbacks_ = 0;

    // Compiled DXBC pair shared by every render-state variant of one
    // (vs_hash, ps_hash) — a new blend/z mode never re-runs FXC, only the
    // driver PSO build. Full component-pair equality resolves bucket collisions.
    struct ShaderPair {
        Microsoft::WRL::ComPtr<ID3DBlob> vs;
        Microsoft::WRL::ComPtr<ID3DBlob> ps;
        std::uint64_t vs_hash = 0;
        std::uint64_t ps_hash = 0;
    };
    struct ShaderPairKey {
        std::uint64_t vs_hash = 0;
        std::uint64_t ps_hash = 0;
        bool operator==(const ShaderPairKey&) const = default;
    };
    struct ShaderPairKeyHasher {
        std::size_t operator()(const ShaderPairKey& key) const {
            return static_cast<std::size_t>(pair_hash(key.vs_hash, key.ps_hash));
        }
    };
    std::unordered_map<ShaderPairKey, ShaderPair, ShaderPairKeyHasher> shader_blobs_;
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3DBlob>>
        vertex_shader_blobs_;
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3DBlob>>
        pixel_shader_blobs_;
    // Pairs compiled this run and not yet appended to shaders.bin.
    std::vector<ShaderPairKey> unsaved_blobs_;
    std::unordered_set<PsoKey, PsoKeyHasher> persisted_pso_keys_;
    std::vector<PsoKey> warm_pso_keys_;
    std::vector<PsoKey> unsaved_pso_keys_;
    // Records the on-disk cache is currently known to contain. The file is an
    // append-only running total, so a session only ever grew it by the few
    // configurations it happened to meet: a cache that is never rewritten stays
    // near first-run size while gameplay keeps meeting configurations that were
    // never persisted, and every later launch re-runs FXC for them. These counts
    // let flush_disk() grow both files to full coverage in bounded batches.
    std::size_t cached_shader_records_ = 0;
    std::size_t cached_pso_records_ = 0;
    // A bounded cache can retain less than the resident set. Once a successful
    // save fills it, further records remain usable in RAM without repeatedly
    // validating/copying the same full file during this session.
    bool shader_cache_at_capacity_ = false;
    bool pso_cache_at_capacity_ = false;
    // Clock-free compile accounting; see compile_stats(). These are read only on
    // the render thread when a stats line is printed, but they are *written* on
    // two different threads, so the split below matters:
    //
    // Written by `create_or_load_pso`, which runs on the render thread for a
    // synchronous first-use compile AND on the prewarm workers, then read by
    // `compile_stats()` on the render thread. A plain counter here is a data
    // race whose only visible symptom is silently wrong diagnostics, so these
    // two are atomics. The increments are per pipeline compile, not per draw,
    // so the cost is irrelevant next to the per-resolution counters.
    std::atomic<std::uint64_t> pso_compiles_{0};
    std::atomic<std::uint64_t> pso_library_loads_{0};
    // Render-thread-only, unlike the two atomics above: `get_or_compile_blobs`
    // has exactly one caller, `PipelineCache::get`, and `worker_main` does its
    // own FXC into local blobs without touching that method or the per-stage
    // maps. So a plain counter, for the same reason the per-resolution counters
    // are plain.
    std::uint64_t blob_compile_us_ = 0;
    std::uint64_t shader_pair_compiles_ = 0;
    std::uint64_t blobs_from_cache_ = 0;
    bool shader_cache_repair_pending_ = false;
    bool pso_cache_repair_pending_ = false;

    struct DiskFlushResult {
        bool shaders_saved = false;
        bool psos_saved = false;
    };
    struct DiskFlushSnapshot {
        std::filesystem::path cache_dir_;
        std::unordered_map<ShaderPairKey, ShaderPair, ShaderPairKeyHasher> shader_blobs_;
        std::vector<ShaderPairKey> unsaved_blobs_;
        std::unordered_set<PsoKey, PsoKeyHasher> persisted_pso_keys_;
        std::vector<PsoKey> unsaved_pso_keys_;
        std::size_t cached_shader_records_ = 0;
        std::size_t cached_pso_records_ = 0;
        bool shader_cache_at_capacity_ = false;
        bool pso_cache_at_capacity_ = false;
        bool shader_cache_repair_pending_ = false;
        bool pso_cache_repair_pending_ = false;
        std::size_t pending_shaders = 0;
        std::size_t pending_psos = 0;
        DiskFlushResult result{};
        std::atomic<bool> done{false};
    };
    std::unique_ptr<DiskFlushSnapshot> disk_flush_;
    std::thread disk_writer_;
    void finish_disk_flush(bool wait);
    template<class State>
    static DiskFlushResult flush_disk_records(State& state);

    [[nodiscard]] static std::uint64_t pair_hash(
        std::uint64_t vs_hash, std::uint64_t ps_hash) {
        const std::uint64_t k[2] = {vs_hash, ps_hash};
        return fnv1a64(k, sizeof(k));
    }
    // Returns the DXBC pair for the keys, compiling (synchronously) on first
    // sight. Returns nullptr on compile failure (logged and retryable).
    [[nodiscard]] const ShaderPair* get_or_compile_blobs(
        const PsoKey& key,
        const PixelShaderKey& ps_key,
        const VertexShaderKey& vs_key);
    // Opt-in fresh-cache seed import; see pipeline_cache.cpp. Never throws.
    void import_fresh_cache_seed() noexcept;
    void load_disk_cache();
    void load_pso_key_cache();
    void load_pipeline_library();
    void flush_pipeline_library();
    [[nodiscard]] bool find_cached_shader_stages(
        const PsoKey& key, ShaderPair& out) const;
    [[nodiscard]] std::uint64_t enqueue_cached_pso_prewarm(
        unsigned worker_count);
    void wait_for_cached_pso_prewarm(std::uint64_t enqueued);
    [[nodiscard]] bool wait_for_in_flight_pso(
        const MemoizedPsoKey& memoized,
        ID3D12PipelineState*& out);
    void remember_pso_key(const PsoKey& key);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12PipelineState>
        create_or_load_pso(
            const PsoKey& key,
            ID3DBlob* vs_blob,
            ID3DBlob* ps_blob);

    void worker_main();
    [[nodiscard]] ID3D12PipelineState* get_or_create_uber(
        const RenderStateKey& render_state);

    std::mutex pipeline_library_mutex_;
    bool pipeline_library_dirty_ = false;
};

}  // namespace galaxy::gx
