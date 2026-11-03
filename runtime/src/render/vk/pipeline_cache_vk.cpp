// SPDX-License-Identifier: GPL-3.0-only
// PipelineCacheVk: native materials' pipelines on the Vulkan renderer (E-04): a material's build-compiled SPIR-V
// (native_shaders.h) under a render state, created at its first draw through the driver's pipeline cache, with the
// optional features a driver refuses left out (learned per driver build in vk_compat.txt).

#include "galaxy/render/vk/pipeline_cache_vk.h"
#include "galaxy/run_options.h"
#include "galaxy/render/pipeline_seed.h"
#include "galaxy/windows_power.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "galaxy/render/native_shaders.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/vk/vk_context.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace galaxy::render {

namespace {

const char* compat_name(std::uint32_t bit) {
    switch (bit) {
    case 1u << 0u: return "depth-clip control";
    case 1u << 1u: return "logic ops";
    case 1u << 2u: return "dual-source blending";
    case 1u << 3u: return "depth clamp";
    default: return "several features";
    }
}

std::string describe_render_state(const RenderStateKey& rs) {
    char text[128];
    std::snprintf(text, sizeof(text), "blend 0x%08x z 0x%04x cull %u format %u topology %u", rs.blend_bits,
                  rs.zmode_bits, rs.cull, rs.pixfmt, rs.primitive_topology);
    return text;
}

// GALAXY_VK_TEST_REFUSE=<names, comma-separated>: pipelines using those features (dual-source, depth-clip,
// depth-clamp, logic-op), or every native pipeline (native), are refused as a driver would, so the
// fallbacks run on any GPU.
struct TestRefusals {
    bool dual_source = false;
    bool depth_clip = false;
    bool depth_clamp = false;
    bool logic_op = false;
    bool native = false;
};

const TestRefusals& test_refusals() {
    static const TestRefusals refusals = [] {
        TestRefusals r;
        const char* names = ::galaxy::run_options().vk_test_refuse;
        const std::string list = names != nullptr ? std::string(",") + names + "," : "";
        const auto has = [&](const char* name) { return list.find(std::string(",") + name + ",") != std::string::npos; };
        r.dual_source = has("dual-source");
        r.depth_clip = has("depth-clip");
        r.depth_clamp = has("depth-clamp");
        r.logic_op = has("logic-op");
        r.native = has("native");
        return r;
    }();
    return refusals;
}

VkBlendFactor src_blend(std::uint32_t f) {
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

VkBlendFactor dst_blend(std::uint32_t f) {
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

// game LogicOp 0-15.
VkLogicOp logic_op_mode(std::uint32_t op) {
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

// RenderVertex (render_vertex.h) as the shaders' VSIn, at its semantics' locations (compile_hlsl): the eight texture
// coordinates one each.
constexpr VkVertexInputAttributeDescription kVertexAttributes[] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
    {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24},   {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36},
    {4, 0, VK_FORMAT_R32_UINT, 48},           {5, 0, VK_FORMAT_R32_UINT, 52},
    {6, 0, VK_FORMAT_R32G32_SFLOAT, 56},      {7, 0, VK_FORMAT_R32G32_SFLOAT, 64},
    {8, 0, VK_FORMAT_R32G32_SFLOAT, 72},      {9, 0, VK_FORMAT_R32G32_SFLOAT, 80},
    {10, 0, VK_FORMAT_R32G32_SFLOAT, 88},     {11, 0, VK_FORMAT_R32G32_SFLOAT, 96},
    {12, 0, VK_FORMAT_R32G32_SFLOAT, 104},    {13, 0, VK_FORMAT_R32G32_SFLOAT, 112},
    {14, 0, VK_FORMAT_R32G32B32_UINT, 120},    {15, 0, VK_FORMAT_R32G32B32_UINT, 132},
};
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

VkShaderModule PipelineCacheVk::native_module(const void* words, std::size_t bytes, vk::ShaderStage stage) {
    if (words == nullptr || bytes < 4u || bytes % 4u != 0u) {
        return VK_NULL_HANDLE;
    }
    const std::uint64_t hash = combine64(fnv1a64(words, bytes), 0x4E41544956450000ull | static_cast<unsigned>(stage));
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        if (const auto it = modules_.find(hash); it != modules_.end()) {
            return it->second;
        }
    }
    std::vector<std::uint32_t> spirv(bytes / 4u);
    std::memcpy(spirv.data(), words, bytes);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> lock(modules_mutex_);
    modules_.emplace(hash, module);
    if (stage == vk::ShaderStage::Vertex) {
        input_masks_.emplace(module, input_location_mask(spirv));
    }
    return module;
}

VkPipeline PipelineCacheVk::create_native(std::uint32_t shader, const RenderStateKey& rs, unsigned variant) {
    const NativeShaderBytes vs_bytes = native_shader_bytes(shader, variant, false);
    const NativeShaderBytes ps_bytes = native_shader_bytes(shader, variant, true);
    const VkShaderModule vs = native_module(vs_bytes.data, vs_bytes.size, vk::ShaderStage::Vertex);
    const VkShaderModule ps = native_module(ps_bytes.data, ps_bytes.size, vk::ShaderStage::Fragment);
    VkPipeline pipeline = VK_NULL_HANDLE;
    // A curved surface's patches: the native patch stages for the output layout.
    VkShaderModule hs = VK_NULL_HANDLE;
    VkShaderModule ds = VK_NULL_HANDLE;
    if ((rs.primitive_topology & kTopologyTypeMask) == kTopologyPatch) {
        const bool motion = variant == kNativeVariantMotion;
        const NativeShaderBytes hull = native_curved_bytes(motion, false);
        const NativeShaderBytes domain = native_curved_bytes(motion, true);
        hs = native_module(hull.data, hull.size, vk::ShaderStage::Hull);
        ds = native_module(domain.data, domain.size, vk::ShaderStage::Domain);
    }
    if (vs != VK_NULL_HANDLE && ps != VK_NULL_HANDLE) {
        // The dual variant's second output is the 8-bit combiner alpha the colour blends against (render state bit 27),
        // as the old path's dual-source pipelines.
        const bool dual = variant == kNativeVariantDual;
        const auto make = [&](std::uint32_t compat, VkResult& result) {
            return create_pipeline(rs, vs, ps, dual, false, compat, result, hs, ds);
        };
        pipeline = with_fallbacks(make, applicable_compat(rs, dual, false), "a native pipeline",
                                  describe_render_state(rs), nullptr);
    }
    return pipeline;
}

VkPipeline PipelineCacheVk::get_native(std::uint32_t shader, const RenderStateKey& render_state) {
    const RenderStateKey rs = native_render_state(render_state);
    const unsigned variant = native_shader_variant(rs.blend_bits);
    const std::uint32_t id = shader * kNativeVariants + variant;
    const std::uint64_t hash = combine64(hash_key_words(&rs, sizeof(rs)), id);
    auto& bucket = native_pipelines_[hash];
    for (const auto& entry : bucket) {
        if (entry.first.first == id && entry.first.second == rs) {
            return entry.second;
        }
    }
    note_pipeline_for_seed(shader, render_state);
    const VkPipeline pipeline = create_native(shader, rs, variant);
    bucket.emplace_back(std::make_pair(id, rs), pipeline);
    // The driver's cache data on disk every 64 new pipelines (the next launch builds them from it).
    if (pipeline != VK_NULL_HANDLE && (++created_ & 63u) == 0u) {
        save_driver_cache();
    }
    return pipeline;
}

void PipelineCacheVk::stop_prepare() noexcept {
    prepare_stop_.store(true, std::memory_order_relaxed);
    for (std::thread& t : prepare_threads_) {
        t.join();
    }
    prepare_threads_.clear();
    for (const Prepared& p : prepared_) {
        if (p.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, p.pipeline, nullptr);
        }
    }
    prepared_.clear();
}

void PipelineCacheVk::start_prepare() {
    if (pipeline_seeds().empty()) {
        return;
    }
    const auto prepare_started_at = std::chrono::steady_clock::now();
    for (unsigned t = 0; t < 2u; ++t) {
        prepare_threads_.emplace_back([this, prepare_started_at] {
            galaxy::host::name_current_thread(L"Nebula Pipeline Prepare");
            const auto seeds = pipeline_seeds();
            while (!prepare_stop_.load(std::memory_order_relaxed)) {
                const std::size_t i = prepare_next_.fetch_add(1u, std::memory_order_relaxed);
                if (i >= seeds.size()) {
                    if (i == seeds.size()) {
                        std::fprintf(stderr, "[pipelines] %zu prepared at %llu ms\n", seeds.size(),
                                     static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - prepare_started_at).count()));
                    }
                    return;
                }
                const RenderStateKey rs = for_device_blending(native_render_state(seed_render_state(seeds[i])));
                const VkPipeline pipeline = create_native(seeds[i].shader, rs, native_shader_variant(rs.blend_bits));
                const std::lock_guard guard(prepared_lock_);
                prepared_.push_back({seeds[i].shader, rs, pipeline});
                prepare_ready_.store(true, std::memory_order_release);
            }
        });
    }
}

void PipelineCacheVk::merge_prepared() {
    if (!prepare_started_) {
        prepare_started_ = true;
        start_prepare();
    }
    if (!prepare_ready_.load(std::memory_order_acquire)) {
        return;
    }
    std::vector<Prepared> done;
    {
        const std::lock_guard guard(prepared_lock_);
        done.swap(prepared_);
        prepare_ready_.store(false, std::memory_order_relaxed);
    }
    for (const Prepared& p : done) {
        const unsigned variant = native_shader_variant(p.rs.blend_bits);
        const std::uint32_t id = p.shader * kNativeVariants + variant;
        auto& bucket = native_pipelines_[combine64(hash_key_words(&p.rs, sizeof(p.rs)), id)];
        bool have = false;
        for (const auto& entry : bucket) {
            have = have || (entry.first.first == id && entry.first.second == p.rs);
        }
        if (have) {
            if (p.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, p.pipeline, nullptr);
            }
        } else {
            bucket.emplace_back(std::make_pair(id, p.rs), p.pipeline);
        }
    }
}

VkPipeline PipelineCacheVk::create_pipeline(const RenderStateKey& rs, VkShaderModule vs, VkShaderModule ps,
                                            bool dual_source, bool export_depth, std::uint32_t compat,
                                            VkResult& result, VkShaderModule hs, VkShaderModule ds) const {
    result = VK_ERROR_INITIALIZATION_FAILED;
    if (vs == VK_NULL_HANDLE || ps == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[4]{};
    std::uint32_t stage_count = 0;
    const auto add_stage = [&](VkShaderStageFlagBits stage, VkShaderModule module) {
        VkPipelineShaderStageCreateInfo& info = stages[stage_count++];
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = stage;
        info.module = module;
        info.pName = "main";
    };
    add_stage(VK_SHADER_STAGE_VERTEX_BIT, vs);
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // A curved surface's patches (kVsKeyCurved): its vertex output variant's hull and domain stages. In the default
    // domain origin (upper left) the hull stage's triangle_cw, VertexOrderCw as glslang writes it, winds each patch's
    // triangles as the patch is wound, as on D3D12, so they cull as the triangles they replace.
    VkPipelineTessellationStateCreateInfo tessellation{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    tessellation.patchControlPoints = 3u;
    const bool curved = (rs.primitive_topology & kTopologyTypeMask) == kTopologyPatch;
    if (curved) {
        const unsigned variant = curved_variant(rs.primitive_topology);
        const VkShaderModule hull = hs;
        const VkShaderModule domain = ds;
        if (hull == VK_NULL_HANDLE || domain == VK_NULL_HANDLE) {
            return VK_NULL_HANDLE;
        }
        topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        add_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, hull);
        add_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, domain);
    }
    add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, ps);

    VkVertexInputBindingDescription binding{0, static_cast<std::uint32_t>(sizeof(RenderVertex)),
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

    // game screen space is y-down against D3D-style NDC, so game front faces are the rasteriser's back faces (the D3D12
    // pipelines make the same swap); the viewport flips y as D3D's does, so clockwise is front as there.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    // D3D clamps fragment depth, a shader-written one too, to the viewport's range and clips primitives to it (game
    // depth clears rely on the clamp: near == far). Without VK_EXT_depth_clip_enable only depth-writing shaders clamp,
    // and their primitives go unclipped in depth.
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    const bool clamp = layout_->depth_clamp && (compat & kCompatNoDepthClamp) == 0u;
    if (clamp && layout_->depth_clip_enable && (compat & kCompatNoDepthClip) == 0u) {
        raster.depthClampEnable = VK_TRUE;
        depth_clip.depthClipEnable = VK_TRUE;
        raster.pNext = &depth_clip;
    } else if (clamp && export_depth) {
        raster.depthClampEnable = VK_TRUE;
    }
    switch (static_cast<CullMode>(rs.cull)) {
    case CullMode::Front: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
    case CullMode::Back: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    default: raster.cullMode = VK_CULL_MODE_NONE; break;
    }
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << rs.samples_log2);

    // Reversed-Z against the game's depth (the projection negates z; RendererVk's viewport maps the game's depth range), so the
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
            // game subtract: dst - src, factors one.
            target.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        } else {
            target.srcColorBlendFactor = src_blend((bb >> 2u) & 0x7u);
            target.dstColorBlendFactor = dst_blend((bb >> 5u) & 0x7u);
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
        if ((bb & (1u << 27u)) != 0u && dual_source && layout_->dual_source_blend &&
            (compat & kCompatNoDualSource) == 0u) {
            // The pixel shader's second output carries the material alpha the colour blends against.
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
        if ((bb & kRenderStateZeroAlpha) != 0u) {  // constant destination alpha 0, drawn single-source
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    } else if ((bb & (1u << 8u)) != 0u && layout_->logic_op && (compat & kCompatNoLogicOp) == 0u) {
        blend.logicOpEnable = VK_TRUE;
        blend.logicOp = logic_op_mode((bb >> 9u) & 0xfu);
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
    info.pTessellationState = curved ? &tessellation : nullptr;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_->layout;
    info.renderPass = layout_->pass_for(rs.samples_log2, false);
    const TestRefusals& refuse = test_refusals();
    if ((refuse.dual_source && dual_source && layout_->dual_source_blend) || (refuse.depth_clip && raster.pNext) ||
        (refuse.depth_clamp && raster.depthClampEnable) || (refuse.logic_op && blend.logicOpEnable) ||
        refuse.native) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        return VK_NULL_HANDLE;
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    result = vkCreateGraphicsPipelines(device_, driver_cache_, 1, &info, nullptr, &pipeline);
    return result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE;
}

std::uint32_t PipelineCacheVk::applicable_compat(const RenderStateKey& rs, bool dual_source, bool export_depth) const {
    std::uint32_t bits = 0;
    if (layout_->depth_clamp && layout_->depth_clip_enable) {
        bits |= kCompatNoDepthClip;
    }
    if (layout_->depth_clamp && (layout_->depth_clip_enable || export_depth)) {
        bits |= kCompatNoDepthClamp;
    }
    if ((rs.blend_bits & 1u) == 0u && (rs.blend_bits & (1u << 8u)) != 0u && layout_->logic_op) {
        bits |= kCompatNoLogicOp;
    }
    if (dual_source && layout_->dual_source_blend) {
        bits |= kCompatNoDualSource;
    }
    return bits;
}

template <typename Make>
VkPipeline PipelineCacheVk::with_fallbacks(Make&& make, std::uint32_t applicable, const char* what,
                                           const std::string& detail, bool* transient) {
    const std::uint32_t base = device_compat_.load(std::memory_order_acquire) & applicable;
    VkResult result = VK_SUCCESS;
    VkPipeline pipeline = make(base, result);
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    refusals_.fetch_add(1u, std::memory_order_relaxed);
    report_refusal(what, detail, result, base);
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
        result == VK_ERROR_DEVICE_LOST) {
        if (transient != nullptr) {
            *transient = true;
        }
        return VK_NULL_HANDLE;  // not this pipeline's form
    }
    // Without each optional feature it uses, cheapest loss first, then without all of them.
    for (std::uint32_t bit = 0; bit < kCompatBits; ++bit) {
        const std::uint32_t flag = 1u << bit;
        if ((applicable & flag) == 0u || (base & flag) != 0u) {
            continue;
        }
        VkResult retry = VK_SUCCESS;
        pipeline = make(base | flag, retry);
        if (pipeline != VK_NULL_HANDLE) {
            std::fprintf(stderr, "[vk-compat] %s accepted without %s (%s)\n", what, compat_name(flag), detail.c_str());
            learn(flag);
            return pipeline;
        }
    }
    if (std::popcount(applicable & ~base) > 1) {
        VkResult retry = VK_SUCCESS;
        pipeline = make(applicable, retry);
        if (pipeline != VK_NULL_HANDLE) {
            std::fprintf(stderr, "[vk-compat] %s accepted without its optional features (%s)\n", what,
                         detail.c_str());
            return pipeline;
        }
    }
    return VK_NULL_HANDLE;
}

void PipelineCacheVk::report_refusal(const char* what, const std::string& detail, VkResult result,
                                     std::uint32_t compat) {
    const std::uint32_t count = refusals_.load(std::memory_order_relaxed);
    if (count <= 16u) {
        std::fprintf(stderr, "[vk-compat] driver refused %s: VkResult %d, without 0x%x (%s)\n", what,
                     static_cast<int>(result), compat, detail.c_str());
    }
    if (refusal_reported_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    // The first refusal: everything needed to find out why, in the game's log.
    std::fprintf(stderr, "[vk-compat] device: %s\n", vk::describe(vk::Context::get().identity()).c_str());
    const std::string driver = host_window::driver_log();
    if (!driver.empty()) {
        std::fprintf(stderr, "[vk-compat] driver log:\n%s[vk-compat] end of driver log\n", driver.c_str());
    }
    std::fflush(stderr);
}

void PipelineCacheVk::learn(std::uint32_t compat_bit) {
    const std::size_t index = compat_bit != 0u ? static_cast<std::size_t>(std::countr_zero(compat_bit)) : kCompatBits;
    // Two pipelines fixed the same way: leave it out of every later one (and the next launches').
    if (fixes_[index].fetch_add(1u, std::memory_order_acq_rel) + 1u != 2u) {
        return;
    }
    if (compat_bit != 0u) {
        device_compat_.fetch_or(compat_bit, std::memory_order_acq_rel);
        std::fprintf(stderr, "[vk-compat] building every pipeline without %s on this driver\n",
                     compat_name(compat_bit));
    }
    save_compat();
}

void PipelineCacheVk::load_compat() {
    std::ifstream in(cache_dir_ / "vk_compat.txt");
    std::string line;
    if (!std::getline(in, line) || line != "nebula-vk-compat 3" || !std::getline(in, line) ||
        line != "driver " + driver_key_) {
        return;  // another driver build (or none yet): earlier refusals are dropped
    }
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string name;
        std::uint64_t value = 0;
        if ((fields >> name >> std::hex >> value) && name == "compat") {
            device_compat_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
        }
    }
}

void PipelineCacheVk::save_compat() {
    std::ostringstream out;
    out << "nebula-vk-compat 3\ndriver " << driver_key_ << "\n" << std::hex;
    out << "compat " << device_compat_.load(std::memory_order_acquire) << "\n";
    const std::string text = out.str();
    replace_file(cache_dir_ / "vk_compat.txt", [&](std::ofstream& file) {
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    });
}

// vk_pipelines.bin: the driver's VkPipelineCache data (it checks its own header; rejected data starts it empty).
void PipelineCacheVk::load_driver_cache() {
    std::vector<char> data;
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
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_) != VK_SUCCESS) {
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        (void)vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_);
    }
}

void PipelineCacheVk::save_driver_cache() {
    std::size_t size = 0;
    if (driver_cache_ == VK_NULL_HANDLE || vkGetPipelineCacheData(device_, driver_cache_, &size, nullptr) != VK_SUCCESS ||
        size == 0u) {
        return;
    }
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device_, driver_cache_, &size, data.data()) == VK_SUCCESS) {
        replace_file(cache_dir_ / "vk_pipelines.bin", [&](std::ofstream& out) {
            out.write(data.data(), static_cast<std::streamsize>(size));
        });
    }
}

bool PipelineCacheVk::initialize(VkDevice device, const vk::RenderLayout* layout, const std::filesystem::path& cache_dir) {
    device_ = device;
    layout_ = layout;
    cache_dir_ = cache_dir;
    std::error_code error;
    std::filesystem::create_directories(cache_dir_, error);
    driver_key_ = vk::driver_key(vk::Context::get().identity());
    load_compat();
    save_compat();
    load_driver_cache();
    return true;
}

}  // namespace galaxy::render
