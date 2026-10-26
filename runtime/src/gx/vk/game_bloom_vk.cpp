// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Vulkan (game_bloom_vk.h): game_bloom.cpp's chain with Vulkan's resources.
#include "galaxy/gx/vk/game_bloom_vk.h"

#include "galaxy/gx/game_bloom_shape.h"
#include "galaxy/gx/vk/shader_compile_vk.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace galaxy::gx {
// The shader source cmake/post_shader_sources.cmake embeds.
extern const unsigned char galaxy_post_game_bloom_hlsl[];
}  // namespace galaxy::gx

namespace galaxy::gx::vk {
namespace {

constexpr VkFormat kWorkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkShaderStageFlags kStages =
    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// game_bloom.hlsl's registers: t0-t2 sampled and u0-u2 storage images in set 0, s0 in set 1, the constants pushed.
constexpr RegisterBinding kBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1}, {"t2", 0, 2},
                                         {"u0", 0, 3},       {"u1", 0, 4}, {"u2", 0, 5}, {"s0", 1, 0}};

void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
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

// A pass's writes before the next pass's (or the composite's) reads.
void written(VkCommandBuffer commands) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
}

}  // namespace

void GameBloomVk::initialize() {
    shutdown();
    compiled_.store(false, std::memory_order_relaxed);
    worker_ = std::thread([this] {
        struct Source {
            Program program;
            ShaderStage stage;
            const char* entry;
        };
        static constexpr Source kSources[] = {{kDown, ShaderStage::Compute, "DownCS"},
                                              {kBox, ShaderStage::Compute, "BoxCS"},
                                              {kBlur, ShaderStage::Compute, "BlurCS"},
                                              {kCompositeVs, ShaderStage::Vertex, "CompositeVS"},
                                              {kCompositePs, ShaderStage::Fragment, "CompositePS"}};
        const std::string source =
            annotate(reinterpret_cast<const char*>(galaxy_post_game_bloom_hlsl), kBindings);
        for (const Source& s : kSources) {
            std::string error;
            spirv_[s.program] = compile_hlsl(source, s.stage, s.entry, {"BLOOM_STORAGE_FORMAT=1"}, error);
            if (spirv_[s.program].empty()) {
                std::cerr << "[bloom] " << s.entry << ": " << error << '\n';
            }
        }
        compiled_.store(true, std::memory_order_release);
    });
}

void GameBloomVk::shutdown() noexcept {
    if (worker_.joinable()) {
        worker_.join();
    }
    destroy();
    ready_ = false;
    kept_ = false;
    pending_ = false;
    for (std::shared_ptr<Texture>* t : {&dummy_, &keep_, &source_, &work_, &box_tmp_, &mask_, &level1_, &level2_in_,
                                        &level2_}) {
        t->reset();
    }
    for (auto& t : blur_h_) t.reset();
    for (auto& t : blur2_h_) t.reset();
}

bool GameBloomVk::poll_ready() {
    if (ready_) {
        return true;
    }
    if (!worker_.joinable() || !compiled_.load(std::memory_order_acquire)) {
        return false;
    }
    worker_.join();
    ready_ = build();
    if (!ready_) {
        destroy();
        std::cerr << "[bloom] Vulkan passes unavailable: the game draws its own bloom\n";
    }
    return ready_;
}

VkShaderModule GameBloomVk::module(Program program) const {
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

bool GameBloomVk::build() {
    const VkDevice device = Context::get().device();
    VkDescriptorSetLayoutBinding images[6]{};
    for (std::uint32_t i = 0; i < 6u; ++i) {
        images[i] = {i, i < 3u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kStages,
                     nullptr};
    }
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 6;
    set_info.pBindings = images;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &image_layout_) != VK_SUCCESS) {
        return false;
    }
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler_) != VK_SUCCESS) {
        return false;
    }
    const VkDescriptorSetLayoutBinding sampler{0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, kStages, &sampler_};
    set_info.bindingCount = 1;
    set_info.pBindings = &sampler;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &sampler_layout_) != VK_SUCCESS) {
        return false;
    }
    const VkDescriptorSetLayout sets[2] = {image_layout_, sampler_layout_};
    const VkPushConstantRange push{kStages, 0, sizeof(Constants)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 2;
    layout_info.pSetLayouts = sets;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        return false;
    }
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &sampler_pool_) != VK_SUCCESS) {
        return false;
    }
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = sampler_pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &sampler_layout_;
    if (vkAllocateDescriptorSets(device, &allocate, &sampler_set_) != VK_SUCCESS) {
        return false;
    }
    const Program programs[3] = {kDown, kBox, kBlur};
    const char* const entries[3] = {"DownCS", "BoxCS", "BlurCS"};
    for (unsigned i = 0; i < 3u; ++i) {
        const VkShaderModule shader = module(programs[i]);
        if (shader == VK_NULL_HANDLE) {
            return false;
        }
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = shader;
        info.stage.pName = entries[i];
        info.layout = layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &compute_[i]);
        vkDestroyShaderModule(device, shader, nullptr);
        if (result != VK_SUCCESS) {
            compute_[i] = VK_NULL_HANDLE;
            return false;
        }
    }
    // The composite's pipelines are made per EFB pass at first use; its programs must have compiled.
    return !spirv_[kCompositeVs].empty() && !spirv_[kCompositePs].empty();
}

void GameBloomVk::destroy() noexcept {
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    for (VkPipeline& pipeline : compute_) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    for (VkPipeline& pipeline : composite_) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    composite_pass_ = {};
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout_, nullptr);
    if (sampler_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, sampler_pool_, nullptr);
    for (VkDescriptorSetLayout set_layout : {image_layout_, sampler_layout_}) {
        if (set_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device, sampler_, nullptr);
    layout_ = VK_NULL_HANDLE;
    sampler_pool_ = VK_NULL_HANDLE;
    sampler_set_ = VK_NULL_HANDLE;
    image_layout_ = sampler_layout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
}

// The kept scene plus the bloom over the frame, RGB only, in an EFB pass of 2^samples_log2 samples.
VkPipeline GameBloomVk::composite_pipeline(VkRenderPass pass, unsigned samples_log2) {
    samples_log2 = std::min(samples_log2, 3u);
    if (composite_[samples_log2] != VK_NULL_HANDLE && composite_pass_[samples_log2] == pass) {
        return composite_[samples_log2];
    }
    const VkDevice device = Context::get().device();
    if (composite_[samples_log2] != VK_NULL_HANDLE) {
        const VkPipeline old = composite_[samples_log2];
        Context::get().defer([device, old] { vkDestroyPipeline(device, old, nullptr); });
        composite_[samples_log2] = VK_NULL_HANDLE;
    }
    const VkShaderModule vs = module(kCompositeVs);
    const VkShaderModule ps = module(kCompositePs);
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vs != VK_NULL_HANDLE && ps != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                     "CompositeVS", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                     "CompositePS", nullptr};
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
        target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
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
            std::cerr << "[bloom] composite pipeline failed for " << (1u << samples_log2) << " samples\n";
        }
    }
    if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(device, vs, nullptr);
    if (ps != VK_NULL_HANDLE) vkDestroyShaderModule(device, ps, nullptr);
    composite_[samples_log2] = pipeline;
    composite_pass_[samples_log2] = pass;
    return pipeline;
}

// A work image (storage and sampled, kept in GENERAL) or a copy target (the EFB's format, transfer destination and
// sampled), made again when its size or format changes; the old one lives while submissions name it.
bool GameBloomVk::ensure(std::shared_ptr<Texture>& target, unsigned width, unsigned height, VkFormat format, bool work,
                         const char* name) {
    if (target && target->width == width && target->height == height && target->format == format) {
        return true;
    }
    const VkImageUsageFlags usage = work ? VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                         : VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    target = Context::get().create_texture(width, height, 1u, format, usage, false, name);
    return target != nullptr;
}

bool GameBloomVk::copy_frame(VkCommandBuffer commands, const Inputs& in, std::shared_ptr<Texture>& target,
                             const char* name) {
    if (!ensure(target, in.frame_width, in.frame_height, in.color->format, false, name)) {
        return false;
    }
    image_barrier(commands, in.color->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    // The last reads of the copy (an earlier composite) finish before it is written again.
    image_barrier(commands, target->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {in.frame_width, in.frame_height, 1};
    vkCmdCopyImage(commands, in.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    image_barrier(commands, in.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(commands, target->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    target->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

bool GameBloomVk::keep(VkCommandBuffer commands, const Inputs& in) {
    kept_ = ready_ && in.color != nullptr && in.frame_width != 0u && in.frame_height != 0u &&
            copy_frame(commands, in, keep_, "bloom kept scene");
    return kept_;
}

VkDescriptorSet GameBloomVk::set_of(const SetAllocator& allocate, std::array<const Texture*, 3> sampled,
                                    std::array<const Texture*, 3> storage, VkImageLayout first_layout) const {
    const VkDescriptorSet set = allocate(image_layout_);
    VkDescriptorImageInfo infos[6]{};
    VkWriteDescriptorSet writes[6]{};
    for (unsigned i = 0; i < 6u; ++i) {
        const Texture* const t = i < 3u ? sampled[i] : storage[i - 3u];
        const Texture* const used = t != nullptr ? t : dummy_.get();
        // Copies are read shader-read-only, work images in GENERAL; the first slot's layout may be given (the EFB).
        const VkImageLayout layout = i == 0u && first_layout != VK_IMAGE_LAYOUT_UNDEFINED ? first_layout
                                     : used->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                         : VK_IMAGE_LAYOUT_GENERAL;
        infos[i] = {VK_NULL_HANDLE, used->view, layout};
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 3u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(Context::get().device(), 6, writes, 0, nullptr);
    return set;
}

void GameBloomVk::dispatch(VkCommandBuffer commands, Program program, const Constants& c, VkDescriptorSet set,
                           unsigned width, unsigned height) const {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, compute_[program]);
    const VkDescriptorSet sets[2] = {set, sampler_set_};
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 2, sets, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(Constants), &c);
    vkCmdDispatch(commands, (width + 7u) / 8u, (height + 7u) / 8u, 1u);
    written(commands);
}

bool GameBloomVk::prepare(VkCommandBuffer commands, const Inputs& in, const Params& p, const SetAllocator& allocate) {
    pending_ = false;
    const bool kept = std::exchange(kept_, false);
    const bool restore = p.kind == Kind::Restore;
    const bool normal = p.kind == Kind::Normal;
    if (!ready_ || in.color == nullptr || in.frame_width == 0u || in.frame_height == 0u || ((normal || restore) && !kept)) {
        return false;
    }
    composite_width_ = in.frame_width;
    composite_height_ = in.frame_height;
    composite_constants_ = {};
    if (restore) {
        // Kind 0: the scene alone, the levels unread.
        composite_inputs_[0] = composite_inputs_[1] = composite_inputs_[2] = keep_.get();
        composite_constants_.params[3] = 1.0f;  // the RGBA8 EFB
        pending_ = true;
        return true;
    }
    const bloom::Shape s = bloom::shape_of(in.frame_width, in.frame_height, in.logical_height, in.scale);
    const unsigned blurs1 = normal ? 2u : 1u;
    bool made = ensure(dummy_, 1u, 1u, kWorkFormat, true, "bloom unused slot") &&
                ensure(work_, s.w1, s.h1, kWorkFormat, true, "bloom work") &&
                ensure(box_tmp_, s.w1, s.h1, kWorkFormat, true, "bloom box") &&
                ensure(mask_, s.w1, s.h1, kWorkFormat, true, "bloom mask") &&
                ensure(level1_, s.w1, s.h1, kWorkFormat, true, "bloom level 1");
    for (unsigned i = 0; made && i < blurs1; ++i) {
        made = ensure(blur_h_[i], s.w1, s.h1, kWorkFormat, true, "bloom level 1 blur");
    }
    if (made && normal) {
        made = ensure(level2_in_, s.w2, s.h2, kWorkFormat, true, "bloom level 2 source") &&
               ensure(level2_, s.w2, s.h2, kWorkFormat, true, "bloom level 2");
        for (unsigned i = 0; made && i < 3u; ++i) {
            made = ensure(blur2_h_[i], s.w2, s.h2, kWorkFormat, true, "bloom level 2 blur");
        }
    }
    if (!made || (!normal && !copy_frame(commands, in, source_, "bloom source"))) {
        return false;
    }
    // The work images live in GENERAL (written as storage, read as sampled images); the earlier frame's reads of them
    // finish before this one writes.
    for (std::shared_ptr<Texture>* t : {&dummy_, &work_, &box_tmp_, &mask_, &level1_, &blur_h_[0], &blur_h_[1],
                                        &level2_in_, &level2_, &blur2_h_[0], &blur2_h_[1], &blur2_h_[2]}) {
        if (*t && (*t)->layout != VK_IMAGE_LAYOUT_GENERAL) {
            image_barrier(commands, (*t)->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_ACCESS_SHADER_WRITE_BIT);
            (*t)->layout = VK_IMAGE_LAYOUT_GENERAL;
        }
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

    const bloom::Widths w = bloom::widths_of(normal, s, in.frame_width, in.frame_height, in.logical_width,
                                             in.logical_height, p.ring_x, p.ring_y);
    // The selection's source: the glowing models drawn over black (normal: the EFB itself), the scene (simple).
    if (normal) {
        image_barrier(commands, in.color->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
    }
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = s.f;
        c.uv[0] = static_cast<float>(in.frame_width);
        c.uv[1] = static_cast<float>(in.frame_height);
        dispatch(commands, kDown, c,
                 set_of(allocate, {normal ? in.color : source_.get(), nullptr, nullptr}, {work_.get(), nullptr, nullptr},
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                 s.w1, s.h1);
    }
    if (normal) {
        image_barrier(commands, in.color->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    // The game's 4 x 4 pixel footprint (across, then down with the selection).
    const bool across = s.box > 1.0f;
    if (across) {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.params[0] = s.box;
        c.params[1] = -1.0f;
        dispatch(commands, kBox, c, set_of(allocate, {work_.get(), nullptr, nullptr}, {box_tmp_.get(), nullptr, nullptr}),
                 s.w1, s.h1);
    }
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[3] = 1u;
        c.params[0] = s.box;
        c.params[1] = static_cast<float>(std::min(p.threshold, 255u));
        c.params[2] = static_cast<float>(normal ? 0u : std::min(p.channel, 3u));
        dispatch(commands, kBox, c,
                 set_of(allocate, {(across ? box_tmp_ : work_).get(), nullptr, nullptr}, {mask_.get(), nullptr, nullptr}),
                 s.w1, s.h1);
    }
    // Level 1: the rings' Gaussians across, then down, summed at the taps' weight (normal: clamped, as the EFB holds
    // it).
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = blurs1;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.x1[i];
        dispatch(commands, kBlur, c,
                 set_of(allocate, {mask_.get(), nullptr, nullptr},
                        {blur_h_[0].get(), blurs1 > 1u ? blur_h_[1].get() : nullptr, nullptr}),
                 s.w1, s.h1);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.y1[i];
        c.params[0] =
            (normal ? bloom::kLevel1Taps : bloom::kSimpleTaps) * static_cast<float>(std::min(p.tap1, 255u)) / 255.0f;
        c.params[1] = normal ? 1.0f : 0.0f;
        dispatch(commands, kBlur, c,
                 set_of(allocate, {blur_h_[0].get(), blurs1 > 1u ? blur_h_[1].get() : nullptr, nullptr},
                        {level1_.get(), nullptr, nullptr}),
                 s.w1, s.h1);
    }
    // Level 2 (normal): level 1 in n x n boxes, its three rings' Gaussians.
    if (normal) {
        Constants c{};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = s.n;
        c.uv[0] = static_cast<float>(s.w1);
        c.uv[1] = static_cast<float>(s.h1);
        dispatch(commands, kDown, c, set_of(allocate, {level1_.get(), nullptr, nullptr}, {level2_in_.get(), nullptr, nullptr}),
                 s.w2, s.h2);
        c = {};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = 3u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.x2[i];
        dispatch(commands, kBlur, c,
                 set_of(allocate, {level2_in_.get(), nullptr, nullptr},
                        {blur2_h_[0].get(), blur2_h_[1].get(), blur2_h_[2].get()}),
                 s.w2, s.h2);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = w.y2[i];
        c.params[0] = bloom::kLevel2Taps * static_cast<float>(std::min(p.tap2, 255u)) / 255.0f;
        c.params[1] = 1.0f;
        dispatch(commands, kBlur, c,
                 set_of(allocate, {blur2_h_[0].get(), blur2_h_[1].get(), blur2_h_[2].get()},
                        {level2_.get(), nullptr, nullptr}),
                 s.w2, s.h2);
    }
    Constants& c = composite_constants_;
    c.params[0] = static_cast<float>(std::min(p.intensity, 255u)) / 255.0f;
    c.params[1] = std::max(p.strength, 0.0f);
    c.params[2] = normal ? 1.0f : 2.0f;
    c.params[3] = 1.0f;  // the RGBA8 EFB: the bloom rounded stochastically
    c.uv[0] = 1.0f / static_cast<float>(s.f * s.w1);
    c.uv[1] = 1.0f / static_cast<float>(s.f * s.h1);
    c.uv[2] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.w2, 1u));
    c.uv[3] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.h2, 1u));
    composite_inputs_[0] = normal ? keep_.get() : source_.get();
    composite_inputs_[1] = level1_.get();
    composite_inputs_[2] = normal ? level2_.get() : level1_.get();
    pending_ = true;
    return true;
}

void GameBloomVk::finish(VkCommandBuffer commands, VkRenderPass pass, unsigned samples_log2,
                         const SetAllocator& allocate) {
    if (!std::exchange(pending_, false)) {
        return;
    }
    const VkPipeline pipeline = composite_pipeline(pass, samples_log2);
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    if (!dummy_ && !ensure(dummy_, 1u, 1u, kWorkFormat, true, "bloom unused slot")) {
        return;
    }
    const VkDescriptorSet set =
        set_of(allocate, {composite_inputs_[0], composite_inputs_[1], composite_inputs_[2]}, {nullptr, nullptr, nullptr});
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDescriptorSet sets[2] = {set, sampler_set_};
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdPushConstants(commands, layout_, kStages, 0, sizeof(Constants), &composite_constants_);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(composite_width_), static_cast<float>(composite_height_),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {composite_width_, composite_height_}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdDraw(commands, 3, 1, 0, 0);
}

void GameBloomVk::hold(std::vector<std::shared_ptr<Texture>>& held) const {
    for (const std::shared_ptr<Texture>* t : {&dummy_, &keep_, &source_, &work_, &box_tmp_, &mask_, &level1_,
                                              &level2_in_, &level2_, &blur_h_[0], &blur_h_[1], &blur_h_[2],
                                              &blur2_h_[0], &blur2_h_[1], &blur2_h_[2]}) {
        if (*t && std::find(held.begin(), held.end(), *t) == held.end()) {
            held.push_back(*t);
        }
    }
}

}  // namespace galaxy::gx::vk
