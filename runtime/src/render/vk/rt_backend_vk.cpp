// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Vulkan (rt_backend.h): VK_KHR_acceleration_structure in pages, VK_KHR_ray_query in compute shaders.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/rt/rt_shaders.h"
#include "galaxy/render/vk/vk_context.h"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstdio>

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
};

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

VkAccelerationStructureGeometryKHR geometry_of(VkDevice device, const TriangleInput& input) {
    VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // Opaque here; an instance whose material tests alpha says so itself.
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    g.geometry.triangles = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    g.geometry.triangles.vertexData.deviceAddress = device_address(device, input.positions);
    g.geometry.triangles.vertexStride = input.position_stride;
    g.geometry.triangles.maxVertex = input.vertex_count != 0u ? input.vertex_count - 1u : 0u;
    g.geometry.triangles.indexType = input.indices == 0u ? VK_INDEX_TYPE_NONE_KHR : input.index_bytes == 2u ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    if (input.indices != 0u) {
        g.geometry.triangles.indexData.deviceAddress = device_address(device, input.indices);
    }
    return g;
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
    return true;
}

void Backend::stop() noexcept {
    if (state_ == nullptr) {
        return;
    }
    for (const VkQueryPool pool : state_->queries) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(state_->device, pool, nullptr);
        }
    }
    for (std::uint32_t page = 0; page < state_->pages.size(); ++page) {
        free_page(page);
    }
    delete state_;
    state_ = nullptr;
}

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind kind) const {
    const VkAccelerationStructureGeometryKHR geometry = geometry_of(state_->device, input);
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

void Backend::begin(void* commands, unsigned frame_slot) noexcept {
    state_->commands = static_cast<VkCommandBuffer>(commands);
    state_->frame_slot = frame_slot % kFramesInFlight;
    state_->asked = false;
}

void Backend::build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit) {
    const VkAccelerationStructureGeometryKHR geometry = geometry_of(state_->device, input);
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

}  // namespace galaxy::render::rt
