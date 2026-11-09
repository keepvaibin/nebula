// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/pipelines.h"

#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/vk/renderer_vk.h"
#include "galaxy/render/vk/shader_compile_vk.h"
#include "galaxy/render/vk/vk_context.h"

#include <nebula_model.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {
namespace {

// The vertex inputs' locations (shader_compile_vk.cpp semantic_location): the fixed attributes, then the coordinates,
// as many as a program names.
enum Location : std::uint32_t {
    kPosition = 0u,
    kNormal = 1u,
    kTangent = 2u,
    kBinormal = 3u,
    kColor0 = 4u,
    kColor1 = 5u,
    kMatrix = 6u,
    kBefore = 7u,
    kWiden = 8u,
    kCoord0 = 9u,
};
constexpr std::uint32_t kNoLocation = ~0u;

VkFormat location_format(std::uint32_t location) noexcept {
    switch (location) {
    case kColor0:
    case kColor1: return VK_FORMAT_R8G8B8A8_UNORM;
    case kMatrix:
    case kWiden: return VK_FORMAT_R32_UINT;
    case kPosition:
    case kNormal:
    case kTangent:
    case kBinormal:
    case kBefore: return VK_FORMAT_R32G32B32_SFLOAT;
    default: return VK_FORMAT_R32G32_SFLOAT;
    }
}

VkFormat input_format(std::uint32_t format) noexcept {
    switch (format) {
    case 0: return VK_FORMAT_R32G32B32_SFLOAT;
    case 1: return VK_FORMAT_R32G32_SFLOAT;
    case 2: return VK_FORMAT_R8G8B8A8_UNORM;
    default: return VK_FORMAT_R32_UINT;
    }
}

std::uint32_t input_location(std::uint32_t semantic) noexcept {
    switch (semantic) {
    case NEBULA_INPUT_NORMAL: return kNormal;
    case NEBULA_INPUT_COLOR0: return kColor0;
    case NEBULA_INPUT_COLOR1: return kColor1;
    case NEBULA_INPUT_TANGENT: return kTangent;
    case NEBULA_INPUT_BINORMAL: return kBinormal;
    case NEBULA_INPUT_WIDEN: return kWiden;
    case NEBULA_INPUT_MATRIX: return kMatrix;
    default: return semantic >= NEBULA_INPUT_COORD0 ? kCoord0 + (semantic - NEBULA_INPUT_COORD0) : kNoLocation;
    }
}

// The locations of a program's stage inputs, one flag for each up to the highest.
std::vector<bool> input_locations(const ProgramBytes& program) {
    constexpr std::uint32_t kOpVariable = 59u, kOpDecorate = 71u, kDecorationLocation = 30u, kStorageInput = 1u;
    const auto* spirv = static_cast<const std::uint32_t*>(program.data);
    const std::size_t words = program.size / 4u;
    std::unordered_map<std::uint32_t, std::uint32_t> locations;
    std::vector<std::uint32_t> inputs;
    for (std::size_t at = 5u; at < words && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        const std::uint32_t op = spirv[at] & 0xFFFFu;
        if (op == kOpDecorate && spirv[at + 2u] == kDecorationLocation) {
            locations[spirv[at + 1u]] = spirv[at + 3u];
        } else if (op == kOpVariable && spirv[at + 3u] == kStorageInput) {
            inputs.push_back(spirv[at + 2u]);
        }
    }
    std::vector<bool> reads;
    for (const std::uint32_t input : inputs) {
        if (const auto it = locations.find(input); it != locations.end()) {
            if (it->second >= reads.size()) {
                reads.resize(std::size_t{it->second} + 1u, false);
            }
            reads[it->second] = true;
        }
    }
    return reads;
}

VkBlendFactor blend_factor(std::uint32_t f) noexcept {
    switch (f) {
    case NEBULA_FACTOR_ZERO: return VK_BLEND_FACTOR_ZERO;
    case NEBULA_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case NEBULA_FACTOR_INV_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case NEBULA_FACTOR_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case NEBULA_FACTOR_INV_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case NEBULA_FACTOR_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case NEBULA_FACTOR_INV_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case NEBULA_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case NEBULA_FACTOR_INV_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

// Colour factors are invalid on the alpha channel.
VkBlendFactor alpha_factor(VkBlendFactor factor) noexcept {
    switch (factor) {
    case VK_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return factor;
    }
}

VkBlendFactor no_destination_alpha(VkBlendFactor factor) noexcept {
    return factor == VK_BLEND_FACTOR_DST_ALPHA             ? VK_BLEND_FACTOR_ONE
           : factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ? VK_BLEND_FACTOR_ZERO
                                                           : factor;
}

// The one device's shader modules and the driver's pipeline cache, shared by the building threads.
struct Modules {
    std::mutex lock;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::filesystem::path cache_file;
    std::unordered_map<const void*, VkShaderModule> modules;
    std::uint64_t on_disk = 0u;  // the hash of the bytes the file holds: an unchanged cache is not written again
};
Modules gModules;

std::uint64_t bytes_hash(const std::vector<char>& bytes) noexcept {
    std::uint64_t hash = 0x9E3779B97F4A7C15ull ^ bytes.size();
    std::size_t i = 0u;
    for (; i + 8u <= bytes.size(); i += 8u) {
        std::uint64_t word;
        std::memcpy(&word, bytes.data() + i, 8u);
        hash = (hash ^ word) * 0xFF51AFD7ED558CCDull;
        hash ^= hash >> 32u;
    }
    for (; i < bytes.size(); ++i) {
        hash = (hash ^ static_cast<unsigned char>(bytes[i])) * 0x100000001B3ull;
    }
    return hash;
}

// The cache file: this header, then the driver's cache data. The header names what the data is good for (the programs'
// compiler revision and the device's vendor, id, driver version and pipeline cache UUID); a file that differs, is short, or
// whose data does not hash to the header's is never handed to the driver, so the cache starts empty and is written again.
struct CacheFileHeader {
    char magic[8];
    std::uint32_t compiler_revision;
    std::uint32_t vendor_id;
    std::uint32_t device_id;
    std::uint32_t driver_version;
    std::uint8_t uuid[VK_UUID_SIZE];
    std::uint64_t size;
    std::uint64_t hash;
};
constexpr char kCacheMagic[8] = {'N', 'B', 'V', 'K', 'P', 'C', 'H', '1'};

CacheFileHeader cache_header(const std::vector<char>& data) noexcept {
    const VkPhysicalDeviceProperties& properties = vk::Context::get().properties();
    CacheFileHeader header{};
    std::memcpy(header.magic, kCacheMagic, sizeof(kCacheMagic));
    header.compiler_revision = vk::kCompilerRevision;
    header.vendor_id = properties.vendorID;
    header.device_id = properties.deviceID;
    header.driver_version = properties.driverVersion;
    std::memcpy(header.uuid, properties.pipelineCacheUUID, VK_UUID_SIZE);
    header.size = data.size();
    header.hash = bytes_hash(data);
    return header;
}

// The driver data inside a cache file; empty when the file is not this device's with the same programs.
std::vector<char> unwrap_cache_file(const std::vector<char>& file) {
    CacheFileHeader header{};
    if (file.size() <= sizeof(header)) {
        return {};
    }
    std::memcpy(&header, file.data(), sizeof(header));
    std::vector<char> data(file.begin() + sizeof(header), file.end());
    const CacheFileHeader expected = cache_header(data);
    return std::memcmp(&header, &expected, sizeof(header)) == 0 ? data : std::vector<char>{};
}

// The cache is internally synchronised, so the workers keep building while a copy goes out; one writer at a time.
std::size_t write_driver_cache() noexcept {
    static std::mutex writing;
    std::lock_guard one(writing);
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::filesystem::path file;
    std::uint64_t on_disk = 0u;
    {
        std::lock_guard guard(gModules.lock);
        device = gModules.device;
        cache = gModules.cache;
        file = gModules.cache_file;
        on_disk = gModules.on_disk;
    }
    std::size_t size = 0u;
    if (cache == VK_NULL_HANDLE || vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || size == 0u) {
        return 0u;
    }
    std::vector<char> bytes(size);
    if (vkGetPipelineCacheData(device, cache, &size, bytes.data()) != VK_SUCCESS) {
        return 0u;
    }
    bytes.resize(size);
    const std::uint64_t hash = bytes_hash(bytes);
    if (hash == on_disk) {
        return 0u;
    }
    const CacheFileHeader header = cache_header(bytes);
    std::vector<char> out(sizeof(header) + bytes.size());
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + sizeof(header), bytes.data(), bytes.size());
    if (!replace_file(file, out.data(), out.size())) {
        return 0u;
    }
    std::lock_guard guard(gModules.lock);
    gModules.on_disk = hash;
    return bytes.size();
}

VkShaderModule module_of(VkDevice device, const ProgramBytes& program) {
    std::lock_guard guard(gModules.lock);
    if (const auto it = gModules.modules.find(program.data); it != gModules.modules.end()) {
        return it->second;
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = program.size;
    info.pCode = static_cast<const std::uint32_t*>(program.data);
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    gModules.modules.emplace(program.data, module);
    return module;
}

VkPipelineCache driver_cache(VkDevice device) {
    std::lock_guard guard(gModules.lock);
    if (gModules.cache != VK_NULL_HANDLE) {
        return gModules.cache;
    }
    gModules.device = device;
    gModules.cache_file = shader_cache_directory() / "vk_pipelines.bin";
    std::vector<char> bytes;
    if (std::ifstream in(gModules.cache_file, std::ios::binary | std::ios::ate); in) {
        std::vector<char> file(static_cast<std::size_t>(in.tellg()));
        in.seekg(0);
        in.read(file.data(), static_cast<std::streamsize>(file.size()));
        bytes = unwrap_cache_file(file);
    }
    gModules.on_disk = bytes.empty() ? 0u : bytes_hash(bytes);
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = bytes.size();
    info.pInitialData = bytes.empty() ? nullptr : bytes.data();
    if (vkCreatePipelineCache(device, &info, nullptr, &gModules.cache) != VK_SUCCESS) {
        info.initialDataSize = 0u;
        info.pInitialData = nullptr;
        vkCreatePipelineCache(device, &info, nullptr, &gModules.cache);
    }
    return gModules.cache;
}

}  // namespace

gpu::Pipeline* Pipelines::build(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    const NebulaPipelineDesc& desc = entry.desc;
    const auto* layout = static_cast<const vk::RenderLayout*>(root_);
    const VkDevice device = device_;
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program = program_variant(desc.pixel, aux,
        (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    const ProgramBytes vs = program_bytes(desc.vertex, program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        std::fprintf(stderr, "[pipelines] no program %llx %llx variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    const VkShaderModule vs_module = module_of(device, vs);
    const VkShaderModule ps_module = module_of(device, ps);
    if (vs_module == VK_NULL_HANDLE || ps_module == VK_NULL_HANDLE) {
        return nullptr;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_module;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_module;
    stages[1].pName = "main";

    // What the vertex program reads; each from the list if named there, else the stream of zeros. A stride belongs to a
    // pipeline here, so the description's input strides decide the bindings.
    const std::vector<bool> reads = input_locations(vs);
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(reads.size());
    std::vector<bool> placed(reads.size(), false);
    VkVertexInputBindingDescription bindings[gpu::kStreamCount]{};
    std::uint32_t binding_count = 0u;
    const auto bind = [&](std::uint32_t slot, std::uint32_t stride) {
        for (std::uint32_t i = 0; i < binding_count; ++i) {
            if (bindings[i].binding == slot) {
                return;
            }
        }
        bindings[binding_count++] = {slot, stride, VK_VERTEX_INPUT_RATE_VERTEX};
    };
    const auto add = [&](std::uint32_t location, VkFormat format, std::uint32_t slot, std::uint32_t stride, std::uint32_t offset) {
        if (location >= reads.size() || !reads[location] || placed[location]) {
            return;
        }
        placed[location] = true;
        attributes.push_back({location, slot, format, offset});
        bind(slot, stride);
    };
    const std::uint32_t position_stride = desc.position_stride != 0u ? desc.position_stride : 12u;
    add(kPosition, VK_FORMAT_R32G32B32_SFLOAT, gpu::kStreamPosition, position_stride, 0u);
    if (desc.position_stride == 0u) {
        add(kMatrix, VK_FORMAT_R32_UINT, gpu::kStreamMatrix, 4u, 0u);
    }
    if (aux == 1u) {
        add(kBefore, VK_FORMAT_R32G32B32_SFLOAT, gpu::kStreamBefore, position_stride, 0u);
    }
    for (const NebulaLayoutInput& input : entry.inputs) {
        const bool zero = input.stream == NEBULA_STREAM_ZERO;
        const std::uint32_t slot = zero ? std::uint32_t{gpu::kStreamZero} : std::uint32_t{input.stream};
        add(input_location(input.semantic), input_format(input.format), slot, zero ? 0u : input.stride, input.offset);
    }
    for (std::uint32_t location = 0; location < reads.size(); ++location) {
        add(location, location_format(location), gpu::kStreamZero, 0u, 0u);
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = binding_count;
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1u;
    viewport.scissorCount = 1u;

    const std::uint32_t state = desc.state;
    // The viewport flips y as D3D's does, so clockwise is front as there. Culling all has no state: the walker draws nothing.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    switch ((state >> 15u) & 3u) {
    case NEBULA_CULL_BACK: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
    case NEBULA_CULL_FRONT: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    default: raster.cullMode = VK_CULL_MODE_NONE; break;
    }
    // D3D clamps fragment depth to the viewport's range and clips primitives to it (depth clears at the far plane rely on
    // the clamp).
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    if (layout->depth_clamp && layout->depth_clip_enable) {
        raster.depthClampEnable = VK_TRUE;
        depth_clip.depthClipEnable = VK_TRUE;
        raster.pNext = &depth_clip;
    }

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);

    // Depth is reversed: the game's less is greater.
    static constexpr VkCompareOp kDepth[8] = {
        VK_COMPARE_OP_NEVER, VK_COMPARE_OP_GREATER,   VK_COMPARE_OP_EQUAL,         VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_LESS,  VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if ((state & (1u << 10u)) != 0u) {
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = kDepth[(state >> 11u) & 7u];
        depth.depthWriteEnable = (state & (1u << 14u)) != 0u ? VK_TRUE : VK_FALSE;
    }
    if (const std::uint32_t test = (state >> 20u) & 3u; test != 0u) {
        static constexpr VkCompareOp kStencilTest[4] = {VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL,
                                                        VK_COMPARE_OP_NOT_EQUAL};
        static constexpr VkStencilOp kStencilOp[4] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_ZERO};
        depth.stencilTestEnable = VK_TRUE;
        VkStencilOpState face{};
        face.failOp = VK_STENCIL_OP_KEEP;
        face.depthFailOp = VK_STENCIL_OP_KEEP;
        face.passOp = kStencilOp[(state >> 22u) & 3u];
        face.compareOp = kStencilTest[test];
        face.compareMask = 0xFFu;
        face.writeMask = 0xFFu;
        depth.front = face;
        depth.back = face;
    }

    const bool target = (variant & kSceneVariantTarget) != 0u;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask =
        ((state & NEBULA_STATE_COLOR_WRITE) != 0u ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0u) |
        ((state & (NEBULA_STATE_ALPHA_WRITE | NEBULA_STATE_ALPHA_PASS)) != 0u && target ? VK_COLOR_COMPONENT_A_BIT : 0u);
    const std::uint32_t op = state & 3u;
    if (op != NEBULA_BLEND_REPLACE) {
        color.blendEnable = VK_TRUE;
        if (op == NEBULA_BLEND_SUBTRACT) {
            color.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        } else {
            color.srcColorBlendFactor = blend_factor((state >> 2u) & 15u);
            color.dstColorBlendFactor = blend_factor((state >> 6u) & 15u);
            color.colorBlendOp = VK_BLEND_OP_ADD;
            if (!target) {
                // No destination alpha in the scene: it reads as one.
                color.srcColorBlendFactor = no_destination_alpha(color.srcColorBlendFactor);
                color.dstColorBlendFactor = no_destination_alpha(color.dstColorBlendFactor);
            }
        }
        color.srcAlphaBlendFactor = alpha_factor(color.srcColorBlendFactor);
        color.dstAlphaBlendFactor = alpha_factor(color.dstColorBlendFactor);
        color.alphaBlendOp = color.colorBlendOp;
    }
    VkPipelineColorBlendAttachmentState targets[2]{color, {}};
    // Aux 4: the motion pass's second target is there and left as it is.
    if (aux == 1u) {
        targets[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    } else if (aux == 2u || aux == 3u) {
        targets[1].blendEnable = VK_TRUE;
        targets[1].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].colorBlendOp = VK_BLEND_OP_MAX;
        targets[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        targets[1].alphaBlendOp = VK_BLEND_OP_MAX;
        targets[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = aux != 0u ? 2u : 1u;
    blend.pAttachments = targets;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2u;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2u;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout->layout;
    info.renderPass = aux != 0u ? layout->aux_pass_for(variant, aux == 4u ? 1u : std::min(aux, 2u)) : layout->pass_for(variant, false);

    const VkPipelineCache cache = driver_cache(device);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, cache, 1u, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS && raster.depthClampEnable == VK_TRUE) {
        // A driver that refuses the depth-clip control still draws, unclamped.
        raster.depthClampEnable = VK_FALSE;
        raster.pNext = nullptr;
        result = vkCreateGraphicsPipelines(device, cache, 1u, &info, nullptr, &pipeline);
    }
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[pipelines] create failed %d: %llx %llx state %08x variant %u aux %u\n", static_cast<int>(result),
                     static_cast<unsigned long long>(desc.vertex), static_cast<unsigned long long>(desc.pixel), state, variant, aux);
        return nullptr;
    }
    std::lock_guard guard(lock_);
    owned_.push_back(pipeline);
    return pipeline;
}

void Pipelines::destroy_all() noexcept {
    const VkDevice device = device_;
    // A simulated loss leaves the queue alive; complete its references before destroying pipelines.
    // On a real loss this returns VK_ERROR_DEVICE_LOST and teardown can continue.
    if (device != VK_NULL_HANDLE) (void)vkDeviceWaitIdle(device);
    for (const VkPipeline pipeline : owned_) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    owned_.clear();
    write_driver_cache();
    std::lock_guard guard(gModules.lock);
    for (const auto& [data, module] : gModules.modules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    gModules.modules.clear();
    if (gModules.cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device, gModules.cache, nullptr);
        gModules.cache = VK_NULL_HANDLE;
    }
}

std::size_t Pipelines::save_cache() noexcept {
    return write_driver_cache();
}

}  // namespace galaxy::render::core
