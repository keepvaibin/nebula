// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Vulkan (rt_backend.h): VK_KHR_ray_query in compute shaders, over VK_KHR_acceleration_structure.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/rt/rt_shaders.h"

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

}  // namespace galaxy::render::rt
