#pragma once

#include "galaxy/render/vk/vk_context.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace galaxy::render::vk {

// The wide 3D scene's camera (scene_camera.h, scene_camera.hlsl) on Vulkan. meter runs outside a render pass, widen and
// draw inside the scene target pass they name.
class SceneCameraVk {
public:
    using SetAllocator = std::function<VkDescriptorSet(VkDescriptorSetLayout)>;

    SceneCameraVk() = default;
    SceneCameraVk(const SceneCameraVk&) = delete;
    SceneCameraVk& operator=(const SceneCameraVk&) = delete;
    ~SceneCameraVk() { shutdown(); }

    bool initialize();
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // Each works in the top-left `width` x `height` of its targets (the scene's drawn part: dynamic resolution's
    // rectangle, or all of them).
    // `picture` is the scene target's colour, sampled; every sample of the wide target takes it.
    void widen(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const Texture& picture, unsigned width,
               unsigned height, const SetAllocator& allocate);
    // `wide` is the resolved wide colour, sampled. Below zero `seconds` starts the eye open. `finish`: the traced
    // relight's (scene_camera.hlsl g_finish), in the meter and the camera alike.
    void meter(VkCommandBuffer commands, const Texture& wide, const Texture& depth, VkImageLayout depth_layout,
               unsigned width, unsigned height, float seconds, unsigned finish, const SetAllocator& allocate);
    // `wide` is the wide colour, sampled: the multisampled target when `per_sample`, else the resolved one.
    void draw(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const Texture& wide, unsigned width,
              unsigned height, bool per_sample, const SetAllocator& allocate, bool resolve = false, unsigned finish = 0u);

private:
    enum Kind : unsigned { kWiden, kCamera, kCameraSamples, kCameraResolve, kKinds };
    [[nodiscard]] VkPipeline pipeline(Kind kind, VkRenderPass pass, unsigned samples_log2);
    [[nodiscard]] VkDescriptorSet set_of(const SetAllocator& allocate, const Texture& sampled, const Texture* depth = nullptr,
                                         VkImageLayout depth_layout = VK_IMAGE_LAYOUT_GENERAL) const;
    void full_screen(VkCommandBuffer commands, VkPipeline pipeline, VkDescriptorSet set, unsigned width,
                     unsigned height, unsigned finish = 0u) const;

    std::shared_ptr<Texture> exposure_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline meter_ = VK_NULL_HANDLE;
    std::array<std::array<VkPipeline, 8>, kKinds> pipelines_{};
    bool exposure_ready_ = false;
    bool ready_ = false;
};

}  // namespace galaxy::render::vk
