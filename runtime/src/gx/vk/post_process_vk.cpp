// SPDX-License-Identifier: GPL-3.0-only
// Presentation post-processing on Vulkan (post_process_vk.h).
#include "galaxy/gx/vk/post_process_vk.h"

#include "galaxy/gx/vk/shader_compile_vk.h"

#include "AreaTex.h"
#include "SearchTex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace galaxy::gx {
// The shader sources cmake/post_shader_sources.cmake embeds.
extern const unsigned char galaxy_post_post_process_hlsl[];
extern const unsigned char galaxy_post_smaa_hlsl[];
extern const unsigned char galaxy_post_ffx_a_h[];
extern const unsigned char galaxy_post_ffx_fsr1_h[];
}  // namespace galaxy::gx

namespace galaxy::gx::vk {
namespace {

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kEdgesFormat = VK_FORMAT_R8G8_UNORM;
constexpr VkColorComponentFlags kRgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
constexpr VkColorComponentFlags kRgba = kRgb | VK_COLOR_COMPONENT_A_BIT;
constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// post_process.hlsl's registers: textures t0-t2 in set 0, the linear and point samplers in set 1, the constants
// pushed.
constexpr RegisterBinding kBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1},
                                         {"t2", 0, 2},       {"s0", 1, 0}, {"s1", 1, 1}};

enum class Library { kNone, kSmaa, kFsr };

const char* text_of(const unsigned char* bytes) {
    return reinterpret_cast<const char*>(bytes);
}

// One pass's source, composed as CMakeLists.txt's galaxy_post_shader composes the D3D12 build's fxc inputs.
std::string compose(const char* pass, const char* preset, Library library) {
    std::string text = std::string("#define ") + pass + " 1\n";
    if (preset != nullptr) {
        text += std::string("#define ") + preset + " 1\n";
    }
    text += "#define POST_SECTION 1\n";
    text += text_of(galaxy_post_post_process_hlsl);
    text += '\n';
    if (library == Library::kSmaa) {
        text += text_of(galaxy_post_smaa_hlsl);
        text += '\n';
    } else if (library == Library::kFsr) {
        text += text_of(galaxy_post_ffx_a_h);
        text += "\n#undef POST_SECTION\n#define POST_SECTION 2\n";
        text += text_of(galaxy_post_post_process_hlsl);
        text += '\n';
        text += text_of(galaxy_post_ffx_fsr1_h);
        text += '\n';
    }
    text += "#undef POST_SECTION\n#define POST_SECTION 3\n";
    text += text_of(galaxy_post_post_process_hlsl);
    text += '\n';
    return text;
}

const char* smaa_preset(unsigned quality) {
    static constexpr const char* kPresets[4] = {"SMAA_PRESET_LOW", "SMAA_PRESET_MEDIUM", "SMAA_PRESET_HIGH",
                                                "SMAA_PRESET_ULTRA"};
    return kPresets[std::min(quality, 3u)];
}

VkSampler create_sampler(VkDevice device, VkFilter filter) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    check(vkCreateSampler(device, &info, nullptr, &sampler), "post-process sampler");
    return sampler;
}

// The depth-stencil format whose stencil marks SMAA's edges; none when the device has neither.
VkFormat stencil_format() {
    for (const VkFormat format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(Context::get().physical_device(), format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

// SMAA's edges pass (clear colour and stencil, keep both) or weights pass (clear colour, read the stencil).
VkRenderPass create_smaa_pass(VkDevice device, VkFormat color_format, VkFormat stencil, bool edges) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = color_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = stencil;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = edges ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilStoreOp = edges ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout =
        edges ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    // Earlier passes' writes (the scene copy, the edges and their stencil) before these draws; the target is read
    // by the next pass's pixel shader.
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    VkRenderPass pass = VK_NULL_HANDLE;
    check(vkCreateRenderPass(device, &info, nullptr, &pass), "SMAA render pass");
    return pass;
}

// A D3D-style viewport over `rect` (y flipped by a negative height), as the post shaders' fullscreen triangle wants.
VkViewport flipped(const VkRect2D& rect) {
    return {static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y + static_cast<std::int32_t>(rect.extent.height)),
            static_cast<float>(rect.extent.width), -static_cast<float>(rect.extent.height), 0.0f, 1.0f};
}

void layout_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
                    VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

template <std::size_t N>
void set_reason(std::array<char, N>& out, const char* text) noexcept {
    std::snprintf(out.data(), out.size(), "%s", text);
}

}  // namespace

void PostProcessorVk::initialize(const PostProcessSettings& settings, VkRenderPass efb_pass,
                                 VkRenderPass color_pass) {
    shutdown();
    settings_ = settings;
    settings_.smaa_quality = std::min<std::uint8_t>(settings_.smaa_quality, 3u);
    status_ = {};
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
    set_reason(status_.smaa_reason, "COMPILING");
    set_reason(status_.fxaa_reason, "COMPILING");
    efb_pass_ = efb_pass;
    color_pass_ = color_pass;
    stencil_format_ = stencil_format();
    compiled_.store(false, std::memory_order_relaxed);
    const unsigned quality = settings_.smaa_quality;
    worker_ = std::thread([this, quality] { compile_programs(quality, false); });
}

void PostProcessorVk::compile_programs(unsigned smaa_quality, bool quality_only) {
    struct Source {
        Program program;
        const char* pass;
        Library library;
        bool quality_preset;  // SMAA: the chosen quality's preset, else HIGH (the D3D12 build's)
        ShaderStage stage;
    };
    static constexpr Source kSources[] = {
        {kFullscreenVs, "PASS_RESAMPLE", Library::kNone, false, ShaderStage::Vertex},
        {kResamplePs, "PASS_RESAMPLE", Library::kNone, false, ShaderStage::Fragment},
        {kFxaaPs, "PASS_FXAA", Library::kNone, false, ShaderStage::Fragment},
        {kEasuPs, "PASS_FSR_EASU", Library::kFsr, false, ShaderStage::Fragment},
        {kRcasPs, "PASS_FSR_RCAS", Library::kFsr, false, ShaderStage::Fragment},
        {kRcasScaledPs, "PASS_FSR_RCAS_SCALED", Library::kFsr, false, ShaderStage::Fragment},
        {kSmaaEdgesVs, "PASS_SMAA_EDGES", Library::kSmaa, false, ShaderStage::Vertex},
        {kSmaaEdgesPs, "PASS_SMAA_EDGES", Library::kSmaa, true, ShaderStage::Fragment},
        {kSmaaWeightsVs, "PASS_SMAA_WEIGHTS", Library::kSmaa, true, ShaderStage::Vertex},
        {kSmaaWeightsPs, "PASS_SMAA_WEIGHTS", Library::kSmaa, true, ShaderStage::Fragment},
        {kSmaaBlendVs, "PASS_SMAA_BLEND", Library::kSmaa, false, ShaderStage::Vertex},
        {kSmaaBlendPs, "PASS_SMAA_BLEND", Library::kSmaa, false, ShaderStage::Fragment},
        {kDebandPs, "PASS_DEBAND", Library::kNone, false, ShaderStage::Fragment},
    };
    for (const Source& source : kSources) {
        if (quality_only && !source.quality_preset) {
            continue;
        }
        const char* preset = source.library != Library::kSmaa ? nullptr
            : source.quality_preset                          ? smaa_preset(smaa_quality)
                                                             : "SMAA_PRESET_HIGH";
        std::string error;
        spirv_[source.program] =
            compile_hlsl(annotate(compose(source.pass, preset, source.library), kBindings), source.stage,
                         source.stage == ShaderStage::Vertex ? "VSMain" : "PSMain", {}, error);
        if (spirv_[source.program].empty() && compile_error_[0] == '\0') {
            std::snprintf(compile_error_.data(), compile_error_.size(), "%s %s: %s", source.pass,
                          source.stage == ShaderStage::Vertex ? "VS" : "PS", error.c_str());
        }
    }
    compiled_.store(true, std::memory_order_release);
}

void PostProcessorVk::set_present_pass(VkRenderPass pass) {
    if (pass == present_pass_) {
        return;
    }
    const VkDevice device = Context::get().device();
    for (VkPipeline pipeline : {to_present_.easu, to_present_.rcas, to_present_.rcas_scaled, to_present_.resample}) {
        if (pipeline != VK_NULL_HANDLE) {
            Context::get().defer([device, pipeline] { vkDestroyPipeline(device, pipeline, nullptr); });
        }
    }
    to_present_ = {};
    present_pass_ = pass;
    if (built_ && present_pass_ != VK_NULL_HANDLE) {
        if (Context::get().gather_extended()) {
            to_present_.easu = create_pipeline(kFullscreenVs, kEasuPs, present_pass_, kRgba, 0);
        }
        to_present_.rcas = create_pipeline(kFullscreenVs, kRcasPs, present_pass_, kRgba, 0);
        to_present_.rcas_scaled = create_pipeline(kFullscreenVs, kRcasScaledPs, present_pass_, kRgba, 0);
        to_present_.resample = create_pipeline(kFullscreenVs, kResamplePs, present_pass_, kRgba, 0);
    }
}

VkShaderModule PostProcessorVk::module(Program program) const {
    const std::vector<std::uint32_t>& code = spirv_[program];
    if (code.empty()) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * sizeof(std::uint32_t);
    info.pCode = code.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    return vkCreateShaderModule(Context::get().device(), &info, nullptr, &shader) == VK_SUCCESS ? shader
                                                                                                 : VK_NULL_HANDLE;
}

// stencil: 0 none, 1 writes 1 where the pixel shader keeps the pixel, 2 draws only where the stencil is 1 (SMAA's
// edge mask).
VkPipeline PostProcessorVk::create_pipeline(Program vs_program, Program ps_program, VkRenderPass pass,
                                            VkColorComponentFlags write_mask, int stencil) const {
    if (pass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const VkDevice device = Context::get().device();
    const VkShaderModule vs = module(vs_program);
    const VkShaderModule ps = module(ps_program);
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vs != VK_NULL_HANDLE && ps != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                     "VSMain", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                     "PSMain", nullptr};
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        if (stencil != 0) {
            depth.stencilTestEnable = VK_TRUE;
            VkStencilOpState op{};
            op.failOp = VK_STENCIL_OP_KEEP;
            op.depthFailOp = VK_STENCIL_OP_KEEP;
            op.passOp = stencil == 1 ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP;
            op.compareOp = stencil == 1 ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL;
            op.compareMask = 0xFFu;
            op.writeMask = 0xFFu;
            op.reference = 1u;
            depth.front = op;
            depth.back = op;
        }
        VkPipelineColorBlendAttachmentState target{};
        target.colorWriteMask = write_mask;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &target;
        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex_input;
        info.pInputAssemblyState = &input_assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout_;
        info.renderPass = pass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(device, vs, nullptr);
    if (ps != VK_NULL_HANDLE) vkDestroyShaderModule(device, ps, nullptr);
    return pipeline;
}

bool PostProcessorVk::build_pipelines() {
    const VkDevice device = Context::get().device();
    {
        VkDescriptorSetLayoutBinding images[3]{};
        for (std::uint32_t i = 0; i < 3u; ++i) {
            images[i] = {i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 3;
        info.pBindings = images;
        check(vkCreateDescriptorSetLayout(device, &info, nullptr, &image_layout_), "post-process image layout");
        samplers_[0] = create_sampler(device, VK_FILTER_LINEAR);
        samplers_[1] = create_sampler(device, VK_FILTER_NEAREST);
        const VkDescriptorSetLayoutBinding samplers[2] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &samplers_[0]},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &samplers_[1]}};
        info.bindingCount = 2;
        info.pBindings = samplers;
        check(vkCreateDescriptorSetLayout(device, &info, nullptr, &sampler_layout_), "post-process sampler layout");
        const VkDescriptorSetLayout sets[2] = {image_layout_, sampler_layout_};
        const VkPushConstantRange push{kStages, 0, sizeof(PostConstants)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = sets;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device, &layout_info, nullptr, &layout_), "post-process pipeline layout");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &sampler_pool_), "post-process sampler pool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = sampler_pool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &sampler_layout_;
        check(vkAllocateDescriptorSets(device, &allocate, &sampler_set_), "post-process sampler set");
    }
    // The scaling passes are required (EASU where the device gathers single channels: without it enlarging is the
    // box filter's); each AA pass is optional and only reported when it cannot be built.
    easu_ = Context::get().gather_extended();
    if (easu_) {
        to_color_.easu = create_pipeline(kFullscreenVs, kEasuPs, color_pass_, kRgba, 0);
    }
    to_color_.rcas = create_pipeline(kFullscreenVs, kRcasPs, color_pass_, kRgba, 0);
    to_color_.rcas_scaled = create_pipeline(kFullscreenVs, kRcasScaledPs, color_pass_, kRgba, 0);
    to_color_.resample = create_pipeline(kFullscreenVs, kResamplePs, color_pass_, kRgba, 0);
    deband_ = create_pipeline(kFullscreenVs, kDebandPs, color_pass_, kRgba, 0);
    built_ = true;
    const VkRenderPass present = std::exchange(present_pass_, VK_NULL_HANDLE);
    set_present_pass(present);
    if ((easu_ && to_color_.easu == VK_NULL_HANDLE) || to_color_.rcas == VK_NULL_HANDLE ||
        to_color_.rcas_scaled == VK_NULL_HANDLE || to_color_.resample == VK_NULL_HANDLE) {
        return false;
    }
    if (stencil_format_ != VK_FORMAT_UNDEFINED) {
        edges_pass_ = create_smaa_pass(device, kEdgesFormat, stencil_format_, true);
        weights_pass_ = create_smaa_pass(device, kColorFormat, stencil_format_, false);
        smaa_edges_ = create_pipeline(kSmaaEdgesVs, kSmaaEdgesPs, edges_pass_, kRgba, 1);
        smaa_weights_ = create_pipeline(kSmaaWeightsVs, kSmaaWeightsPs, weights_pass_, kRgba, 2);
    }
    smaa_blend_ = create_pipeline(kSmaaBlendVs, kSmaaBlendPs, color_pass_, kRgba, 0);
    smaa_blend_rgb_ = create_pipeline(kSmaaBlendVs, kSmaaBlendPs, efb_pass_, kRgb, 0);
    fxaa_ = create_pipeline(kFullscreenVs, kFxaaPs, color_pass_, kRgba, 0);
    fxaa_rgb_ = create_pipeline(kFullscreenVs, kFxaaPs, efb_pass_, kRgb, 0);
    const bool smaa = smaa_edges_ != VK_NULL_HANDLE && smaa_weights_ != VK_NULL_HANDLE &&
        smaa_blend_ != VK_NULL_HANDLE && smaa_blend_rgb_ != VK_NULL_HANDLE;
    const bool fxaa = fxaa_ != VK_NULL_HANDLE && fxaa_rgb_ != VK_NULL_HANDLE;
    status_.smaa_ok = smaa;
    status_.fxaa_ok = fxaa;
    set_reason(status_.smaa_reason, smaa ? "" : stencil_format_ == VK_FORMAT_UNDEFINED ? "NO STENCIL" : "PIPELINES");
    set_reason(status_.fxaa_reason, fxaa ? "" : "PIPELINES");
    return true;
}

void PostProcessorVk::destroy_pipelines() noexcept {
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    for (VkPipeline pipeline : {smaa_edges_, smaa_weights_, smaa_blend_, smaa_blend_rgb_, fxaa_, fxaa_rgb_, deband_,
                                to_color_.easu, to_color_.rcas, to_color_.rcas_scaled, to_color_.resample,
                                to_present_.easu, to_present_.rcas, to_present_.rcas_scaled, to_present_.resample}) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    }
    smaa_edges_ = smaa_weights_ = smaa_blend_ = smaa_blend_rgb_ = fxaa_ = fxaa_rgb_ = deband_ = VK_NULL_HANDLE;
    to_color_ = {};
    to_present_ = {};
    for (VkRenderPass pass : {edges_pass_, weights_pass_}) {
        if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
    }
    edges_pass_ = weights_pass_ = VK_NULL_HANDLE;
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout_, nullptr);
    if (sampler_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, sampler_pool_, nullptr);
    for (VkDescriptorSetLayout set_layout : {image_layout_, sampler_layout_}) {
        if (set_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    }
    for (VkSampler& sampler : samplers_) {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    layout_ = VK_NULL_HANDLE;
    sampler_pool_ = VK_NULL_HANDLE;
    sampler_set_ = VK_NULL_HANDLE;
    image_layout_ = sampler_layout_ = VK_NULL_HANDLE;
    built_ = false;
}

void PostProcessorVk::shutdown() noexcept {
    if (worker_.joinable()) {
        worker_.join();
    }
    ready_ = false;
    status_.initialized = false;
    status_.smaa_ok = false;
    status_.fxaa_ok = false;
    // The renderer waits for the device before its shutdown; the targets go with their deferred destruction.
    destroy_pipelines();
    area_texture_.reset();
    search_texture_.reset();
    edges_ = weights_ = stencil_ = antialiased_ = scaled_ = debanded_ = {};
    spirv_ = {};
    compile_error_ = {};
}

bool PostProcessorVk::poll_ready() {
    if (ready_ || built_ || !compiled_.load(std::memory_order_acquire)) {
        return ready_;
    }
    worker_.join();
    if (compile_error_[0] != '\0') {
        std::cerr << "[post-process] compile failed: " << compile_error_.data() << '\n';
    }
    if (!build_pipelines()) {
        std::cerr << "[post-process] scaling pipelines failed; plain blit\n";
        set_reason(status_.skip_reason, "INIT FAILED");
        destroy_pipelines();
        built_ = true;  // not again
        return false;
    }
    self_test();
    status_.initialized = true;
    ready_ = true;
    std::cerr << "[post-process] self-test smaa=" << (status_.smaa_ok ? "pass" : "fail")
              << (status_.smaa_ok ? "" : " smaa-reason=") << status_.smaa_reason.data()
              << " fxaa=" << (status_.fxaa_ok ? "pass" : "fail")
              << (status_.fxaa_ok ? "" : " fxaa-reason=") << status_.fxaa_reason.data() << '\n';
    return true;
}

void PostProcessorVk::apply_settings(const PostProcessSettings& settings) {
    PostProcessSettings next = settings;
    next.smaa_quality = std::min<std::uint8_t>(next.smaa_quality, 3u);
    if (ready_ && status_.smaa_ok && next.smaa_quality != settings_.smaa_quality) {
        // The quality's edges and weights shaders, compiled here: a settings change, not a frame.
        compile_programs(next.smaa_quality, true);
        const VkPipeline edges = create_pipeline(kSmaaEdgesVs, kSmaaEdgesPs, edges_pass_, kRgba, 1);
        const VkPipeline weights = create_pipeline(kSmaaWeightsVs, kSmaaWeightsPs, weights_pass_, kRgba, 2);
        if (edges == VK_NULL_HANDLE || weights == VK_NULL_HANDLE) {
            std::cerr << "[post-process] SMAA quality " << unsigned(next.smaa_quality) << " pipelines failed; keeping "
                      << unsigned(settings_.smaa_quality) << '\n';
            const VkDevice device = Context::get().device();
            if (edges != VK_NULL_HANDLE) vkDestroyPipeline(device, edges, nullptr);
            if (weights != VK_NULL_HANDLE) vkDestroyPipeline(device, weights, nullptr);
            next.smaa_quality = settings_.smaa_quality;
        } else {
            const VkDevice device = Context::get().device();
            const VkPipeline old_edges = std::exchange(smaa_edges_, edges);
            const VkPipeline old_weights = std::exchange(smaa_weights_, weights);
            Context::get().defer([device, old_edges, old_weights] {
                vkDestroyPipeline(device, old_edges, nullptr);
                vkDestroyPipeline(device, old_weights, nullptr);
            });
        }
    }
    settings_ = next;
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
}

bool PostProcessorVk::ensure_target(Target& target, unsigned width, unsigned height, VkFormat format,
                                    VkRenderPass pass, bool with_stencil) {
    if (target.texture && target.width == width && target.height == height && target.texture->format == format) {
        return true;
    }
    target = {};
    const bool stencil = format == stencil_format_;
    const VkImageUsageFlags usage = stencil
        ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    std::shared_ptr<Texture> texture =
        Context::get().create_texture(width, height, 1u, format, usage, true, "post-process target");
    if (!texture) {
        return false;
    }
    if (pass != VK_NULL_HANDLE) {
        const VkImageView views[2] = {texture->target_view,
                                      with_stencil ? stencil_.texture->target_view : VK_NULL_HANDLE};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = pass;
        info.attachmentCount = with_stencil ? 2u : 1u;
        info.pAttachments = views;
        info.width = width;
        info.height = height;
        info.layers = 1;
        if (vkCreateFramebuffer(Context::get().device(), &info, nullptr, &texture->framebuffer) != VK_SUCCESS) {
            return false;
        }
    }
    target.texture = std::move(texture);
    target.width = width;
    target.height = height;
    return true;
}

bool PostProcessorVk::ensure_smaa_targets(unsigned width, unsigned height) {
    if (stencil_.texture && stencil_.width == width && stencil_.height == height && edges_.texture &&
        weights_.texture) {
        return true;
    }
    edges_ = weights_ = stencil_ = {};
    return ensure_target(stencil_, width, height, stencil_format_, VK_NULL_HANDLE, false) &&
           ensure_target(edges_, width, height, kEdgesFormat, edges_pass_, true) &&
           ensure_target(weights_, width, height, kColorFormat, weights_pass_, true);
}

void PostProcessorVk::begin_pass(VkCommandBuffer commands, VkRenderPass pass, const Target& target,
                                 bool clear_stencil) const {
    VkClearValue clears[2]{};
    clears[1].depthStencil = {1.0f, 0u};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass;
    begin.framebuffer = target.texture->framebuffer;
    begin.renderArea = {{0, 0}, {target.width, target.height}};
    begin.clearValueCount = clear_stencil ? 2u : 1u;
    begin.pClearValues = clears;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void PostProcessorVk::draw(VkCommandBuffer commands, VkPipeline pipeline, std::initializer_list<VkImageView> inputs,
                           const VkRect2D& viewport, const PostConstants& constants, const SetAllocator& allocate,
                           const VkRect2D* scissor) const {
    const VkDescriptorSet set = allocate(image_layout_);
    std::array<VkDescriptorImageInfo, 3> images{};
    std::array<VkWriteDescriptorSet, 3> writes{};
    std::uint32_t count = 0;
    for (const VkImageView view : inputs) {
        images[count] = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[count] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[count].dstSet = set;
        writes[count].dstBinding = count;
        writes[count].descriptorCount = 1;
        writes[count].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[count].pImageInfo = &images[count];
        ++count;
    }
    vkUpdateDescriptorSets(Context::get().device(), count, writes.data(), 0, nullptr);
    const VkDescriptorSet sets[2] = {set, sampler_set_};
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(PostConstants), &constants);
    const VkViewport vp = flipped(viewport);
    vkCmdSetViewport(commands, 0, 1, &vp);
    vkCmdSetScissor(commands, 0, 1, scissor != nullptr ? scissor : &viewport);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

// SMAA's edges and weights, or nothing for FXAA, from `source` (a shader-readable copy of the picture).
bool PostProcessorVk::record_aa(VkCommandBuffer commands, AaRun pass, VkImageView source, unsigned width,
                                unsigned height, const SetAllocator& allocate) {
    if (pass != AaRun::kSmaa) {
        return true;
    }
    if (!ensure_smaa_targets(width, height)) {
        return false;
    }
    const VkRect2D full{{0, 0}, {width, height}};
    PostConstants c{};
    set_post_input_size(c, width, height);
    begin_pass(commands, edges_pass_, edges_, true);
    draw(commands, smaa_edges_, {source}, full, c, allocate);
    vkCmdEndRenderPass(commands);
    begin_pass(commands, weights_pass_, weights_, false);
    draw(commands, smaa_weights_, {edges_.texture->view, area_texture_->view, search_texture_->view}, full, c,
         allocate);
    vkCmdEndRenderPass(commands);
    return true;
}

AaRun PostProcessorVk::prepare_scene_aa(VkCommandBuffer commands, const Texture& color, unsigned width,
                                        unsigned height, unsigned picture_width, unsigned picture_height,
                                        unsigned display_width, unsigned display_height,
                                        const SetAllocator& allocate) {
    if (!ready_ || width == 0u || height == 0u) {
        return AaRun::kNone;
    }
    const PostProcessPlan plan = plan_post_process(picture_width, picture_height, display_width, display_height,
                                                   settings_, status_.smaa_ok, status_.fxaa_ok);
    AaRun pass = plan.smaa ? AaRun::kSmaa : plan.fxaa ? AaRun::kFxaa : AaRun::kNone;
    // The passes read a copy of the scene and write the EFB.
    if (pass != AaRun::kNone && !ensure_target(antialiased_, width, height, kColorFormat, color_pass_, false)) {
        pass = AaRun::kNone;
    }
    if (pass != AaRun::kNone) {
        layout_barrier(commands, color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT);
        layout_barrier(commands, antialiased_.texture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {width, height, 1};
        vkCmdCopyImage(commands, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, antialiased_.texture->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        layout_barrier(commands, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        layout_barrier(commands, antialiased_.texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT);
        if (!record_aa(commands, pass, antialiased_.texture->view, width, height, allocate)) {
            pass = status_.fxaa_ok ? AaRun::kFxaa : AaRun::kNone;
        }
    }
    ++status_.frames;
    status_.last_run = pass;
    status_.last_wanted = plan.aa_wanted;
    status_.last_supersampled = plan.supersampled;
    status_.smaa_frames += pass == AaRun::kSmaa;
    status_.fxaa_frames += pass == AaRun::kFxaa;
    if (plan.aa_wanted) {
        ++status_.aa_wanted_frames;
        if (pass == AaRun::kNone) {
            ++status_.aa_skipped_frames;
            set_reason(status_.skip_reason, plan.smaa || plan.fxaa ? "NO MEMORY" : "FAILED");
        }
    }
    return pass;
}

void PostProcessorVk::finish_scene_aa(VkCommandBuffer commands, AaRun pass, unsigned width, unsigned height,
                                      const SetAllocator& allocate) {
    if (pass == AaRun::kNone) {
        return;
    }
    const VkRect2D full{{0, 0}, {width, height}};
    PostConstants c{};
    set_post_input_size(c, width, height);
    if (pass == AaRun::kFxaa) {
        draw(commands, fxaa_rgb_, {antialiased_.texture->view}, full, c, allocate);
    } else {
        draw(commands, smaa_blend_rgb_, {antialiased_.texture->view, weights_.texture->view}, full, c, allocate);
    }
}

void PostProcessorVk::hold_scene_targets(std::vector<std::shared_ptr<Texture>>& held) const {
    for (const Target* target : {&antialiased_, &edges_, &weights_, &stencil_}) {
        if (target->texture &&
            std::find(held.begin(), held.end(), target->texture) == held.end()) {
            held.push_back(target->texture);
        }
    }
}

PostProcessorVk::PresentStep PostProcessorVk::prepare_present(VkCommandBuffer commands, const Texture& source,
                                                              unsigned source_width, unsigned source_height,
                                                              unsigned target_width, unsigned target_height,
                                                              bool present, const SetAllocator& allocate) {
    const PassPipelines& last = present ? to_present_ : to_color_;
    // The scene was antialiased before the 2D layer (prepare_scene_aa).
    PostProcessSettings scaling = settings_;
    scaling.aa = PostProcessSettings::Aa::kOff;
    if (!easu_) {
        scaling.upscaler = PostProcessSettings::Upscaler::kBilinear;
    }
    PostProcessPlan plan = plan_post_process(source_width, source_height, target_width, target_height, scaling,
                                             status_.smaa_ok, status_.fxaa_ok);
    PresentStep step{};
    set_post_input_size(step.constants, source_width, source_height);
    // The picture's 8-bit steps smoothed at its own size, before any scaling (PASS_DEBAND).
    VkImageView input = source.view;
    if (deband_ != VK_NULL_HANDLE && deband_wanted() &&
        ensure_target(debanded_, source_width, source_height, kColorFormat, color_pass_, false)) {
        PostConstants d = step.constants;
        set_deband_constants(d, source_height);
        begin_pass(commands, color_pass_, debanded_, false);
        draw(commands, deband_, {input}, VkRect2D{{0, 0}, {source_width, source_height}}, d, allocate);
        vkCmdEndRenderPass(commands);
        input = debanded_.texture->view;
    }
    step.input = input;
    // Enlarging on both axes with sharpening: one pass straight into the target.
    if (plan.resample && plan.rcas && last.rcas_scaled != VK_NULL_HANDLE && target_width >= source_width &&
        target_height >= source_height) {
        set_fsr_rcas_constants(step.constants, settings_.sharpness);
        step.constants.params[0] = static_cast<float>(source_width);
        step.constants.params[1] = static_cast<float>(source_height);
        step.constants.params[2] = static_cast<float>(target_width);
        step.constants.params[3] = static_cast<float>(target_height);
        step.pipeline = last.rcas_scaled;
        return step;
    }
    // A window-sized intermediate for a scaling step that RCAS then reads.
    if ((plan.easu || plan.resample) && plan.rcas &&
        !ensure_target(scaled_, target_width, target_height, kColorFormat, color_pass_, false)) {
        plan.rcas = false;
        plan.resample = !plan.easu;
        plan.resample_to_target = plan.resample;
    }
    const VkRect2D window{{0, 0}, {target_width, target_height}};
    if (plan.easu) {
        set_fsr_easu_constants(step.constants, static_cast<float>(source_width), static_cast<float>(source_height),
                               static_cast<float>(target_width), static_cast<float>(target_height));
        if (!plan.rcas) {
            step.pipeline = last.easu;
            return step;
        }
        begin_pass(commands, color_pass_, scaled_, false);
        draw(commands, to_color_.easu, {input}, window, step.constants, allocate);
        vkCmdEndRenderPass(commands);
    } else if (plan.resample) {
        step.constants.params[0] = static_cast<float>(source_width);
        step.constants.params[1] = static_cast<float>(source_height);
        step.constants.params[2] = static_cast<float>(target_width);
        step.constants.params[3] = static_cast<float>(target_height);
        if (plan.resample_to_target) {
            step.pipeline = last.resample;
            return step;
        }
        begin_pass(commands, color_pass_, scaled_, false);
        draw(commands, to_color_.resample, {input}, window, step.constants, allocate);
        vkCmdEndRenderPass(commands);
    }
    // RCAS of the scaled picture (or of the source at its own size).
    if (plan.easu || plan.resample) {
        step.input = scaled_.texture->view;
    }
    step.constants = {};
    set_post_input_size(step.constants, target_width, target_height);
    set_fsr_rcas_constants(step.constants, settings_.sharpness);
    step.constants.params[0] = static_cast<float>(target_width);
    step.constants.params[1] = static_cast<float>(target_height);
    step.pipeline = last.rcas;
    return step;
}

void PostProcessorVk::finish_present(VkCommandBuffer commands, const PresentStep& step, const VkRect2D& viewport,
                                     const SetAllocator& allocate) {
    PostConstants constants = step.constants;
    constants.origin[0] = viewport.offset.x;
    constants.origin[1] = viewport.offset.y;
    draw(commands, step.pipeline, {step.input}, viewport, constants, allocate);
}

void PostProcessorVk::self_test() {
    const auto fail = [&](AaRun pass, const char* why) {
        if (pass != AaRun::kFxaa && status_.smaa_ok) {
            status_.smaa_ok = false;
            set_reason(status_.smaa_reason, why);
        }
        if (pass != AaRun::kSmaa && status_.fxaa_ok) {
            status_.fxaa_ok = false;
            set_reason(status_.fxaa_reason, why);
        }
    };
    if (!status_.smaa_ok && !status_.fxaa_ok) {
        return;
    }
    Context& context = Context::get();
    const VkDevice device = context.device();
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = context.queue_family();
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 24};
    VkDescriptorPoolCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_info.maxSets = 8;
    descriptor_info.poolSizeCount = 1;
    descriptor_info.pPoolSizes = &size;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS ||
        vkCreateDescriptorPool(device, &descriptor_info, nullptr, &descriptors) != VK_SUCCESS ||
        (allocate_info.commandPool = pool, vkAllocateCommandBuffers(device, &allocate_info, &commands)) !=
            VK_SUCCESS) {
        if (descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptors, nullptr);
        if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    const SetAllocator allocate = [&](VkDescriptorSetLayout set_layout) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = descriptors;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &set_layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        check(vkAllocateDescriptorSets(device, &info, &set), "post-process self-test set");
        return set;
    };
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &begin);

    std::vector<std::shared_ptr<Buffer>> staging;
    if (status_.smaa_ok && !create_lookup_textures(commands, staging)) {
        fail(AaRun::kSmaa, "LOOKUP");
    }
    constexpr unsigned kSize = kPostSelfTestSize;
    constexpr unsigned kPitch = kSize * 4u;
    std::array<std::uint8_t, kPitch * kSize> pattern{};
    write_post_self_test_pattern(pattern.data(), kPitch);
    const std::shared_ptr<Buffer> upload =
        context.create_buffer(pattern.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "post-process self-test pattern");
    const std::shared_ptr<Texture> source = context.create_texture(
        kSize, kSize, 1u, kColorFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,
        "post-process self-test source");
    const std::shared_ptr<Buffer> readback = context.create_readback_buffer(2u * pattern.size(), "post-process self-test");
    if (!upload || !source || !readback) {
        vkEndCommandBuffer(commands);
        vkDestroyDescriptorPool(device, descriptors, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    std::memcpy(upload->mapped, pattern.data(), pattern.size());
    layout_barrier(commands, source->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {kSize, kSize, 1};
    vkCmdCopyBufferToImage(commands, upload->buffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    layout_barrier(commands, source->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    const AaRun passes[2] = {AaRun::kSmaa, AaRun::kFxaa};
    bool recorded[2] = {false, false};
    for (unsigned i = 0; i < 2u; ++i) {
        const bool ok = passes[i] == AaRun::kSmaa ? status_.smaa_ok : status_.fxaa_ok;
        if (!ok) continue;
        if (!ensure_target(antialiased_, kSize, kSize, kColorFormat, color_pass_, false) ||
            !record_aa(commands, passes[i], source->view, kSize, kSize, allocate)) {
            fail(passes[i], "NO MEMORY");
            continue;
        }
        const VkRect2D full{{0, 0}, {kSize, kSize}};
        PostConstants c{};
        set_post_input_size(c, kSize, kSize);
        begin_pass(commands, color_pass_, antialiased_, false);
        if (passes[i] == AaRun::kSmaa) {
            draw(commands, smaa_blend_, {source->view, weights_.texture->view}, full, c, allocate);
        } else {
            draw(commands, fxaa_, {source->view}, full, c, allocate);
        }
        vkCmdEndRenderPass(commands);
        layout_barrier(commands, antialiased_.texture->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy out{};
        out.bufferOffset = i * pattern.size();
        out.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        out.imageExtent = {kSize, kSize, 1};
        vkCmdCopyImageToBuffer(commands, antialiased_.texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback->buffer, 1, &out);
        layout_barrier(commands, antialiased_.texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        recorded[i] = true;
    }
    vkEndCommandBuffer(commands);
    const bool done = context.wait(context.submit(commands), 5'000'000'000ull);
    if (!done) {
        // The GPU may still own these: they stay for the process's life.
        fail(AaRun::kNone, "GPU TIMEOUT");
        return;
    }
    vkDestroyDescriptorPool(device, descriptors, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    context.invalidate(*readback);
    for (unsigned i = 0; i < 2u; ++i) {
        if (!recorded[i]) continue;
        const PostSelfTestResult result = check_post_self_test(
            pattern.data(), reinterpret_cast<const std::uint8_t*>(readback->mapped) + i * pattern.size(), kPitch);
        std::cerr << "[post-process] self-test " << (passes[i] == AaRun::kSmaa ? "smaa" : "fxaa")
                  << " changed=" << result.changed << " stray=" << result.stray << '\n';
        if (!result.passed) fail(passes[i], "SELF-TEST");
    }
    // The 32 x 32 targets go now instead of at the first frame.
    edges_ = weights_ = stencil_ = antialiased_ = {};
}

bool PostProcessorVk::create_lookup_textures(VkCommandBuffer commands,
                                             std::vector<std::shared_ptr<Buffer>>& staging) {
    Context& context = Context::get();
    const auto upload = [&](std::shared_ptr<Texture>& texture, const unsigned char* data, unsigned width,
                            unsigned height, unsigned pitch, VkFormat format) {
        texture = context.create_texture(width, height, 1u, format,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,
                                         "SMAA lookup");
        std::shared_ptr<Buffer> buffer = context.create_buffer(static_cast<std::uint64_t>(pitch) * height,
                                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "SMAA lookup upload");
        if (!texture || !buffer) {
            return false;
        }
        std::memcpy(buffer->mapped, data, static_cast<std::size_t>(pitch) * height);
        layout_barrier(commands, texture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(commands, buffer->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        layout_barrier(commands, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT);
        staging.push_back(std::move(buffer));
        return true;
    };
    return upload(area_texture_, areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH,
                  VK_FORMAT_R8G8_UNORM) &&
           upload(search_texture_, searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH,
                  VK_FORMAT_R8_UNORM);
}

}  // namespace galaxy::gx::vk
