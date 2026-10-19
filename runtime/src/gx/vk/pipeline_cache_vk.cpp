// SPDX-License-Identifier: GPL-3.0-only
// GX pipelines on Vulkan (pipeline_cache_vk.h).

#include "galaxy/gx/vk/pipeline_cache_vk.h"

#include "galaxy/gx/vk/shader_compile_vk.h"
#include "galaxy/gx/vertex_loader.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace galaxy::gx {

namespace {

// vk_shaders.bin: magic, version, then records of (u64 HLSL hash, u32 words, SPIR-V).
// vk_keys.bin: magic, version, then Entry records (the keys the shaders are generated from).
// vk_pipelines.bin: the driver's VkPipelineCache data (it checks its own header).
constexpr std::uint32_t kShaderFileMagic = 0x53565847u;  // "GXVS"
constexpr std::uint32_t kKeyFileMagic = 0x4b565847u;     // "GXVK"
constexpr std::uint32_t kFileVersion = 1u;
constexpr std::uint32_t kMaxRecords = 65536u;
constexpr std::uint32_t kMaxSpirvWords = 1u << 20u;

std::uint64_t elapsed_us(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

// The HLSL a module comes from, with what changes its SPIR-V for the same text.
std::uint64_t module_hash(const std::string& hlsl, vk::ShaderStage stage, bool dual_source) {
    std::uint64_t hash = fnv1a64(hlsl.data(), hlsl.size());
    hash = combine64(hash, static_cast<std::uint64_t>(stage));
    hash = combine64(hash, vk::kCompilerRevision);
    return combine64(hash, dual_source ? 1u : 0u);
}

VkBlendFactor gx_src_blend(std::uint32_t f) {
    switch (f) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 2: return VK_BLEND_FACTOR_DST_COLOR;
    case 3: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_DST_ALPHA;
    case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendFactor gx_dst_blend(std::uint32_t f) {
    switch (f) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 2: return VK_BLEND_FACTOR_SRC_COLOR;
    case 3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_DST_ALPHA;
    case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return VK_BLEND_FACTOR_ZERO;
    }
}

// Colour-only factors are invalid for the alpha channel; the alpha equation takes their alpha forms.
VkBlendFactor to_alpha_blend(VkBlendFactor factor) {
    switch (factor) {
    case VK_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return factor;
    }
}

// GX LogicOp 0-15.
VkLogicOp gx_logic_op(std::uint32_t op) {
    static constexpr VkLogicOp kOps[16] = {
        VK_LOGIC_OP_CLEAR,        VK_LOGIC_OP_AND,          VK_LOGIC_OP_AND_REVERSE, VK_LOGIC_OP_COPY,
        VK_LOGIC_OP_AND_INVERTED, VK_LOGIC_OP_NO_OP,        VK_LOGIC_OP_XOR,         VK_LOGIC_OP_OR,
        VK_LOGIC_OP_NOR,          VK_LOGIC_OP_EQUIVALENT,   VK_LOGIC_OP_INVERT,      VK_LOGIC_OP_OR_REVERSE,
        VK_LOGIC_OP_COPY_INVERTED, VK_LOGIC_OP_OR_INVERTED, VK_LOGIC_OP_NAND,        VK_LOGIC_OP_SET,
    };
    return kOps[op & 0xfu];
}

// The locations of a module's stage inputs, as a mask.
std::uint32_t input_location_mask(const std::vector<std::uint32_t>& spirv) {
    constexpr std::uint32_t kOpVariable = 59;
    constexpr std::uint32_t kOpDecorate = 71;
    constexpr std::uint32_t kDecorationLocation = 30;
    constexpr std::uint32_t kStorageInput = 1;
    std::unordered_map<std::uint32_t, std::uint32_t> locations;
    std::vector<std::uint32_t> inputs;
    for (std::size_t at = 5; at < spirv.size() && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (op == kOpDecorate && spirv[at + 2] == kDecorationLocation) {
            locations[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpVariable && spirv[at + 3] == kStorageInput) {
            inputs.push_back(spirv[at + 2]);
        }
    }
    std::uint32_t mask = 0;
    for (const std::uint32_t input : inputs) {
        if (const auto it = locations.find(input); it != locations.end() && it->second < 32u) {
            mask |= 1u << it->second;
        }
    }
    return mask;
}

// GxVertexOut (vertex_loader.h) as the shaders' VSIn, at its semantics' locations (compile_hlsl): the eight texture
// coordinates one each.
constexpr VkVertexInputAttributeDescription kVertexAttributes[] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
    {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24},   {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36},
    {4, 0, VK_FORMAT_R32_UINT, 48},           {5, 0, VK_FORMAT_R32_UINT, 52},
    {6, 0, VK_FORMAT_R32G32_SFLOAT, 56},      {7, 0, VK_FORMAT_R32G32_SFLOAT, 64},
    {8, 0, VK_FORMAT_R32G32_SFLOAT, 72},      {9, 0, VK_FORMAT_R32G32_SFLOAT, 80},
    {10, 0, VK_FORMAT_R32G32_SFLOAT, 88},     {11, 0, VK_FORMAT_R32G32_SFLOAT, 96},
    {12, 0, VK_FORMAT_R32G32_SFLOAT, 104},    {13, 0, VK_FORMAT_R32G32_SFLOAT, 112},
    {14, 0, VK_FORMAT_R32G32B32A32_UINT, 120},
};
static_assert(sizeof(GxVertexOut) == 144u);

template <typename T>
bool read_pod(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
void write_pod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Written beside the target and renamed, so a reader never sees a partial file.
template <typename Write>
bool replace_file(const std::filesystem::path& path, Write&& write) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        write(out);
        if (!out) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    return !error;
}

}  // namespace

std::size_t PipelineCacheVk::UberKeyHasher::operator()(const UberKey& key) const noexcept {
    PsoKey pso{};
    pso.render_state = key.render_state;
    return static_cast<std::size_t>(combine64(pso.hash(), key.variant));
}

PipelineCacheVk::~PipelineCacheVk() noexcept {
    try {
        shutdown();
    } catch (...) {
    }
}

bool PipelineCacheVk::initialize(VkDevice device, const vk::GxLayout* layout, const std::filesystem::path& cache_dir,
                                 unsigned worker_count) {
    device_ = device;
    layout_ = layout;
    cache_dir_ = cache_dir;
    std::error_code error;
    std::filesystem::create_directories(cache_dir_, error);
    load_disk();
    stopping_ = false;
    {
        const char* value = std::getenv("GALAXY_GX_UBER");
        const std::string mode = value != nullptr && value[0] != '\0' ? value : "1";
        uber_mode_ = mode == "force" ? UberMode::Force
            : (mode == "0" || mode == "off") ? UberMode::Off
                                              : UberMode::Hybrid;
    }
    if (uber_mode_ == UberMode::Force) {
        compile_uber_shaders();
    } else if (uber_mode_ == UberMode::Hybrid) {
        uber_thread_ = std::thread([this] { compile_uber_shaders(); });
    }
    const unsigned workers = std::max(1u, worker_count);
    for (unsigned i = 0; i < workers; ++i) {
        workers_.emplace_back([this] { worker_main(); });
    }
    // Every pipeline a previous session used is built again up front.
    if (uber_mode_ != UberMode::Force) {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        for (const Entry& entry : entries_) {
            in_flight_.insert(entry.key);
            enqueue(entry);
        }
    }
    std::fprintf(stderr, "[pipeline-cache-vk] shaders=%zu keys=%zu workers=%u\n", spirv_.size(), entries_.size(),
                 workers);
    return true;
}

void PipelineCacheVk::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    jobs_cv_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
    workers_.clear();
    if (uber_thread_.joinable()) {
        uber_thread_.join();
    }
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
    save_disk();
    vkDeviceWaitIdle(device_);
    drain_completions();
    for (auto& [key, pipeline] : published_) {
        vkDestroyPipeline(device_, pipeline, nullptr);
    }
    for (auto& [key, pipeline] : uber_pipelines_) {
        vkDestroyPipeline(device_, pipeline, nullptr);
    }
    for (auto& [hash, module] : modules_) {
        vkDestroyShaderModule(device_, module, nullptr);
    }
    published_.clear();
    uber_pipelines_.clear();
    modules_.clear();
    input_masks_.clear();
    in_flight_.clear();
    uber_ready_.store(false, std::memory_order_release);
    uber_vs_ = line_gs_ = point_gs_ = VK_NULL_HANDLE;
    uber_ps_.fill(VK_NULL_HANDLE);
    memo_pipeline_ = VK_NULL_HANDLE;
    if (driver_cache_ != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, driver_cache_, nullptr);
        driver_cache_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

VkShaderModule PipelineCacheVk::module_for(const std::string& hlsl, vk::ShaderStage stage, bool dual_source) {
    const std::uint64_t hash = module_hash(hlsl, stage, dual_source);
    std::vector<std::uint32_t> spirv;
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        if (const auto it = modules_.find(hash); it != modules_.end()) {
            return it->second;
        }
        if (const auto it = spirv_.find(hash); it != spirv_.end()) {
            spirv = it->second;
            shaders_from_cache_.fetch_add(1u, std::memory_order_relaxed);
        }
    }
    if (spirv.empty()) {
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        spirv = vk::compile_hlsl(vk::annotate(hlsl, vk::gx_draw_bindings()), stage, "main", {}, error,
                                 {false, dual_source});
        compile_us_.fetch_add(elapsed_us(start), std::memory_order_relaxed);
        shader_compiles_.fetch_add(1u, std::memory_order_relaxed);
        if (spirv.empty()) {
            static std::atomic<int> reported{0};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 4) {
                std::fprintf(stderr, "[pipeline-cache-vk] shader compile failed: %s\n", error.c_str());
            }
            return VK_NULL_HANDLE;
        }
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size() * sizeof(std::uint32_t);
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    const std::uint32_t inputs = stage == vk::ShaderStage::Vertex ? input_location_mask(spirv) : 0u;
    std::lock_guard<std::mutex> lock(modules_mutex_);
    if (const auto [it, inserted] = modules_.emplace(hash, module); !inserted) {
        vkDestroyShaderModule(device_, module, nullptr);  // another thread made it first
        return it->second;
    }
    if (stage == vk::ShaderStage::Vertex) {
        input_masks_.emplace(module, inputs);
    }
    if (spirv_.emplace(hash, std::move(spirv)).second) {
        unsaved_.store(true, std::memory_order_release);
    }
    return module;
}

VkPipeline PipelineCacheVk::create_pipeline(const RenderStateKey& rs, VkShaderModule vs, VkShaderModule ps,
                                            bool dual_source, bool export_depth) const {
    if (vs == VK_NULL_HANDLE || ps == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[3]{};
    std::uint32_t stage_count = 0;
    const auto add_stage = [&](VkShaderStageFlagBits stage, VkShaderModule module) {
        VkPipelineShaderStageCreateInfo& info = stages[stage_count++];
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = stage;
        info.module = module;
        info.pName = "main";
    };
    add_stage(VK_SHADER_STAGE_VERTEX_BIT, vs);
    // Lines and points are expanded to quads in the geometry stage (GX widths), as on D3D12. A device without
    // geometry shaders draws them one pixel wide.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool expanded = false;
    if (rs.primitive_topology == gpu::kTopologyTypePointValue) {
        topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        if (point_gs_ != VK_NULL_HANDLE) {
            add_stage(VK_SHADER_STAGE_GEOMETRY_BIT, point_gs_);
            expanded = true;
        }
    } else if (rs.primitive_topology == gpu::kTopologyTypeLineValue) {
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        if (line_gs_ != VK_NULL_HANDLE) {
            add_stage(VK_SHADER_STAGE_GEOMETRY_BIT, line_gs_);
            expanded = true;
        }
    }
    add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, ps);

    VkVertexInputBindingDescription binding{0, static_cast<std::uint32_t>(sizeof(GxVertexOut)),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    // Only the attributes the vertex shader reads (the others would be fetched for nothing).
    std::uint32_t inputs = ~0u;
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        if (const auto it = input_masks_.find(vs); it != input_masks_.end()) {
            inputs = it->second;
        }
    }
    std::array<VkVertexInputAttributeDescription, std::size(kVertexAttributes)> attributes{};
    std::uint32_t attribute_count = 0;
    for (const VkVertexInputAttributeDescription& attribute : kVertexAttributes) {
        if ((inputs >> attribute.location) & 1u) {
            attributes[attribute_count++] = attribute;
        }
    }
    vertex_input.vertexAttributeDescriptionCount = attribute_count;
    vertex_input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // GX screen space is y-down against D3D-style NDC, so GX front faces are the rasteriser's back faces (the D3D12
    // pipelines make the same swap); the viewport flips y as D3D's does, so clockwise is front as there.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    // D3D clamps fragment depth, a shader-written one too, to the viewport's range and clips primitives to it (GX
    // depth clears rely on the clamp: near == far). Without VK_EXT_depth_clip_enable only depth-writing shaders clamp,
    // and their primitives go unclipped in depth.
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    if (layout_->depth_clamp && layout_->depth_clip_enable) {
        raster.depthClampEnable = VK_TRUE;
        depth_clip.depthClipEnable = VK_TRUE;
        raster.pNext = &depth_clip;
    } else if (layout_->depth_clamp && export_depth) {
        raster.depthClampEnable = VK_TRUE;
    }
    switch (static_cast<CullMode>(rs.cull)) {
    case CullMode::Front: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
    case CullMode::Back: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    default: raster.cullMode = VK_CULL_MODE_NONE; break;
    }
    if (topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || expanded) {
        raster.cullMode = VK_CULL_MODE_NONE;  // GX culls polygons, not lines or points
    }
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Reversed-Z against GX's z24 (the projection negates z; RendererVk's viewport maps the GX depth range), so the
    // compare directions flip, as on D3D12.
    static constexpr VkCompareOp kDepthCompare[8] = {
        VK_COMPARE_OP_NEVER,   VK_COMPARE_OP_GREATER,   VK_COMPARE_OP_EQUAL,         VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_LESS,    VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    const std::uint32_t zb = rs.zmode_bits;
    if ((zb & 1u) != 0u) {
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = kDepthCompare[(zb >> 1u) & 0x7u];
        depth.depthWriteEnable = (zb & (1u << 4u)) != 0u ? VK_TRUE : VK_FALSE;
    }

    const std::uint32_t bb = rs.blend_bits;
    VkPipelineColorBlendAttachmentState target{};
    target.colorWriteMask =
        ((bb & (1u << 25u)) != 0u ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                  : 0u) |
        ((bb & (1u << 26u)) != 0u ? VK_COLOR_COMPONENT_A_BIT : 0u);
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    if ((bb & 1u) != 0u) {
        target.blendEnable = VK_TRUE;
        if ((bb & 2u) != 0u) {
            // GX subtract: dst - src, factors one.
            target.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        } else {
            target.srcColorBlendFactor = gx_src_blend((bb >> 2u) & 0x7u);
            target.dstColorBlendFactor = gx_dst_blend((bb >> 5u) & 0x7u);
            target.colorBlendOp = VK_BLEND_OP_ADD;
            if (rs.pixfmt != 1u) {
                // No destination alpha in this pixel format: it reads as one.
                const auto no_dst_alpha = [](VkBlendFactor factor) {
                    return factor == VK_BLEND_FACTOR_DST_ALPHA ? VK_BLEND_FACTOR_ONE
                        : factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ? VK_BLEND_FACTOR_ZERO : factor;
                };
                target.srcColorBlendFactor = no_dst_alpha(target.srcColorBlendFactor);
                target.dstColorBlendFactor = no_dst_alpha(target.dstColorBlendFactor);
            }
        }
        target.srcAlphaBlendFactor = to_alpha_blend(target.srcColorBlendFactor);
        target.dstAlphaBlendFactor = to_alpha_blend(target.dstColorBlendFactor);
        target.alphaBlendOp = target.colorBlendOp;
        if ((bb & (1u << 27u)) != 0u && dual_source && layout_->dual_source_blend) {
            // The pixel shader's second output carries the TEV alpha the colour blends against.
            const auto second_source = [](VkBlendFactor factor) {
                return factor == VK_BLEND_FACTOR_SRC_ALPHA ? VK_BLEND_FACTOR_SRC1_ALPHA
                    : factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ? VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA : factor;
            };
            target.srcColorBlendFactor = second_source(target.srcColorBlendFactor);
            target.dstColorBlendFactor = second_source(target.dstColorBlendFactor);
        }
        if ((bb & (1u << 28u)) != 0u) {  // constant destination alpha is independent
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    } else if ((bb & (1u << 8u)) != 0u && layout_->logic_op) {
        blend.logicOpEnable = VK_TRUE;
        blend.logicOp = gx_logic_op((bb >> 9u) & 0xfu);
    }
    blend.attachmentCount = 1;
    blend.pAttachments = &target;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_->layout;
    info.renderPass = layout_->efb_pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, driver_cache_, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        static std::atomic<int> reported{0};
        if (reported.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::fprintf(stderr, "[pipeline-cache-vk] vkCreateGraphicsPipelines failed\n");
        }
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline PipelineCacheVk::build(const Entry& entry) {
    const bool dual_source = (entry.ps_key.flags & 0x80u) != 0u;
    const VkShaderModule vs = module_for(generator_.generate_vs(entry.vs_key), vk::ShaderStage::Vertex, false);
    const VkShaderModule ps =
        module_for(generator_.generate_ps(entry.ps_key), vk::ShaderStage::Fragment, dual_source);
    const bool export_depth = (entry.ps_key.flags & 0x02u) != 0u && (entry.ps_key.flags & 0x01u) != 0u;
    const VkPipeline pipeline = create_pipeline(entry.key.render_state, vs, ps, dual_source, export_depth);
    if (pipeline != VK_NULL_HANDLE) {
        pso_compiles_.fetch_add(1u, std::memory_order_relaxed);
    }
    return pipeline;
}

void PipelineCacheVk::compile_uber_shaders() noexcept {
    try {
        const auto start = std::chrono::steady_clock::now();
        const std::string vs(uber_vs_source());
        uber_vs_ = module_for(vs, vk::ShaderStage::Vertex, false);
        for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
            const bool dual = uber_ps_variant_dual_source(variant);
            std::string ps;
            ps.reserve(uber_ps_source().size() + 96u);
            ps += dual ? "#define UBER_DUAL_SRC 1\n" : "#define UBER_DUAL_SRC 0\n";
            ps += uber_ps_variant_export_depth(variant) ? "#define UBER_EXPORT_DEPTH 1\n" : "#define UBER_EXPORT_DEPTH 0\n";
            ps += uber_ps_variant_early_depth(variant) ? "#define UBER_EARLY_DEPTH 1\n" : "#define UBER_EARLY_DEPTH 0\n";
            ps += uber_ps_source();
            uber_ps_[variant] = module_for(ps, vk::ShaderStage::Fragment, dual);
        }
        if (layout_->geometry_shaders) {
            line_gs_ = module_for(std::string(ShaderGenerator::line_geometry_shader_source()),
                                  vk::ShaderStage::Geometry, false);
            point_gs_ = module_for(std::string(ShaderGenerator::point_geometry_shader_source()),
                                   vk::ShaderStage::Geometry, false);
        }
        const bool complete = uber_vs_ != VK_NULL_HANDLE &&
            std::all_of(uber_ps_.begin(), uber_ps_.end(), [](VkShaderModule m) { return m != VK_NULL_HANDLE; });
        std::fprintf(stderr, "[uber-pipeline-vk] %s in %llu ms\n", complete ? "shaders ready" : "shader compile failed",
                     static_cast<unsigned long long>(elapsed_us(start) / 1000u));
        uber_ready_.store(complete, std::memory_order_release);
    } catch (...) {
        // Without the uber shaders every first use builds its specialised pipeline on the render thread.
    }
}

VkPipeline PipelineCacheVk::uber_pipeline(const RenderStateKey& render_state, unsigned variant) {
    UberKey key{};
    key.render_state = render_state;
    key.render_state.blend_bits &= ~(kRenderStateMotionTarget | kRenderStateReactiveTarget);
    key.variant = variant;
    if (const auto it = uber_pipelines_.find(key); it != uber_pipelines_.end()) {
        return it->second;
    }
    const VkPipeline pipeline =
        create_pipeline(key.render_state, uber_vs_, uber_ps_[variant], uber_ps_variant_dual_source(variant),
                        uber_ps_variant_export_depth(variant));
    uber_pipelines_.emplace(key, pipeline);
    return pipeline;
}

void PipelineCacheVk::enqueue(const Entry& entry) {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_.push_back(entry);
    }
    jobs_cv_.notify_one();
}

void PipelineCacheVk::worker_main() {
    for (;;) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(jobs_mutex_);
            jobs_cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) {
                return;
            }
            entry = jobs_.front();
            jobs_.pop_front();
        }
        const VkPipeline pipeline = build(entry);
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        completions_.push_back({entry.key, pipeline});
    }
}

VkPipeline PipelineCacheVk::get(const PsoKey& key, const PixelShaderKey& ps_key, const VertexShaderKey& vs_key) {
    if (uber_mode_ == UberMode::Force && uber_ready_.load(std::memory_order_relaxed)) {
        ++uber_fallbacks_;
        last_get_used_uber_ = true;
        return uber_pipeline(key.render_state, uber_ps_variant(ps_key));
    }
    last_get_used_uber_ = false;
    if (memo_pipeline_ != VK_NULL_HANDLE && memo_key_ == key) {
        ++specialized_hits_;
        return memo_pipeline_;
    }
    if (const auto it = published_.find(key); it != published_.end() && it->second != VK_NULL_HANDLE) {
        ++specialized_hits_;
        memo_key_ = key;
        memo_pipeline_ = it->second;
        return it->second;
    }
    if (!in_flight_.contains(key)) {
        const Entry entry{key, vs_key, ps_key};
        {
            std::lock_guard<std::mutex> lock(entries_mutex_);
            if (entry_keys_.insert(key).second) {
                entries_.push_back(entry);
                unsaved_.store(true, std::memory_order_release);
            }
        }
        if (!uber_ready_.load(std::memory_order_acquire)) {
            // No stand-in yet: build it here.
            const VkPipeline pipeline = build(entry);
            published_[key] = pipeline;
            if (pipeline != VK_NULL_HANDLE) {
                memo_key_ = key;
                memo_pipeline_ = pipeline;
            }
            return pipeline;
        }
        in_flight_.insert(key);
        enqueue(entry);
    }
    if (!uber_ready_.load(std::memory_order_acquire)) {
        // A prewarm job queued before the uber shaders exist: build it here too (drain_completions keeps one).
        drain_completions();
        if (const auto it = published_.find(key); it != published_.end()) {
            return it->second;
        }
        const VkPipeline pipeline = build(Entry{key, vs_key, ps_key});
        published_[key] = pipeline;
        return pipeline;
    }
    ++uber_fallbacks_;
    last_get_used_uber_ = true;
    return uber_pipeline(key.render_state, uber_ps_variant(ps_key));
}

void PipelineCacheVk::drain_completions() {
    std::vector<Completion> done;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        done.swap(completions_);
    }
    for (const Completion& completion : done) {
        in_flight_.erase(completion.key);
        auto [it, inserted] = published_.emplace(completion.key, completion.pipeline);
        if (!inserted) {
            // Built on the render thread meanwhile: keep that one.
            if (completion.pipeline != VK_NULL_HANDLE && completion.pipeline != it->second) {
                vkDestroyPipeline(device_, completion.pipeline, nullptr);
            }
        }
    }
}

void PipelineCacheVk::load_disk() {
    {
        std::ifstream in(cache_dir_ / "vk_shaders.bin", std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        if (in && read_pod(in, magic) && read_pod(in, version) && read_pod(in, count) && magic == kShaderFileMagic &&
            version == kFileVersion && count <= kMaxRecords) {
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint64_t hash = 0;
                std::uint32_t words = 0;
                if (!read_pod(in, hash) || !read_pod(in, words) || words == 0u || words > kMaxSpirvWords) {
                    break;
                }
                std::vector<std::uint32_t> spirv(words);
                if (!in.read(reinterpret_cast<char*>(spirv.data()),
                             static_cast<std::streamsize>(words * sizeof(std::uint32_t))) ||
                    spirv[0] != 0x07230203u) {
                    break;
                }
                spirv_.emplace(hash, std::move(spirv));
            }
        }
        spirv_on_disk_ = spirv_.size();
    }
    {
        std::ifstream in(cache_dir_ / "vk_keys.bin", std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        if (in && read_pod(in, magic) && read_pod(in, version) && read_pod(in, count) && magic == kKeyFileMagic &&
            version == kFileVersion && count <= kMaxRecords) {
            for (std::uint32_t i = 0; i < count; ++i) {
                Entry entry;
                if (!read_pod(in, entry)) {
                    break;
                }
                // The key must be the one its shader keys make (a different build changes the hashes).
                if (entry.key.vs_hash != entry.vs_key.hash() || entry.key.ps_hash != entry.ps_key.hash()) {
                    continue;
                }
                if (entry_keys_.insert(entry.key).second) {
                    entries_.push_back(entry);
                }
            }
        }
        entries_on_disk_ = entries_.size();
    }
    std::vector<char> data;
    {
        std::ifstream in(cache_dir_ / "vk_pipelines.bin", std::ios::binary | std::ios::ate);
        if (in) {
            const std::streamsize size = in.tellg();
            if (size > 0 && size < (256 << 20)) {
                data.resize(static_cast<std::size_t>(size));
                in.seekg(0);
                if (!in.read(data.data(), size)) {
                    data.clear();
                }
            }
        }
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_) != VK_SUCCESS) {
        // The driver rejected the data (another driver or device): start empty.
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        (void)vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_);
    }
}

void PipelineCacheVk::save_disk() noexcept {
    try {
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> spirv;
        {
            std::lock_guard<std::mutex> lock(modules_mutex_);
            spirv = spirv_;
        }
        std::vector<Entry> entries;
        {
            std::lock_guard<std::mutex> lock(entries_mutex_);
            entries = entries_;
        }
        unsaved_.store(false, std::memory_order_release);
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(spirv.size(), kMaxRecords));
        replace_file(cache_dir_ / "vk_shaders.bin", [&](std::ofstream& out) {
            write_pod(out, kShaderFileMagic);
            write_pod(out, kFileVersion);
            write_pod(out, count);
            std::uint32_t written = 0;
            for (const auto& [hash, words] : spirv) {
                if (written++ == count) {
                    break;
                }
                write_pod(out, hash);
                write_pod(out, static_cast<std::uint32_t>(words.size()));
                out.write(reinterpret_cast<const char*>(words.data()),
                          static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
            }
        });
        const auto key_count = static_cast<std::uint32_t>(std::min<std::size_t>(entries.size(), kMaxRecords));
        replace_file(cache_dir_ / "vk_keys.bin", [&](std::ofstream& out) {
            write_pod(out, kKeyFileMagic);
            write_pod(out, kFileVersion);
            write_pod(out, key_count);
            for (std::uint32_t i = 0; i < key_count; ++i) {
                write_pod(out, entries[i]);
            }
        });
        if (driver_cache_ != VK_NULL_HANDLE) {
            std::size_t size = 0;
            if (vkGetPipelineCacheData(device_, driver_cache_, &size, nullptr) == VK_SUCCESS && size != 0u) {
                std::vector<char> data(size);
                if (vkGetPipelineCacheData(device_, driver_cache_, &size, data.data()) == VK_SUCCESS) {
                    replace_file(cache_dir_ / "vk_pipelines.bin", [&](std::ofstream& out) {
                        out.write(data.data(), static_cast<std::streamsize>(size));
                    });
                }
            }
        }
        spirv_on_disk_ = count;
        entries_on_disk_ = key_count;
    } catch (...) {
        unsaved_.store(true, std::memory_order_release);  // retried at the next flush
    }
}

void PipelineCacheVk::flush_disk() {
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
    save_disk();
}

void PipelineCacheVk::flush_disk_async() {
    if (flush_thread_.joinable() || !has_unsaved_records()) {
        return;
    }
    flush_done_.store(false, std::memory_order_release);
    flush_thread_ = std::thread([this] {
        save_disk();
        flush_done_.store(true, std::memory_order_release);
    });
}

void PipelineCacheVk::poll_disk_flush() {
    // Joined once it has finished, so the render thread never waits for the disk.
    if (flush_thread_.joinable() && flush_done_.load(std::memory_order_acquire)) {
        flush_thread_.join();
    }
}

PipelineCacheVk::CompileStats PipelineCacheVk::compile_stats() const {
    CompileStats stats;
    stats.pso_compiles = pso_compiles_.load(std::memory_order_relaxed);
    stats.shader_pair_compiles = shader_compiles_.load(std::memory_order_relaxed);
    stats.blobs_from_cache = shaders_from_cache_.load(std::memory_order_relaxed);
    stats.blob_compile_us = compile_us_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        stats.shader_records = spirv_.size();
    }
    stats.shader_records_on_disk = spirv_on_disk_;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        stats.pso_keys = entries_.size();
    }
    stats.pso_keys_on_disk = entries_on_disk_;
    stats.shader_cache_at_capacity = stats.shader_records >= kMaxRecords;
    stats.pso_cache_at_capacity = stats.pso_keys >= kMaxRecords;
    return stats;
}

}  // namespace galaxy::gx
