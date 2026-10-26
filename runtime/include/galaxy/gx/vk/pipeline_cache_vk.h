// SPDX-License-Identifier: GPL-3.0-only
// GX pipelines on Vulkan (docs/VULKAN.md), with the interface GxBackend uses on PipelineCache.
//
// A pipeline is its key's specialised shaders (ShaderGenerator's HLSL, compiled by glslang) with its render state. A
// key seen for the first time draws with the uber pipeline of its render state while a worker builds the specialised
// one; drain_completions publishes finished pipelines at the frame boundary, so the render thread never waits for a
// compile once the uber shaders exist. SPIR-V (by its HLSL's hash, so a changed generator never serves stale code),
// the keys seen and the driver's VkPipelineCache persist in cache_dir; a later session builds every known pipeline
// on the workers at start.
//
// Driver compatibility (docs/VULKAN.md): a pipeline the driver refuses is built again without each optional feature it
// uses (CompatBit, cheapest loss first; for a specialised pipeline also with the other TEV zero form), and a feature
// whose removal fixed two pipelines is left out of every later one. A key no form of which the driver accepts is
// remembered as refused and drawn by its uber pipeline (none: the draw is skipped). The first launch on a driver
// build tests the uber pipelines of every pixel-shader variant and feature in the background; what it and the
// fallbacks found is kept in vk_compat.txt for that driver build. The first refusal logs the driver's result, the
// pipeline's state, the driver's own log (host_window::driver_log) and the shaders' HLSL.
#pragma once

#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/vk/compile_journal.h"
#include "galaxy/gx/vk/renderer_vk.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace galaxy::gx {

class PipelineCacheVk {
public:

    bool initialize(VkDevice device, const vk::GxLayout* layout, const std::filesystem::path& cache_dir,
                    unsigned worker_count);

    // The SPIR-V of every module the keys of a vk_keys.bin can use on any Vulkan device, in vk_shaders.bin's form, as
    // `out`. Shipped as vk_seed_shaders.bin beside the seed keys, so a device compiles no HLSL for them. No device
    // needed. Returns the modules written (0: `error` says why).
    static std::size_t build_shader_seed(const std::filesystem::path& keys, const std::filesystem::path& out,
                                         unsigned threads, std::string& error);

    // Render thread only.
    [[nodiscard]] VkPipeline get(const PsoKey& key, const PixelShaderKey& ps_key, const VertexShaderKey& vs_key);
    void drain_completions();

    void flush_disk();
    void flush_disk_async();
    void poll_disk_flush();
    [[nodiscard]] bool has_unsaved_records() const noexcept { return unsaved_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t specialized_hits() const noexcept { return specialized_hits_; }
    [[nodiscard]] std::uint64_t uber_fallbacks() const noexcept { return uber_fallbacks_; }
    [[nodiscard]] bool last_get_used_uber() const noexcept { return last_get_used_uber_; }

    struct CompileStats {
        std::uint64_t pso_compiles = 0;
        std::uint64_t pso_library_loads = 0;  // not separable from builds on Vulkan
        std::uint64_t shader_pair_compiles = 0;
        std::uint64_t blobs_from_cache = 0;
        std::uint64_t blob_compile_us = 0;
        std::size_t shader_records = 0;
        std::size_t shader_records_on_disk = 0;
        std::size_t pso_keys = 0;
        std::size_t pso_keys_on_disk = 0;
        bool shader_cache_at_capacity = false;
        bool pso_cache_at_capacity = false;
    };
    [[nodiscard]] CompileStats compile_stats() const;

    // Optional features a pipeline can be built without (create_pipeline), in the order the fallbacks try them.
    enum CompatBit : std::uint32_t {
        kCompatNoDepthClip = 1u << 0u,   // no VK_EXT_depth_clip_enable (depth clamped, primitives unclipped)
        kCompatNoLogicOp = 1u << 1u,     // GX logic ops draw as plain writes
        kCompatNoDualSource = 1u << 2u,  // single-source blend factors, no second pixel-shader output
        kCompatNoDepthClamp = 1u << 3u,
        kCompatNoGeometry = 1u << 4u,    // lines and points one pixel wide
        kCompatBits = 5u,
    };
    // Shader-generator forms the fallbacks can switch (ShaderGenOptions).
    enum GeneratorBit : std::uint32_t {
        kGeneratorTevZeroFromConstant = 1u << 0u,
    };

private:
    // A pipeline's full keys: the shaders are generated again from them.
    struct Entry {
        PsoKey key{};
        VertexShaderKey vs_key{};
        PixelShaderKey ps_key{};
    };
    // Written and read as raw records (vk_keys.bin).
    static_assert(std::has_unique_object_representations_v<Entry>);
    struct UberKey {
        RenderStateKey render_state{};
        unsigned variant = 0;
        bool operator==(const UberKey& other) const noexcept {
            return render_state == other.render_state && variant == other.variant;
        }
    };
    struct UberKeyHasher {
        std::size_t operator()(const UberKey& key) const noexcept;
    };
    struct Completion {
        PsoKey key{};
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    [[nodiscard]] VkShaderModule module_for(const std::string& hlsl, vk::ShaderStage stage, bool dual_source);
    [[nodiscard]] ShaderGenerator generator_for(std::uint32_t generator_bits) const;
    // Points drawn as points (no expanding geometry shader): the vertex shader must write the point size.
    [[nodiscard]] bool unexpanded_points(const RenderStateKey& render_state, std::uint32_t compat) const noexcept;
    [[nodiscard]] VkShaderModule point_size_variant(const std::string& vertex_hlsl);
    [[nodiscard]] VkPipeline build(const Entry& entry);
    // `export_depth`: the pixel shader writes the fragment depth (Z textures). `compat`: CompatBits to build without.
    [[nodiscard]] VkPipeline create_pipeline(const RenderStateKey& render_state, VkShaderModule vs, VkShaderModule ps,
                                             bool dual_source, bool export_depth, std::uint32_t compat,
                                             VkResult& result) const;
    // The CompatBits `render_state` (with these shaders' outputs) uses.
    [[nodiscard]] std::uint32_t applicable_compat(const RenderStateKey& render_state, bool dual_source,
                                                  bool export_depth) const;
    // `make(compat, result)` with this device's CompatBits, then (refused) with each applicable fallback, then all.
    // `transient`: the driver was out of memory or lost the device (no answer about this pipeline).
    template <typename Make>
    VkPipeline with_fallbacks(Make&& make, std::uint32_t applicable, const char* what, const std::string& detail,
                              bool* transient = nullptr);
    void report_refusal(const char* what, const std::string& detail, VkResult result, std::uint32_t compat);
    void learn(std::uint32_t compat_bit, std::uint32_t generator_bit);
    void load_compat();
    void save_compat();
    [[nodiscard]] VkPipeline uber_pipeline(const RenderStateKey& render_state, unsigned variant);
    // The uber pipeline drawing a key whose own pipeline is missing (refused, or still building).
    [[nodiscard]] VkPipeline stand_in(const PsoKey& key, const PixelShaderKey& ps_key);
    // The uber pixel-shader variant this key draws with links on this driver (uber_refused_).
    [[nodiscard]] bool uber_usable(const PixelShaderKey& ps_key) const noexcept;
    // Takes a queued job for `key` off the workers' queue (it is built elsewhere); false when none was queued.
    bool unqueue(const PsoKey& key);
    // The self-test's look at a refused uber variant: which of its two shaders the driver rejects.
    void diagnose_uber(const RenderStateKey& render_state, unsigned variant);
    void compile_uber_shaders() noexcept;
    void self_test();
    void worker_main();
    void enqueue(const Entry& entry);
    void load_disk();
    void save_disk() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    const vk::GxLayout* layout_ = nullptr;
    std::filesystem::path cache_dir_;
    VkPipelineCache driver_cache_ = VK_NULL_HANDLE;
    ShaderGenOptions generator_defaults_;  // this device's (DeviceQuirks), before the fallbacks' changes

    // Shader modules by the hash of their HLSL (and output form); SPIR-V kept for the disk.
    mutable std::mutex modules_mutex_;
    std::unordered_map<std::uint64_t, VkShaderModule> modules_;
    // A vertex module's input locations (bit n: location n): its pipelines bind only those attributes.
    std::unordered_map<VkShaderModule, std::uint32_t> input_masks_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> spirv_;
    std::size_t spirv_on_disk_ = 0;
    // vk_seed_shaders.bin: by hash, the record's file offset and words (read when its module is made).
    std::filesystem::path seed_shaders_path_;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint32_t>> seed_index_;
    std::atomic<bool> prewarm_enqueued_{false};
    std::atomic<bool> seed_drained_{false};  // every seeded pipeline built: save_disk deletes the seeds

    // Uber shaders: built on their own thread at start.
    std::thread uber_thread_;
    std::atomic<bool> uber_ready_{false};
    bool uber_probed_ = false;  // diagnose_uber's cut-down forms run (uber thread)
    VkShaderModule uber_vs_ = VK_NULL_HANDLE;
    std::array<VkShaderModule, kUberPsVariants> uber_ps_{};
    VkShaderModule line_gs_ = VK_NULL_HANDLE;
    VkShaderModule point_gs_ = VK_NULL_HANDLE;
    // Curved surfaces' hull and domain stages by vertex output variant (curved_variant).
    std::array<VkShaderModule, 4> curved_hs_{};
    std::array<VkShaderModule, 4> curved_ds_{};
    std::unordered_map<UberKey, VkPipeline, UberKeyHasher> uber_pipelines_;  // render thread

    // Render thread.
    std::unordered_map<PsoKey, VkPipeline, PsoKeyHasher> published_;
    std::unordered_set<PsoKey, PsoKeyHasher> in_flight_;
    PsoKey memo_key_{};
    VkPipeline memo_pipeline_ = VK_NULL_HANDLE;
    bool last_get_used_uber_ = false;
    std::uint64_t specialized_hits_ = 0;
    std::uint64_t uber_fallbacks_ = 0;

    // Keys seen (persisted), workers and their queues.
    mutable std::mutex entries_mutex_;
    std::vector<Entry> entries_;
    std::unordered_set<PsoKey, PsoKeyHasher> entry_keys_;
    std::size_t entries_on_disk_ = 0;
    std::atomic<bool> unsaved_{false};
    std::mutex jobs_mutex_;
    std::condition_variable jobs_cv_;
    std::deque<Entry> jobs_;
    std::vector<Completion> completions_;
    std::vector<std::thread> workers_;
    std::thread flush_thread_;
    std::atomic<bool> flush_done_{true};

    // Driver compatibility (CompatBit / GeneratorBit): what every build leaves out or switches, fixes counted towards
    // that, the refusals seen.
    std::atomic<std::uint32_t> device_compat_{0};
    std::atomic<std::uint32_t> generator_bits_{0};
    std::array<std::atomic<std::uint32_t>, kCompatBits + 1u> fixes_{};
    std::atomic<std::uint32_t> refusals_{0};
    std::atomic<bool> refusal_reported_{false};
    std::mutex compat_mutex_;
    // Keys (PsoKey::hash) the driver accepted in no form, and the ones it crashed on (CompileJournal); uber pipelines
    // (uber_hash) it crashed on.
    std::unordered_set<std::uint64_t> refused_keys_;
    std::unordered_set<std::uint64_t> crashed_keys_;
    // Shader modules (by HLSL hash) the driver refuses with the simplest partner: every later pipeline using one is
    // refused at once instead of trying its fallbacks (each a full driver compile, on the render thread at a first
    // draw). Their keys are recorded as refused; the modules themselves only this session.
    std::unordered_set<std::uint64_t> rejected_modules_;
    // Which of a refused pipeline's shaders the driver rejects on its own (`rejected_modules_`).
    void classify_refusal(const RenderStateKey& rs, const std::string& vs_hlsl, const std::string& ps_hlsl,
                          bool dual_source, bool export_depth, std::uint32_t compat);
    std::unordered_set<std::uint64_t> crashed_uber_;
    // Uber pixel-shader variants (bit n: variant n) the driver accepts in no form: their keys are built specialised,
    // at once, when nothing has built them yet (Adreno 840 512.842: "Failed to link shaders").
    std::atomic<std::uint32_t> uber_refused_{0};
    vk::CompileJournal journal_;
    std::string driver_key_;
    bool compat_known_ = false;  // vk_compat.txt was this driver build's

    std::atomic<std::uint64_t> pso_compiles_{0};
    std::atomic<std::uint64_t> shader_compiles_{0};
    std::atomic<std::uint64_t> shaders_from_cache_{0};
    std::atomic<std::uint64_t> compile_us_{0};
};

using PipelineCache = PipelineCacheVk;

}  // namespace galaxy::gx
