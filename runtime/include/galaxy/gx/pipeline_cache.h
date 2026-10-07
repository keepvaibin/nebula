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
#include <cstdint>
#include <deque>
#include <filesystem>
#include <exception>
#include <mutex>
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

    // Stats for the on-screen debug overlay / logs.
    [[nodiscard]] std::uint64_t specialized_hits() const;
    [[nodiscard]] std::uint64_t uber_fallbacks() const;

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
    std::unordered_map<PsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>,
        PsoKeyHasher> published_;
    // Keys with a specialization already in flight (render-thread-only).
    std::unordered_set<PsoKey, PsoKeyHasher> in_flight_;
    // Uber PSOs per RenderStateKey hash, created lazily from embedded DXBC.
    std::unordered_map<std::uint64_t,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>> uber_;

    PsoCompletionQueue completions_;

    // Worker-side job intake (mutex + cv is fine here; only the completion
    // path back to the render thread must be lock-free).
    std::mutex job_mutex_;
    std::condition_variable job_cv_;
    std::condition_variable completion_cv_;
    std::exception_ptr worker_error_;
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

    std::atomic<std::uint64_t> specialized_hits_{0};
    std::atomic<std::uint64_t> uber_fallbacks_{0};

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
    bool shader_cache_repair_pending_ = false;
    bool pso_cache_repair_pending_ = false;

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
        const PsoKey& key,
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
