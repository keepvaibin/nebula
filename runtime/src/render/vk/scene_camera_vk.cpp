// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/vk/scene_camera_vk.h"

#include "galaxy/render/scene_camera_shape.h"
#include "galaxy/render/vk/utility_spirv.h"

#include "barriers_vk.h"

#include <algorithm>
#include <iostream>

namespace galaxy::render::vk {

namespace {

constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

VkShaderModule module_of(UtilitySpirv shader) {
    const std::span<const std::uint32_t> spirv = utility_spirv(shader);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size_bytes();
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    return vkCreateShaderModule(Context::get().device(), &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

}  // namespace

bool SceneCameraVk::initialize() {
    shutdown();
    const VkDevice device = Context::get().device();
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        bindings[i] = {i, i != 2u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kStages, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 4;
    set_info.pBindings = bindings;
    const VkPushConstantRange push{kStages, 0, sizeof(camera::Constants)};
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
    const VkShaderModule shader = module_of(UtilitySpirv::CameraMeterCs);
    if (shader == VK_NULL_HANDLE) {
        shutdown();
        return false;
    }
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shader;
    info.stage.pName = "MeterCS";
    info.layout = layout_;
    const VkResult result = vkCreateComputePipelines(device, shared_pipeline_cache(), 1, &info, nullptr, &meter_);
    vkDestroyShaderModule(device, shader, nullptr);
    if (result != VK_SUCCESS) {
        meter_ = VK_NULL_HANDLE;
        std::cerr << "[scene-camera] Vulkan meter failed\n";
        shutdown();
        return false;
    }
    exposure_ = Context::get().create_texture(2u, 1u, 1u, VK_FORMAT_R32_SFLOAT,
                                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
                                              "scene camera exposure");
    if (!exposure_) {
        shutdown();
        return false;
    }
    exposure_ready_ = false;
    ready_ = true;
    return true;
}

void SceneCameraVk::shutdown() noexcept {
    ready_ = false;
    exposure_.reset();
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    if (meter_ != VK_NULL_HANDLE) vkDestroyPipeline(device, meter_, nullptr);
    meter_ = VK_NULL_HANDLE;
    for (auto& kind : pipelines_) {
        for (VkPipeline& pipeline : kind) {
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
}

VkPipeline SceneCameraVk::pipeline(Kind kind, VkRenderPass pass, unsigned samples_log2) {
    samples_log2 = std::min(samples_log2, 3u);
    VkPipeline& out = pipelines_[kind][samples_log2];
    if (out != VK_NULL_HANDLE) {
        return out;
    }
    const VkDevice device = Context::get().device();
    const VkShaderModule vs = module_of(UtilitySpirv::CameraVs);
    const VkShaderModule ps = module_of(kind == kWiden           ? UtilitySpirv::CameraWidenPs
                                        : kind == kCameraSamples ? UtilitySpirv::CameraSamplesPs
                                        : kind == kCameraResolve ? UtilitySpirv::CameraResolvePs
                                                                 : UtilitySpirv::CameraPs);
    if (vs != VK_NULL_HANDLE && ps != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                     "CameraVS", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                     kind == kWiden ? "WidenPS" : kind == kCameraResolve ? "CameraResolvePS" : "CameraPS", nullptr};
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
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
        if (kind == kCameraSamples) {
            multisample.sampleShadingEnable = VK_TRUE;
            multisample.minSampleShading = 1.0f;
        }
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState target{};
        target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
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
        if (vkCreateGraphicsPipelines(device, shared_pipeline_cache(), 1, &info, nullptr, &out) != VK_SUCCESS) {
            out = VK_NULL_HANDLE;
            std::cerr << "[scene-camera] pipeline failed for " << (1u << samples_log2) << " samples\n";
        }
    }
    if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(device, vs, nullptr);
    if (ps != VK_NULL_HANDLE) vkDestroyShaderModule(device, ps, nullptr);
    return out;
}

VkDescriptorSet SceneCameraVk::set_of(const SetAllocator& allocate, const Texture& sampled, const Texture* depth,
                                      VkImageLayout depth_layout) const {
    const VkDescriptorSet set = allocate(set_layout_);
    // Only the meter reads the depth; elsewhere the slot holds the exposure, an image in the layout it is in.
    const VkDescriptorImageInfo infos[4] = {{VK_NULL_HANDLE, sampled.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                            {VK_NULL_HANDLE, exposure_->view, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, exposure_->view, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, depth != nullptr ? depth->view : exposure_->view,
                                             depth != nullptr ? depth_layout : VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[4]{};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i != 2u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(Context::get().device(), 4, writes, 0, nullptr);
    return set;
}

void SceneCameraVk::full_screen(VkCommandBuffer commands, VkPipeline pipeline, VkDescriptorSet set, unsigned width,
                                unsigned height) const {
    const VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    camera::Constants constants{};
    constants.width = static_cast<float>(width);
    constants.height = static_cast<float>(height);
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(constants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

void SceneCameraVk::widen(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const Texture& picture,
                          const SetAllocator& allocate) {
    const VkPipeline widen = pipeline(kWiden, pass, samples_log2);
    if (widen != VK_NULL_HANDLE) {
        full_screen(commands, widen, set_of(allocate, picture), picture.width, picture.height);
    }
}

void SceneCameraVk::meter(VkCommandBuffer commands, const Texture& wide, const Texture& depth, VkImageLayout depth_layout,
                          float seconds, const SetAllocator& allocate) {
    // The last frame's reads finish before the meter writes (a read before a write needs the execution dependency alone).
    image_barrier(commands, exposure_->image, VK_IMAGE_ASPECT_COLOR_BIT,
                  exposure_ready_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 1u);
    camera::Constants constants{};
    constants.width = static_cast<float>(wide.width);
    constants.height = static_cast<float>(wide.height);
    constants.seconds = exposure_ready_ ? seconds : -1.0f;
    exposure_ready_ = true;
    const VkDescriptorSet set = set_of(allocate, wide, &depth, depth_layout);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, meter_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, 1u, 1u, 1u);
    image_barrier(commands, exposure_->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 1u);
}

void SceneCameraVk::draw(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2, const Texture& wide,
                         bool per_sample, const SetAllocator& allocate, bool resolve) {
    const VkPipeline camera = pipeline(resolve ? kCameraResolve : per_sample ? kCameraSamples : kCamera, pass, samples_log2);
    if (camera != VK_NULL_HANDLE) {
        full_screen(commands, camera, set_of(allocate, wide), wide.width, wide.height);
    }
}

}  // namespace galaxy::render::vk
