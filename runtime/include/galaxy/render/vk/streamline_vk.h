// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// NVIDIA Streamline on Vulkan: DLSS super resolution and Frame Generation with Reflex, from the vendor folder the player
// installs (streamline_bridge.h). Manual hooking without its create proxies: the instance and device are made here with what
// it reports it needs, and the swap chain's calls go through its proxies once it has the device.

#include "galaxy/render/render_config.h"
#include "galaxy/render/vk/temporal_upscaler_vk.h"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::render::vk::streamline_vk {

#if defined(_WIN32)

struct Needs {
    std::vector<const char*> instance_extensions;
    std::vector<const char*> device_extensions;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    std::uint32_t graphics_queues = 0u;
    std::uint32_t compute_queues = 0u;
    // Frame generation's on top of those, when its files are here (a Vulkan 1.3 device: its features).
    bool frame_generation = false;
    std::vector<const char*> generation_extensions;
    std::uint32_t generation_graphics_queues = 0u;
    std::uint32_t generation_compute_queues = 0u;
};

// Before the instance: loads and initializes it on a machine with an RTX adapter and its files. False otherwise; the
// reason is logged.
[[nodiscard]] bool start() noexcept;
[[nodiscard]] const Needs& needs() noexcept;
// The device can give what DLSS needs (a device with Vulkan 1.2).
[[nodiscard]] bool device_meets_needs(VkPhysicalDevice physical) noexcept;
// The device can give what frame generation adds. Its optical flow queue family, `optical_flow_family`, when it has
// one: frame generation's own (the first queue of the first family that does optical flow alone).
[[nodiscard]] bool device_meets_generation_needs(VkPhysicalDevice physical, std::uint32_t& optical_flow_family) noexcept;
// Right after the device: hands it over, and the swap chain's calls go through Streamline from here on (it reads each frame's
// present; VK_NULL_HANDLE family for no native optical flow). True when DLSS runs on it.
bool attach(VkInstance instance, VkPhysicalDevice physical, VkDevice device, std::uint32_t queue_family,
            std::uint32_t first_extra_queue, bool frame_generation, std::uint32_t optical_flow_family) noexcept;
// The window's surface goes through Streamline as well, which finds the window from it: the proxies go in here, with the
// instance Streamline has. The caller makes its surface again after this.
void hook_surface_calls(VkInstance instance) noexcept;
void shutdown() noexcept;

// quality: 0 DLAA, 1 quality, 2 balanced, 3 performance, 4 ultra performance. Null when DLSS is not available.
// With `ray_reconstruction` (streamline::dlss_rr_supported, the tracer running): DLSS Ray Reconstruction, the tracer's outside
// denoiser too (rt_denoiser.h), in the same modes.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                                     unsigned output_height, unsigned quality,
                                                                     bool ray_reconstruction = false);

// The presented frame, as RendererD3D12 does around its Present.
// At the first begin_frame after a present: whether frame generation runs this frame (with DLSS, on a GPU that runs it, while chosen,
// or AMD's), then Reflex's sleep and the first marker.
void open_frame(bool amd_generation) noexcept;
void mark_submit_start() noexcept;
void mark_submit_end() noexcept;
void mark_present_start() noexcept;
void mark_present_end() noexcept;
// Whether the swap chain may vsync while frame generation runs on this build.
[[nodiscard]] bool generation_vsync() noexcept;
// Right before the swap chain changes or goes (Streamline's guide).
void generation_off() noexcept;
// What it generates this frame (at the present).
void set_generation(unsigned generated, bool dynamic, float target_fps) noexcept;
// This frame's scene depth and motion vectors at the render size, in `depth_layout` and `motion_layout` on `commands`
// (Streamline copies them there).
void tag_generation_inputs(VkCommandBuffer commands, const TemporalImage& depth, const TemporalImage& motion, unsigned render_width,
                           unsigned render_height, VkImageLayout depth_layout, VkImageLayout motion_layout) noexcept;
void tag_backbuffer_region(VkCommandBuffer commands, unsigned left, unsigned top, unsigned width, unsigned height) noexcept;
// The frame without its 2D layer at the swap chain's size, shader-readable until the present; null image: none this frame.
void tag_hudless(VkCommandBuffer commands, const TemporalImage* picture) noexcept;

#else

// No Streamline here: frame generation runs while AMD's swap chain does.
inline void open_frame(bool amd_generation) noexcept { set_frame_generation_running(amd_generation); }
inline void mark_submit_start() noexcept {}
inline void mark_submit_end() noexcept {}
inline void mark_present_start() noexcept {}
inline void mark_present_end() noexcept {}
[[nodiscard]] inline bool generation_vsync() noexcept { return true; }
inline void generation_off() noexcept {}
inline void set_generation(unsigned, bool, float) noexcept {}
inline void tag_generation_inputs(VkCommandBuffer, const TemporalImage&, const TemporalImage&, unsigned, unsigned, VkImageLayout, VkImageLayout) noexcept {}
inline void tag_backbuffer_region(VkCommandBuffer, unsigned, unsigned, unsigned, unsigned) noexcept {}
inline void tag_hudless(VkCommandBuffer, const TemporalImage*) noexcept {}

#endif

}  // namespace galaxy::render::vk::streamline_vk
