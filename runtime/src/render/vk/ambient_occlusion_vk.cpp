// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Vulkan (ambient_occlusion_vk.h).
#include "galaxy/render/vk/ambient_occlusion_vk.h"

#include "galaxy/render/vk/utility_spirv.h"

#include "barriers_vk.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <span>

namespace galaxy::render::vk {
namespace {

constexpr VkFormat kDistanceFormat = VK_FORMAT_R32_SFLOAT;
constexpr VkFormat kOcclusionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// The multiplier: one channel, a colour target (every device draws R8; a tile GPU keeps it compressed); no storage format
// feature needed.
constexpr VkFormat kScaleFormat = VK_FORMAT_R8_UNORM;
// module()'s shaders after the compute passes'.
constexpr unsigned kApplyVs = 4u, kApplyPs = 5u, kUpsamplePs = 6u;
constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// A pass's writes before the next pass's reads.
void written(VkCommandBuffer commands) {
    shader_written(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

// A new work image to GENERAL; an old one's last reads finish before it is written again.
void to_general(VkCommandBuffer commands, Texture& target) {
    image_barrier(commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ShaderRead, gpu::Use::ComputeWrite,
                  gpu::Contents::Discard, 1u);
    target.layout = VK_IMAGE_LAYOUT_GENERAL;
}

}  // namespace

VkShaderModule AmbientOcclusionVk::module(unsigned index) const {
    static constexpr UtilitySpirv kShaders[] = {UtilitySpirv::AoDistanceCs, UtilitySpirv::AoOcclusionCs,
                                                UtilitySpirv::AoDenoiseCs,  UtilitySpirv::AoSsaoCs,
                                                UtilitySpirv::AoApplyVs,    UtilitySpirv::AoApplyPs,
                                                UtilitySpirv::AoUpsamplePs};
    static_assert(kPassCount == kApplyVs && kUpsamplePs + 1u == std::size(kShaders));
    const std::span<const std::uint32_t> code = utility_spirv(kShaders[index]);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size_bytes();
    info.pCode = code.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    return vkCreateShaderModule(Context::get().device(), &info, nullptr, &shader) == VK_SUCCESS ? shader
                                                                                                 : VK_NULL_HANDLE;
}

bool AmbientOcclusionVk::initialize() {
    shutdown();
    const VkDevice device = Context::get().device();
    VkDescriptorSetLayoutBinding bindings[6]{};
    for (std::uint32_t i = 0; i < 6u; ++i) {
        bindings[i] = {i, i < 3u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : i == 3u ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       1, kStages, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 6;
    set_info.pBindings = bindings;
    const VkPushConstantRange push{kStages, 0, sizeof(ao::Constants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout_) != VK_SUCCESS ||
        vkCreatePipelineLayout(device, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        shutdown();
        return false;
    }
    static constexpr const char* kEntries[kPassCount] = {"DepthCS", "AoCS", "DenoiseCS", "SsaoCS"};
    for (unsigned i = 0; i < kPassCount; ++i) {
        const VkShaderModule shader = module(i);
        if (shader == VK_NULL_HANDLE) {
            shutdown();
            return false;
        }
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = shader;
        info.stage.pName = kEntries[i];
        info.layout = layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &compute_[i]);
        vkDestroyShaderModule(device, shader, nullptr);
        if (result != VK_SUCCESS) {
            compute_[i] = VK_NULL_HANDLE;
            std::cerr << "[ambient-occlusion] Vulkan pass " << kEntries[i] << " failed\n";
            shutdown();
            return false;
        }
    }
    // The multiplier's pass: its target's old contents are not loaded (every pixel is drawn), the result stored for the
    // multiply's reads. In: the last frame's multiply read it (fragment). Out: this frame's multiply reads it.
    VkAttachmentDescription attachment{};
    attachment.format = kScaleFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference color{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    const VkSubpassDependency dependencies[2] = {
        {VK_SUBPASS_EXTERNAL, 0u, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0u},
        {0u, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0u}};
    VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 2;
    pass_info.pDependencies = dependencies;
    if (vkCreateRenderPass(device, &pass_info, nullptr, &upsample_pass_) != VK_SUCCESS) {
        upsample_pass_ = VK_NULL_HANDLE;
        shutdown();
        return false;
    }
    upsample_ = graphics_pipeline(upsample_pass_, 0u, false);
    if (upsample_ == VK_NULL_HANDLE) {
        std::cerr << "[ambient-occlusion] Vulkan pass UpsamplePS failed\n";
        shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void AmbientOcclusionVk::shutdown() noexcept {
    ready_ = false;
    distance_.reset();
    raw_.reset();
    denoised_.reset();
    scale_.reset();
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    for (VkPipeline& pipeline : compute_) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    for (VkPipeline& pipeline : apply_) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    apply_pass_ = {};
    if (upsample_ != VK_NULL_HANDLE) vkDestroyPipeline(device, upsample_, nullptr);
    if (upsample_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device, upsample_pass_, nullptr);
    upsample_ = VK_NULL_HANDLE;
    upsample_pass_ = VK_NULL_HANDLE;
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
}

// The multiply: the shader's output times the scene target's colour (RGB; the game's alpha stays), no depth.
VkPipeline AmbientOcclusionVk::apply_pipeline(VkRenderPass pass, unsigned samples_log2) {
    samples_log2 = std::min(samples_log2, 3u);
    if (apply_[samples_log2] != VK_NULL_HANDLE && apply_pass_[samples_log2] == pass) {
        return apply_[samples_log2];
    }
    if (apply_[samples_log2] != VK_NULL_HANDLE) {
        const VkDevice device = Context::get().device();
        const VkPipeline old = apply_[samples_log2];
        Context::get().defer([device, old] { vkDestroyPipeline(device, old, nullptr); });
        apply_[samples_log2] = VK_NULL_HANDLE;
    }
    apply_[samples_log2] = graphics_pipeline(pass, samples_log2, true);
    apply_pass_[samples_log2] = pass;
    return apply_[samples_log2];
}

VkPipeline AmbientOcclusionVk::graphics_pipeline(VkRenderPass pass, unsigned samples_log2, bool multiply) const {
    const VkDevice device = Context::get().device();
    const VkShaderModule vs = module(kApplyVs);
    const VkShaderModule ps = module(multiply ? kApplyPs : kUpsamplePs);
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vs != VK_NULL_HANDLE && ps != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                     "ApplyVS", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                     multiply ? "ApplyPS" : "UpsamplePS", nullptr};
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
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState target{};
        if (multiply) {
            target.blendEnable = VK_TRUE;
            target.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.colorBlendOp = VK_BLEND_OP_ADD;
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        } else {
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        }
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

bool AmbientOcclusionVk::ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, VkFormat format,
                                const char* name) {
    if (target && target->width == width && target->height == height) {
        return true;
    }
    target = Context::get().create_texture(width, height, 1u, format,
                                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, name);
    return target != nullptr;
}

// The multiplier's target and its framebuffer (the texture destroys it with itself).
bool AmbientOcclusionVk::ensure_scale(unsigned width, unsigned height) {
    if (scale_ && scale_->width == width && scale_->height == height) {
        return true;
    }
    scale_ = Context::get().create_texture(width, height, 1u, kScaleFormat,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true,
                                           "occlusion scale");
    if (!scale_) {
        return false;
    }
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = upsample_pass_;
    info.attachmentCount = 1;
    info.pAttachments = &scale_->target_view;
    info.width = width;
    info.height = height;
    info.layers = 1;
    if (vkCreateFramebuffer(Context::get().device(), &info, nullptr, &scale_->framebuffer) != VK_SUCCESS) {
        scale_->framebuffer = VK_NULL_HANDLE;
        scale_.reset();
        return false;
    }
    return true;
}

void AmbientOcclusionVk::trim_idle() {
    constexpr std::uint64_t kIdleFrames = 600u;
    ++clock_;
    if (clock_ > used_ + kIdleFrames && (distance_ || raw_ || denoised_ || scale_)) {
        distance_.reset();
        raw_.reset();
        denoised_.reset();
        scale_.reset();
    }
}

VkDescriptorSet AmbientOcclusionVk::set_of(const SetAllocator& allocate, VkImageView depth, VkImageLayout depth_layout,
                                           const Texture& sampled1, const Texture& sampled2, const Texture& storage,
                                           VkImageLayout sampled1_layout) {
    const VkDescriptorSet set = allocate(set_layout_);
    const VkDescriptorImageInfo infos[4] = {{VK_NULL_HANDLE, depth, depth_layout},
                                            {VK_NULL_HANDLE, sampled1.view, sampled1_layout},
                                            {VK_NULL_HANDLE, sampled2.view, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, storage.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[6]{};
    for (std::uint32_t i = 0; i < 6u; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        if (i < 4u) {
            writes[i].descriptorType = i < 3u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &infos[i];
        } else {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = i == 4u ? &objects_ : &tiles_;
        }
    }
    vkUpdateDescriptorSets(Context::get().device(), 6, writes, 0, nullptr);
    return set;
}

void AmbientOcclusionVk::dispatch(VkCommandBuffer commands, Pass pass, VkDescriptorSet set, unsigned width,
                                  unsigned height) const {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, compute_[pass]);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(ao::Constants), &constants_);
    vkCmdDispatch(commands, (width + 7u) / 8u, (height + 7u) / 8u, 1u);
    written(commands);
}

// A full-screen triangle over a width x height target (D3D-style viewport, y flipped), in the pass being recorded.
void AmbientOcclusionVk::draw(VkCommandBuffer commands, VkPipeline pipeline, VkDescriptorSet set, unsigned width,
                              unsigned height) const {
    const VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(ao::Constants), &constants_);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

bool AmbientOcclusionVk::prepare(VkCommandBuffer commands, const Texture& depth, VkImageLayout depth_layout,
                                 const RtView& view, bool ssao, std::span<const ao::ObjectOccluder> objects, UploadRing& ring,
                                 const SetAllocator& allocate, const StageMarker& mark) {
    if (!ready_) {
        return false;
    }
    used_ = clock_;
    const unsigned divisor = ao::occlusion_divisor(depth.height);
    const unsigned width = (depth.width + divisor - 1u) / divisor;
    const unsigned height = (depth.height + divisor - 1u) / divisor;
    if (!ao::make_constants(view, width, height, divisor, ssao, constants_) ||
        !ensure(distance_, width, height, kDistanceFormat, "occlusion distance") ||
        !ensure(raw_, width, height, kOcclusionFormat, "occlusion") ||
        !ensure(denoised_, width, height, kOcclusionFormat, "occlusion denoised") ||
        !ensure_scale(depth.width, depth.height)) {
        return false;
    }
    for (Texture* target : {distance_.get(), raw_.get(), denoised_.get()}) {
        to_general(commands, *target);
    }
    ao::bin_objects(objects, constants_, object_values_, object_tiles_, object_scratch_);
    const VkDeviceSize alignment = Context::get().properties().limits.minStorageBufferOffsetAlignment;
    const std::size_t object_bytes = object_values_.size() * sizeof(float);
    const std::size_t tile_bytes = object_tiles_.size() * sizeof(std::uint32_t);
    const UploadRing::Allocation objects_at = ring.allocate(object_bytes, static_cast<std::size_t>(alignment));
    const UploadRing::Allocation tiles_at = ring.allocate(tile_bytes, static_cast<std::size_t>(alignment));
    std::memcpy(objects_at.cpu, object_values_.data(), object_bytes);
    std::memcpy(tiles_at.cpu, object_tiles_.data(), tile_bytes);
    // Each in the buffer it lies in (a chunk when the ring's segment and spill were full).
    objects_ = {reinterpret_cast<VkBuffer>(ring.resource_of(objects_at)), objects_at.offset, object_bytes};
    tiles_ = {reinterpret_cast<VkBuffer>(ring.resource_of(tiles_at)), tiles_at.offset, tile_bytes};
    if (mark) mark(Stage::Distance);
    dispatch(commands, kDistance, set_of(allocate, depth.view, depth_layout, *denoised_, *distance_, *distance_), width, height);
    if (mark) mark(Stage::Occlusion);
    dispatch(commands, ssao ? kSsao : kOcclusion, set_of(allocate, depth.view, depth_layout, *denoised_, *distance_, *raw_), width, height);
    if (mark) mark(Stage::Denoise);
    dispatch(commands, kDenoise, set_of(allocate, depth.view, depth_layout, *raw_, *distance_, *denoised_), width, height);
    if (mark) mark(Stage::Upsample);
    // The storage slot holds an image it does not write (the multiplier is the pass's colour target).
    const VkDescriptorSet upsample_set = set_of(allocate, depth.view, depth_layout, *denoised_, *distance_, *raw_);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = upsample_pass_;
    begin.framebuffer = scale_->framebuffer;
    begin.renderArea = {{0, 0}, {scale_->width, scale_->height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    draw(commands, upsample_, upsample_set, scale_->width, scale_->height);
    vkCmdEndRenderPass(commands);
    scale_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

void AmbientOcclusionVk::finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2,
                                const SetAllocator& allocate) {
    const VkPipeline pipeline = apply_pipeline(pass, samples_log2);
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    // Only the multiplier (t1) is read; every slot holds a valid image in the layout it is in.
    const VkDescriptorSet set = set_of(allocate, scale_->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *scale_,
                                       *denoised_, *denoised_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    draw(commands, pipeline, set, scale_->width, scale_->height);
}

}  // namespace galaxy::render::vk
