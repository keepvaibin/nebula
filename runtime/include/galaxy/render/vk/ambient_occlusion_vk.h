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

    AmbientOcclusionVk() = default;
    AmbientOcclusionVk(const AmbientOcclusionVk&) = delete;
    AmbientOcclusionVk& operator=(const AmbientOcclusionVk&) = delete;
    ~AmbientOcclusionVk() { shutdown(); }

    // The compute pipelines; the multiply's are made per scene target pass at first use. False leaves the pass off.
    bool initialize();
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Outside a render pass, the scene's single-sample depth readable as `depth_layout`: the colour multiplier of each
    // scene target pixel. False: nothing to multiply.
    bool prepare(VkCommandBuffer commands, const Texture& depth, VkImageLayout depth_layout, const RtView& view,
                 bool ssao, std::span<const ao::ObjectOccluder> objects, UploadRing& ring, const SetAllocator& allocate);
    // Inside a scene target pass of 2^samples_log2 samples: the multiply.
    void finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const SetAllocator& allocate);
    // What the frame's commands read stays alive with its submission (recorded frames that replay).
    void hold(std::vector<std::shared_ptr<Texture>>& held) const;

private:
    enum Pass : unsigned { kDistance, kOcclusion, kDenoise, kUpsample, kSsao, kPassCount };
    [[nodiscard]] VkShaderModule module(unsigned index) const;
    [[nodiscard]] VkPipeline apply_pipeline(VkRenderPass pass, unsigned samples_log2);
    [[nodiscard]] bool ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, VkFormat format,
                              const char* name);
    [[nodiscard]] VkDescriptorSet set_of(const SetAllocator& allocate, VkImageView depth, VkImageLayout depth_layout,
                                         const Texture& sampled1, const Texture& sampled2, const Texture& storage);
    void dispatch(VkCommandBuffer commands, Pass pass, VkDescriptorSet set, unsigned width, unsigned height) const;

    bool ready_ = false;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, kPassCount> compute_{};
    std::array<VkPipeline, 4> apply_{};
    std::array<VkRenderPass, 4> apply_pass_{};
    std::shared_ptr<Texture> distance_;
    std::shared_ptr<Texture> raw_;
    std::shared_ptr<Texture> denoised_;
    std::shared_ptr<Texture> scale_;
    ao::Constants constants_{};
    std::vector<float> object_values_;
    std::vector<std::uint32_t> object_tiles_;
    VkDescriptorBufferInfo objects_{};
    VkDescriptorBufferInfo tiles_{};
};

}  // namespace galaxy::render::vk
