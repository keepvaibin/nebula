#pragma once

// PipelineCache: the GX shader and PSO cache of the D3D12 renderer.
//
// Threading:
//   - get() and drain_completions() are render-thread-only, and only the render
//     thread touches the published lookup map.
//   - Worker threads compile specialized shaders and build PSOs, then push the
//     finished pipeline onto the lock-free MPSC PsoCompletionQueue.
//   - GxBackend calls drain_completions() from RendererD3D12::begin_frame(),
//     before any command-list recording, so a PSO pointer never changes while a
//     list is recording.
//
// Lookup: a published specialized PSO is returned. Otherwise a draw whose
// render state has an uber pipeline draws with it while a worker
// compiles the specialization; without one the specialization is compiled
// synchronously, since skipping the draw would show as missing geometry.
//
// Disk cache, %LOCALAPPDATA%\Nebula\shadercache\RMGE01\:
//   - shaders.bin   DXBC pairs by vs/ps hash, loaded at initialize.
//   - psos.bin      PsoKeys that built successfully, prewarmed at startup.
//   - pipelines.bin ID3D12PipelineLibrary1 blob, discarded when the stored
//                   adapter LUID or shader-cache version differs, or the driver
//                   rejects it.
//   - uber.bin      uber shader DXBC, keyed by a hash of the uber HLSL source.

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
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/shader_keys.h"

#include <array>
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

// The PSO descriptor of a render state. Point and line topologies take the
// matching geometry shader, triangles none, a curved surface's patches its
// variant's hull and domain shaders. Public for the WARP tests.
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    ID3DBlob* vertex_shader,
    ID3DBlob* pixel_shader,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader,
    ID3DBlob* hull_shader = nullptr,
    ID3DBlob* domain_shader = nullptr);

// Lock-free multi-producer single-consumer queue (Vyukov intrusive list).
// Producers: compile workers.  Consumer: the render thread, exclusively.
class PsoCompletionQueue {
public:
    struct Item {
        PsoKey key{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
        // Re-optimization of a fast first-use compile: the optimized blobs
        // ride along so the render thread can persist them.
        bool reoptimize = false;
        Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
        // A specialization compiled while its draws used the uber pipeline:
        // its pair is cached and persisted like a synchronous compile's.
        bool remember_pair = false;
        // An uber pipeline for key.render_state and this output variant.
        bool uber = false;
        unsigned uber_variant = 0;
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
    // `root_signature` is the shared GX root signature; `cache_dir` is created
    // if missing. Prewarms the cached PSOs on up to `worker_count` threads
    // (at most 4).
    bool initialize(
        ID3D12Device* device,
        ID3D12RootSignature* root_signature,
        const std::filesystem::path& cache_dir,
        unsigned worker_count);

    void shutdown();                // joins workers, flushes disk

    // RENDER THREAD ONLY. May block on first use of a new material or state;
    // see the lookup policy above.
    [[nodiscard]] ID3D12PipelineState* get(
        const PsoKey& key,
        const PixelShaderKey& ps_key,
        const VertexShaderKey& vs_key);
    // RENDER THREAD ONLY. The specialized pipeline once published, else null with its compile queued on a worker:
    // never compiles or waits here (a variant the draw can do without).
    [[nodiscard]] ID3D12PipelineState* get_if_ready(
        const PsoKey& key,
        const PixelShaderKey& ps_key,
        const VertexShaderKey& vs_key);

    // RENDER THREAD ONLY, called from begin_frame(): publishes every
    // completed specialization into the lookup map.
    void drain_completions();

    // Writes the shader and key caches (each file replaced atomically, its valid
    // records kept) and the pipeline library. Also called from shutdown().
    void flush_disk();

    // RENDER THREAD ONLY. Writes a snapshot of the shader and key records and the
    // pipeline library on a background thread, one batch at a time. The host ends
    // the process without shutdown(), so this is how the driver's compiled
    // pipelines reach the next start.
    void flush_disk_async();
    void poll_disk_flush();

    // RENDER THREAD ONLY. True when shader pairs, PSO keys or pipelines learned
    // this run are not yet on disk.
    [[nodiscard]] bool has_unsaved_records() const {
        return !unsaved_blobs_.empty() || !unsaved_pso_keys_.empty() ||
            pipeline_library_dirty_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t specialized_hits() const;
    [[nodiscard]] std::uint64_t uber_fallbacks() const;
    // True when the last get() returned an uber pipeline: the draw's
    // specialized pipeline is compiling, so the caller must not memoize the
    // result and should resolve the key again on a later draw.
    [[nodiscard]] bool last_get_used_uber() const noexcept { return last_get_used_uber_; }

    // Pipeline compile and cache-coverage counters for this process.
    struct CompileStats {
        std::uint64_t pso_compiles = 0;      // create_or_load_pso builds
        std::uint64_t pso_library_loads = 0; // served by the driver library
        std::uint64_t shader_pair_compiles = 0; // FXC-generated pairs
        std::uint64_t blobs_from_cache = 0;  // pairs whose DXBC came off disk
        // Total FXC time of render-thread compiles, in microseconds.
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
        bool reoptimize = false;
        bool remember_pair = false;
        // Builds the uber pipeline for key.render_state / uber_variant.
        bool uber = false;
        unsigned uber_variant = 0;
    };

    // Render-thread-only published map.
    std::unordered_map<MemoizedPsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>,
        MemoizedPsoKeyHasher> published_;
    // One-entry memo of the last resolution, checked before the map probe: a frame alternates
    // between a few keys, which flush_draw_state's comparison with the previous key misses.
    // published_ is only cleared in shutdown(), last_hit_owner_ keeps the pipeline alive, and
    // drain_completions() clears the memo, since a completion can replace a published pipeline.
    MemoizedPsoKey last_hit_key_{};
    ID3D12PipelineState* last_hit_pipeline_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> last_hit_owner_;
    void clear_last_hit_memo() noexcept {
        last_hit_pipeline_ = nullptr;
        last_hit_owner_.Reset();
    }
    // Keys with a specialization already in flight (render-thread-only).
    std::unordered_set<MemoizedPsoKey, MemoizedPsoKeyHasher> in_flight_;
    // Uber pipelines (uber_shaders.h). The DXBC is compiled (or read from uber.bin)
    // once per session on uber_compile_thread_ and is read-only once uber_blobs_ready_ is set;
    // PSOs per (render state, output variant) are built by the compile
    // workers and published to uber_psos_ by drain_completions().
    struct UberPsoKey {
        RenderStateKey render_state{};
        std::uint32_t variant = 0;
        [[nodiscard]] bool operator==(const UberPsoKey& other) const noexcept {
            return render_state == other.render_state && variant == other.variant;
        }
    };
    struct UberPsoKeyHasher {
        [[nodiscard]] std::size_t operator()(const UberPsoKey& key) const noexcept {
            std::uint64_t hash = mix64(key.render_state.blend_bits);
            hash = combine64(hash, key.render_state.zmode_bits);
            hash = combine64(hash,
                static_cast<std::uint64_t>(key.render_state.cull) |
                (static_cast<std::uint64_t>(key.render_state.pixfmt) << 8) |
                (static_cast<std::uint64_t>(key.render_state.primitive_topology) << 16) |
                (static_cast<std::uint64_t>(key.render_state.samples_log2) << 24));
            return static_cast<std::size_t>(combine64(hash, key.variant));
        }
    };
    // Set by initialize(); a cache that was never initialized has no uber pipelines.
    bool uber_enabled_ = false;
    std::thread uber_compile_thread_;
    std::atomic<bool> uber_blobs_ready_{false};
    Microsoft::WRL::ComPtr<ID3DBlob> uber_vs_blob_;
    std::array<Microsoft::WRL::ComPtr<ID3DBlob>, kUberPsVariants> uber_ps_blobs_{};
    std::uint64_t uber_source_hash_ = 0;
    std::unordered_map<UberPsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>,
        UberPsoKeyHasher> uber_psos_;
    std::unordered_set<UberPsoKey, UberPsoKeyHasher> uber_pending_;
    // Bit n: draws asked for pipelines with 2^n samples per pixel (MSAA) / the uber prewarm ran for them.
    std::uint8_t seen_samples_mask_ = 0u;
    std::uint8_t uber_prewarmed_samples_mask_ = 0u;
    bool last_get_used_uber_ = false;
    std::uint64_t uber_resolutions_ = 0;
    std::uint64_t uber_async_specializations_ = 0;
    void compile_uber_blobs() noexcept;
    [[nodiscard]] ID3D12PipelineState* uber_pipeline(
        const RenderStateKey& render_state, unsigned variant);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12PipelineState> create_uber_pso(
        const RenderStateKey& render_state, unsigned variant);
    void enqueue_uber_pso(const UberPsoKey& key);
    void enqueue_uber_prewarm(std::uint8_t samples_log2);
    void prewarm_uber_for_seen_samples();
    void enqueue_async_specialization(
        const MemoizedPsoKey& memoized,
        const PixelShaderKey& ps_key,
        const VertexShaderKey& vs_key);

    PsoCompletionQueue completions_;

    // Draining the completion queue is lock-free; waiting for a completion
    // takes this mutex.
    std::mutex job_mutex_;
    std::condition_variable job_cv_;
    std::condition_variable completion_cv_;
    // Guarded by job_mutex_: read before draining and advanced after each
    // completion is pushed, so one landing between the drain and the wait is not missed.
    std::uint64_t completion_sequence_{};
    [[nodiscard]] std::uint64_t completion_snapshot();
    void wait_for_worker_completion(std::uint64_t observed,
                                    std::chrono::milliseconds timeout);
    std::exception_ptr worker_error_;
    // Set under job_mutex_ when worker_error_ (an exception_ptr, which cannot be
    // atomic) is published, so get() locks only after a worker failed.
    std::atomic<bool> worker_failed_{false};
    std::deque<CompileJob> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3DBlob> line_geometry_shader_;
    Microsoft::WRL::ComPtr<ID3DBlob> point_geometry_shader_;
    // Curved surfaces' hull and domain shaders by vertex output variant (curved_variant).
    static constexpr unsigned kCurvedVariants = 4u;
    bool curved_ready_ = false;
    Microsoft::WRL::ComPtr<ID3DBlob> curved_hull_shaders_[kCurvedVariants];
    Microsoft::WRL::ComPtr<ID3DBlob> curved_domain_shaders_[kCurvedVariants];
    // CreatePipelineLibrary borrows these bytes until the library is released;
    // declared before it, so the library is destroyed first.
    std::vector<std::uint8_t> pipeline_library_blob_;
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> pipeline_library_;
    std::filesystem::path cache_dir_;
    ShaderGenerator generator_;

    // Render-thread-only, like published_.
    std::uint64_t specialized_hits_ = 0;
    std::uint64_t uber_fallbacks_ = 0;
    // Fast first-use compiles (FXC without optimization, published at once)
    // and the optimized replacements published later by a worker. Replaced
    // pipelines stay alive here: frames in flight may still reference them.
    std::uint64_t fast_first_compiles_ = 0;
    std::uint64_t reoptimized_pipelines_ = 0;
    std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> retired_pipelines_;
    void ensure_compile_workers(unsigned count);
    void remember_reoptimized_pair(
        const PsoKey& key,
        const Microsoft::WRL::ComPtr<ID3DBlob>& vs,
        const Microsoft::WRL::ComPtr<ID3DBlob>& ps);

    // Compiled DXBC pair shared by every render-state variant of one
    // (vs_hash, ps_hash): a new blend/z mode never re-runs FXC, only the
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
    // Blobs of the current fast first-use compile (valid until the next one).
    ShaderPair fast_pair_scratch_{};
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3DBlob>>
        vertex_shader_blobs_;
    std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID3DBlob>>
        pixel_shader_blobs_;
    // Pairs compiled this run and not yet appended to shaders.bin.
    std::vector<ShaderPairKey> unsaved_blobs_;
    std::unordered_set<PsoKey, PsoKeyHasher> persisted_pso_keys_;
    std::vector<PsoKey> warm_pso_keys_;
    std::vector<PsoKey> unsaved_pso_keys_;
    // Records the disk caches are known to hold (see kCacheCoverageAppendBatch).
    std::size_t cached_shader_records_ = 0;
    std::size_t cached_pso_records_ = 0;
    // Set once a save fills a file; later records stay in memory only.
    bool shader_cache_at_capacity_ = false;
    bool pso_cache_at_capacity_ = false;
    // Written by create_or_load_pso on the render thread and on the prewarm
    // workers, so atomic; see compile_stats().
    std::atomic<std::uint64_t> pso_compiles_{0};
    std::atomic<std::uint64_t> pso_library_loads_{0};
    // Render-thread-only: written by get_or_compile_blobs, whose only caller is get().
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
        const VertexShaderKey& vs_key,
        bool fast_first,
        bool* fast_compiled);
    void load_disk_cache();
    void load_pso_key_cache();
    void load_pipeline_library();
    void flush_pipeline_library();
    [[nodiscard]] bool find_cached_shader_stages(
        const PsoKey& key, ShaderPair& out) const;
    [[nodiscard]] std::uint64_t enqueue_cached_pso_prewarm(
        unsigned worker_count);
    void wait_for_cached_pso_prewarm();
    [[nodiscard]] bool wait_for_in_flight_pso(
        const MemoizedPsoKey& memoized,
        ID3D12PipelineState*& out);
    void remember_pso_key(const PsoKey& key);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12PipelineState>
        create_or_load_pso(
            const PsoKey& key,
            ID3DBlob* vs_blob,
            ID3DBlob* ps_blob,
        bool store_in_library = true);

    void worker_main();

    std::mutex pipeline_library_mutex_;
    // Written under pipeline_library_mutex_; read without it by has_unsaved_records().
    std::atomic<bool> pipeline_library_dirty_{false};
};

}  // namespace galaxy::gx
