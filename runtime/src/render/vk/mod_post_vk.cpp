// SPDX-License-Identifier: GPL-3.0-only
// draw.post and draw.native on Vulkan (LumaCore plan 2.4 layers 8 and 9; contract draw 15 and 16; the D3D12 side is
// mod_post_d3d12.cpp). A post program draws in a pass of its own over the scene target's colour alone: the scene's pass
// holds the depth as an attachment, which the program samples. A native function gets the command buffer between
// passes.
#include "galaxy/render/vk/renderer_vk.h"

#include "galaxy/render/mod_post.h"
#include "renderer_vk_internal.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <iostream>

namespace galaxy::render {

using namespace vk_detail;

// Frame t0 (binding 0), depth t1 (1), motion t2 (2), linear and point clamp s0 and s1 (3 and 4, immutable), the
// block as 128 bytes of push constants (mod_post.h).
bool RendererVk::ensure_mod_post_layout() {
    if (mod_post_layout_ != VK_NULL_HANDLE) {
        return true;
    }
    if (mod_post_failed_) {
        return false;
    }
    const VkDevice device = vk::Context::get().device();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    bool made = vkCreateSampler(device, &sampler, nullptr, &mod_post_samplers_[0]) == VK_SUCCESS;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    made = made && vkCreateSampler(device, &sampler, nullptr, &mod_post_samplers_[1]) == VK_SUCCESS;
    VkDescriptorSetLayoutBinding bindings[5]{};
    for (std::uint32_t i = 0; i < 5u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1u;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].descriptorType = i < 3u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    bindings[3].pImmutableSamplers = &mod_post_samplers_[0];
    bindings[4].pImmutableSamplers = &mod_post_samplers_[1];
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 5u;
    set.pBindings = bindings;
    made = made && vkCreateDescriptorSetLayout(device, &set, nullptr, &mod_post_set_layout_) == VK_SUCCESS;
    const VkPushConstantRange block{VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(ModPostConstants)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1u;
    layout.pSetLayouts = &mod_post_set_layout_;
    layout.pushConstantRangeCount = 1u;
    layout.pPushConstantRanges = &block;
    made = made && vkCreatePipelineLayout(device, &layout, nullptr, &mod_post_layout_) == VK_SUCCESS;
    if (!made) {
        std::cerr << "[draw.post] the pipeline layout failed: post programs draw nothing\n";
        release_mod_post();
        mod_post_failed_ = true;
        return false;
    }
    return true;
}

// The colour-only pass for a target format and samples: everything is drawn (no load); multisampled, the samples are
// kept and resolved into the scene target's colour, as the scene's own pass does at its end.
VkRenderPass RendererVk::mod_post_pass(VkFormat format, unsigned samples) {
    for (const ModPostPass& kept : mod_post_passes_) {
        if (kept.format == format && kept.samples == samples) {
            return kept.pass;
        }
    }
    VkAttachmentDescription attachments[2]{};
    for (VkAttachmentDescription& a : attachments) {
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    attachments[0].samples = static_cast<VkSampleCountFlagBits>(samples);
    const VkAttachmentReference color{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference resolve{1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &color;
    subpass.pResolveAttachments = samples > 1u ? &resolve : nullptr;
    // The inputs' barriers are recorded before the pass begins; these order the target's writes with the work around.
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0u;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0u;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = samples > 1u ? 2u : 1u;
    info.pAttachments = attachments;
    info.subpassCount = 1u;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2u;
    info.pDependencies = dependencies;
    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(vk::Context::get().device(), &info, nullptr, &pass) != VK_SUCCESS) {
        std::cerr << "[draw.post] the render pass for format " << static_cast<int>(format) << " failed\n";
        return VK_NULL_HANDLE;
    }
    mod_post_passes_.push_back({format, samples, pass});
    return pass;
}

VkPipeline RendererVk::mod_post_pipeline(const ModPostProgram& program, VkFormat format, unsigned samples, VkRenderPass pass) {
    const VkDevice device = vk::Context::get().device();
    // Programs freed since the last look leave the cache; the device drops their pipelines after the frames in flight.
    const std::uint64_t epoch = mod_post_epoch();
    if (epoch != mod_post_epoch_) {
        mod_post_epoch_ = epoch;
        std::erase_if(mod_post_pipelines_, [device](ModPostPipelineVk& kept) {
            if (mod_post_program(kept.program) != nullptr) {
                return false;
            }
            if (kept.pipeline != VK_NULL_HANDLE) {
                const VkPipeline old = kept.pipeline;
                vk::Context::get().defer([device, old] { vkDestroyPipeline(device, old, nullptr); });
            }
            return true;
        });
    }
    for (const ModPostPipelineVk& kept : mod_post_pipelines_) {
        if (kept.program == program.id && kept.format == format && kept.samples == samples) {
            return kept.pipeline;
        }
    }
    ModPostPipelineVk made{program.id, format, samples, VK_NULL_HANDLE};
    VkShaderModule modules[2]{};
    const std::vector<std::uint8_t>* codes[2] = {&program.vertex, &program.pixel};
    bool ok = true;
    for (unsigned i = 0; i < 2u; ++i) {
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = codes[i]->size();
        module.pCode = reinterpret_cast<const std::uint32_t*>(codes[i]->data());
        ok = ok && codes[i]->size() % 4u == 0u && !codes[i]->empty() &&
             vkCreateShaderModule(device, &module, nullptr, &modules[i]) == VK_SUCCESS;
    }
    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (unsigned i = 0; i < 2u; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i == 0u ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1u;
        viewport.scissorCount = 1u;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1u;
        blending.pAttachments = &blend;
        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2u;
        dynamic.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2u;
        info.pStages = stages;
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = mod_post_layout_;
        info.renderPass = pass;
        ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &info, nullptr, &made.pipeline) == VK_SUCCESS;
    }
    for (VkShaderModule module : modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, module, nullptr);
        }
    }
    if (!ok) {
        // Kept empty: the pass draws nothing for it from now on, and the log says why once.
        std::cerr << "[draw.post] program " << program.id << " pipeline failed\n";
        made.pipeline = VK_NULL_HANDLE;
    }
    mod_post_pipelines_.push_back(made);
    return made.pipeline;
}

void RendererVk::record_mod_post(const ModPostProgram& program, ModPostConstants constants, const gpu::TextureRef& picture,
                                 bool depth_known) {
    if (!picture || !scene_.color || !ensure_mod_post_layout()) {
        return;
    }
    const GpuPassScope pass_scope{*this, GpuPass::FramePass};
    end_scene_pass("mod_post");
    settle_depth_clear();
    const unsigned samples = std::max(scene_.samples, 1u);
    const VkFormat format = scene_.color->format;
    const VkRenderPass pass = mod_post_pass(format, samples);
    if (pass == VK_NULL_HANDLE || (samples > 1u && !scene_.ms_color)) {
        return;
    }
    const VkPipeline pipeline = mod_post_pipeline(program, format, samples, pass);
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    const std::uint32_t width = scene_map_.width;
    const std::uint32_t height = scene_map_.height;
    constants.size[0] = static_cast<float>(width);
    constants.size[1] = static_cast<float>(height);
    constants.inv_size[0] = 1.0f / static_cast<float>(width);
    constants.inv_size[1] = 1.0f / static_cast<float>(height);
    constants.flags = 0u;

    // The scene's depth (RendererD3D12::record_mod_post): the scene target's own (multisampled: its resolve), or with the
    // output scene target in place the internal one the scene was drawn at. None inside a target pass.
    const vk::Texture* depth = nullptr;
    if (depth_known && (program.reads & kModPostReadsDepth) != 0u && !target_pass_active_) {
        if (!output_scene_active_) {
            if (scene_.depth && scene_.samples > 1u && scene_.depth_dirty) {
                resolve_scene_depth(commands_);
            }
            depth = scene_.depth.get();
        } else if (alt_scene_.depth && (alt_scene_.samples <= 1u || !alt_scene_.depth_dirty)) {
            depth = alt_scene_.depth.get();
        }
    }
    const vk::Texture* motion = nullptr;
    if ((program.reads & kModPostReadsMotion) != 0u && !target_pass_active_ && scene_motion_ && motion_.motion &&
        motion_used_frame_ == frame_index_ && motion_.map.width != 0u && motion_.map.height != 0u) {
        motion = motion_.motion.get();
        constants.motion_scale[0] = 1.0f / static_cast<float>(motion_.map.width);
        constants.motion_scale[1] = 1.0f / static_cast<float>(motion_.map.height);
    }
    if (depth != nullptr) {
        constants.flags |= kModPostHasDepth;
    }
    if (motion != nullptr) {
        constants.flags |= kModPostHasMotion;
    }

    const VkCommandBuffer commands = commands_;
    const VkImageAspectFlags aspects = depth_aspects(depth_format_);
    if (depth != nullptr) {
        image_barrier(commands, depth->image, aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    if (motion != nullptr) {
        image_barrier(commands, motion->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    // The inputs; one without its source reads the frame (a valid image of the kind; the flags keep the program
    // from reading it).
    const VkDescriptorSet set = allocate_frame_set(mod_post_set_layout_);
    VkDescriptorImageInfo images[3]{};
    images[0] = {VK_NULL_HANDLE, picture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[1] = depth != nullptr ? VkDescriptorImageInfo{VK_NULL_HANDLE, depth->view, kDepthSampleLayout} : images[0];
    images[2] = motion != nullptr ? VkDescriptorImageInfo{VK_NULL_HANDLE, motion->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                                  : images[0];
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0u;
    write.descriptorCount = 3u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = images;
    const VkDevice device = vk::Context::get().device();
    vkUpdateDescriptorSets(device, 1u, &write, 0u, nullptr);

    // The framebuffer over this target, gone after the frames in flight (the scene's colour changes with every post).
    const VkImageView views[2] = {samples > 1u ? scene_.ms_color->target_view : scene_.color->target_view,
                                  scene_.color->target_view};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = pass;
    fb.attachmentCount = samples > 1u ? 2u : 1u;
    fb.pAttachments = views;
    fb.width = scene_.color->width;
    fb.height = scene_.color->height;
    fb.layers = 1u;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &fb, nullptr, &framebuffer) == VK_SUCCESS) {
        vk::Context::get().defer([device, framebuffer] { vkDestroyFramebuffer(device, framebuffer, nullptr); });
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = pass;
        begin.framebuffer = framebuffer;
        begin.renderArea = {{0, 0}, {width, height}};
        vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, mod_post_layout_, 0u, 1u, &set, 0u, nullptr);
        vkCmdPushConstants(commands, mod_post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(constants), &constants);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetViewport(commands, 0u, 1u, &viewport);
        vkCmdSetScissor(commands, 0u, 1u, &scissor);
        vkCmdDraw(commands, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(commands);
    } else {
        std::cerr << "[draw.post] a framebuffer failed: the pass is skipped\n";
    }

    if (depth != nullptr) {
        image_barrier(commands, depth->image, aspects, kDepthSampleLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    if (motion != nullptr) {
        image_barrier(commands, motion->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    // The next draw opens the scene's pass again (set_draw_aux) with its own pipeline and bindings.
    reset_draw_bindings();
    bind_scene();
}

void RendererVk::record_native_call(const NebulaNativeCall& call, LumaDrawNativeFrame& frame, bool scene_point) {
    if (call.fn == nullptr || !scene_.color) {
        return;
    }
    const GpuPassScope pass_scope{*this, GpuPass::FramePass};
    end_scene_pass("native_call");
    settle_depth_clear();
    const bool msaa = scene_.samples > 1u && scene_.ms_color;
    const vk::Texture& target = msaa ? *scene_.ms_color : *scene_.color;
    const vk::Texture* depth = scene_point ? (msaa ? scene_.ms_depth.get() : scene_.depth.get()) : nullptr;
    vk::Context& context = vk::Context::get();
    frame.backend = LUMA_DRAW_NATIVE_VULKAN;
    frame.width = scene_map_.width;
    frame.height = scene_map_.height;
    frame.samples = std::max(scene_.samples, 1u);
    frame.vulkan.instance = context.instance();
    frame.vulkan.physical_device = context.physical_device();
    frame.vulkan.device = context.device();
    frame.vulkan.queue = context.queue();
    frame.vulkan.get_instance_proc_addr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
    frame.vulkan.command_buffer = commands_;
    frame.vulkan.queue_family = context.queue_family();
    frame.vulkan.target_format = static_cast<std::uint32_t>(target.format);
    frame.vulkan.target_image = reinterpret_cast<std::uint64_t>(target.image);
    frame.vulkan.target_view = reinterpret_cast<std::uint64_t>(target.target_view);
    frame.vulkan.target_layout = static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (depth != nullptr) {
        frame.vulkan.depth_format = static_cast<std::uint32_t>(depth_format_);
        frame.vulkan.depth_image = reinterpret_cast<std::uint64_t>(depth->image);
        frame.vulkan.depth_view = reinterpret_cast<std::uint64_t>(depth->target_view);
        frame.vulkan.depth_layout = static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    // The host's projection is Direct3D's clip space (RenderCore::walk_native); Vulkan's y points down.
    if (scene_point) {
        for (unsigned c = 0; c < 4u; ++c) {
            frame.projection[4u + c] = -frame.projection[4u + c];
        }
    }
    call.fn(call.user, &frame);
    reset_draw_bindings();
    if (msaa) {
        // What the function drew into the samples reaches the scene's colour at the end of the scene's pass (its
        // resolve): the pass opens now, so a picture taken next ends it.
        scene_.depth_dirty = scene_.depth_dirty || depth != nullptr;
        begin_scene_pass(nullptr, "native_call");
    }
    bind_scene();
}

void RendererVk::release_mod_post() {
    const VkDevice device = vk::Context::get().device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (ModPostPipelineVk& kept : mod_post_pipelines_) {
        if (kept.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, kept.pipeline, nullptr);
    }
    mod_post_pipelines_.clear();
    for (ModPostPass& kept : mod_post_passes_) {
        vkDestroyRenderPass(device, kept.pass, nullptr);
    }
    mod_post_passes_.clear();
    if (mod_post_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, mod_post_layout_, nullptr);
    if (mod_post_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, mod_post_set_layout_, nullptr);
    for (VkSampler& sampler : mod_post_samplers_) {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    mod_post_layout_ = VK_NULL_HANDLE;
    mod_post_set_layout_ = VK_NULL_HANDLE;
    mod_post_failed_ = false;
}

}  // namespace galaxy::render
