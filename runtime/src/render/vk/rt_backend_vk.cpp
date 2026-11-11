// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Vulkan (rt_backend.h): VK_KHR_acceleration_structure in pages, VK_KHR_ray_query in compute shaders.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/rt/rt_shaders.h"
#include "galaxy/render/vk/vk_context.h"

#include "barriers_vk.h"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>

namespace galaxy::render::rt {

bool shaders_load(gpu::Device* device) {
    const RtShaderBytes probe = rt_shader(RtShader::Probe);
    if (device == nullptr || probe.size == 0u) {
        std::fprintf(stderr, "[rt] shaders: none in this build\n");
        return false;
    }
    // rt_probe.hlsl as compile_rt_shaders.py places its registers: the scene (t0) at binding 0, its output (u0) at 200,
    // its constants (b0) at 300, all of set 0.
    const VkDescriptorSetLayoutBinding bindings[3] = {
        {0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {200u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {300u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 3u;
    set_info.pBindings = bindings;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool ok = vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout) == VK_SUCCESS;
    if (ok) {
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1u;
        layout_info.pSetLayouts = &set_layout;
        ok = vkCreatePipelineLayout(device, &layout_info, nullptr, &layout) == VK_SUCCESS;
    }
    if (ok) {
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = probe.size;
        module_info.pCode = static_cast<const std::uint32_t*>(probe.data);
        ok = vkCreateShaderModule(device, &module_info, nullptr, &module) == VK_SUCCESS;
    }
    if (ok) {
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = layout;
        ok = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &info, nullptr, &pipeline) == VK_SUCCESS;
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }
    if (set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    }
    std::fprintf(stderr, ok ? "[rt] shaders: loaded (vulkan, %zu bytes)\n" : "[rt] shaders: the driver refused the measuring pipeline (%zu bytes)\n",
                 probe.size);
    return ok;
}

struct Backend::State {
    struct Page {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        std::uint64_t bytes = 0u;
        std::uint64_t address = 0u;
    };
    VkDevice device = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    std::vector<Page> pages;
    // Compacted sizes: a pool of queries per frame slot, reset at the slot's first ask of a frame.
    VkQueryPool queries[kFramesInFlight]{};
    unsigned frame_slot = 0u;
    bool asked = false;

    // Passes: one set layout for every pass (rt_bindings.hlsli as compile_rt_shaders.py places the registers: t at its
    // number, u at 200, b at 300), a pipeline a shader made at its first dispatch.
    // A frame slot's passes take their sets from that slot's pools, reset when the slot comes round; a frame that
    // fills them adds one.
    std::vector<VkDescriptorPool> pools[kFramesInFlight];
    std::size_t pool_at = 0u;
    [[nodiscard]] VkDescriptorSet allocate_set();
    std::vector<std::shared_ptr<vk::Texture>> images;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;  // made when the renderer's image set's layout is known (begin)
    VkSampler sampler = VK_NULL_HANDLE;        // s0
    VkDescriptorSet texture_set = VK_NULL_HANDLE;  // the renderer's textures, set 1
    VkDescriptorSetLayout images_layout = VK_NULL_HANDLE;
    VkPipeline pipelines[static_cast<unsigned>(RtShader::Count)]{};
    bool refused[static_cast<unsigned>(RtShader::Count)]{};
    // Counters: the GPU adds to `counters`; a frame slot's copy is read a frame later.
    VkBuffer counters = VK_NULL_HANDLE;
    VmaAllocation counters_allocation = nullptr;
    std::shared_ptr<vk::Buffer> counters_read[kFramesInFlight];
    bool counters_bound = false;
    // The timed stretch (time_mark): two timestamps a frame slot (none: no measure).
    VkQueryPool times = VK_NULL_HANDLE;
    bool timed[kFramesInFlight][kTimedStretches]{};
    // Buffers passes write and builds read.
    struct OwnBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        std::uint64_t bytes = 0u;
        std::uint64_t address = 0u;
        bool for_builds = false;
    };
    std::vector<OwnBuffer> buffers;
    // Opacity micromaps (5.3 G-2): the device builds them (the extension's feature and synchronization2).
    bool micromaps = false;

    [[nodiscard]] bool make_passes();
    [[nodiscard]] std::uint64_t own_address(const TriangleInput& input) const {
        return input.own_buffer < buffers.size() ? buffers[input.own_buffer].address : 0u;
    }
    [[nodiscard]] std::uint64_t micromap_indices(const TriangleInput& input) const {
        return input.micromap_buffer < buffers.size() ? buffers[input.micromap_buffer].address + input.micromap_indices : 0u;
    }
};

namespace {

constexpr std::uint32_t kMeshBinding = 1u + kPassBuffers + kPassTextures + kPassReads;
constexpr std::uint32_t kSamplerBinding = 100u;  // s0 (the shaders' samplers are shifted by 100)
constexpr std::uint32_t kImageBinding = 200u, kCountersBinding = 200u + kPassWrites, kWriteBufferBinding = kCountersBinding + 1u,
                        kConstantsBinding = 300u, kPassConstantsBinding = 301u;
constexpr VkDeviceSize kCountersBytes = VkDeviceSize{kCounters} * 4u;

}  // namespace

VkDescriptorSet Backend::State::allocate_set() {
    constexpr std::uint32_t kSets = 256u;
    std::vector<VkDescriptorPool>& slot_pools = pools[frame_slot];
    for (unsigned tries = 0; tries < 64u; ++tries, ++pool_at) {
        if (pool_at >= slot_pools.size()) {
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, kSets},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSets * (kPassBuffers + 2u + kPassMeshBuffers)},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSets * (kPassTextures + kPassReads)},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSets * kPassWrites},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets * 2u},
                {VK_DESCRIPTOR_TYPE_SAMPLER, kSets},
            };
            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.maxSets = kSets;
            info.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
            info.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            slot_pools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = slot_pools[pool_at];
        info.descriptorSetCount = 1u;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device, &info, &set) == VK_SUCCESS) {
            return set;
        }
    }
    return VK_NULL_HANDLE;
}

bool Backend::State::make_passes() {
    VkDescriptorSetLayoutBinding bindings[1u + kPassBuffers + kPassTextures + kPassReads + kPassWrites + 5u + kPassMeshBuffers]{};
    std::uint32_t count = 0u;
    bindings[count++] = {0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    for (std::uint32_t i = 0; i < kPassBuffers; ++i) {
        bindings[count++] = {1u + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    for (std::uint32_t i = 0; i < kPassTextures + kPassReads; ++i) {
        bindings[count++] = {1u + kPassBuffers + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    for (std::uint32_t i = 0; i < kPassWrites; ++i) {
        bindings[count++] = {kImageBinding + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    bindings[count++] = {kCountersBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[count++] = {kWriteBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[count++] = {kConstantsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[count++] = {kPassConstantsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    // s0: one sampler for the renderer's textures (linear, repeating), fixed in the layout.
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
        return false;
    }
    bindings[count++] = {kSamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, &sampler};
    for (std::uint32_t i = 0; i < kPassMeshBuffers; ++i) {  // t21 up: the mesh memory
        bindings[count++] = {kMeshBinding + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = count;
    set_info.pBindings = bindings;
    // A pass binds only the images it has (an image not made yet: the second layer's body before glass is seen, a
    // filter step's pictures it does not read): every binding partially bound, as Direct3D 12's null descriptors; the
    // shaders read those only under the constants that bind them. The device has it with bindless (the tracer needs it).
    VkDescriptorBindingFlags flags[std::size(bindings)]{};
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    if (vk::Context::get().bindless()) {
        for (std::uint32_t i = 0; i < count; ++i) {
            flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        }
        binding_flags.bindingCount = count;
        binding_flags.pBindingFlags = flags;
        set_info.pNext = &binding_flags;
    }
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout) != VK_SUCCESS) {
        return false;
    }
    // The pipeline layout waits for the renderer's image set's layout (begin's `descriptors`): the passes' set is
    // set 0, the renderer's textures set 1.
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = kCountersBytes;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(vk::Context::get().allocator(), &buffer_info, &alloc_info, &counters, &counters_allocation, nullptr) != VK_SUCCESS) {
        counters = VK_NULL_HANDLE;
        return false;
    }
    for (std::shared_ptr<vk::Buffer>& read : counters_read) {
        read = vk::Context::get().create_readback_buffer(kCountersBytes, "Nebula RT Counters Read");
        if (read == nullptr || read->mapped == nullptr) {
            return false;
        }
        std::memset(read->mapped, 0, kCountersBytes);
    }
    return true;
}

namespace {

// The device address of a place in one of the renderer's buffers (made with the address usage while ray tracing is on).
std::uint64_t device_address(VkDevice device, gpu::Address address) {
    const vk::Context::BufferOffset at = vk::Context::get().resolve(address);
    if (at.buffer == VK_NULL_HANDLE) {
        return 0u;
    }
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = at.buffer;
    return vkGetBufferDeviceAddress(device, &info) + at.offset;
}

// `own`: the address of the tracer's buffer the input names, or 0. With a micromap, `omm` and `usage` (the caller's,
// alive while the geometry is) carry it, its indices at `omm_indices`.
VkAccelerationStructureGeometryKHR geometry_of(VkDevice device, const TriangleInput& input, std::uint64_t own,
                                               VkAccelerationStructureTrianglesOpacityMicromapEXT& omm, VkMicromapUsageEXT& usage,
                                               std::uint64_t omm_indices) {
    VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // Opaque here; an instance whose material tests alpha says so itself.
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.triangles = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    g.geometry.triangles.vertexData.deviceAddress = own != 0u ? own + input.positions : device_address(device, input.positions);
    g.geometry.triangles.vertexStride = input.position_stride;
    g.geometry.triangles.maxVertex = input.vertex_count != 0u ? input.vertex_count - 1u : 0u;
    g.geometry.triangles.indexType = input.indices == 0u ? VK_INDEX_TYPE_NONE_KHR : input.index_bytes == 2u ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    if (input.indices != 0u) {
        g.geometry.triangles.indexData.deviceAddress = device_address(device, input.indices);
    }
    if (input.micromap != 0u) {
        // Its micromap decides where a triangle is opaque, transparent or asks (the geometry's opaque flag stands where an
        // instance turns micromaps off).
        usage = {input.triangles(), input.micromap_level, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT};
        omm = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT};
        omm.indexType = VK_INDEX_TYPE_UINT32;
        omm.indexBuffer.deviceAddress = omm_indices;
        omm.indexStride = 4u;
        omm.usageCountsCount = 1u;
        omm.pUsageCounts = &usage;
        omm.micromap = reinterpret_cast<VkMicromapEXT>(input.micromap);
        g.geometry.triangles.pNext = &omm;
    }
    return g;
}

VkMicromapBuildInfoEXT micromap_info(const VkMicromapUsageEXT& usage) {
    VkMicromapBuildInfoEXT info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
    info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    info.flags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    info.usageCountsCount = 1u;
    info.pUsageCounts = &usage;
    info.triangleArrayStride = 8u;
    return info;
}

VkBuildAccelerationStructureFlagsKHR flags_of(BuildKind kind) {
    return kind == BuildKind::Static
               ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR
               : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
}

}  // namespace

Backend::Backend() = default;

Backend::~Backend() { stop(); }

bool Backend::started() const noexcept { return state_ != nullptr; }

bool Backend::start(gpu::Device* device) {
    stop();
    const vk::Context& context = vk::Context::get();
    if (device == nullptr || !context.rt_caps().enabled) {
        return false;
    }
    VkPhysicalDeviceAccelerationStructurePropertiesKHR limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &limits;
    vkGetPhysicalDeviceProperties2(context.physical_device(), &properties);
    if (limits.minAccelerationStructureScratchOffsetAlignment > kRangeAlign) {
        std::fprintf(stderr, "[rt] the device aligns build scratch to %u bytes; ranges are aligned to %llu\n",
                     limits.minAccelerationStructureScratchOffsetAlignment, static_cast<unsigned long long>(kRangeAlign));
        return false;
    }
    auto state = new State{};
    state->device = device;
    for (VkQueryPool& pool : state->queries) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        info.queryCount = kSizeSlots;
        if (vkCreateQueryPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
            pool = VK_NULL_HANDLE;
            state_ = state;
            stop();
            return false;
        }
    }
    state_ = state;
    if (!state->make_passes()) {
        stop();
        return false;
    }
    state->micromaps = context.rt_caps().micromaps && context.pipeline_barrier2() != nullptr && vkCreateMicromapEXT != nullptr &&
                       vkCmdBuildMicromapsEXT != nullptr && vkGetMicromapBuildSizesEXT != nullptr;
    if (context.properties().limits.timestampComputeAndGraphics == VK_TRUE) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2u * kTimedStretches * kFramesInFlight;
        if (vkCreateQueryPool(state->device, &info, nullptr, &state->times) != VK_SUCCESS) {
            state->times = VK_NULL_HANDLE;
        }
    }
    return true;
}

void Backend::stop() noexcept {
    if (state_ == nullptr) {
        return;
    }
    if (state_->times != VK_NULL_HANDLE) {
        vkDestroyQueryPool(state_->device, state_->times, nullptr);
    }
    for (const VkQueryPool pool : state_->queries) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(state_->device, pool, nullptr);
        }
    }
    for (std::uint32_t page = 0; page < state_->pages.size(); ++page) {
        free_page(page);
    }
    for (const VkPipeline pipeline : state_->pipelines) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state_->device, pipeline, nullptr);
        }
    }
    if (state_->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(state_->device, state_->layout, nullptr);
    }
    if (state_->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(state_->device, state_->sampler, nullptr);
    }
    if (state_->set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(state_->device, state_->set_layout, nullptr);
    }
    if (state_->counters != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vk::Context::get().allocator(), state_->counters, state_->counters_allocation);
    }
    for (const std::vector<VkDescriptorPool>& slot_pools : state_->pools) {
        for (const VkDescriptorPool pool : slot_pools) {
            vkDestroyDescriptorPool(state_->device, pool, nullptr);
        }
    }
    for (std::uint32_t buffer = 0; buffer < state_->buffers.size(); ++buffer) {
        free_buffer(buffer);
    }
    delete state_;
    state_ = nullptr;
}

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind kind) const {
    VkAccelerationStructureTrianglesOpacityMicromapEXT omm{};
    VkMicromapUsageEXT usage{};
    const VkAccelerationStructureGeometryKHR geometry =
        geometry_of(state_->device, input, state_->own_address(input), omm, usage, state_->micromap_indices(input));
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.flags = flags_of(kind);
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1u;
    info.pGeometries = &geometry;
    const std::uint32_t triangles = input.triangles();
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(state_->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &triangles, &sizes);
    return {sizes.accelerationStructureSize, sizes.buildScratchSize, sizes.updateScratchSize};
}

std::uint32_t Backend::make_page(std::uint64_t bytes, bool scratch) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = bytes;
    buffer_info.usage = (scratch ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (!scratch && state_->micromaps) {
        buffer_info.usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;  // micromaps lie in the structures' pages
    }
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    State::Page page;
    if (vmaCreateBuffer(vk::Context::get().allocator(), &buffer_info, &alloc_info, &page.buffer, &page.allocation, nullptr) != VK_SUCCESS) {
        return ~0u;
    }
    vk::Context::get().set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(page.buffer),
                                scratch ? "Nebula RT Scratch" : "Nebula RT Structures");
    page.bytes = bytes;
    VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address.buffer = page.buffer;
    page.address = vkGetBufferDeviceAddress(state_->device, &address);
    for (std::uint32_t i = 0; i < state_->pages.size(); ++i) {
        if (state_->pages[i].buffer == VK_NULL_HANDLE) {
            state_->pages[i] = page;
            return i;
        }
    }
    state_->pages.push_back(page);
    return static_cast<std::uint32_t>(state_->pages.size() - 1u);
}

void Backend::free_page(std::uint32_t page) noexcept {
    if (state_ == nullptr || page >= state_->pages.size() || state_->pages[page].buffer == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyBuffer(vk::Context::get().allocator(), state_->pages[page].buffer, state_->pages[page].allocation);
    state_->pages[page] = State::Page{};
}

Structure Backend::make_structure(const Range& range, bool top_level) {
    VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    info.buffer = state_->pages[range.page].buffer;
    info.offset = range.offset;
    info.size = range.size;
    info.type = top_level ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    if (vkCreateAccelerationStructureKHR(state_->device, &info, nullptr, &handle) != VK_SUCCESS) {
        return {};
    }
    VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    address.accelerationStructure = handle;
    Structure out;
    out.range = range;
    out.handle = reinterpret_cast<std::uint64_t>(handle);
    out.name = vkGetAccelerationStructureDeviceAddressKHR(state_->device, &address);
    return out;
}

void Backend::free_structure(Structure& structure) noexcept {
    if (state_ != nullptr && structure.handle != 0u) {
        vkDestroyAccelerationStructureKHR(state_->device, reinterpret_cast<VkAccelerationStructureKHR>(structure.handle), nullptr);
    }
    structure = Structure{};
}

void Backend::frame(unsigned frame_slot) noexcept {
    state_->frame_slot = frame_slot % kFramesInFlight;
    for (const VkDescriptorPool pool : state_->pools[state_->frame_slot]) {
        vkResetDescriptorPool(state_->device, pool, 0u);
    }
    state_->pool_at = 0u;
    state_->counters_bound = false;  // the frame's first pass that counts zeroes them
}

void Backend::begin(void* commands, unsigned frame_slot, void* descriptors) noexcept {
    State& s = *state_;
    s.commands = static_cast<VkCommandBuffer>(commands);
    s.frame_slot = frame_slot % kFramesInFlight;
    s.asked = false;
    if (const auto* textures = static_cast<const TextureTable*>(descriptors); textures != nullptr && textures->layout != nullptr) {
        s.texture_set = static_cast<VkDescriptorSet>(textures->set);
        const auto images_layout = static_cast<VkDescriptorSetLayout>(textures->layout);
        if (s.layout == VK_NULL_HANDLE || s.images_layout != images_layout) {
            // The passes' pipeline layout over the two sets (anew should the renderer's layout ever change: the
            // pipelines made with the old one go with it).
            const VkDevice device = s.device;
            for (VkPipeline& pipeline : s.pipelines) {
                if (pipeline != VK_NULL_HANDLE) {
                    const VkPipeline old = pipeline;
                    vk::Context::get().defer([device, old] { vkDestroyPipeline(device, old, nullptr); });
                    pipeline = VK_NULL_HANDLE;
                }
            }
            if (s.layout != VK_NULL_HANDLE) {
                const VkPipelineLayout old = s.layout;
                vk::Context::get().defer([device, old] { vkDestroyPipelineLayout(device, old, nullptr); });
                s.layout = VK_NULL_HANDLE;
            }
            const VkDescriptorSetLayout sets[2] = {s.set_layout, images_layout};
            VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layout_info.setLayoutCount = 2u;
            layout_info.pSetLayouts = sets;
            if (vkCreatePipelineLayout(device, &layout_info, nullptr, &s.layout) != VK_SUCCESS) {
                s.layout = VK_NULL_HANDLE;
            }
            s.images_layout = images_layout;
        }
    }
}

void Backend::build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit) {
    VkAccelerationStructureTrianglesOpacityMicromapEXT omm{};
    VkMicromapUsageEXT usage{};
    const VkAccelerationStructureGeometryKHR geometry =
        geometry_of(state_->device, input, state_->own_address(input), omm, usage, state_->micromap_indices(input));
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.flags = flags_of(kind);
    info.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    const auto handle = reinterpret_cast<VkAccelerationStructureKHR>(to.handle);
    info.srcAccelerationStructure = refit ? handle : VK_NULL_HANDLE;
    info.dstAccelerationStructure = handle;
    info.geometryCount = 1u;
    info.pGeometries = &geometry;
    info.scratchData.deviceAddress = state_->pages[scratch.page].address + scratch.offset;
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = input.triangles();
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(state_->commands, 1u, &info, &ranges);
}

namespace {

VkAccelerationStructureGeometryKHR instances_of(std::uint64_t device_address_of_instances) {
    VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    g.geometry.instances.arrayOfPointers = VK_FALSE;
    g.geometry.instances.data.deviceAddress = device_address_of_instances;
    return g;
}

}  // namespace

BuildSizes Backend::scene_sizes(std::uint32_t instances) const {
    const VkAccelerationStructureGeometryKHR geometry = instances_of(0u);
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1u;
    info.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(state_->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &instances, &sizes);
    return {sizes.accelerationStructureSize, sizes.buildScratchSize, 0u};
}

void Backend::build_scene(gpu::Address instances, std::uint32_t count, const Structure& to, const Range& scratch) {
    const VkAccelerationStructureGeometryKHR geometry = instances_of(device_address(state_->device, instances));
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.dstAccelerationStructure = reinterpret_cast<VkAccelerationStructureKHR>(to.handle);
    info.geometryCount = 1u;
    info.pGeometries = &geometry;
    info.scratchData.deviceAddress = state_->pages[scratch.page].address + scratch.offset;
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(state_->commands, 1u, &info, &ranges);
}

void Backend::barrier() {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier(state_->commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &barrier, 0u,
                         nullptr, 0u, nullptr);
}

void Backend::ask_compacted_size(const Structure& structure, std::uint32_t slot) {
    const VkQueryPool pool = state_->queries[state_->frame_slot];
    if (!state_->asked) {
        vkCmdResetQueryPool(state_->commands, pool, 0u, kSizeSlots);
        state_->asked = true;
    }
    const auto handle = reinterpret_cast<VkAccelerationStructureKHR>(structure.handle);
    vkCmdWriteAccelerationStructuresPropertiesKHR(state_->commands, 1u, &handle, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                  pool, slot);
}

void Backend::end_size_asks() { state_->asked = false; }

std::uint64_t Backend::compacted_size(std::uint32_t slot, unsigned frame_slot) const {
    std::uint64_t bytes = 0u;
    const VkResult result = vkGetQueryPoolResults(state_->device, state_->queries[frame_slot % kFramesInFlight], slot, 1u, sizeof(bytes),
                                                  &bytes, sizeof(bytes), VK_QUERY_RESULT_64_BIT);
    return result == VK_SUCCESS ? bytes : 0u;
}

void Backend::compact(const Structure& from, const Structure& to) {
    VkCopyAccelerationStructureInfoKHR info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    info.src = reinterpret_cast<VkAccelerationStructureKHR>(from.handle);
    info.dst = reinterpret_cast<VkAccelerationStructureKHR>(to.handle);
    info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    vkCmdCopyAccelerationStructureKHR(state_->commands, &info);
}

namespace {

constexpr VkFormat format_of(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Rgba8: return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::Rgba16f: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case ImageFormat::Rg16f: return VK_FORMAT_R16G16_SFLOAT;
    case ImageFormat::R16f: return VK_FORMAT_R16_SFLOAT;
    case ImageFormat::R8: return VK_FORMAT_R8_UNORM;
    case ImageFormat::R32f: return VK_FORMAT_R32_SFLOAT;
    case ImageFormat::Rgb10a2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case ImageFormat::Rgba32ui: return VK_FORMAT_R32G32B32A32_UINT;
    case ImageFormat::Rgba32f: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ImageFormat::Rg16ui: return VK_FORMAT_R16G16_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

// A volume of `depth` slices, read and written by passes (Texture3D): its own image and a 3D view.
std::shared_ptr<vk::Texture> make_volume(std::uint32_t width, std::uint32_t height, std::uint32_t depth, VkFormat format) {
    vk::Context& context = vk::Context::get();
    auto texture = std::make_shared<vk::Texture>();
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_3D;
    image_info.format = format;
    image_info.extent = {width, height, depth};
    image_info.mipLevels = 1u;
    image_info.arrayLayers = 1u;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocationInfo allocated{};
    if (vmaCreateImage(context.allocator(), &image_info, &alloc_info, &texture->image, &texture->allocation, &allocated) != VK_SUCCESS) {
        texture->image = VK_NULL_HANDLE;
        texture->allocation = nullptr;
        return {};
    }
    texture->format = format;
    texture->usage = image_info.usage;
    texture->width = width;
    texture->height = height;
    texture->levels = 1u;
    texture->allocation_bytes = allocated.size;
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view_info.format = format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    if (vkCreateImageView(context.device(), &view_info, nullptr, &texture->view) != VK_SUCCESS) {
        texture->view = VK_NULL_HANDLE;
        return {};  // the texture's end lets the image go
    }
    return texture;
}

// What a pass wrote, before the next pass or a draw reads it.
void written(VkCommandBuffer commands) {
    vk::shader_written(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

}  // namespace

std::uint32_t Backend::make_image(ImageFormat format, std::uint32_t width, std::uint32_t height, const char* name, std::uint32_t depth) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(vk::Context::get().physical_device(), format_of(format), &properties);
    if (width == 0u || height == 0u || (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0u) {
        return kNoImage;
    }
    std::shared_ptr<vk::Texture> image =
        depth > 1u ? make_volume(width, height, depth, format_of(format))
                   : vk::Context::get().create_texture(width, height, 1u, format_of(format),
                                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                                       false, name);
    if (image == nullptr) {
        return kNoImage;
    }
    for (std::uint32_t i = 0; i < state_->images.size(); ++i) {
        if (state_->images[i] == nullptr) {
            state_->images[i] = std::move(image);
            return i;
        }
    }
    state_->images.push_back(std::move(image));
    return static_cast<std::uint32_t>(state_->images.size() - 1u);
}

void Backend::free_image(std::uint32_t image) noexcept {
    if (state_ != nullptr && image < state_->images.size()) {
        state_->images[image].reset();
    }
}

// An image of the tracer's stays in the general layout, which a copy reads too, once the pass's writes are done.
void* Backend::image_to_copy(std::uint32_t image) {
    if (image >= state_->images.size() || state_->images[image] == nullptr) {
        return nullptr;
    }
    VkMemoryBarrier written_out{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written_out.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    written_out.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(state_->commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 1u, &written_out, 0u,
                         nullptr, 0u, nullptr);
    return state_->images[image].get();
}

// The draws' programs sample it in the read-only layout (the texture array's); the next pass that writes it takes it
// back to the general one.
void* Backend::image_to_sample(std::uint32_t image) {
    if (image >= state_->images.size() || state_->images[image] == nullptr) {
        return nullptr;
    }
    vk::Texture& texture = *state_->images[image];
    if (texture.layout == VK_IMAGE_LAYOUT_GENERAL) {
        vk::image_barrier(state_->commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ComputeWrite, gpu::Use::ShaderRead,
                          gpu::Contents::Keep, 1u);
        texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return &texture;
}

void Backend::dispatch(RtShader shader, const PassBinds& binds, std::uint32_t groups_x, std::uint32_t groups_y) {
    State& s = *state_;
    const auto index = static_cast<unsigned>(shader);
    if (index >= static_cast<unsigned>(RtShader::Count) || s.refused[index] || groups_x == 0u || groups_y == 0u ||
        s.layout == VK_NULL_HANDLE) {
        return;
    }
    if (s.pipelines[index] == VK_NULL_HANDLE) {
        const RtShaderBytes code = rt_shader(shader);
        VkShaderModule module = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = code.size;
        module_info.pCode = static_cast<const std::uint32_t*>(code.data);
        bool made = code.size != 0u && vkCreateShaderModule(s.device, &module_info, nullptr, &module) == VK_SUCCESS;
        if (made) {
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = module;
            info.stage.pName = "main";
            info.layout = s.layout;
            made = vkCreateComputePipelines(s.device, VK_NULL_HANDLE, 1u, &info, nullptr, &s.pipelines[index]) == VK_SUCCESS;
            vkDestroyShaderModule(s.device, module, nullptr);
        }
        if (!made) {
            s.pipelines[index] = VK_NULL_HANDLE;
            s.refused[index] = true;
            std::fprintf(stderr, "[rt] pass %u: no pipeline; the pass is left out\n", index);
            return;
        }
    }
    const VkCommandBuffer commands = s.commands;
    const auto image_of = [&](std::uint32_t image) -> vk::Texture* {
        return image < s.images.size() ? s.images[image].get() : nullptr;
    };
    // A new image goes to the general layout, where it stays: read, written and copied from there.
    for (const std::uint32_t write : binds.writes) {
        vk::Texture* const image = image_of(write);
        if (image != nullptr && image->layout != VK_IMAGE_LAYOUT_GENERAL) {
            vk::image_barrier(commands, image->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ShaderRead, gpu::Use::ComputeWrite,
                              gpu::Contents::Discard, 1u);
            image->layout = VK_IMAGE_LAYOUT_GENERAL;
        }
    }
    // So does an image read before any pass wrote it (the filter's history on its first frame): a descriptor names
    // no image without a layout, whatever the pass makes of its contents.
    for (const std::uint32_t read : binds.reads) {
        vk::Texture* const image = image_of(read);
        if (image != nullptr && image->layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            vk::image_barrier(commands, image->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ShaderRead, gpu::Use::ComputeWrite,
                              gpu::Contents::Discard, 1u);
            image->layout = VK_IMAGE_LAYOUT_GENERAL;
        }
    }
    if (binds.counters && !s.counters_bound) {
        vkCmdFillBuffer(commands, s.counters, 0u, kCountersBytes, 0u);
        VkMemoryBarrier filled{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        filled.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        filled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &filled, 0u, nullptr,
                             0u, nullptr);
        s.counters_bound = true;
    }

    const VkDescriptorSet set = s.allocate_set();
    if (set == VK_NULL_HANDLE) {
        return;  // the device has no pool left for it: the pass is left out this frame
    }
    constexpr unsigned kMost = 5u + kPassBuffers + kPassTextures + kPassReads + kPassWrites + kPassMeshBuffers;
    VkWriteDescriptorSet writes[kMost]{};
    VkDescriptorBufferInfo buffers[kPassBuffers + 4u + kPassMeshBuffers]{};
    VkDescriptorImageInfo views[kPassTextures + kPassReads + kPassWrites]{};
    std::uint32_t write_count = 0u, buffer_count = 0u, view_count = 0u;
    const auto next = [&](std::uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
        VkWriteDescriptorSet& write = writes[write_count++];
        write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1u;
        write.descriptorType = type;
        return write;
    };
    const auto buffer_at = [&](const BufferView& view) -> const VkDescriptorBufferInfo* {
        const vk::Context::BufferOffset at = vk::Context::get().resolve(view.at);
        buffers[buffer_count] = {at.buffer, at.offset, view.bytes};
        return &buffers[buffer_count++];
    };
    const auto handle = reinterpret_cast<VkAccelerationStructureKHR>(binds.scene.handle);
    VkWriteDescriptorSetAccelerationStructureKHR scene{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    scene.accelerationStructureCount = 1u;
    scene.pAccelerationStructures = &handle;
    if (binds.scene.handle != 0u) {
        next(0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR).pNext = &scene;
    }
    for (std::uint32_t i = 0; i < kPassBuffers; ++i) {
        if (binds.buffers[i].at != 0u) {
            next(1u + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = buffer_at(binds.buffers[i]);
        }
    }
    for (std::uint32_t i = 0; i < kPassTextures; ++i) {
        if (binds.textures[i].texture != nullptr) {
            views[view_count] = {VK_NULL_HANDLE, static_cast<VkImageView>(binds.textures[i].texture),
                                 static_cast<VkImageLayout>(binds.textures[i].format)};
            next(1u + kPassBuffers + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE).pImageInfo = &views[view_count++];
        }
    }
    for (std::uint32_t i = 0; i < kPassReads; ++i) {
        if (const vk::Texture* image = image_of(binds.reads[i])) {
            // As it lies: the general layout, or read-only once the draws sampled it (image_to_sample).
            views[view_count] = {VK_NULL_HANDLE, image->view, image->layout};
            next(1u + kPassBuffers + kPassTextures + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE).pImageInfo = &views[view_count++];
        }
    }
    for (std::uint32_t i = 0; i < kPassWrites; ++i) {
        if (const vk::Texture* image = image_of(binds.writes[i])) {
            views[view_count] = {VK_NULL_HANDLE, image->view, VK_IMAGE_LAYOUT_GENERAL};
            next(kImageBinding + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &views[view_count++];
        }
    }
    if (binds.write_texture.texture != nullptr && image_of(binds.writes[kPassWrites - 1u]) == nullptr) {
        views[view_count] = {VK_NULL_HANDLE, static_cast<VkImageView>(binds.write_texture.texture), VK_IMAGE_LAYOUT_GENERAL};
        next(kImageBinding + kPassWrites - 1u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).pImageInfo = &views[view_count++];
    }
    if (binds.counters) {
        buffers[buffer_count] = {s.counters, 0u, kCountersBytes};
        next(kCountersBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = &buffers[buffer_count++];
    }
    if (binds.write_buffer < s.buffers.size() && s.buffers[binds.write_buffer].buffer != VK_NULL_HANDLE) {
        buffers[buffer_count] = {s.buffers[binds.write_buffer].buffer, 0u, s.buffers[binds.write_buffer].bytes};
        next(kWriteBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = &buffers[buffer_count++];
    }
    if (binds.constants.at != 0u) {
        next(kConstantsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = buffer_at(binds.constants);
    }
    if (binds.pass_constants.at != 0u) {
        next(kPassConstantsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = buffer_at(binds.pass_constants);
    }
    for (std::uint32_t i = 0; i < kPassMeshBuffers; ++i) {
        if (binds.mesh[i].at != 0u) {
            next(kMeshBinding + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER).pBufferInfo = buffer_at(binds.mesh[i]);
        }
    }
    vkUpdateDescriptorSets(s.device, write_count, writes, 0u, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipelines[index]);
    const VkDescriptorSet sets[2] = {set, s.texture_set};
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.layout, 0u, s.texture_set != VK_NULL_HANDLE ? 2u : 1u, sets, 0u, nullptr);
    vkCmdDispatch(commands, groups_x, groups_y, 1u);
    written(commands);
}

void Backend::end_counters() {
    State& s = *state_;
    if (!s.counters_bound) {
        return;
    }
    VkMemoryBarrier added{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    added.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    added.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(s.commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 1u, &added, 0u, nullptr, 0u,
                         nullptr);
    const VkBufferCopy all{0u, 0u, kCountersBytes};
    vkCmdCopyBuffer(s.commands, s.counters, s.counters_read[s.frame_slot]->buffer, 1u, &all);
}

void Backend::read_counters(unsigned frame_slot, std::uint32_t out[kCounters]) const {
    const vk::Buffer& read = *state_->counters_read[frame_slot % kFramesInFlight];
    vk::Context::get().invalidate(read);
    std::memcpy(out, read.mapped, kCountersBytes);
}

void Backend::time_mark(unsigned stretch, bool end) {
    State& s = *state_;
    if (s.times == VK_NULL_HANDLE || stretch >= kTimedStretches) {
        return;
    }
    const std::uint32_t at = (s.frame_slot * kTimedStretches + stretch) * 2u;
    if (!end) {
        vkCmdResetQueryPool(s.commands, s.times, at, 2u);
    }
    // Each mark when every command before it has finished.
    vkCmdWriteTimestamp(s.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s.times, at + (end ? 1u : 0u));
    if (end) {
        s.timed[s.frame_slot][stretch] = true;
    }
}

std::uint64_t Backend::timed_ticks(unsigned stretch, unsigned frame_slot) const {
    State& s = *state_;
    const unsigned slot = frame_slot % kFramesInFlight;
    if (s.times == VK_NULL_HANDLE || stretch >= kTimedStretches || !s.timed[slot][stretch]) {
        return 0u;
    }
    s.timed[slot][stretch] = false;
    std::uint64_t ticks[2]{};
    if (vkGetQueryPoolResults(s.device, s.times, (slot * kTimedStretches + stretch) * 2u, 2u, sizeof(ticks), ticks, sizeof(std::uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return 0u;
    }
    return ticks[1] > ticks[0] ? ticks[1] - ticks[0] : 0u;
}

std::uint32_t Backend::make_buffer(std::uint64_t bytes, const char* name) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (state_->micromaps) {
        buffer_info.usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;  // a micromap's input
    }
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    State::OwnBuffer made;
    // At a multiple of 256 bytes: a micromap build reads its input from there.
    if (vmaCreateBufferWithAlignment(vk::Context::get().allocator(), &buffer_info, &alloc_info, kRangeAlign, &made.buffer, &made.allocation,
                                     nullptr) != VK_SUCCESS) {
        return kNoBuffer;
    }
    if (name != nullptr) {
        vk::Context::get().set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(made.buffer), name);
    }
    made.bytes = bytes;
    VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address.buffer = made.buffer;
    made.address = vkGetBufferDeviceAddress(state_->device, &address);
    for (std::uint32_t i = 0; i < state_->buffers.size(); ++i) {
        if (state_->buffers[i].buffer == VK_NULL_HANDLE) {
            state_->buffers[i] = made;
            return i;
        }
    }
    state_->buffers.push_back(made);
    return static_cast<std::uint32_t>(state_->buffers.size() - 1u);
}

void Backend::free_buffer(std::uint32_t buffer) noexcept {
    if (state_ == nullptr || buffer >= state_->buffers.size() || state_->buffers[buffer].buffer == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyBuffer(vk::Context::get().allocator(), state_->buffers[buffer].buffer, state_->buffers[buffer].allocation);
    state_->buffers[buffer] = State::OwnBuffer{};
}

bool Backend::micromaps() const noexcept { return state_ != nullptr && state_->micromaps; }

BuildSizes Backend::micromap_sizes(const MicromapInput& input) const {
    const VkMicromapUsageEXT usage{input.triangles, input.level, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT};
    const VkMicromapBuildInfoEXT info = micromap_info(usage);
    VkMicromapBuildSizesInfoEXT sizes{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
    vkGetMicromapBuildSizesEXT(state_->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &sizes);
    return {sizes.micromapSize, sizes.buildScratchSize, 0u};
}

Structure Backend::make_micromap(const Range& range) {
    VkMicromapCreateInfoEXT info{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
    info.buffer = state_->pages[range.page].buffer;
    info.offset = range.offset;
    info.size = range.size;
    info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    VkMicromapEXT handle = VK_NULL_HANDLE;
    if (vkCreateMicromapEXT(state_->device, &info, nullptr, &handle) != VK_SUCCESS) {
        return {};
    }
    Structure out;
    out.range = range;
    out.handle = reinterpret_cast<std::uint64_t>(handle);
    out.name = out.handle;
    return out;
}

void Backend::free_micromap(Structure& micromap) noexcept {
    if (state_ != nullptr && micromap.handle != 0u) {
        vkDestroyMicromapEXT(state_->device, reinterpret_cast<VkMicromapEXT>(micromap.handle), nullptr);
    }
    micromap = Structure{};
}

void Backend::build_micromap(const MicromapInput& input, const Structure& to, const Range& scratch) {
    State& s = *state_;
    const PFN_vkCmdPipelineBarrier2 barrier2 = vk::Context::get().pipeline_barrier2();
    if (barrier2 == nullptr || input.buffer >= s.buffers.size()) {
        return;
    }
    const std::uint64_t at = s.buffers[input.buffer].address;
    // The pass's writes before this build and the structure's build read them (the indices), and the scratch's last
    // use by a build before this one writes it.
    VkMemoryBarrier2 before[2] = {{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}, {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}};
    before[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    before[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    before[0].dstStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    before[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    before[1].srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    before[1].srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    before[1].dstStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    before[1].dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 2u;
    dependency.pMemoryBarriers = before;
    barrier2(s.commands, &dependency);
    const VkMicromapUsageEXT usage{input.triangles, input.level, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT};
    VkMicromapBuildInfoEXT info = micromap_info(usage);
    info.dstMicromap = reinterpret_cast<VkMicromapEXT>(to.handle);
    info.data.deviceAddress = at;
    info.scratchData.deviceAddress = s.pages[scratch.page].address + scratch.offset;
    info.triangleArray.deviceAddress = at + input.triangles_at;
    vkCmdBuildMicromapsEXT(s.commands, 1u, &info);
    // The micromap whole for the structure built over it next, and the scratch free for that build.
    VkMemoryBarrier2 after{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    after.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    after.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    after.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    after.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT |
                          VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    dependency.memoryBarrierCount = 1u;
    dependency.pMemoryBarriers = &after;
    barrier2(s.commands, &dependency);
}

// What the passes wrote before a build reads it, and a build's reads before the passes write again.
void Backend::buffer_for(std::uint32_t buffer, bool builds) {
    if (buffer >= state_->buffers.size() || state_->buffers[buffer].buffer == VK_NULL_HANDLE || state_->buffers[buffer].for_builds == builds) {
        return;
    }
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = builds ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = builds ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(state_->commands,
                         builds ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         builds ? VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u,
                         &barrier, 0u, nullptr, 0u, nullptr);
    state_->buffers[buffer].for_builds = builds;
}

// A storage buffer's view starts at a multiple of the device's alignment (256 bytes covers every device).
BufferView Backend::view_of(gpu::Address at, std::uint64_t bytes, std::uint32_t& offset) const {
    const vk::Context::BufferOffset place = vk::Context::get().resolve(at);
    offset = static_cast<std::uint32_t>(place.offset % kRangeAlign);
    return {at - offset, bytes + offset};
}

}  // namespace galaxy::render::rt
