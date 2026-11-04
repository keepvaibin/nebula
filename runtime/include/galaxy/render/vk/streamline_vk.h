// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// NVIDIA Streamline on Vulkan: DLSS super resolution, from the vendor folder the player installs (streamline_bridge.h).
// Manual hooking without its create proxies: the instance and device are made here with what it reports it needs.

#include "galaxy/render/vk/temporal_upscaler_vk.h"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::render::vk::streamline_vk {

struct Needs {
    std::vector<const char*> instance_extensions;
    std::vector<const char*> device_extensions;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    std::uint32_t graphics_queues = 0u;
    std::uint32_t compute_queues = 0u;
};

// Before the instance: loads and initializes it on a machine with an RTX adapter and its files. False otherwise; the
// reason is logged.
[[nodiscard]] bool start() noexcept;
[[nodiscard]] const Needs& needs() noexcept;
// The device can give what it needs (a device with Vulkan 1.2).
[[nodiscard]] bool device_meets_needs(VkPhysicalDevice physical) noexcept;
// Right after the device: hands it over. True when DLSS runs on it.
bool attach(VkInstance instance, VkPhysicalDevice physical, VkDevice device, std::uint32_t queue_family,
            std::uint32_t first_extra_queue) noexcept;
void shutdown() noexcept;

// quality: 0 DLAA, 1 quality, 2 balanced, 3 performance, 4 ultra performance. Null when DLSS is not available.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                                     unsigned output_height, unsigned quality);

}  // namespace galaxy::render::vk::streamline_vk
