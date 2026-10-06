// SPDX-License-Identifier: GPL-3.0-only
// Tests the actual cache loaders/writers with valid compiled DXBC; no GPU is created.
#include "galaxy/gx/pipeline_cache.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <d3dcompiler.h>
#pragma warning(pop)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::gx {

// Defined only in this target's instrumented pipeline_cache.cpp object.
void set_pipeline_cache_persistence_test_fault(std::size_t write_budget, bool fail_close);

struct PipelineCachePersistenceTestAccess {
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
    static void load(PipelineCache& cache, bool shader) {
        if (shader) cache.load_disk_cache();
        else cache.load_pso_key_cache();
    }
    static std::size_t pending(const PipelineCache& cache, bool shader) {
        return shader ? cache.unsaved_blobs_.size() : cache.unsaved_pso_keys_.size();
    }
    static std::size_t loaded(const PipelineCache& cache, bool shader) {
        return shader ? cache.shader_blobs_.size() : cache.warm_pso_keys_.size();
    }
    static bool repair_pending(const PipelineCache& cache, bool shader) {
        return shader ? cache.shader_cache_repair_pending_ : cache.pso_cache_repair_pending_;
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
        cache.in_flight_.insert(record);
        ID3D12PipelineState* result = nullptr;
        return !cache.wait_for_in_flight_pso(record, result) && result == nullptr &&
            cache.jobs_.empty() && cache.in_flight_.empty();
    }
    static bool worker_failure_propagates() {
        PipelineCache cache;
        cache.worker_error_ = std::make_exception_ptr(std::runtime_error("worker fixture failure"));
        cache.in_flight_.insert(key(1u));
        try { cache.wait_for_cached_pso_prewarm(1u); }
        catch (const std::runtime_error& e) {
            return std::string(e.what()) == "worker fixture failure";
        }
        return false;
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
        // Even an unsuccessful first repair/persist must leave the original
        // prefix+tail untouched and retain the pending entry for this process.
        const Bytes damaged = read_bytes(path);
        galaxy::gx::set_pipeline_cache_persistence_test_fault(original.size() + 12u, false);
        live.flush_disk();
        passed &= expect(Access::pending(live, shader) == 1u && read_bytes(path) == damaged,
            "partial replacement failure retains pending data and original bytes");
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
    }
    live.flush_disk();
    const Bytes repaired = read_bytes(path);
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
        // Header(8) + record header(40): corrupt the DXBC payload, keeping
        // all lengths and framing intact so this exercises integrity checking.
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
        // CTest runs in the C: build tree. No LOCALAPPDATA or user cache access.
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

int main() {
    try {
        IsolatedFiles files;
        bool passed = true;
        unsigned case_index = 0u;
        passed &= damaged_shader_recovery(files.root / std::to_string(case_index++), false);
        passed &= damaged_shader_recovery(files.root / std::to_string(case_index++), true);
        passed &= shared_stage_reload(files.root / std::to_string(case_index++));
        passed &= busy_writer_retry(files.root / std::to_string(case_index++));
        passed &= expect(Access::queued_takeover(), "first use claims queued prewarm without duplicate work");
        passed &= expect(Access::worker_failure_propagates(), "worker exception is propagated during startup wait");
        for (bool shader : {true, false}) {
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
        if (passed) std::printf("pipeline cache persistence: %u isolated cases passed; no GPU created\n", case_index);
        return passed ? 0 : 1;
    } catch (const std::exception& e) {
        galaxy::gx::set_pipeline_cache_persistence_test_fault(kNoWriteFault, false);
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
}
