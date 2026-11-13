// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Temporal upscaling of the Vulkan scene (AMD FidelityFX FSR 3.1, Intel XeSS, Qualcomm Snapdragon Game Super Resolution 2).

#include <volk.h>

#include "galaxy/render/rt/rt_denoiser.h"

#include <array>
#include <cstdint>
#include <memory>

namespace galaxy::render::vk {

struct TemporalImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;  // sampled (a depth image's: its depth aspect), the output's: storage
    VkImageAspectFlags barrier_aspects = VK_IMAGE_ASPECT_COLOR_BIT;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    VkImageUsageFlags usage = 0u;
};

// Scene inputs for one temporal upscale, recorded outside a render pass. Every image is in its steady attachment layout
// and is left in it. Sizes are the regions used, from each image's origin.
struct TemporalUpscaleInputs {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    TemporalImage color;     // RGBA8, display values, jittered
    TemporalImage depth;     // reversed (near = 1)
    TemporalImage motion;    // RG16F, render pixels, current -> previous
    TemporalImage reactive;  // R8, blended draws' alpha
    TemporalImage output;    // RGBA8 with storage
    unsigned render_width = 0u;
    unsigned render_height = 0u;
    unsigned output_width = 0u;
    unsigned output_height = 0u;
    float jitter_x = 0.0f;  // render pixels, the offset applied to the projection
    float jitter_y = 0.0f;
    float camera_near = 1.0f;
    float camera_far = 100000.0f;
    float fov_y = 1.0f;  // radians
    float frame_ms = 0.0f;
    bool reset = false;
    // The scene camera (world to eye, rows 3x4; the eye looks down -z), when known.
    std::array<float, 12> view{};
    bool view_known = false;
};

class TemporalUpscaler {
public:
    virtual ~TemporalUpscaler() = default;
    // False when the dispatch could not be recorded (the caller falls back to a resample for this frame).
    virtual bool evaluate(const TemporalUpscaleInputs& inputs) = 0;
    [[nodiscard]] virtual unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const = 0;
    // Sub-pixel offset (render pixels, -0.5..0.5) for frame `index`.
    virtual void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
    // The tracer's outside denoiser, when this upscaler is one (rt_denoiser.h): null for the others.
    [[nodiscard]] virtual rt::Denoiser* denoiser() noexcept { return nullptr; }
};

// Every input to its compute read state and the output to general (back: to their steady layouts again).
void temporal_barriers(const TemporalUpscaleInputs& in, bool back);

// Intel XeSS Super Resolution (the library from xess_library.h); quality: the temporal_quality row. Null without the library
// or when the device lacks what it asked for (Context::create).
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_xess_upscaler(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                                                                     unsigned output_width, unsigned output_height, unsigned quality);

// Snapdragon Game Super Resolution 2 (sgsr2_vk.cpp): two fragment passes on any Vulkan device. `output_format`: the
// output image's, which the history shares. Null when a pipeline or image cannot be made.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_sgsr2_upscaler(unsigned render_width, unsigned render_height,
                                                                      unsigned output_width, unsigned output_height,
                                                                      VkFormat output_format);

// Whether this build holds it (GALAXY_WITH_FSR3_VK).
[[nodiscard]] bool fsr3_compiled() noexcept;
// Null without it or when the context cannot be made. Sized for exactly these sizes.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(VkDevice device, VkPhysicalDevice physical,
                                                                     unsigned render_width, unsigned render_height,
                                                                     unsigned output_width, unsigned output_height);

}  // namespace galaxy::render::vk
