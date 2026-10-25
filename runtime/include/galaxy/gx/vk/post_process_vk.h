// SPDX-License-Identifier: GPL-3.0-only
// Presentation post-processing on Vulkan, the D3D12 chain's (post_process.h) with the same shaders: SMAA 1x or FXAA
// of the 3D scene before the 2D layer draws, so menus and text stay sharp, and the scaling chain to the window (FSR 1
// EASU when enlarging, else the footprint box filter, then RCAS). post_process.hlsl is composed with the vendored SMAA
// and FidelityFX FSR 1 sources as the D3D12 build composes its fxc inputs, and compiled on the device, on a worker
// thread: until it is ready the renderer presents with its plain blit.
#pragma once

#include "galaxy/gx/post_process_settings.h"
#include "galaxy/gx/vk/vk_context.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace galaxy::gx::vk {

class PostProcessorVk {
public:
    // The renderer's descriptor sets for one draw (its frame pool, or a replay list's while one is recorded).
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;

    PostProcessorVk() = default;
    PostProcessorVk(const PostProcessorVk&) = delete;
    PostProcessorVk& operator=(const PostProcessorVk&) = delete;
    ~PostProcessorVk() { shutdown(); }

    // Starts compiling every pass, for the renderer's EFB pass (RGBA8 and depth) and its RGBA8 colour pass (one
    // attachment, ending shader-readable). The present pass comes with set_present_pass.
    void initialize(const PostProcessSettings& settings, VkRenderPass efb_pass, VkRenderPass color_pass);
    // The swap chain's render pass (made again when its format changes): the chain's last pass draws in it.
    void set_present_pass(VkRenderPass pass);
    void shutdown() noexcept;
    // Once the worker has finished: builds the pipelines and runs the SMAA and FXAA self-tests (a submission the
    // render thread waits for), so status() is final. True when the chain is ready.
    bool poll_ready();
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const PostProcessStatus& status() const noexcept { return status_; }
    [[nodiscard]] const PostProcessSettings& settings() const noexcept { return settings_; }
    // New settings between frames; an SMAA quality change rebuilds its two pipelines.
    void apply_settings(const PostProcessSettings& settings);

    // Scene AA of the EFB colour `color` (RGBA8, COLOR_ATTACHMENT_OPTIMAL, no render pass open), in two parts
    // around the renderer beginning its EFB pass again: prepare_scene_aa records the passes before (a copy of the
    // scene, SMAA's edges and weights) and returns the pass to finish with; finish_scene_aa draws the result into the
    // open EFB pass, RGB only (the alpha the game left there stays).
    AaRun prepare_scene_aa(VkCommandBuffer commands, const Texture& color, unsigned width, unsigned height,
                           unsigned picture_width, unsigned picture_height, unsigned display_width,
                           unsigned display_height, const SetAllocator& allocate);
    void finish_scene_aa(VkCommandBuffer commands, AaRun pass, unsigned width, unsigned height,
                         const SetAllocator& allocate);
    // The targets recorded scene AA reads, for a command buffer submitted again later (the interpolation replay):
    // it keeps them alive.
    void hold_scene_targets(std::vector<std::shared_ptr<Texture>>& held) const;

    // The scaling chain from `source` (shader-readable) to a `target_width` x `target_height` picture: prepare
    // records the passes outside the target's render pass (into the window-sized intermediate); finish draws the
    // last one in the target's pass (the present pass when `present`, else the colour pass) at `viewport`, whose
    // origin is the picture's in the target. A step without a pipeline (no present pass yet) is the renderer's to
    // draw with its blit.
    struct PresentStep {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkImageView input = VK_NULL_HANDLE;
        PostConstants constants{};
    };
    PresentStep prepare_present(VkCommandBuffer commands, const Texture& source, unsigned source_width,
                                unsigned source_height, unsigned target_width, unsigned target_height,
                                bool present, const SetAllocator& allocate);
    void finish_present(VkCommandBuffer commands, const PresentStep& step, const VkRect2D& viewport,
                        const SetAllocator& allocate);

private:
    // The pipelines' SPIR-V, compiled by the worker: vertex and pixel shader per pass.
    enum Program : unsigned {
        kFullscreenVs, kResamplePs, kFxaaPs, kEasuPs, kRcasPs, kRcasScaledPs, kSmaaEdgesVs, kSmaaEdgesPs,
        kSmaaWeightsVs, kSmaaWeightsPs, kSmaaBlendVs, kSmaaBlendPs, kDebandPs, kProgramCount
    };
    struct Target {
        std::shared_ptr<Texture> texture;  // its framebuffer is the pass's (with the stencil for edges and weights)
        unsigned width = 0;
        unsigned height = 0;
    };
    // Target pipelines by the render pass they draw in.
    struct PassPipelines {
        VkPipeline easu = VK_NULL_HANDLE;
        VkPipeline rcas = VK_NULL_HANDLE;
        VkPipeline rcas_scaled = VK_NULL_HANDLE;
        VkPipeline resample = VK_NULL_HANDLE;
    };

    // Every program, or only the three that depend on the SMAA quality.
    void compile_programs(unsigned smaa_quality, bool quality_only);
    VkShaderModule module(Program program) const;
    VkPipeline create_pipeline(Program vs, Program ps, VkRenderPass pass, VkColorComponentFlags write_mask,
                               int stencil) const;
    bool build_pipelines();
    void destroy_pipelines() noexcept;
    bool create_lookup_textures(VkCommandBuffer commands, std::vector<std::shared_ptr<Buffer>>& staging);
    bool ensure_target(Target& target, unsigned width, unsigned height, VkFormat format, VkRenderPass pass,
                       bool with_stencil);
    // SMAA's edges and weights with the stencil both their framebuffers hold, made together.
    bool ensure_smaa_targets(unsigned width, unsigned height);
    bool record_aa(VkCommandBuffer commands, AaRun pass, VkImageView source, unsigned width, unsigned height,
                   const SetAllocator& allocate);
    void begin_pass(VkCommandBuffer commands, VkRenderPass pass, const Target& target, bool clear_stencil) const;
    void draw(VkCommandBuffer commands, VkPipeline pipeline, std::initializer_list<VkImageView> inputs,
              const VkRect2D& viewport, const PostConstants& constants, const SetAllocator& allocate,
              const VkRect2D* scissor = nullptr) const;
    void self_test();

    PostProcessSettings settings_{};
    PostProcessStatus status_{};
    bool ready_ = false;
    bool built_ = false;
    bool easu_ = false;  // the device has EASU's single-channel gathers
    VkRenderPass efb_pass_ = VK_NULL_HANDLE;
    VkRenderPass color_pass_ = VK_NULL_HANDLE;
    VkRenderPass present_pass_ = VK_NULL_HANDLE;
    VkFormat stencil_format_ = VK_FORMAT_UNDEFINED;

    // The worker: SPIR-V for every program, done when `compiled_`.
    std::thread worker_;
    std::atomic<bool> compiled_{false};
    std::array<std::vector<std::uint32_t>, kProgramCount> spirv_{};
    std::array<char, 160> compile_error_{};

    VkDescriptorSetLayout image_layout_ = VK_NULL_HANDLE;    // set 0: t0-t2
    VkDescriptorSetLayout sampler_layout_ = VK_NULL_HANDLE;  // set 1: s0 linear, s1 point (immutable)
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkSampler samplers_[2]{};
    VkDescriptorPool sampler_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sampler_set_ = VK_NULL_HANDLE;
    VkRenderPass edges_pass_ = VK_NULL_HANDLE;    // RG8 + stencil, both cleared
    VkRenderPass weights_pass_ = VK_NULL_HANDLE;  // RGBA8 cleared + stencil kept

    VkPipeline smaa_edges_ = VK_NULL_HANDLE;
    VkPipeline smaa_weights_ = VK_NULL_HANDLE;
    VkPipeline smaa_blend_ = VK_NULL_HANDLE;      // colour pass (self-test)
    VkPipeline smaa_blend_rgb_ = VK_NULL_HANDLE;  // EFB pass, RGB only
    VkPipeline fxaa_ = VK_NULL_HANDLE;
    VkPipeline fxaa_rgb_ = VK_NULL_HANDLE;
    PassPipelines to_color_{};
    PassPipelines to_present_{};

    std::shared_ptr<Texture> area_texture_;
    std::shared_ptr<Texture> search_texture_;
    Target edges_;
    Target weights_;
    Target stencil_;  // shared by edges and weights
    Target antialiased_;
    Target scaled_;
    // The debanded picture, source-sized, and its pass (optional).
    Target debanded_;
    VkPipeline deband_ = VK_NULL_HANDLE;
};

}  // namespace galaxy::gx::vk
