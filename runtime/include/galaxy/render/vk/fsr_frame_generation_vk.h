// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// AMD FSR 3 frame generation on Vulkan (fsr_frame_generation.h): AMD's frame-pacing swap chain and the functions that drive it. Its
// images are handed to its present shader-readable. It needs two queues of its own, for acquiring and presenting.

#include "galaxy/render/vk/temporal_upscaler_vk.h"

#include <volk.h>

#include <cstdint>
#include <memory>

namespace galaxy::render::vk {

// The swap chain calls the renderer makes: Vulkan's (through Streamline's proxies when it runs), or AMD's.
struct SwapchainCalls {
    PFN_vkDestroySwapchainKHR destroy = nullptr;
    PFN_vkGetSwapchainImagesKHR get_images = nullptr;
    PFN_vkAcquireNextImageKHR acquire = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
};

class FsrFrameGenerationVk {
public:
    FsrFrameGenerationVk();
    ~FsrFrameGenerationVk();

    // A pacing swap chain for `info`, taking the place of `swap_chain` (destroyed; may be null); `calls` then drive it.
    [[nodiscard]] static bool create_swap_chain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR& swap_chain, SwapchainCalls& calls) noexcept;

    [[nodiscard]] bool matches(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height) const noexcept;
    bool create(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height, VkFormat backbuffer_format) noexcept;
    void destroy(VkSwapchainKHR swap_chain) noexcept;

    // This frame's scene, after its upscale: depth and motion in their attachment layouts.
    bool prepare(VkCommandBuffer commands, const TemporalUpscaleInputs& in) noexcept;

    // At the present (fsr_frame_generation.h): the frame without its 2D layer shader-readable.
    void configure(VkSwapchainKHR swap_chain, bool enabled, const TemporalImage* hudless, unsigned left, unsigned top, unsigned width,
                   unsigned height) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace galaxy::render::vk
