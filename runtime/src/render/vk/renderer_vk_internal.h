// SPDX-License-Identifier: GPL-3.0-only
// RendererVk's helpers shared by its sources (renderer_vk.cpp, renderer_vk_frame.cpp).
#pragma once

#include <volk.h>
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/vk/shader_compile_vk.h"
#include "barriers_vk.h"

#include <cstdint>
#include <vector>

namespace galaxy::render::vk_detail {

inline constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
inline constexpr VkImageLayout kDepthSampleLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

// Descriptor capacity of one frame: a texture table per distinct set of maps (only without descriptor indexing), plus the
// renderer's own sets (the present's, a resolution change's carry), whose immutable samplers count against the pool's
// samplers.
inline constexpr std::uint32_t kFrameSets = 4096u;
inline constexpr std::uint32_t kFrameUtilitySamplers = 256u;
// Sampler slots a frame may add: the sampler array starts again between frames when fewer than this are free.
inline constexpr unsigned kSamplerFrameRoom = 512u;

using vk::image_barrier;

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
// The storage images a frame's renderer passes take: the game's bloom (GameBloomVk).
inline constexpr std::uint32_t kPassStorageImages = 64u;
// The storage buffers they take: the ambient occlusion's objects and tiles, per set of its passes.
inline constexpr std::uint32_t kPassStorageBuffers = 128u;
// Native constants sets (set 0: b0, b1, b2, composed rows, instance table and the frame's texture lists): room for
// `sets` of them.
constexpr PoolSizes constants_sets(std::uint32_t sets, PoolSizes others = {}) {
    others.dynamic_uniforms += 3u * sets;
    others.storage += 3u * sets;
    return others;
}
// A frame's descriptor pool: its constants sets, the renderer's own passes' sets, and without descriptor indexing a bound
// table of kBoundTableSlots maps for each set.
constexpr PoolSizes frame_pool(bool bound_tables) {
    return constants_sets(kFrameSets, {.images = (bound_tables ? kFrameSets * vk::kBoundTableSlots : 0u) + 64u,
                                       .samplers = kFrameUtilitySamplers, .storage = kPassStorageBuffers,
                                       .storage_images = kPassStorageImages});
}
// The persistent pool (and each overflow pool after it): samplers, the corner set and cached constants sets.
inline constexpr std::uint32_t kPersistentSets = 1024u;
inline constexpr PoolSizes kPersistentPool = constants_sets(64u, {.images = kPersistentSets * vk::kBoundTableSlots, .samplers = kPersistentSets * vk::kBoundTableSlots});

VkDescriptorPool create_pool(VkDevice device, std::uint32_t sets, const PoolSizes& sizes);

}  // namespace galaxy::render::vk_detail
