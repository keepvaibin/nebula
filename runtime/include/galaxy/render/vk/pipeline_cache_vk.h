#pragma once

// PipelineCacheVk: native materials' pipelines on the Vulkan renderer (E-04). A material's SPIR-V is compiled at build
// (native_shaders.h); its pipeline under a render state is created on the render thread at its first draw, through
// the driver's VkPipelineCache (vk_pipelines.bin). A pipeline the driver refuses is built again without each optional
// feature it uses; a feature two pipelines needed leaving out is left out of every later one on this driver build
// (vk_compat.txt).

#include "galaxy/render/render_state_key.h"
#include "galaxy/render/vk/renderer_vk.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy::render {

class PipelineCacheVk {
public:
    ~PipelineCacheVk() noexcept { stop_prepare(); }

    // `cache_dir` holds vk_pipelines.bin and vk_compat.txt (created if missing).
    bool initialize(VkDevice device, const vk::RenderLayout* layout, const std::filesystem::path& cache_dir);

    // RENDER THREAD ONLY. A native material's pipeline: its build-compiled shaders under the draw's render state; null
    // when the build has none or the driver refuses every form.
    [[nodiscard]] VkPipeline get_native(std::uint32_t shader, const RenderStateKey& render_state, bool pixel_lit = false);
    // Starts building the seed pipelines on background threads; merge_prepared takes in what they finished.
    void start_prepare();
    void merge_prepared();
    void stop_prepare() noexcept;

    // The driver's cache data written to vk_pipelines.bin.
    void save_driver_cache();

    // Optional features a pipeline can be built without.
    enum CompatBit : std::uint32_t {
        kCompatNoDepthClip = 1u << 0u,   // no VK_EXT_depth_clip_enable (depth clamped, primitives unclipped)
        kCompatNoDepthClamp = 1u << 1u,
        kCompatBits = 2u,
    };

private:
    [[nodiscard]] VkShaderModule native_module(const void* words, std::size_t bytes, vk::ShaderStage stage);
    [[nodiscard]] VkPipeline create_pipeline(const RenderStateKey& render_state, VkShaderModule vs, VkShaderModule ps,
                                             bool export_depth, std::uint32_t compat,
                                             VkResult& result) const;
    [[nodiscard]] std::uint32_t applicable_compat(const RenderStateKey& render_state, bool export_depth) const;
    template <typename Make>
    VkPipeline with_fallbacks(Make&& make, std::uint32_t applicable, const char* what, const std::string& detail,
                              bool* transient = nullptr);
    void report_refusal(const char* what, const std::string& detail, VkResult result, std::uint32_t compat);
    void learn(std::uint32_t compat_bit);
    void load_compat();
    void save_compat();
    void load_driver_cache();

    VkDevice device_ = VK_NULL_HANDLE;
    const vk::RenderLayout* layout_ = nullptr;
    std::filesystem::path cache_dir_;
    VkPipelineCache driver_cache_ = VK_NULL_HANDLE;
    std::string driver_key_;
    mutable std::mutex modules_mutex_;
    std::unordered_map<std::uint64_t, VkShaderModule> modules_;
    // A vertex module's input locations (bit i: location i), for the vertex input state.
    std::unordered_map<VkShaderModule, std::uint32_t> input_masks_;
    // Native materials' pipelines by shader, output variant and render state.
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::pair<std::uint64_t, RenderStateKey>, VkPipeline>>>
        native_pipelines_;
    std::uint32_t created_ = 0;
    [[nodiscard]] VkPipeline create_native(std::uint32_t shader, const RenderStateKey& rs, unsigned variant);
    struct Prepared {
        std::uint32_t shader;
        RenderStateKey rs;
        VkPipeline pipeline;
    };
    bool prepare_started_ = false;
    std::vector<std::thread> prepare_threads_;
    std::atomic<std::size_t> prepare_next_{0u};
    std::atomic<bool> prepare_stop_{false};
    std::atomic<bool> prepare_ready_{false};
    std::mutex prepared_lock_;
    std::vector<Prepared> prepared_;
    // The optional features left out of every pipeline on this driver build, and how often each fixed a refusal.
    std::atomic<std::uint32_t> device_compat_{0};
    std::array<std::atomic<std::uint32_t>, kCompatBits + 1u> fixes_{};
    std::atomic<std::uint32_t> refusals_{0};
    std::atomic<bool> refusal_reported_{false};
};

// RenderCore's pipeline cache on this backend.
using PipelineCache = PipelineCacheVk;

}  // namespace galaxy::render
