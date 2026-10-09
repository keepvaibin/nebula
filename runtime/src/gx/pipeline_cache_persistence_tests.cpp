// SPDX-License-Identifier: GPL-3.0-only
// Exercises the cache loaders/writers with valid compiled DXBC; no GPU device.
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/scope_exit.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <d3dcompiler.h>
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::gx {

using PipelineTestCreator = Microsoft::WRL::ComPtr<ID3D12PipelineState> (*)(const PsoKey&);
void set_pipeline_cache_test_creator(PipelineTestCreator creator);
void set_pipeline_cache_completion_wait_hook(void (*hook)());
unsigned pipeline_cache_completion_test_parks();

// Defined only in this target's instrumented pipeline_cache.cpp object.
void set_pipeline_cache_persistence_test_fault(std::size_t write_budget, bool fail_close);
void set_pipeline_cache_persistence_test_write_gate(HANDLE entered, HANDLE release);
void set_pipeline_cache_persistence_test_read_fault(std::size_t read_budget, bool reflection);
void set_pipeline_cache_persistence_test_load_fault(
    unsigned stage, std::size_t budget, unsigned exception_kind);

namespace {
std::atomic<unsigned> mock_pipeline_live{0u};
class MockPipelineState final : public ID3D12PipelineState {
public:
    MockPipelineState() { ++mock_pipeline_live; }
    ~MockPipelineState() { --mock_pipeline_live; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        if (out == nullptr) return E_POINTER;
        *out = static_cast<ID3D12PipelineState*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0u) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
private:
    std::atomic<ULONG> references_{1u};
};

// No D3D device: this fixture stops at a deliberately blocked lease/open, then
// fails serialization on the retry. It exercises the real flush_disk gate.
//
// The base must be ID3D12PipelineLibrary1, not ID3D12PipelineLibrary: the member
// this attaches to is `ComPtr<ID3D12PipelineLibrary1> pipeline_library_`
// (pipeline_cache.h) and production obtains it via
// `device1->CreatePipelineLibrary(..., IID_PPV_ARGS(&pipeline_library_))`, i.e.
// through the PipelineLibrary1 IID. ID3D12PipelineLibrary1 adds exactly one vtable
// slot over the base — `LoadPipeline(LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC*,
// REFIID, void**)` — so handing a `RetryPipelineLibrary*` to that ComPtr is a base-to-
// derived pointer conversion the compiler rejects, however many other overrides are
// present. Matching the base and implementing only the new slot is the whole fix;
// every other override below already belongs to the correct ancestor
// (ID3D12PipelineLibrary, ID3D12DeviceChild, ID3D12Object, IUnknown).
class RetryPipelineLibrary final : public ID3D12PipelineLibrary1 {
public:
    unsigned serialize_attempts = 0u;
    bool fail_serialize = true;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3D12Object) &&
            iid != __uuidof(ID3D12DeviceChild) &&
            iid != __uuidof(ID3D12PipelineLibrary) &&
            iid != __uuidof(ID3D12PipelineLibrary1))
            return E_NOINTERFACE;
        *out = static_cast<ID3D12PipelineLibrary1*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0u) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE StorePipeline(LPCWSTR, ID3D12PipelineState*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LoadGraphicsPipeline(LPCWSTR,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE LoadComputePipeline(LPCWSTR,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
    // The one slot ID3D12PipelineLibrary1 adds over ID3D12PipelineLibrary. The
    // fixture never loads a pipeline, so E_NOTIMPL is the same answer the other
    // Load* overrides give and no test path reaches it.
    HRESULT STDMETHODCALLTYPE LoadPipeline(LPCWSTR,
        const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOTIMPL;
    }
    SIZE_T STDMETHODCALLTYPE GetSerializedSize() override { return 8u; }
    HRESULT STDMETHODCALLTYPE Serialize(void* out, SIZE_T bytes) override {
        ++serialize_attempts;
        if (fail_serialize) return E_FAIL;
        if (out == nullptr || bytes < GetSerializedSize()) return E_INVALIDARG;
        std::memset(out, 0x5a, GetSerializedSize());
        return S_OK;
    }
private:
    ULONG references_ = 1u;
};
} // namespace

struct PipelineCachePersistenceTestAccess {
    inline static PipelineCache* waiting_cache{};
    inline static std::atomic_bool creator_entered{false}, release_creator{false};
    inline static std::atomic_uint waiting_creations{0u};
    inline static unsigned waiting_failure{};
    inline static bool publish_before_park{}, released_once{};
    inline static std::thread delayed_release;
    inline static std::chrono::steady_clock::time_point published_before_wait{};
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> waiting_create(const PsoKey&) {
        ++waiting_creations;
        creator_entered.store(true, std::memory_order_release);
        creator_entered.notify_one();
        release_creator.wait(false, std::memory_order_acquire);
        if (waiting_failure == 1u) return nullptr;
        if (waiting_failure == 2u) throw std::runtime_error("running job fixture failure");
        Microsoft::WRL::ComPtr<ID3D12PipelineState> result;
        result.Attach(new MockPipelineState);
        return result;
    }
    static void release_waiting_creator() {
        release_creator.store(true, std::memory_order_release);
        release_creator.notify_one();
    }
    static void before_completion_park() {
        if (released_once) return;
        released_once = true;
        if (!publish_before_park) {
            delayed_release = std::thread([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                release_waiting_creator();
            });
            return;
        }
        release_waiting_creator();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(waiting_cache->job_mutex_);
                if (waiting_cache->completion_sequence_ != 0u || waiting_cache->worker_error_) break;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("fixture publisher failed to finish");
            std::this_thread::yield();
        }
        published_before_wait = std::chrono::steady_clock::now();
    }
    static bool running_completion_wake(bool before_park, unsigned failure, bool startup) {
        PipelineCache cache;
        const auto record = key(1u);
        seed_stages(cache, 1u);
        PipelineCache::CompileJob job{record, {}, {}};
        job.vs_blob = cache.vertex_shader_blobs_.at(record.vs_hash);
        job.ps_blob = cache.pixel_shader_blobs_.at(record.ps_hash);
        job.use_cached_blobs = true;
        cache.jobs_.push_back(std::move(job));
        cache.in_flight_.insert(MemoizedPsoKey{record});
        waiting_cache = &cache;
        creator_entered = false; release_creator = false; waiting_creations = 0u;
        waiting_failure = failure; publish_before_park = before_park; released_once = false;
        const ScopeExit cleanup([&]() noexcept {
            release_waiting_creator();
            if (delayed_release.joinable()) delayed_release.join();
            try { cache.shutdown(); } catch (...) {}
            set_pipeline_cache_completion_wait_hook(nullptr);
            set_pipeline_cache_test_creator(nullptr);
            waiting_cache = nullptr;
        });
        set_pipeline_cache_test_creator(&waiting_create);
        set_pipeline_cache_completion_wait_hook(&before_completion_park);
        cache.workers_.emplace_back([&] { cache.worker_main(); });
        creator_entered.wait(false, std::memory_order_acquire);
        ID3D12PipelineState* result = nullptr;
        bool resolved = false, threw = false;
        try {
            if (startup) {
                cache.wait_for_cached_pso_prewarm(1u);
                const auto it = cache.published_.find(MemoizedPsoKey{record});
                resolved = it != cache.published_.end();
                if (resolved) result = it->second.Get();
            } else resolved = cache.wait_for_in_flight_pso(MemoizedPsoKey{record}, result);
        } catch (const std::runtime_error& e) {
            threw = std::string(e.what()) == "running job fixture failure";
        }
        const auto after = std::chrono::steady_clock::now();
        const auto parks = pipeline_cache_completion_test_parks();
        std::cout << "[running-completion-fixture] before-park=" << before_park
                  << " failure=" << failure << " startup=" << startup
                  << " kernel-wait-admissions=" << parks;
        if (before_park)
            std::cout << " publication-to-return-us="
                      << std::chrono::duration<double, std::micro>(after - published_before_wait).count();
        std::cout << '\n';
        return waiting_creations == 1u && (before_park ? parks == 0u : parks == 1u) &&
            (failure == 0u ? resolved && result != nullptr :
             failure == 1u ? !resolved && result == nullptr && cache.in_flight_.empty() : threw);
    }
    static bool stopped_completion_wake() {
        PipelineCache cache;
        const auto observed = cache.completion_snapshot();
        cache.stopping_.store(true, std::memory_order_relaxed);
        try { cache.wait_for_worker_completion(observed, std::chrono::seconds(30)); }
        catch (const std::runtime_error& e) {
            return std::string(e.what()) == "GX pipeline workers stopped before required completion";
        }
        return false;
    }
    inline static PipelineCache* takeover_cache{};
    inline static std::thread::id takeover_owner{};
    inline static std::atomic_uint takeover_creations{};
    inline static unsigned takeover_failure{};
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> takeover_create(const PsoKey&) {
        ++takeover_creations;
        if (std::this_thread::get_id() == takeover_owner) {
            // Launch the real worker exactly while inline creation is in
            // progress. stopping=true drains any queued job and then exits.
            // If first use has not claimed the job, this creates it twice.
            std::thread worker([] { takeover_cache->worker_main(); });
            worker.join();
        }
        if (takeover_failure == 1u) return nullptr;
        if (takeover_failure == 2u) throw std::runtime_error("inline creation fixture failure");
        Microsoft::WRL::ComPtr<ID3D12PipelineState> result;
        result.Attach(new MockPipelineState);
        return result;
    }
    static bool queued_takeover_single_owner(unsigned failure) {
        PipelineCache cache;
        const auto record = key(1u);
        seed_stages(cache, 1u);
        PipelineCache::CompileJob job{record, {}, {}};
        job.vs_blob = cache.vertex_shader_blobs_.at(record.vs_hash);
        job.ps_blob = cache.pixel_shader_blobs_.at(record.ps_hash);
        job.use_cached_blobs = true;
        cache.jobs_.push_back(std::move(job));
        cache.in_flight_.insert(MemoizedPsoKey{record});
        cache.stopping_.store(true, std::memory_order_relaxed);
        takeover_cache = &cache;
        takeover_owner = std::this_thread::get_id();
        takeover_creations = 0u;
        takeover_failure = failure;
        struct CreatorReset {
            ~CreatorReset() { set_pipeline_cache_test_creator(nullptr); takeover_cache = nullptr; }
        } reset;
        set_pipeline_cache_test_creator(&takeover_create);
        ID3D12PipelineState* result = nullptr;
        bool resolved = false;
        bool threw = false;
        try { resolved = cache.wait_for_in_flight_pso(MemoizedPsoKey{record}, result); }
        catch (const std::runtime_error& e) { threw = std::string(e.what()) == "inline creation fixture failure"; }
        const bool single = takeover_creations == 1u;
        std::cout << "[queued-takeover-fixture] failure=" << failure
                  << " creations=" << takeover_creations << '\n';
        return single && cache.jobs_.empty() && cache.in_flight_.empty() &&
            (failure == 0u ? resolved && result != nullptr :
             failure == 1u ? !resolved && result == nullptr : threw);
    }
    static inline unsigned completion_retry_creations = 0u;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> completion_retry_create(const PsoKey&) {
        ++completion_retry_creations;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> result;
        result.Attach(new MockPipelineState);
        return result;
    }
    static bool completion_publication_failure(unsigned stage, unsigned exception_kind,
                                               bool existing_published) {
        const unsigned live_before = mock_pipeline_live.load();
        bool passed = false;
        {
            PipelineCache cache;
            const auto record = key(1u);
            const MemoizedPsoKey memoized{record};
            seed_stages(cache, 1u);
            Microsoft::WRL::ComPtr<ID3D12PipelineState> existing;
            if (existing_published) {
                existing.Attach(new MockPipelineState);
                cache.published_.emplace(memoized, existing);
            }
            Microsoft::WRL::ComPtr<ID3D12PipelineState> completed;
            completed.Attach(new MockPipelineState);
            cache.completions_.push({record, std::move(completed)});
            cache.in_flight_.insert(memoized);
            completion_retry_creations = 0u;
            struct FaultReset {
                ~FaultReset() {
                    set_pipeline_cache_test_creator(nullptr);
                    set_pipeline_cache_persistence_test_load_fault(0u, 0u, 0u);
                }
            } reset;
            set_pipeline_cache_test_creator(&completion_retry_create);
            set_pipeline_cache_persistence_test_load_fault(stage, 0u, exception_kind);
            bool threw = false;
            try { cache.drain_completions(); }
            catch (const std::bad_alloc&) { threw = exception_kind == 0u; }
            catch (const std::length_error&) { threw = exception_kind == 1u; }
            catch (const std::runtime_error&) { threw = exception_kind == 2u; }
            set_pipeline_cache_persistence_test_load_fault(0u, 0u, 0u);
            PsoCompletionQueue::Item unexpected;
            const bool queue_empty = !cache.completions_.try_pop(unexpected);
            const bool retired = !cache.in_flight_.contains(memoized);
            const auto found = cache.published_.find(memoized);
            const bool published = found != cache.published_.end() && found->second;
            std::cout << "[completion-retirement-fixture] stage=" << stage
                      << " exception=" << exception_kind << " existing=" << existing_published
                      << " retired=" << retired << " published=" << published << '\n';
            // Avoid waiting30s on the known broken preimage. Once retired,
            // exercise the real get retry through cached stages and mock driver.
            ID3D12PipelineState* retry = retired ? cache.get(record, {}, {}) : nullptr;
            const bool retained_existing = !existing_published || retry == existing.Get();
            passed = threw && queue_empty && retired && retry != nullptr && retained_existing &&
                (stage == 7u ? !published && completion_retry_creations == 1u
                             : published && completion_retry_creations == 0u);
        }
        return passed && mock_pipeline_live.load() == live_before;
    }
    static bool pipeline_library_retry(const std::filesystem::path& dir, bool busy_writer) {
        PipelineCache cache;
        set_directory(cache, dir);
        auto* library = new RetryPipelineLibrary;
        // `Attach` takes exactly `InterfaceType*`, and MSVC's ComPtr declares it
        // non-template, so a `RetryPipelineLibrary*` is not accepted by template
        // deduction even though the upcast is implicit. Name the base conversion.
        // The static_assert keeps the fixture honest if the member's interface type
        // in pipeline_cache.h ever moves: the base must stay the same type, or the
        // vtable slot count stops matching what production obtains through
        // IID_PPV_ARGS.
        static_assert(std::is_base_of_v<
                          ID3D12PipelineLibrary1, RetryPipelineLibrary>,
                      "fixture must derive from the member's exact interface type");
        cache.pipeline_library_.Attach(
            static_cast<ID3D12PipelineLibrary1*>(library));
        cache.pipeline_library_dirty_ = true;
        const auto path = dir / "pipelines.bin";
        const auto tmp = dir / "pipelines.bin.tmp";
        HANDLE writer = INVALID_HANDLE_VALUE;
        if (busy_writer) {
            const auto lease = dir / "pipelines.bin.lock";
            writer = CreateFileW(lease.c_str(), GENERIC_READ | GENERIC_WRITE, 0u,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (writer == INVALID_HANDLE_VALUE) throw std::runtime_error("test pipeline writer lease failed");
        } else {
            std::filesystem::create_directory(tmp);
        }
        library->fail_serialize = false;
        cache.flush_disk();
        cache.flush_disk();
        library->fail_serialize = true;
        const bool retained = cache.pipeline_library_dirty_ &&
            library->serialize_attempts == 2u && !std::filesystem::exists(path);
        if (busy_writer) CloseHandle(writer);
        else std::filesystem::remove(tmp);
        // Still no GPU/device access: fail before writing on the third attempt.
        cache.flush_disk();
        return retained && library->serialize_attempts == 3u &&
            cache.pipeline_library_dirty_ && !std::filesystem::exists(path);
    }
    static Microsoft::WRL::ComPtr<ID3DBlob> shader_blob(bool vertex, unsigned id) {
        const std::string source = vertex
            ? "float4 main(float4 p : POSITION) : SV_Position { return p + float4(" +
                std::to_string(id) + ",0,0,0); }"
            : "float4 main() : SV_Target { return float4(" +
                std::to_string(id) + ",0,0,1); }";
        Microsoft::WRL::ComPtr<ID3DBlob> result, errors;
        if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr,
                nullptr, "main", vertex ? "vs_5_1" : "ps_5_1", 0u, 0u,
                &result, &errors))) throw std::runtime_error("test shader compilation failed");
        return result;
    }
    static void set_directory(PipelineCache& cache, const std::filesystem::path& dir) {
        std::filesystem::create_directories(dir);
        cache.cache_dir_ = dir;
    }
    static PsoKey key(unsigned id) {
        PsoKey result{};
        result.vs_hash = 0x110000u + id;
        result.ps_hash = 0x220000u + id;
        result.render_state.blend_bits = id;
        result.render_state.primitive_topology = 3u;
        return result;
    }
    static void seed(PipelineCache& cache, bool shader, unsigned id) {
        const PsoKey record = key(id);
        if (!shader) {
            cache.remember_pso_key(record);
            return;
        }
        PipelineCache::ShaderPair pair{};
        pair.vs_hash = record.vs_hash;
        pair.ps_hash = record.ps_hash;
        pair.vs = shader_blob(true, id);
        pair.ps = shader_blob(false, id);
        const PipelineCache::ShaderPairKey identity{record.vs_hash, record.ps_hash};
        cache.shader_blobs_.emplace(identity, std::move(pair));
        cache.unsaved_blobs_.push_back(identity);
    }
    static void seed_stages(PipelineCache& cache, unsigned id) {
        const auto record = key(id);
        cache.vertex_shader_blobs_.emplace(record.vs_hash, shader_blob(true, id));
        cache.pixel_shader_blobs_.emplace(record.ps_hash, shader_blob(false, id));
    }
    static bool publish_stages(PipelineCache& cache, unsigned id) {
        return cache.get_or_compile_blobs(key(id), PixelShaderKey{}, VertexShaderKey{}) != nullptr;
    }
    static bool stages_present(const PipelineCache& cache, unsigned id) {
        const auto record = key(id);
        return cache.vertex_shader_blobs_.contains(record.vs_hash) &&
            cache.pixel_shader_blobs_.contains(record.ps_hash);
    }
    static void load(PipelineCache& cache, bool shader) {
        if (shader) cache.load_disk_cache();
        else cache.load_pso_key_cache();
    }
    static std::size_t pending(const PipelineCache& cache, bool shader) {
        return shader ? cache.unsaved_blobs_.size() : cache.unsaved_pso_keys_.size();
    }
    static void finish_disk(PipelineCache& cache) { cache.finish_disk_flush(true); }
    static bool disk_active(const PipelineCache& cache) { return cache.disk_flush_ != nullptr; }
    static std::size_t loaded(const PipelineCache& cache, bool shader) {
        return shader ? cache.shader_blobs_.size() : cache.warm_pso_keys_.size();
    }
    static bool repair_pending(const PipelineCache& cache, bool shader) {
        return shader ? cache.shader_cache_repair_pending_ : cache.pso_cache_repair_pending_;
    }
    static bool coherent_pso_residents(const PipelineCache& cache) {
        return cache.persisted_pso_keys_.size() == cache.warm_pso_keys_.size() &&
            std::all_of(cache.warm_pso_keys_.begin(), cache.warm_pso_keys_.end(),
                [&](const PsoKey& key) { return cache.persisted_pso_keys_.contains(key); });
    }
    static bool remembered(const PipelineCache& cache, unsigned id) {
        return cache.persisted_pso_keys_.contains(key(id));
    }
    static bool contains(const PipelineCache& cache, bool shader, unsigned id) {
        const PsoKey record = key(id);
        if (!shader) {
            return std::find(cache.warm_pso_keys_.begin(), cache.warm_pso_keys_.end(), record) !=
                cache.warm_pso_keys_.end();
        }
        const auto it = cache.shader_blobs_.find({record.vs_hash, record.ps_hash});
        if (it == cache.shader_blobs_.end() || !it->second.vs || !it->second.ps) return false;
        const auto vs = shader_blob(true, id);
        const auto ps = shader_blob(false, id);
        return it->second.vs->GetBufferSize() == vs->GetBufferSize() &&
            it->second.ps->GetBufferSize() == ps->GetBufferSize() &&
            std::memcmp(it->second.vs->GetBufferPointer(), vs->GetBufferPointer(), vs->GetBufferSize()) == 0 &&
            std::memcmp(it->second.ps->GetBufferPointer(), ps->GetBufferPointer(), ps->GetBufferSize()) == 0;
    }
    static void wrong_stage(PipelineCache& cache) {
        cache.shader_blobs_.begin()->second.vs = shader_blob(false, 1u);
    }
    static void seed_shared_vertex(PipelineCache& cache) {
        PipelineCache::ShaderPair pair{};
        pair.vs_hash = key(1u).vs_hash;
        pair.ps_hash = key(2u).ps_hash;
        pair.vs = shader_blob(true, 1u);
        pair.ps = shader_blob(false, 2u);
        const PipelineCache::ShaderPairKey identity{pair.vs_hash, pair.ps_hash};
        cache.shader_blobs_.emplace(identity, std::move(pair));
        cache.unsaved_blobs_.push_back(identity);
    }
    static bool shared_vertex_loaded(const PipelineCache& cache) {
        const auto first = cache.shader_blobs_.find({key(1u).vs_hash, key(1u).ps_hash});
        const auto second = cache.shader_blobs_.find({key(1u).vs_hash, key(2u).ps_hash});
        return first != cache.shader_blobs_.end() && second != cache.shader_blobs_.end() &&
            cache.vertex_shader_blobs_.size() == 1u && cache.pixel_shader_blobs_.size() == 2u &&
            first->second.vs.Get() == second->second.vs.Get() &&
            first->second.ps.Get() != second->second.ps.Get();
    }
    static bool queued_takeover() {
        PipelineCache cache;
        const auto record = key(1u);
        cache.jobs_.push_back(PipelineCache::CompileJob{record, {}, {}});
        cache.in_flight_.insert(MemoizedPsoKey{record});
        ID3D12PipelineState* result = nullptr;
        return !cache.wait_for_in_flight_pso(MemoizedPsoKey{record}, result) &&
            result == nullptr &&
            cache.jobs_.empty() && cache.in_flight_.empty();
    }
    static bool recombined_prewarm_stages() {
        PipelineCache cache;
        const auto vertex = shader_blob(true, 1u);
        const auto pixel = shader_blob(false, 2u);
        auto record = key(1u);
        record.ps_hash = key(2u).ps_hash;
        cache.vertex_shader_blobs_.emplace(record.vs_hash, vertex);
        cache.pixel_shader_blobs_.emplace(record.ps_hash, pixel);
        PipelineCache::ShaderPair stages{};
        if (!cache.shader_blobs_.empty() ||
            !cache.find_cached_shader_stages(record, stages) ||
            stages.vs.Get() != vertex.Get() || stages.ps.Get() != pixel.Get() ||
            stages.vs_hash != record.vs_hash || stages.ps_hash != record.ps_hash) return false;
        record.ps_hash = key(3u).ps_hash;
        if (cache.find_cached_shader_stages(record, stages) || stages.vs || stages.ps) return false;
        record = key(2u);
        return !cache.find_cached_shader_stages(record, stages) && !stages.vs && !stages.ps;
    }
    static bool worker_failure_propagates() {
        PipelineCache cache;
        cache.worker_error_ = std::make_exception_ptr(std::runtime_error("worker fixture failure"));
        cache.in_flight_.insert(MemoizedPsoKey{key(1u)});
        try { cache.wait_for_cached_pso_prewarm(1u); }
        catch (const std::runtime_error& e) {
            return std::string(e.what()) == "worker fixture failure";
        }
        return false;
    }
    // The hot path no longer takes job_mutex_ on every request; prove the
    // atomic gate still exposes a failed worker to the render thread.
    static bool worker_failure_gate_visible() {
        PipelineCache cache;
        if (cache.worker_failed_.load(std::memory_order_acquire)) return false;
        cache.worker_error_ =
            std::make_exception_ptr(std::runtime_error("gate fixture failure"));
        cache.worker_failed_.store(true, std::memory_order_release);
        try { (void)cache.get(key(1u), PixelShaderKey{}, VertexShaderKey{}); }
        catch (const std::runtime_error& e) {
            return std::string(e.what()) == "gate fixture failure";
        }
        return false;
    }
    // A memoized key's cached hash must equal a freshly derived one, or the
    // unordered_map probe would miss entries that are present.
    static bool memoized_hash_consistent() {
        for (unsigned id = 1u; id <= 32u; ++id) {
            const PsoKey record = key(id);
            const MemoizedPsoKey memoized{record};
            if (memoized.hash != record.hash()) return false;
            if (!(memoized == MemoizedPsoKey{record})) return false;
            PsoKey other = record;
            other.render_state.primitive_topology =
                static_cast<std::uint8_t>(record.render_state.primitive_topology ^ 1u);
            if (record.hash() == other.hash()) return false;
            if (memoized == MemoizedPsoKey{other}) return false;
        }
        return true;
    }
};
} // namespace galaxy::gx

namespace {
using galaxy::gx::PipelineCache;
using Access = galaxy::gx::PipelineCachePersistenceTestAccess;
using Bytes = std::vector<unsigned char>;
constexpr std::size_t kNoWriteFault = std::numeric_limits<std::size_t>::max();

bool expect(bool value, const char* label) {
    if (!value) std::fprintf(stderr, "FAILED: %s\n", label);
    return value;
}
std::filesystem::path cache_file(const std::filesystem::path& dir, bool shader) {
    return dir / (shader ? "shaders.bin" : "psos.bin");
}
Bytes read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("test cache read failed");
    return Bytes(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
}
void append_bytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) throw std::runtime_error("test tail write failed");
}
Bytes make_truncated_record(const std::filesystem::path& dir, bool shader) {
    PipelineCache source;
    Access::set_directory(source, dir);
    Access::seed(source, shader, 2u);
    source.flush_disk();
    Bytes bytes = read_bytes(cache_file(dir, shader));
    if (bytes.size() <= 9u) throw std::runtime_error("test record missing");
    bytes.erase(bytes.begin(), bytes.begin() + 8);
    bytes.pop_back();
    return bytes;
}
bool valid_reload(const std::filesystem::path& dir, bool shader, unsigned last) {
    PipelineCache reload;
    Access::set_directory(reload, dir);
    Access::load(reload, shader);
    return Access::loaded(reload, shader) == 2u && Access::contains(reload, shader, 1u) &&
        Access::contains(reload, shader, last) && !Access::repair_pending(reload, shader);
}

bool shader_resource_retry(const std::filesystem::path& root, bool during_load,
    bool reflection_failure, std::size_t readable_records) {
    PipelineCache source;
    Access::set_directory(source, root);
    Access::seed(source, true, 1u);
    Access::seed(source, true, 2u);
    source.flush_disk();
    const auto path = cache_file(root, true);
    const Bytes original = read_bytes(path);

    PipelineCache live;
    Access::set_directory(live, root);
    if (during_load) {
        galaxy::gx::set_pipeline_cache_persistence_test_read_fault(
            readable_records, reflection_failure);
    }
    Access::load(live, true);
    bool passed = expect(Access::loaded(live, true) == (during_load ? readable_records : 2u) &&
        !Access::repair_pending(live, true) && read_bytes(path) == original,
        "resource-limited load preserves the file and does not classify it as corrupt");
    Access::seed(live, true, 3u);
    galaxy::gx::set_pipeline_cache_persistence_test_read_fault(
        readable_records, reflection_failure);
    live.flush_disk();
    passed &= expect(read_bytes(path) == original && Access::pending(live, true) == 1u &&
        !std::filesystem::exists(root / "shaders.bin.tmp"),
        "resource-limited prefix validation aborts replacement and retains pending work");

    galaxy::gx::set_pipeline_cache_persistence_test_read_fault(kNoWriteFault, false);
    live.flush_disk();
    const Bytes merged = read_bytes(path);
    passed &= expect(merged.size() > original.size() &&
        std::equal(original.begin(), original.end(), merged.begin()) &&
        Access::pending(live, true) == 0u,
        "retry preserves every original byte and appends pending shader work");
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, true);
    passed &= expect(Access::loaded(reload, true) == 3u &&
        Access::contains(reload, true, 1u) && Access::contains(reload, true, 2u) &&
        Access::contains(reload, true, 3u) && !Access::repair_pending(reload, true),
        "resource failure retry reloads all three shader pairs intact");
    return passed;
}

bool loader_publication_retry(const std::filesystem::path& root, unsigned stage,
    std::size_t completed_records, unsigned exception_kind) {
    const bool shader = stage <= 3u;
    PipelineCache source;
    Access::set_directory(source, root);
    Access::seed(source, shader, 1u);
    Access::seed(source, shader, 2u);
    source.flush_disk();
    const auto path = cache_file(root, shader);
    const Bytes original = read_bytes(path);
    PipelineCache live;
    Access::set_directory(live, root);
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(
        stage, completed_records, exception_kind);
    bool propagated = false;
    try {
        Access::load(live, shader);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(0u, kNoWriteFault, 0u);
    bool passed = expect(propagated == (exception_kind == 2u) &&
        Access::loaded(live, shader) == completed_records &&
        !Access::repair_pending(live, shader) && read_bytes(path) == original,
        "publication failure retains completed residents without corrupting disk or swallowing unexpected errors");
    if (!shader) passed &= expect(Access::coherent_pso_residents(live),
        "failed warm-list publication rolls back only the current PSO set insertion");
    // An exclusive open fails if any loader input handle remains open, even
    // when that handle allowed other readers/writers.
    HANDLE exclusive = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        0u, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    passed &= expect(exclusive != INVALID_HANDLE_VALUE,
        "all loader exits release the input file handle");
    if (exclusive != INVALID_HANDLE_VALUE) CloseHandle(exclusive);

    Access::load(live, shader);
    passed &= expect(Access::loaded(live, shader) == 2u &&
        Access::contains(live, shader, 1u) && Access::contains(live, shader, 2u) &&
        (shader || Access::coherent_pso_residents(live)),
        "same-instance retry publishes every valid resident and warm key");
    Access::seed(live, shader, 3u);
    live.flush_disk();
    const Bytes merged = read_bytes(path);
    passed &= expect(merged.size() > original.size() &&
        std::equal(original.begin(), original.end(), merged.begin()) &&
        Access::pending(live, shader) == 0u,
        "post-load retry preserves original bytes and appends pending work");
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, shader);
    passed &= expect(Access::loaded(reload, shader) == 3u &&
        Access::contains(reload, shader, 1u) && Access::contains(reload, shader, 2u) &&
        Access::contains(reload, shader, 3u) && !Access::repair_pending(reload, shader) &&
        (shader || Access::coherent_pso_residents(reload)),
        "fresh reload after publication failure retains all disk identities");
    return passed;
}

bool shader_pair_publication_retry(const std::filesystem::path& root, unsigned exception_kind) {
    PipelineCache live;
    Access::set_directory(live, root);
    Access::seed(live, true, 1u);
    live.flush_disk();
    const auto path = cache_file(root, true);
    const Bytes original = read_bytes(path);
    Access::seed(live, true, 2u); // earlier pending work must survive
    Access::seed_stages(live, 3u); // exercise actual producer without GPU/FXC
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(7u, 0u, exception_kind);
    bool propagated = false;
    try { Access::publish_stages(live, 3u); }
    catch (const std::bad_alloc&) { propagated = exception_kind == 0u; }
    catch (const std::length_error&) { propagated = exception_kind == 1u; }
    catch (const std::runtime_error&) { propagated = exception_kind == 2u; }
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(0u, kNoWriteFault, 0u);
    bool passed = expect(propagated && Access::pending(live, true) == 1u &&
        Access::stages_present(live, 3u) && !Access::contains(live, true, 3u) &&
        read_bytes(path) == original,
        "failed pair insertion rolls back only its new pending identity and preserves stages");
    live.flush_disk();
    passed &= expect(Access::pending(live, true) == 0u && read_bytes(path).size() > original.size(),
        "earlier pending shader can flush despite failed pair publication");
    passed &= expect(Access::publish_stages(live, 3u), "failed pair retries from retained stages");
    live.flush_disk();
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, true);
    passed &= expect(Access::loaded(reload, true) == 3u && Access::contains(reload, true, 1u) &&
        Access::contains(reload, true, 2u) && Access::contains(reload, true, 3u),
        "reload preserves original, earlier pending and retried shader pairs");
    return passed;
}

bool pending_publication_retry(const std::filesystem::path& root,
    std::size_t completed_records, unsigned exception_kind) {
    PipelineCache source;
    Access::set_directory(source, root);
    Access::seed(source, false, 1u);
    source.flush_disk();
    const auto path = cache_file(root, false);
    const Bytes original = read_bytes(path);
    PipelineCache live;
    Access::set_directory(live, root);
    Access::load(live, false);
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(6u,
        completed_records, exception_kind);
    bool propagated = false;
    try {
        Access::seed(live, false, 2u);
        if (completed_records != 0u) Access::seed(live, false, 3u);
    } catch (const std::exception&) {
        propagated = true;
    }
    galaxy::gx::set_pipeline_cache_persistence_test_load_fault(0u, kNoWriteFault, 0u);
    const unsigned failed_id = completed_records != 0u ? 3u : 2u;
    bool passed = expect(propagated && Access::remembered(live, 1u) &&
        (completed_records == 0u || Access::remembered(live, 2u)) &&
        !Access::remembered(live, failed_id) &&
        Access::pending(live, false) == completed_records && read_bytes(path) == original,
        "failed pending publication rolls back current PSO identity and retains earlier pending work");
    Access::seed(live, false, failed_id);
    live.flush_disk();
    const Bytes merged = read_bytes(path);
    passed &= expect(merged.size() > original.size() &&
        std::equal(original.begin(), original.end(), merged.begin()) &&
        Access::pending(live, false) == 0u,
        "retry after pending publication failure preserves original records and persists new work");
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, false);
    passed &= expect(Access::loaded(reload, false) == completed_records + 2u &&
        Access::contains(reload, false, 1u) && Access::contains(reload, false, failed_id) &&
        (completed_records == 0u || Access::contains(reload, false, 2u)) &&
        Access::coherent_pso_residents(reload),
        "newly remembered PSO keys remain available to the next launch prewarm");
    return passed;
}

bool tail_repair(const std::filesystem::path& root, bool shader, bool add_new, bool after_load) {
    const auto dir = root / (shader ? "shader" : "pso");
    PipelineCache first;
    Access::set_directory(first, dir);
    Access::seed(first, shader, 1u);
    first.flush_disk();
    const auto path = cache_file(dir, shader);
    const Bytes original = read_bytes(path);
    const Bytes tail = make_truncated_record(root / "tail-source", shader);
    PipelineCache live;
    Access::set_directory(live, dir);
    if (!after_load) append_bytes(path, tail);
    Access::load(live, shader);
    bool passed = expect(Access::loaded(live, shader) == 1u &&
        Access::contains(live, shader, 1u) && !Access::contains(live, shader, 2u),
        "loader retains exactly the valid record prefix");
    passed &= expect(Access::repair_pending(live, shader) == !after_load,
        "loader distinguishes complete file from incomplete trailing record");
    if (after_load) append_bytes(path, tail);
    if (add_new) {
        Access::seed(live, shader, 3u);
        // A failed first repair must leave the original prefix and tail untouched
        // and keep the pending entry.
        const Bytes damaged = read_bytes(path);
        galaxy::gx::set_pipeline_cache_persistence_test_fault(original.size() + 12u, false);
        live.flush_disk();
        passed &= expect(Access::pending(live, shader) == 1u && read_bytes(path) == damaged,
            "partial replacement failure retains pending data and original bytes");
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    }
    live.flush_disk();
    const Bytes repaired = read_bytes(path);
    if (repaired.size() < original.size() ||
        !std::equal(original.begin(), original.end(), repaired.begin())) {
        const auto n = std::min(original.size(), repaired.size());
        std::size_t at = 0u;
        while (at < n && original[at] == repaired[at]) ++at;
        std::fprintf(stderr, "tail-repair shader=%u add-new=%u after-load=%u old=%zu new=%zu first-diff=%zu\n",
            shader ? 1u : 0u, add_new ? 1u : 0u, after_load ? 1u : 0u,
            original.size(), repaired.size(), at);
    }
    passed &= expect(repaired.size() >= original.size() &&
        std::equal(original.begin(), original.end(), repaired.begin()),
        "replacement preserves every byte of the valid prefix");
    passed &= expect(Access::pending(live, shader) == 0u && !Access::repair_pending(live, shader),
        "only completed replacement retires pending persistence and repair");
    if (add_new) {
        passed &= expect(repaired.size() == 2u * original.size() - 8u && valid_reload(dir, shader, 3u),
            "repaired cache reloads the prefix and new entry without the truncated entry");
    } else {
        passed &= expect(repaired == original,
            "repair without new records removes only the incomplete tail");
    }
    return passed;
}

// A file lease serializes flushes, but a live cache may have loaded before
// another writer repaired the tail and added records. Keep that newer prefix.
bool stale_repair_preserves_other_writer(const std::filesystem::path& root, bool shader) {
    const auto dir = root / "shared";
    PipelineCache seed;
    Access::set_directory(seed, dir);
    Access::seed(seed, shader, 1u);
    seed.flush_disk();
    append_bytes(cache_file(dir, shader), make_truncated_record(root / "tail", shader));
    PipelineCache stale;
    Access::set_directory(stale, dir);
    Access::load(stale, shader);
    bool passed = expect(Access::repair_pending(stale, shader), "stale writer loaded a repairable tail");
    PipelineCache newer;
    Access::set_directory(newer, dir);
    Access::load(newer, shader);
    Access::seed(newer, shader, 3u);
    newer.flush_disk();
    passed &= expect(valid_reload(dir, shader, 3u), "other writer persisted its new record before stale flush");
    stale.flush_disk();
    passed &= expect(valid_reload(dir, shader, 3u), "stale repair preserves the other writer's complete record");
    return passed;
}

bool shutdown_repair_retry(const std::filesystem::path& root, bool shader, bool add_new) {
    const auto dir = root / "shared";
    PipelineCache seed;
    Access::set_directory(seed, dir);
    Access::seed(seed, shader, 1u);
    seed.flush_disk();
    append_bytes(cache_file(dir, shader), make_truncated_record(root / "tail", shader));
    PipelineCache live;
    Access::set_directory(live, dir);
    Access::load(live, shader);
    if (add_new) Access::seed(live, shader, 3u);
    const Bytes damaged = read_bytes(cache_file(dir, shader));
    galaxy::gx::set_pipeline_cache_persistence_test_fault(0u, false);
    live.shutdown();
    bool passed = expect(Access::repair_pending(live, shader) &&
        read_bytes(cache_file(dir, shader)) == damaged,
        "failed shutdown repair retains original bytes and retry flag");
    galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    live.flush_disk();
    PipelineCache reload;
    Access::set_directory(reload, dir);
    Access::load(reload, shader);
    passed &= expect(Access::loaded(reload, shader) == (add_new ? 2u : 1u) &&
        Access::contains(reload, shader, 1u) &&
        (!add_new || Access::contains(reload, shader, 3u)) &&
        !Access::repair_pending(reload, shader) && Access::pending(live, shader) == 0u,
        "post-shutdown repair retry retains prefix and pending records");
    return passed;
}

enum class Failure { Open, Write, Close, Replace };
bool failure_retry(const std::filesystem::path& root, bool shader, Failure failure, bool shutdown_retry = false) {
    PipelineCache cache;
    Access::set_directory(cache, root);
    Access::seed(cache, shader, 1u);
    cache.flush_disk();
    const auto path = cache_file(root, shader);
    const Bytes original = read_bytes(path);
    Access::seed(cache, shader, 2u);
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    HANDLE lock = INVALID_HANDLE_VALUE;
    if (failure == Failure::Open) {
        std::filesystem::create_directory(tmp);
    } else if (failure == Failure::Write) {
        galaxy::gx::set_pipeline_cache_persistence_test_fault(original.size() + 12u, false);
    } else if (failure == Failure::Close) {
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, true);
    } else {
        // Permit the actual read, but deny replacing the destination.
        lock = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("test replacement lock failed");
    }
    cache.flush_disk();
    if (shutdown_retry) cache.shutdown();
    else cache.flush_disk();
    bool passed = expect(Access::pending(cache, shader) == 1u && read_bytes(path) == original,
        "failed open/write/close/replace retains pending entry through repeated attempts");
    galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    if (failure == Failure::Open) std::filesystem::remove(tmp);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    cache.flush_disk();
    passed &= expect(Access::pending(cache, shader) == 0u &&
        read_bytes(path).size() == 2u * original.size() - 8u && valid_reload(root, shader, 2u),
        "successful retry persists the original and pending entry exactly once");
    return passed;
}

bool new_file_failed_write_retry(const std::filesystem::path& root, bool shader) {
    PipelineCache cache;
    Access::set_directory(cache, root);
    Access::seed(cache, shader, 1u);
    galaxy::gx::set_pipeline_cache_persistence_test_fault(4u, false);
    cache.flush_disk();
    bool passed = expect(Access::pending(cache, shader) == 1u &&
        !std::filesystem::exists(cache_file(root, shader)),
        "failed new header write cannot publish a partial cache");
    galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    cache.flush_disk();
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, shader);
    passed &= expect(Access::pending(cache, shader) == 0u &&
        Access::loaded(reload, shader) == 1u && Access::contains(reload, shader, 1u),
        "fresh-cache retry preserves pending data and reloads a complete header and record");
    return passed;
}

bool truncated_header_retry(const std::filesystem::path& root, bool shader, std::size_t bytes) {
    PipelineCache cache;
    Access::set_directory(cache, root);
    Access::seed(cache, shader, 1u);
    cache.flush_disk();
    const auto path = cache_file(root, shader);
    Bytes header = read_bytes(path);
    header.resize(bytes);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        file.close();
        if (!file) throw std::runtime_error("test truncated header write failed");
    }
    PipelineCache recovered;
    Access::set_directory(recovered, root);
    Access::load(recovered, shader);
    bool passed = expect(Access::loaded(recovered, shader) == 0u,
        "truncated header never exposes records");
    Access::seed(recovered, shader, 2u);
    recovered.flush_disk();
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, shader);
    passed &= expect(Access::loaded(reload, shader) == 1u && Access::contains(reload, shader, 2u),
        "truncated header recovers a new complete file");
    Access::seed(reload, shader, 3u);
    reload.flush_disk();
    PipelineCache second_reload;
    Access::set_directory(second_reload, root);
    Access::load(second_reload, shader);
    passed &= expect(Access::loaded(second_reload, shader) == 2u &&
        Access::contains(second_reload, shader, 2u) && Access::contains(second_reload, shader, 3u),
        "repeated reload and append keep both complete records accessible");
    return passed;
}

bool damaged_shader_recovery(const std::filesystem::path& root, bool wrong_stage) {
    PipelineCache writer;
    Access::set_directory(writer, root);
    Access::seed(writer, true, 1u);
    if (wrong_stage) Access::wrong_stage(writer);
    writer.flush_disk();
    const auto path = cache_file(root, true);
    if (!wrong_stage) {
        Bytes damaged = read_bytes(path);
        // Past the 8-byte file header and 40-byte record header: corrupt a DXBC
        // payload byte with all lengths and framing intact.
        damaged.at(48u) ^= 1u;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
        file.close();
        if (!file) throw std::runtime_error("test corruption write failed");
    }
    PipelineCache recovered;
    Access::set_directory(recovered, root);
    Access::load(recovered, true);
    bool passed = expect(Access::loaded(recovered, true) == 0u && Access::repair_pending(recovered, true),
        "framed corruption or wrong-stage DXBC is excluded and scheduled for repair");
    Access::seed(recovered, true, 2u);
    recovered.flush_disk();
    PipelineCache reloaded;
    Access::set_directory(reloaded, root);
    Access::load(reloaded, true);
    passed &= expect(Access::loaded(reloaded, true) == 1u && Access::contains(reloaded, true, 2u) &&
        !Access::repair_pending(reloaded, true), "validated replacement recovers after invalid DXBC");
    return passed;
}

bool shared_stage_reload(const std::filesystem::path& root) {
    PipelineCache writer;
    Access::set_directory(writer, root);
    Access::seed(writer, true, 1u);
    Access::seed_shared_vertex(writer);
    writer.flush_disk();
    PipelineCache reload;
    Access::set_directory(reload, root);
    Access::load(reload, true);
    return expect(Access::shared_vertex_loaded(reload),
        "distinct shader pairs intern their shared vertex bytecode after loading");
}

bool busy_writer_retry(const std::filesystem::path& root) {
    PipelineCache cache;
    Access::set_directory(cache, root);
    Access::seed(cache, false, 1u);
    const auto lock_path = root / "psos.bin.lock";
    HANDLE lease = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0u,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lease == INVALID_HANDLE_VALUE) throw std::runtime_error("test writer lease failed");
    cache.flush_disk();
    const bool retained = Access::pending(cache, false) == 1u &&
        !std::filesystem::exists(root / "psos.bin");
    CloseHandle(lease);
    cache.flush_disk();
    return expect(retained && Access::pending(cache, false) == 0u &&
        std::filesystem::exists(root / "psos.bin"), "a busy cache writer retains pending records and retries later");
}

struct IsolatedFiles {
    std::filesystem::path root;
    IsolatedFiles() {
        // Stay inside the build tree; never touch LOCALAPPDATA or the user cache.
        root = std::filesystem::absolute(std::filesystem::current_path() / "generated" /
            ("cache-persistence-test-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount64())));
        if (std::filesystem::exists(root)) throw std::runtime_error("test isolation path already exists");
        std::filesystem::create_directories(root);
    }
    ~IsolatedFiles() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
} // namespace

namespace {
bool asynchronous_persistence(const std::filesystem::path& dir, bool fail_write,
                              bool shutdown_with_work) {
    PipelineCache cache;
    Access::set_directory(cache, dir);
    struct WriteGate {
        PipelineCache& owner;
        HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~WriteGate() {
            if (release) SetEvent(release);
            Access::finish_disk(owner);
            galaxy::gx::set_pipeline_cache_persistence_test_write_gate(nullptr, nullptr);
            galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
            if (entered) CloseHandle(entered);
            if (release) CloseHandle(release);
        }
    } gate{cache};
    if (!gate.entered || !gate.release) throw std::runtime_error("write gate creation failed");
    for (bool shader : {true, false}) Access::seed(cache, shader, 1u);
    galaxy::gx::set_pipeline_cache_persistence_test_write_gate(gate.entered, gate.release);
    galaxy::gx::set_pipeline_cache_persistence_test_fault(
        fail_write ? 0u : kNoWriteFault, false);
    const auto begin = std::chrono::steady_clock::now();
    cache.flush_disk_async();
    const auto submitted = std::chrono::steady_clock::now();
    bool passed = expect(WaitForSingleObject(gate.entered, 2000u) == WAIT_OBJECT_0,
        "background writer reaches held disk boundary");
    const auto poll_begin = std::chrono::steady_clock::now();
    cache.poll_disk_flush();
    cache.flush_disk_async();
    const auto polled = std::chrono::steady_clock::now();
    passed &= expect(Access::disk_active(cache), "poll leaves held writer outstanding");
    passed &= expect(Access::pending(cache, true) == 1u && Access::pending(cache, false) == 1u,
        "submission does not retire unsaved records");
    // New shader/PSO publication is independent of the held snapshot. The
    // worker may only access its immutable blobs and copied identities.
    for (bool shader : {true, false}) Access::seed(cache, shader, 2u);
    SetEvent(gate.release);
    Access::finish_disk(cache);
    for (bool shader : {true, false}) {
        passed &= expect(Access::pending(cache, shader) == (fail_write ? 2u : 1u),
            "only successfully saved prefix retires; failures and new records survive");
    }
    {
        PipelineCache reload;
        Access::set_directory(reload, dir);
        for (bool shader : {true, false}) {
            Access::load(reload, shader);
            passed &= expect(Access::loaded(reload, shader) == (fail_write ? 0u : 1u),
                "first persisted snapshot excludes concurrently added records");
        }
    }
    galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    cache.flush_disk_async();
    if (shutdown_with_work) cache.shutdown();
    else Access::finish_disk(cache);
    passed &= expect(!Access::disk_active(cache), "shutdown or completion joins writer ownership");
    PipelineCache reload;
    Access::set_directory(reload, dir);
    for (bool shader : {true, false}) {
        Access::load(reload, shader);
        passed &= expect(Access::loaded(reload, shader) == 2u &&
            Access::contains(reload, shader, 1u) && Access::contains(reload, shader, 2u),
            "next launch reuses both exact shader/PSO records after retry or shutdown");
    }
    const auto request_us = std::chrono::duration<double, std::micro>(submitted-begin).count();
    const auto poll_us = std::chrono::duration<double, std::micro>(polled-poll_begin).count();
    std::cout << "[async-cache-fixture] fail=" << fail_write << " shutdown=" << shutdown_with_work
              << " request-us=" << request_us << " held-writer-poll-us=" << poll_us << '\n';
    // Five-second gate timeout makes a mistakenly synchronous call measurable
    // rather than hanging the suite. Allow ample scheduling noise in this check.
    passed &= expect(request_us < 1000000.0 && poll_us < 1000000.0,
        "normal submission/poll does not wait for held file writes");
    return passed;
}
} // namespace

int main() {
    try {
        IsolatedFiles files;
        bool passed = true;
        unsigned case_index = 0u;
        for (bool failed : {false, true}) {
            for (bool stop : {false, true}) {
                passed &= asynchronous_persistence(files.root / std::to_string(case_index++), failed, stop);
            }
        }
        passed &= damaged_shader_recovery(files.root / std::to_string(case_index++), false);
        passed &= damaged_shader_recovery(files.root / std::to_string(case_index++), true);
        passed &= shared_stage_reload(files.root / std::to_string(case_index++));
        passed &= busy_writer_retry(files.root / std::to_string(case_index++));
        for (bool busy_writer : {true, false}) {
            passed &= expect(Access::pipeline_library_retry(
                files.root / std::to_string(case_index++), busy_writer),
                "failed pipeline-library lease/open keeps the real flush gate pending across retries");
        }
        for (bool startup : {false, true}) {
            for (bool before : {false, true}) {
                for (unsigned failure = 0u; failure < 3u; ++failure) {
                    passed &= expect(Access::running_completion_wake(before, failure, startup),
                        "running pipeline completion/error retains a pre-wait signal and wakes a parked owner");
                }
            }
        }
        passed &= expect(Access::stopped_completion_wake(),
            "stopped pipeline worker cannot leave an owner sleeping on unfinishable work");
        passed &= expect(Access::queued_takeover(), "first use claims queued prewarm without duplicate work");
        passed &= expect(Access::queued_takeover_single_owner(0u), "queued cached first use excludes real worker duplicate creation");
        passed &= expect(Access::queued_takeover_single_owner(1u), "declined inline creation retires owned marker for full-key retry");
        passed &= expect(Access::queued_takeover_single_owner(2u), "throwing inline creation retires owned marker");
        for (unsigned exception_kind : {0u, 1u, 2u}) {
            passed &= expect(Access::completion_publication_failure(7u, exception_kind, false),
                             "popped completion retires marker before map publication failure; get retries");
            for (bool existing : {false, true}) {
                passed &= expect(Access::completion_publication_failure(6u, exception_kind, existing),
                                 "popped completion retires marker before persistence failure; published owner survives");
            }
        }
        passed &= expect(Access::recombined_prewarm_stages(), "prewarm reuses independently cached stages and rejects missing components");
        passed &= expect(Access::worker_failure_propagates(), "worker exception is propagated during startup wait");
        passed &= expect(Access::worker_failure_gate_visible(), "lock-free worker-failure gate still surfaces the error to get()");
        passed &= expect(Access::memoized_hash_consistent(), "memoized PsoKey hash matches a freshly derived hash and separates keys");
        for (bool during_load : {false, true}) {
            for (bool reflection_failure : {false, true}) {
                for (std::size_t readable_records : {0u, 1u}) {
                    passed &= shader_resource_retry(files.root / std::to_string(case_index++),
                        during_load, reflection_failure, readable_records);
                }
            }
        }
        for (unsigned stage : {1u, 2u, 3u, 4u, 5u}) {
            for (std::size_t completed : {0u, 1u}) {
                for (unsigned exception_kind : {0u, 1u, 2u}) {
                    passed &= loader_publication_retry(files.root / std::to_string(case_index++),
                        stage, completed, exception_kind);
                }
            }
        }
        for (unsigned exception_kind : {0u, 1u, 2u}) {
            passed &= shader_pair_publication_retry(files.root / std::to_string(case_index++), exception_kind);
        }
        for (std::size_t completed : {0u, 1u}) {
            for (unsigned exception_kind : {0u, 1u, 2u}) {
                passed &= pending_publication_retry(files.root / std::to_string(case_index++),
                    completed, exception_kind);
            }
        }
        for (bool shader : {true, false}) {
            passed &= stale_repair_preserves_other_writer(files.root / std::to_string(case_index++), shader);
            for (bool add_new : {false, true}) {
                passed &= shutdown_repair_retry(files.root / std::to_string(case_index++), shader, add_new);
            }
            for (std::size_t header_bytes : {0u, 4u, 7u}) {
                passed &= truncated_header_retry(files.root / std::to_string(case_index++), shader, header_bytes);
            }
            for (bool add_new : {false, true}) {
                passed &= tail_repair(files.root / std::to_string(case_index++), shader, add_new, false);
            }
            passed &= tail_repair(files.root / std::to_string(case_index++), shader, true, true);
            for (Failure failure : {Failure::Open, Failure::Write, Failure::Close, Failure::Replace}) {
                passed &= failure_retry(files.root / std::to_string(case_index++), shader, failure);
            }
            passed &= failure_retry(files.root / std::to_string(case_index++), shader, Failure::Open, true);
            passed &= new_file_failed_write_retry(files.root / std::to_string(case_index++), shader);
        }
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
        galaxy::gx::set_pipeline_cache_persistence_test_read_fault(kNoWriteFault, false);
        galaxy::gx::set_pipeline_cache_persistence_test_load_fault(0u, kNoWriteFault, 0u);
        if (passed) std::printf("pipeline cache persistence: %u isolated cases passed; no GPU created\n", case_index);
        return passed ? 0 : 1;
    } catch (const std::exception& e) {
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
        galaxy::gx::set_pipeline_cache_persistence_test_read_fault(kNoWriteFault, false);
        galaxy::gx::set_pipeline_cache_persistence_test_load_fault(0u, kNoWriteFault, 0u);
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
}
