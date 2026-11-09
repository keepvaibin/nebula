// SPDX-License-Identifier: GPL-3.0-only
// Snapdragon Game Super Resolution 2 on Vulkan (temporal_upscaler_vk.h; the shaders: sgsr2.hlsl). Two fragment passes, no
// compute: the depth separation at the render size, then the upsample at the output size, which writes the output and
// the next frame's history together. Tile GPUs keep both passes on chip.
#include "galaxy/render/halton.h"
#include "galaxy/render/vk/temporal_upscaler_vk.h"

#include "galaxy/render/vk/utility_spirv.h"
#include "galaxy/render/vk/vk_context.h"

#include "barriers_vk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

namespace galaxy::render::vk {

namespace {

constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr VkFormat kDepthclipFormat = VK_FORMAT_R16_SFLOAT;
constexpr unsigned kPhases = 32u;  // SGSR's sample: the first 32 points of Halton(2, 3)

// sgsr2.hlsl's SgsrConstants.
struct Constants {
    float render_size[2];
    float render_rcp[2];
    float output_size[2];
    float output_rcp[2];
    float jitter[2];
    float scale_ratio[2];
    float input_rcp[2];
    float fov_hor;
    float min_lerp;
    float reset;
    float pad;
};
static_assert(sizeof(Constants) == 72u, "sgsr2.hlsl's constants");

VkShaderModule module_of(VkDevice device, UtilitySpirv shader) {
    const std::span<const std::uint32_t> spirv = utility_spirv(shader);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size_bytes();
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

// The descriptor pool, kept until the last deferred free of its sets has run.
struct Pool {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    }
};

class Sgsr2Upscaler final : public TemporalUpscaler {
public:
    ~Sgsr2Upscaler() override {
        const VkDevice device = device_;
        if (device == VK_NULL_HANDLE) {
            return;
        }
        // The owner waited for the GPU before releasing an upscaler (prepare_temporal_upscaler).
        for (VkFramebuffer& framebuffer : upscale_framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        if (convert_framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device, convert_framebuffer_, nullptr);
        if (convert_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, convert_pipeline_, nullptr);
        if (upscale_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, upscale_pipeline_, nullptr);
        if (convert_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device, convert_pass_, nullptr);
        if (upscale_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device, upscale_pass_, nullptr);
        if (convert_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, convert_layout_, nullptr);
        if (upscale_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, upscale_layout_, nullptr);
        if (convert_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, convert_set_layout_, nullptr);
        if (upscale_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, upscale_set_layout_, nullptr);
        if (point_ != VK_NULL_HANDLE) vkDestroySampler(device, point_, nullptr);
        if (linear_ != VK_NULL_HANDLE) vkDestroySampler(device, linear_, nullptr);
    }

    bool create(unsigned render_width, unsigned render_height, unsigned output_width, unsigned output_height,
                VkFormat output_format) {
        Context& context = Context::get();
        device_ = context.device();
        render_width_ = render_width;
        render_height_ = render_height;
        output_width_ = output_width;
        output_height_ = output_height;
        output_format_ = output_format;
        if (!create_samplers() || !create_layouts() || !create_passes() || !create_pipelines()) {
            return false;
        }
        constexpr VkImageUsageFlags kTarget = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depthclip_ = context.create_texture(render_width, render_height, 1u, kDepthclipFormat, kTarget, true,
                                            "sgsr2 depth clip");
        for (std::size_t i = 0; i < history_.size(); ++i) {
            history_[i] = context.create_texture(output_width, output_height, 1u, output_format,
                                                 kTarget | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, "sgsr2 history");
        }
        if (!depthclip_ || !history_[0] || !history_[1]) {
            return false;
        }
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = convert_pass_;
        info.attachmentCount = 1u;
        info.pAttachments = &depthclip_->target_view;
        info.width = render_width;
        info.height = render_height;
        info.layers = 1u;
        return vkCreateFramebuffer(device_, &info, nullptr, &convert_framebuffer_) == VK_SUCCESS;
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (in.commands == VK_NULL_HANDLE || in.output.view == VK_NULL_HANDLE || in.render_width != render_width_ ||
            in.render_height != render_height_ || in.output_width != output_width_ || in.output_height != output_height_ ||
            in.output.format != output_format_) {
            return false;
        }
        const VkCommandBuffer commands = in.commands;
        const unsigned write = history_index_ ^ 1u;
        const VkFramebuffer upscale_framebuffer = upscale_framebuffer_for(in.output.view, write);
        if (upscale_framebuffer == VK_NULL_HANDLE) {
            return false;
        }
        if (!sets_match(in)) {
            make_sets(in);
            if (convert_set_ == VK_NULL_HANDLE) {
                return false;
            }
        }
        const bool reset = in.reset || !history_ready_;
        if (!history_ready_) {
            // Both histories start black and readable; the reset ignores the one read this frame.
            BarriersVk to_clear(commands);
            for (unsigned i = 0; i < 2u; ++i) {
                to_clear.image_scoped(history_[i]->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                      VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1u);
            }
            to_clear.issue();
            const VkClearColorValue black{};
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
            for (unsigned i = 0; i < 2u; ++i) {
                vkCmdClearColorImage(commands, history_[i]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1u, &range);
            }
            BarriersVk to_read(commands);
            for (unsigned i = 0; i < 2u; ++i) {
                to_read.image(history_[i]->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopyDest, gpu::Use::PixelRead,
                              gpu::Contents::Keep, 1u);
            }
            to_read.issue();
            history_ready_ = true;
        }

        // The scene's colour, motion and depth from their attachment layouts to read; back after.
        constexpr VkPipelineStageFlags2 kDepthStages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        BarriersVk scene_to_read(commands);
        scene_to_read.image(in.color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::PixelRead,
                            gpu::Contents::Keep, 1u);
        scene_to_read.image(in.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::PixelRead,
                            gpu::Contents::Keep, 1u);
        scene_to_read.image_scoped(in.depth.image, in.depth.barrier_aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, kDepthStages,
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 1u);
        scene_to_read.issue();

        const Constants constants = constants_for(in, reset);
        const VkClearValue unused[2]{};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = convert_pass_;
        begin.framebuffer = convert_framebuffer_;
        begin.renderArea = {{0, 0}, {render_width_, render_height_}};
        begin.clearValueCount = 0u;
        begin.pClearValues = unused;
        vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        draw(commands, convert_pipeline_, convert_layout_, convert_set_, render_width_, render_height_, constants);
        vkCmdEndRenderPass(commands);

        begin.renderPass = upscale_pass_;
        begin.framebuffer = upscale_framebuffer;
        begin.renderArea = {{0, 0}, {output_width_, output_height_}};
        vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        draw(commands, upscale_pipeline_, upscale_layout_, upscale_sets_[history_index_], output_width_, output_height_,
             constants);
        vkCmdEndRenderPass(commands);

        BarriersVk scene_back(commands);
        scene_back.image(in.color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::PixelRead, gpu::Use::ColorWrite,
                         gpu::Contents::Keep, 1u);
        scene_back.image(in.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::PixelRead, gpu::Use::ColorWrite,
                         gpu::Contents::Keep, 1u);
        scene_back.image_scoped(in.depth.image, in.depth.barrier_aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_NONE, kDepthStages,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1u);
        scene_back.issue();
        history_index_ = write;
        return true;
    }

    unsigned jitter_phase_count(unsigned, unsigned) const override { return kPhases; }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
        x = halton(i, 2u) - 0.5f;
        y = halton(i, 3u) - 0.5f;
    }

    const char* name() const noexcept override { return "SGSR 2"; }

private:
    bool create_samplers() {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        // The depth is only gathered, which no filter changes; depth formats need not filter linearly.
        info.magFilter = info.minFilter = VK_FILTER_NEAREST;
        if (vkCreateSampler(device_, &info, nullptr, &point_) != VK_SUCCESS) {
            return false;
        }
        info.magFilter = info.minFilter = VK_FILTER_LINEAR;
        return vkCreateSampler(device_, &info, nullptr, &linear_) == VK_SUCCESS;
    }

    // The convert pass: the depth (t0) and s0. The upscale pass: t0-t3 and s0 (utility_spirv_sources.h, kSgsrBindings).
    bool create_layouts() {
        VkDescriptorSetLayoutBinding bindings[5]{};
        for (std::uint32_t i = 0; i < 4u; ++i) {
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        }
        bindings[4] = {4u, VK_DESCRIPTOR_TYPE_SAMPLER, 1u, VK_SHADER_STAGE_FRAGMENT_BIT, &point_};
        const VkDescriptorSetLayoutBinding convert[2] = {bindings[0], bindings[4]};
        VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_info.bindingCount = 2u;
        set_info.pBindings = convert;
        if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &convert_set_layout_) != VK_SUCCESS) {
            return false;
        }
        bindings[4].pImmutableSamplers = &linear_;
        set_info.bindingCount = 5u;
        set_info.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &upscale_set_layout_) != VK_SUCCESS) {
            return false;
        }
        const VkPushConstantRange push{kStages, 0u, sizeof(Constants)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1u;
        layout_info.pushConstantRangeCount = 1u;
        layout_info.pPushConstantRanges = &push;
        layout_info.pSetLayouts = &convert_set_layout_;
        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &convert_layout_) != VK_SUCCESS) {
            return false;
        }
        layout_info.pSetLayouts = &upscale_set_layout_;
        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &upscale_layout_) != VK_SUCCESS) {
            return false;
        }
        // One convert set and the two upscale sets (one per history), remade together when an input changes; the sets
        // they replace are freed after the GPU is done with them.
        const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4u * kSetGenerations * 3u},
                                               {VK_DESCRIPTOR_TYPE_SAMPLER, kSetGenerations * 3u}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = kSetGenerations * 3u;
        pool_info.poolSizeCount = 2u;
        pool_info.pPoolSizes = sizes;
        pool_ = std::make_shared<Pool>();
        pool_->device = device_;
        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool_->pool) != VK_SUCCESS) {
            pool_->pool = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    bool create_passes() {
        // The depth clip: written whole, read by the upscale.
        VkAttachmentDescription clip{};
        clip.format = kDepthclipFormat;
        clip.samples = VK_SAMPLE_COUNT_1_BIT;
        clip.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        clip.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        clip.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        clip.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        clip.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        clip.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkAttachmentReference clip_ref{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1u;
        subpass.pColorAttachments = &clip_ref;
        // In: the last frame's upscale read of the clip (WAR) and the scene's writes, made readable by evaluate's barrier.
        // Out: the writes to the fragment shaders that read them.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0u;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0u;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 1u;
        info.pAttachments = &clip;
        info.subpassCount = 1u;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2u;
        info.pDependencies = dependencies;
        if (vkCreateRenderPass(device_, &info, nullptr, &convert_pass_) != VK_SUCCESS) {
            return false;
        }
        // The output (left in its attachment layout for the 2D layer) and the next history, both written whole.
        VkAttachmentDescription targets[2] = {clip, clip};
        targets[0].format = targets[1].format = output_format_;
        targets[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        targets[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference refs[2] = {{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                               {1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        subpass.colorAttachmentCount = 2u;
        subpass.pColorAttachments = refs;
        info.attachmentCount = 2u;
        info.pAttachments = targets;
        return vkCreateRenderPass(device_, &info, nullptr, &upscale_pass_) == VK_SUCCESS;
    }

    VkPipeline create_pipeline(VkRenderPass pass, VkPipelineLayout layout, VkShaderModule vs, VkShaderModule ps,
                               const char* entry, std::uint32_t targets) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "SgsrVS",
                     nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, entry,
                     nullptr};
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1u;
        viewport.scissorCount = 1u;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState blend_targets[2]{};
        for (VkPipelineColorBlendAttachmentState& target : blend_targets) {
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = targets;
        blend.pAttachments = blend_targets;
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
        info.layout = layout;
        info.renderPass = pass;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1u, &info, nullptr, &pipeline) != VK_SUCCESS) {
            std::cerr << "[sgsr2] " << entry << " pipeline failed\n";
            return VK_NULL_HANDLE;
        }
        return pipeline;
    }

    bool create_pipelines() {
        const VkShaderModule vs = module_of(device_, UtilitySpirv::SgsrVs);
        const VkShaderModule convert = module_of(device_, UtilitySpirv::SgsrConvertPs);
        const VkShaderModule upscale = module_of(device_, UtilitySpirv::SgsrUpscalePs);
        if (vs != VK_NULL_HANDLE && convert != VK_NULL_HANDLE && upscale != VK_NULL_HANDLE) {
            convert_pipeline_ = create_pipeline(convert_pass_, convert_layout_, vs, convert, "ConvertPS", 1u);
            upscale_pipeline_ = create_pipeline(upscale_pass_, upscale_layout_, vs, upscale, "UpscalePS", 2u);
        }
        for (const VkShaderModule module : {vs, convert, upscale}) {
            if (module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module, nullptr);
        }
        return convert_pipeline_ != VK_NULL_HANDLE && upscale_pipeline_ != VK_NULL_HANDLE;
    }

    // The output's framebuffer with history `write`, made again when the renderer replaces its output image.
    VkFramebuffer upscale_framebuffer_for(VkImageView output, unsigned write) {
        if (framebuffer_output_ != output) {
            for (VkFramebuffer& framebuffer : upscale_framebuffers_) {
                if (framebuffer != VK_NULL_HANDLE) {
                    const VkDevice device = device_;
                    const VkFramebuffer retired = framebuffer;
                    Context::get().defer([device, retired] { vkDestroyFramebuffer(device, retired, nullptr); });
                    framebuffer = VK_NULL_HANDLE;
                }
            }
            framebuffer_output_ = output;
        }
        VkFramebuffer& framebuffer = upscale_framebuffers_[write];
        if (framebuffer == VK_NULL_HANDLE) {
            const VkImageView views[2] = {output, history_[write]->target_view};
            VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            info.renderPass = upscale_pass_;
            info.attachmentCount = 2u;
            info.pAttachments = views;
            info.width = output_width_;
            info.height = output_height_;
            info.layers = 1u;
            if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffer) != VK_SUCCESS) {
                framebuffer = VK_NULL_HANDLE;
            }
        }
        return framebuffer;
    }

    bool sets_match(const TemporalUpscaleInputs& in) const {
        return convert_set_ != VK_NULL_HANDLE && set_color_ == in.color.view && set_depth_ == in.depth.view &&
               set_motion_ == in.motion.view;
    }

    // Upscale set i reads history i (and the pass writes the other).
    void make_sets(const TemporalUpscaleInputs& in) {
        if (convert_set_ != VK_NULL_HANDLE) {
            const std::shared_ptr<Pool> pool = pool_;
            const std::array<VkDescriptorSet, 3> retired = {convert_set_, upscale_sets_[0], upscale_sets_[1]};
            Context::get().defer([pool, retired] { vkFreeDescriptorSets(pool->device, pool->pool, 3u, retired.data()); });
            convert_set_ = upscale_sets_[0] = upscale_sets_[1] = VK_NULL_HANDLE;
        }
        const VkDescriptorSetLayout layouts[3] = {convert_set_layout_, upscale_set_layout_, upscale_set_layout_};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool_->pool;
        allocate.descriptorSetCount = 3u;
        allocate.pSetLayouts = layouts;
        VkDescriptorSet sets[3]{};
        if (vkAllocateDescriptorSets(device_, &allocate, sets) != VK_SUCCESS) {
            // Every earlier generation is still in flight: the pool holds kSetGenerations of them.
            std::cerr << "[sgsr2] descriptor sets exhausted\n";
            return;
        }
        convert_set_ = sets[0];
        upscale_sets_[0] = sets[1];
        upscale_sets_[1] = sets[2];
        set_color_ = in.color.view;
        set_depth_ = in.depth.view;
        set_motion_ = in.motion.view;
        constexpr VkImageLayout kRead = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkDescriptorImageInfo depth{VK_NULL_HANDLE, in.depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo clip{VK_NULL_HANDLE, depthclip_->view, kRead};
        const VkDescriptorImageInfo motion{VK_NULL_HANDLE, in.motion.view, kRead};
        const VkDescriptorImageInfo color{VK_NULL_HANDLE, in.color.view, kRead};
        const VkDescriptorImageInfo history[2] = {{VK_NULL_HANDLE, history_[0]->view, kRead},
                                                  {VK_NULL_HANDLE, history_[1]->view, kRead}};
        VkWriteDescriptorSet writes[9]{};
        const auto write = [&](unsigned slot, VkDescriptorSet set, std::uint32_t binding, const VkDescriptorImageInfo& image) {
            writes[slot] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[slot].dstSet = set;
            writes[slot].dstBinding = binding;
            writes[slot].descriptorCount = 1u;
            writes[slot].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[slot].pImageInfo = &image;
        };
        write(0u, convert_set_, 0u, depth);
        for (unsigned i = 0; i < 2u; ++i) {
            write(1u + i * 4u, upscale_sets_[i], 0u, history[i]);
            write(2u + i * 4u, upscale_sets_[i], 1u, clip);
            write(3u + i * 4u, upscale_sets_[i], 2u, motion);
            write(4u + i * 4u, upscale_sets_[i], 3u, color);
        }
        vkUpdateDescriptorSets(device_, 9u, writes, 0u, nullptr);
    }

    Constants constants_for(const TemporalUpscaleInputs& in, bool reset) {
        Constants c{};
        const float rw = static_cast<float>(render_width_);
        const float rh = static_cast<float>(render_height_);
        const float ow = static_cast<float>(output_width_);
        const float oh = static_cast<float>(output_height_);
        c.render_size[0] = rw;
        c.render_size[1] = rh;
        c.render_rcp[0] = 1.0f / rw;
        c.render_rcp[1] = 1.0f / rh;
        c.output_size[0] = ow;
        c.output_size[1] = oh;
        c.output_rcp[0] = 1.0f / ow;
        c.output_rcp[1] = 1.0f / oh;
        c.jitter[0] = in.jitter_x;
        c.jitter[1] = in.jitter_y;
        const float area = (ow * oh) / (rw * rh);
        c.scale_ratio[0] = ow / rw;
        c.scale_ratio[1] = std::min(20.0f, area * area * area);
        c.input_rcp[0] = 1.0f / static_cast<float>(std::max(in.color.width, 1u));
        c.input_rcp[1] = 1.0f / static_cast<float>(std::max(in.color.height, 1u));
        c.fov_hor = std::tan(in.fov_y * 0.5f) * rw / rh;
        // SGSR's still camera: the view and the lens unchanged for more than five frames lets the history hold a
        // little where the box would reject it (minLerpContribution).
        bool still = false;
        if (in.view_known && view_known_ && !reset) {
            float change = std::fabs(in.fov_y - fov_y_);
            for (std::size_t i = 0; i < in.view.size(); ++i) {
                change += std::fabs(in.view[i] - view_[i]);
            }
            still = change < 1.0e-05f;
        }
        still_frames_ = still ? still_frames_ + 1u : 0u;
        view_ = in.view;
        view_known_ = in.view_known;
        fov_y_ = in.fov_y;
        c.min_lerp = still_frames_ > 5u ? 0.3f : 0.0f;
        c.reset = reset ? 1.0f : 0.0f;
        return c;
    }

    void draw(VkCommandBuffer commands, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet set, unsigned width,
              unsigned height, const Constants& constants) const {
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetViewport(commands, 0u, 1u, &viewport);
        vkCmdSetScissor(commands, 0u, 1u, &scissor);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0u, 1u, &set, 0u, nullptr);
        vkCmdPushConstants(commands, layout, kStages, 0u, sizeof(constants), &constants);
        vkCmdDraw(commands, 3u, 1u, 0u, 0u);
    }

    // Set generations the pool holds: the live one and those still in flight after a change of input.
    static constexpr std::uint32_t kSetGenerations = 8u;

    VkDevice device_ = VK_NULL_HANDLE;
    unsigned render_width_ = 0u, render_height_ = 0u, output_width_ = 0u, output_height_ = 0u;
    VkFormat output_format_ = VK_FORMAT_UNDEFINED;
    VkSampler point_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout convert_set_layout_ = VK_NULL_HANDLE, upscale_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout convert_layout_ = VK_NULL_HANDLE, upscale_layout_ = VK_NULL_HANDLE;
    std::shared_ptr<Pool> pool_;
    VkRenderPass convert_pass_ = VK_NULL_HANDLE, upscale_pass_ = VK_NULL_HANDLE;
    VkPipeline convert_pipeline_ = VK_NULL_HANDLE, upscale_pipeline_ = VK_NULL_HANDLE;
    std::shared_ptr<Texture> depthclip_;
    std::array<std::shared_ptr<Texture>, 2> history_{};
    VkFramebuffer convert_framebuffer_ = VK_NULL_HANDLE;
    std::array<VkFramebuffer, 2> upscale_framebuffers_{};
    VkImageView framebuffer_output_ = VK_NULL_HANDLE;
    VkDescriptorSet convert_set_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> upscale_sets_{};
    VkImageView set_color_ = VK_NULL_HANDLE, set_depth_ = VK_NULL_HANDLE, set_motion_ = VK_NULL_HANDLE;
    unsigned history_index_ = 0u;  // the history read next
    bool history_ready_ = false;
    std::array<float, 12> view_{};
    bool view_known_ = false;
    float fov_y_ = 0.0f;
    unsigned still_frames_ = 0u;
};

}  // namespace

std::unique_ptr<TemporalUpscaler> create_sgsr2_upscaler(unsigned render_width, unsigned render_height, unsigned output_width,
                                                        unsigned output_height, VkFormat output_format) {
    auto upscaler = std::make_unique<Sgsr2Upscaler>();
    if (!upscaler->create(render_width, render_height, output_width, output_height, output_format)) {
        std::cerr << "[sgsr2] creation failed for " << render_width << 'x' << render_height << " -> " << output_width << 'x'
                  << output_height << '\n';
        return nullptr;
    }
    return upscaler;
}

}  // namespace galaxy::render::vk
