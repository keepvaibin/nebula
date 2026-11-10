// SPDX-License-Identifier: GPL-3.0-only
// SPIR-V to MSL through SPIRV-Cross (shader_msl.h).
#include "galaxy/render/mtl/shader_msl.h"

#include <spirv_msl.hpp>

#include <exception>
#include <vector>

namespace galaxy::render::mtl {

namespace {

constexpr std::uint32_t kBoundTableSlots = 16u;  // vk::kBoundTableSlots, the programs' compile-time table

spv::ExecutionModel model_of(MslStage stage) noexcept {
    switch (stage) {
    case MslStage::Vertex: return spv::ExecutionModelVertex;
    case MslStage::Fragment: return spv::ExecutionModelFragment;
    default: return spv::ExecutionModelGLCompute;
    }
}

// glslang's HLSL front end gives an array declared without a length (`Texture2D gTextures[]`) the length 1, and the
// programs index past it, as descriptor indexing lets them. SPIRV-Cross reads an array of one: the arrays of every
// texture and sampler (sets 1 and 2) become runtime arrays here, which is what they are. The instruction is a word
// shorter; a no-op takes the word left.
std::vector<std::uint32_t> with_runtime_arrays(std::span<const std::uint32_t> spirv) {
    constexpr std::uint32_t kOpNop = 0u, kOpTypeArray = 28u, kOpTypeRuntimeArray = 29u, kOpTypePointer = 32u,
                            kOpVariable = 59u, kOpDecorate = 71u, kDecorationDescriptorSet = 34u;
    std::vector<std::uint32_t> module(spirv.begin(), spirv.end());
    std::vector<std::uint32_t> array_sets;  // variables in sets 1 and 2
    for (std::size_t at = 5u; at < module.size();) {
        const std::uint32_t count = module[at] >> 16u, op = module[at] & 0xFFFFu;
        if (count == 0u || at + count > module.size()) {
            break;
        }
        if (op == kOpDecorate && count >= 4u && module[at + 2u] == kDecorationDescriptorSet &&
            (module[at + 3u] == 1u || module[at + 3u] == 2u)) {
            array_sets.push_back(module[at + 1u]);
        }
        at += count;
    }
    const auto contains = [](const std::vector<std::uint32_t>& list, std::uint32_t id) {
        for (const std::uint32_t entry : list) {
            if (entry == id) return true;
        }
        return false;
    };
    // The variables' pointer types, then what those point to.
    std::vector<std::uint32_t> pointers, pointees;
    for (std::size_t at = 5u; at < module.size();) {
        const std::uint32_t count = module[at] >> 16u, op = module[at] & 0xFFFFu;
        if (count == 0u || at + count > module.size()) {
            break;
        }
        if (op == kOpVariable && count >= 4u && contains(array_sets, module[at + 2u])) {
            pointers.push_back(module[at + 1u]);
        }
        at += count;
    }
    for (std::size_t at = 5u; at < module.size();) {
        const std::uint32_t count = module[at] >> 16u, op = module[at] & 0xFFFFu;
        if (count == 0u || at + count > module.size()) {
            break;
        }
        if (op == kOpTypePointer && count >= 4u && contains(pointers, module[at + 1u])) {
            pointees.push_back(module[at + 3u]);
        }
        at += count;
    }
    for (std::size_t at = 5u; at < module.size();) {
        const std::uint32_t count = module[at] >> 16u, op = module[at] & 0xFFFFu;
        if (count == 0u || at + count > module.size()) {
            break;
        }
        if (op == kOpTypeArray && count == 4u && contains(pointees, module[at + 1u])) {
            module[at] = (3u << 16u) | kOpTypeRuntimeArray;
            module[at + 3u] = (1u << 16u) | kOpNop;
        }
        at += count;
    }
    return module;
}

}  // namespace

std::string spirv_to_msl(std::span<const std::uint32_t> spirv, MslStage stage, const MslOptions& options,
                         std::string& error) {
    try {
        const bool arrays = !options.utility && !options.bound_tables && stage == MslStage::Fragment;
        spirv_cross::CompilerMSL compiler(arrays ? with_runtime_arrays(spirv)
                                                 : std::vector<std::uint32_t>(spirv.begin(), spirv.end()));
        spirv_cross::CompilerMSL::Options msl;
        msl.platform = options.ios ? spirv_cross::CompilerMSL::Options::iOS : spirv_cross::CompilerMSL::Options::macOS;
        // The deployment targets' language (macOS 12, iOS 15); arrays without a length need 3.0 (macOS 13, iOS 16), which
        // every device that reads them so has.
        msl.set_msl_version(arrays ? 3u : 2u, arrays ? 0u : 4u);
        // The HLSL sources are written for Direct3D's clip space, which is Metal's: nothing is turned.
        msl.enable_decoration_binding = false;
        msl.argument_buffers = arrays;
        msl.argument_buffers_tier = spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
        // Every draw binds the same arrays whatever a program reads of them.
        msl.force_active_argument_buffer_resources = arrays;
        compiler.set_msl_options(msl);

        const spv::ExecutionModel model = model_of(stage);
        const auto bind = [&](std::uint32_t set, std::uint32_t binding, std::uint32_t index, std::uint32_t count = 0u) {
            spirv_cross::MSLResourceBinding resource;
            resource.stage = model;
            resource.desc_set = set;
            resource.binding = binding;
            resource.count = count;
            resource.msl_buffer = resource.msl_texture = resource.msl_sampler = index;
            compiler.add_msl_resource_binding(resource);
        };
        if (options.utility) {
            // Plain indices for every binding the module declares.
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            const auto each = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& list) {
                for (const spirv_cross::Resource& resource : list) {
                    const std::uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                    const std::uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                    bind(set, binding, options.set_stride * set + binding);
                }
            };
            each(resources.uniform_buffers);
            each(resources.storage_buffers);
            each(resources.separate_images);
            each(resources.separate_samplers);
            each(resources.sampled_images);
            each(resources.storage_images);
            bind(spirv_cross::kPushConstDescSet, spirv_cross::kPushConstBinding, options.set_stride * 3u);
        } else {
            bind(0u, 0u, kBufferView);
            bind(0u, 1u, kBufferVertexRows);
            bind(0u, 2u, kBufferPixelRows);
            bind(0u, 3u, kBufferRows);
            bind(0u, 4u, kBufferInstances);
            bind(0u, 5u, kBufferTextureLists);
            bind(spirv_cross::kPushConstDescSet, spirv_cross::kPushConstBinding, kBufferDraw);
            if (arrays) {
                compiler.add_discrete_descriptor_set(0u);
                // Arrays without a length are read through the buffer's address.
                compiler.set_argument_buffer_device_address_space(1u, true);
                compiler.set_argument_buffer_device_address_space(2u, true);
                bind(1u, spirv_cross::kArgumentBufferBinding, kBufferTextureArray);
                bind(2u, spirv_cross::kArgumentBufferBinding, kBufferSamplerArray);
                bind(1u, 0u, 0u);
                bind(2u, 0u, 0u);
            } else {
                bind(1u, 0u, 0u, kBoundTableSlots);
                bind(2u, 0u, 0u, kBoundTableSlots);
            }
        }

        // One entry point a module, under one name.
        const auto entries = compiler.get_entry_points_and_stages();
        if (entries.empty()) {
            error = "the module has no entry point";
            return {};
        }
        compiler.rename_entry_point(entries[0].name, "main0", entries[0].execution_model);
        std::string text = compiler.compile();
        if (text.empty()) {
            error = "SPIRV-Cross wrote nothing";
        }
        return text;
    } catch (const std::exception& failure) {
        error = failure.what();
        return {};
    }
}

}  // namespace galaxy::render::mtl
