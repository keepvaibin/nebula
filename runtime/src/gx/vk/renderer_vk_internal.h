// SPDX-License-Identifier: GPL-3.0-only
// RendererVk's helpers shared by its sources (renderer_vk.cpp, renderer_vk_efb.cpp, renderer_vk_replay.cpp).
#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace galaxy::gx::vk_detail {

inline constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
inline constexpr VkImageLayout kDepthSampleLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

// Descriptor capacity of one frame: a texture table per distinct set of maps, plus the renderer's own sets (the
// present's, a resolution change's carry), whose immutable samplers count against the pool's samplers.
inline constexpr std::uint32_t kFrameSets = 4096u;
inline constexpr std::uint32_t kFrameUtilitySamplers = 256u;

// Vertex blending's verdicts (one per draw pair; the last for stripes), then the presented frame's values (t, the t
// of the frame before, jitter) a sub-frame's draws read, then a zero element the game frames' draws read.
inline constexpr std::uint32_t kMaxBlendPairs = 8192u;
inline constexpr std::uint32_t kStripeVerdict = kMaxBlendPairs - 1u;
inline constexpr std::uint32_t kFrameValuesElement = kMaxBlendPairs;
inline constexpr std::uint32_t kZeroValuesElement = kMaxBlendPairs + 1u;

inline void image_barrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                          VkImageLayout to, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                          VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    vkCmdPipelineBarrier(commands, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

inline VkImageAspectFlags depth_aspects(VkFormat format) {
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT
        ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
        : VK_IMAGE_ASPECT_DEPTH_BIT;
}

// The descriptors a pool holds, by type. Drivers may refuse a set beyond them (AMD does; NVIDIA allocates anyway).
struct PoolSizes {
    std::uint32_t images = 0;
    std::uint32_t samplers = 0;
    std::uint32_t dynamic_uniforms = 0;
    std::uint32_t dynamic_storage = 0;
    std::uint32_t storage = 0;
    std::uint32_t storage_images = 0;
};
// The storage images a frame's (or a replay list's) renderer passes take: the game's bloom (GameBloomVk).
inline constexpr std::uint32_t kPassStorageImages = 64u;
// GX constants sets (set 0: b0 and b1, the matrix palette, vertex blending's earlier vertices and verdicts): room for
// `sets` of them.
constexpr PoolSizes constants_sets(std::uint32_t sets, PoolSizes others = {}) {
    others.dynamic_uniforms += 2u * sets;
    others.dynamic_storage += sets;
    others.storage += 2u * sets;
    return others;
}
// The persistent pool (and each overflow pool after it): samplers, the corner set and cached constants sets.
inline constexpr std::uint32_t kPersistentSets = 1024u;
inline constexpr PoolSizes kPersistentPool = constants_sets(64u, {.images = 64u, .samplers = 1024u * 8u});

VkDescriptorPool create_pool(VkDevice device, std::uint32_t sets, const PoolSizes& sizes);

}  // namespace galaxy::gx::vk_detail
