// SPDX-License-Identifier: GPL-3.0-only
// Presentation post-processing on Vulkan, the D3D12 chain's (post_process.h) with the same shaders: SMAA 1x or FXAA
// of the 3D scene before the 2D layer draws, so menus and text stay sharp, and the scaling chain to the window (FSR 1
// EASU when enlarging, else the footprint box filter, then RCAS). post_process.hlsl is composed with the vendored SMAA
// and FidelityFX FSR 1 sources as the D3D12 build composes its fxc inputs, and compiled on the device, on a worker
// thread: until it is ready the renderer presents with its plain blit.
#pragma once

#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/vk/vk_context.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace galaxy::render::vk {

class PostProcessorVk {
public:
    // The renderer's descriptor sets for one draw.
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;

    PostProcessorVk() = default;
    PostProcessorVk(const PostProcessorVk&) = delete;
    PostProcessorVk& operator=(const PostProcessorVk&) = delete;
    ~PostProcessorVk() { shutdown(); }

    // Starts compiling every pass, for the renderer's scene target pass (RGBA8 and depth) and its RGBA8 colour pass (one
    // attachment, ending shader-readable). The present pass comes with set_present_pass.
    void initialize(const PostProcessSettings& settings, VkRenderPass scene_pass, VkRenderPass color_pass,
                    VkRenderPass window_pass);
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

    // The scene target's image traded for `spare` (RendererVk::swap_scene_color): the scene's own image comes back,
    // shader-readable, and the scene target goes on in `spare` (or one made for it), its old contents dropped.
    using SceneTrade = std::function<std::shared_ptr<Texture>(std::shared_ptr<Texture> spare)>;

    // Scene AA of the scene target colour `color` (RGBA8, COLOR_ATTACHMENT_OPTIMAL, no render pass open), in two parts
    // around the renderer beginning its scene target pass again: prepare_scene_aa records the passes before (a copy of the
    // scene, SMAA's edges and weights) and returns the pass to finish with; finish_scene_aa draws the result into the
    // open scene target pass, RGB only (the alpha the game left there stays). With `trade` (a single-sample scene) a pass
    // that runs reads the scene's own image instead of a copy, and finish_scene_aa writes every pixel and channel of
    // the image that took its place (the alpha the scene's): the scene target pass may open without loading it.
    AaRun prepare_scene_aa(VkCommandBuffer commands, const Texture& color, unsigned width, unsigned height,
                           unsigned picture_width, unsigned picture_height, unsigned display_width,
                           unsigned display_height, const SetAllocator& allocate, const SceneTrade& trade = {});
    void finish_scene_aa(VkCommandBuffer commands, AaRun pass, unsigned width, unsigned height,
                         const SetAllocator& allocate, VkRenderPass scene_pass, unsigned samples_log2);
    // The last prepare_scene_aa traded the scene's image: its finish writes the whole scene target.
    [[nodiscard]] bool scene_traded() const noexcept { return scene_traded_; }
    // The scaling chain from `source` (shader-readable) to a `target_width` x `target_height` picture: prepare
    // records the passes outside the target's render pass (into the window-sized intermediate); finish draws the
    // last one in the target's pass (the present pass when `present`, else the RGBA8 pass, dithered either way) at `viewport`, whose
    // origin is the picture's in the target. A step without a pipeline (no present pass yet) is the renderer's to
    // draw with its blit.
    struct PresentStep {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkImageView input = VK_NULL_HANDLE;
        PostConstants constants{};
        unsigned width = 0;  // the target size prepare_present was given
        unsigned height = 0;
    };
    PresentStep prepare_present(VkCommandBuffer commands, const Texture& source, unsigned source_width,
                                unsigned source_height, unsigned target_width, unsigned target_height,
                                bool present, const SetAllocator& allocate);
    // `output.hdr`: the target is an HDR10 swap chain image (the present pass made for its format); the default is the 8-bit
    // window's write. `quarter_turns`: the target is turned by 1-3 quarter turns clockwise (a pre-rotated swap chain image;
    // `viewport` is then the turned rectangle), and the pass writes each of its pixels from the picture's turned-back one.
    void finish_present(VkCommandBuffer commands, const PresentStep& step, const VkRect2D& viewport,
                        const SetAllocator& allocate, const PostOutput& output = PostOutput{},
                        unsigned quarter_turns = 0u);
    // The chain's pipelines into the present pass exist (an HDR10 swap chain needs them: nothing else writes its encoding).
    [[nodiscard]] bool present_ready() const noexcept {
        return to_present_.rcas != VK_NULL_HANDLE && to_present_.rcas_scaled != VK_NULL_HANDLE && to_present_.resample != VK_NULL_HANDLE &&
               to_present_.copy != VK_NULL_HANDLE;
    }

private:
    // The passes' vertex and pixel shaders, compiled at build.
    enum Program : unsigned {
#define POST(name, pass, library, quality, stage) name,
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
        kProgramCount
    };
    struct Target {
        std::shared_ptr<Texture> texture;  // its framebuffer is the pass's (with the stencil for edges and weights)
        unsigned width = 0;
        unsigned height = 0;
        std::uint64_t used = 0;  // clock_ when a pass last asked for it
    };
    // Target pipelines by the render pass they draw in.
    struct PassPipelines {
        VkPipeline easu = VK_NULL_HANDLE;
        VkPipeline rcas = VK_NULL_HANDLE;
        VkPipeline rcas_scaled = VK_NULL_HANDLE;
        VkPipeline resample = VK_NULL_HANDLE;
        VkPipeline copy = VK_NULL_HANDLE;  // the resample at 1:1 (PostProcessPlan::copy)
    };

    VkShaderModule module(Program program) const;
    VkPipeline create_pipeline(Program vs, Program ps, VkRenderPass pass, VkColorComponentFlags write_mask,
                               int stencil,
                               VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;
    [[nodiscard]] VkPipeline scene_pass_pipeline(bool smaa, VkRenderPass pass, unsigned samples_log2);
    bool build_pipelines();
    void destroy_pipelines() noexcept;
    bool create_lookup_textures(VkCommandBuffer commands, std::vector<std::shared_ptr<Buffer>>& staging);
    bool ensure_target(Target& target, unsigned width, unsigned height, VkFormat format, VkRenderPass pass,
                       bool with_stencil);
    // SMAA's edges and weights with the stencil both their framebuffers hold, made together.
    bool ensure_smaa_targets(unsigned width, unsigned height);
    // Hands back the targets no pass has asked for in 600 scene AA calls (SMAA's after a switch to FXAA or off, the scaling
    // intermediate after a window size that needs none); the next pass that wants one makes it again.
    void trim_idle_targets();
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
    bool to_build_ = false;  // initialized: poll_ready makes the pipelines
    unsigned spirv_quality_ = 0;  // the SMAA quality module() takes the quality-dependent programs at
    bool easu_ = false;  // the device has EASU's single-channel gathers
    VkRenderPass scene_pass_ = VK_NULL_HANDLE;
    VkRenderPass color_pass_ = VK_NULL_HANDLE;
    VkRenderPass window_pass_ = VK_NULL_HANDLE;
    VkRenderPass present_pass_ = VK_NULL_HANDLE;
    VkFormat stencil_format_ = VK_FORMAT_UNDEFINED;


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
    VkPipeline smaa_blend_rgb_ = VK_NULL_HANDLE;  // scene target pass, RGB only
    VkPipeline fxaa_ = VK_NULL_HANDLE;
    VkPipeline fxaa_rgb_ = VK_NULL_HANDLE;
    // The same last passes writing every channel, after a trade (prepare_scene_aa).
    VkPipeline smaa_blend_scene_ = VK_NULL_HANDLE;
    VkPipeline fxaa_scene_ = VK_NULL_HANDLE;
    // FXAA's, SMAA's last pass over MSAA: per sample count (log2).
    VkPipeline scene_pass_ms_[2][4]{};
    PassPipelines to_color_{};
    PassPipelines to_window_{};
    PassPipelines to_present_{};

    std::shared_ptr<Texture> area_texture_;
    std::shared_ptr<Texture> search_texture_;
    Target edges_;
    Target weights_;
    Target stencil_;  // shared by edges and weights
    Target antialiased_;
    Target scaled_;
    // The scene's image traded out this frame (prepare_scene_aa with a trade): the passes' source, the next trade's spare.
    std::shared_ptr<Texture> traded_;
    std::uint64_t traded_used_ = 0;  // clock_ at the last trade
    bool scene_traded_ = false;  // this frame's passes read traded_
    std::uint64_t clock_ = 0;      // scene AA calls so far
    std::uint64_t smaa_used_ = 0;  // clock_ when SMAA's three targets were last asked for
};

}  // namespace galaxy::render::vk
