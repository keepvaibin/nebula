// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Ambient occlusion on Vulkan: ambient_occlusion.cpp's passes with Vulkan's resources (ambient_occlusion.hlsl).

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/vk/vk_context.h"

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace galaxy::render::vk {

class AmbientOcclusionVk {
public:
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;
    enum class Stage { Distance, Occlusion, Denoise, Upsample };
    using StageMarker = std::function<void(Stage)>;

    AmbientOcclusionVk() = default;
    AmbientOcclusionVk(const AmbientOcclusionVk&) = delete;
    AmbientOcclusionVk& operator=(const AmbientOcclusionVk&) = delete;
    ~AmbientOcclusionVk() { shutdown(); }

    // The compute pipelines and the multiplier's pass and pipeline; the multiply's are made per scene target pass at first
    // use. False leaves the pass off.
    bool initialize();
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Outside a render pass, the scene's single-sample depth readable as `depth_layout` by compute and fragment shaders: the
    // colour multiplier of each scene target pixel (drawn into an 8-bit target in a pass of its own). False: nothing to
    // multiply.
    bool prepare(VkCommandBuffer commands, const Texture& depth, VkImageLayout depth_layout, const RtView& view,
                 bool ssao, std::span<const ao::ObjectOccluder> objects, UploadRing& ring, const SetAllocator& allocate,
                 const StageMarker& mark = {});
    // Inside a scene target pass of 2^samples_log2 samples: the multiply.
    void finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const SetAllocator& allocate);
    // Once a frame, outside a submission being recorded: hands back the occlusion targets after 600 frames in which prepare()
    // did not run (the occlusion switched off); the next prepare() makes them again. The images go through the context's
    // deferred destruction, behind the fence of any submission naming them.
    void trim_idle();

private:
    enum Pass : unsigned { kDistance, kOcclusion, kDenoise, kSsao, kPassCount };
    [[nodiscard]] VkShaderModule module(unsigned index) const;
    [[nodiscard]] VkPipeline apply_pipeline(VkRenderPass pass, unsigned samples_log2);
    // A full-screen pipeline in `pass`: the multiply (ApplyPS, multiplicative blend) or the multiplier (UpsamplePS, R).
    [[nodiscard]] VkPipeline graphics_pipeline(VkRenderPass pass, unsigned samples_log2, bool multiply) const;
    [[nodiscard]] bool ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, VkFormat format,
                              const char* name);
    [[nodiscard]] bool ensure_scale(unsigned width, unsigned height);
    [[nodiscard]] VkDescriptorSet set_of(const SetAllocator& allocate, VkImageView depth, VkImageLayout depth_layout,
                                         const Texture& sampled1, const Texture& sampled2, const Texture& storage,
                                         VkImageLayout sampled1_layout = VK_IMAGE_LAYOUT_GENERAL);
    void dispatch(VkCommandBuffer commands, Pass pass, VkDescriptorSet set, unsigned width, unsigned height) const;
    void draw(VkCommandBuffer commands, VkPipeline pipeline, VkDescriptorSet set, unsigned width, unsigned height) const;

    bool ready_ = false;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, kPassCount> compute_{};
    std::array<VkPipeline, 4> apply_{};
    std::array<VkRenderPass, 4> apply_pass_{};
    VkRenderPass upsample_pass_ = VK_NULL_HANDLE;  // the multiplier: its one R8 target, not loaded, stored
    VkPipeline upsample_ = VK_NULL_HANDLE;
    VkFormat occlusion_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;  // raw_ and denoised_ (initialize: R16G16 where allowed)
    std::shared_ptr<Texture> distance_;
    std::shared_ptr<Texture> raw_;
    std::shared_ptr<Texture> denoised_;
    std::shared_ptr<Texture> scale_;  // the multiplier per scene target pixel (R8, a colour target; its framebuffer with it)
    std::uint64_t clock_ = 0;  // trim_idle() calls
    std::uint64_t used_ = 0;   // the clock at the last prepare()
    ao::Constants constants_{};
    std::vector<float> object_values_;
    std::vector<std::uint32_t> object_tiles_;
    ao::ObjectBinScratch object_scratch_;
    VkDescriptorBufferInfo objects_{};
    VkDescriptorBufferInfo tiles_{};
};

}  // namespace galaxy::render::vk
