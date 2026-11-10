// SPDX-License-Identifier: GPL-3.0-only
// What the Metal renderer's parts agree on for a draw: the pipeline a draw binds, what pipelines are built against,
// and a draw's own tables. Plain C++ (mtl_context.h): the shared renderer code names these through gpu_types.h.
#pragma once

#include "galaxy/render/mtl/mtl_context.h"

#include <array>
#include <cstdint>

namespace galaxy::render::mtl {

// A draw's pipeline. Metal splits what Direct3D 12 and Vulkan keep in one object: the programs, vertex layout, blend
// and target formats are the render pipeline state; depth and stencil are a state of their own; the rest is set on the
// encoder with each change of pipeline (core/gpu_encoder.h).
struct Pipeline {
    Object state = nullptr;          // id<MTLRenderPipelineState>
    Object depth_stencil = nullptr;  // id<MTLDepthStencilState>, shared by every pipeline of the same state
    std::uint8_t primitive = 3u;     // MTLPrimitiveType (3: triangles)
    std::uint8_t cull = 0u;          // MTLCullMode
    std::uint8_t winding = 0u;       // MTLWinding
    std::uint8_t depth_clip = 0u;    // MTLDepthClipMode (1: clamp)
    std::uint8_t fill = 0u;          // MTLTriangleFillMode
    std::uint32_t stencil_reference = 0u;
    float depth_bias = 0.0f, depth_slope = 0.0f, depth_bias_clamp = 0.0f;
};

// The formats and counts pipelines are built against (the renderer's pipeline_root()): a pipeline of scene variant
// `v` (render_state_key.h: samples, the wide scene, a texture target) with aux targets `a` draws to these.
struct RenderLayout {
    Format scene_color = Format::Rgb10a2;   // the scene: gamma, 10 bits a channel
    Format wide_color = Format::Rgba16f;    // the wide scene (kSceneVariantHdr)
    Format target_color = Format::Rgba8;    // a texture target (kSceneVariantTarget)
    Format depth = Format::Depth32fStencil8;
    Format motion = Format::Rg16f;          // aux 1 (and 4): the motion vectors
    Format reactive = Format::R8;           // aux 2 and 3: the reactive mask
    Format layer = Format::Rgba16f;         // aux 3: the effects layer's colour and its transmittance
    bool texture_arrays = false;            // the programs' array form (else their bound-table form)
    bool layer_targets = false;             // aux 3 can be made
    unsigned max_samples = 1u;
};

// A draw's own textures and samplers where the device has no arrays (the programs' bound-table form, sixteen maps):
// gpu::Table::ptr of the texture table points at a BoundTextures, of the sampler table at a BoundSamplers; the
// renderer owns both and keeps them until the GPU is done with the draws that name them. Unused slots hold the
// renderer's null texture and default sampler, never nil.
// With arrays, gpu::Table::ptr is the id<MTLBuffer> of resource ids itself (the texture array, the sampler array).
inline constexpr std::uint32_t kBoundTableSlots = 16u;
struct BoundTextures {
    std::array<Object, kBoundTableSlots> textures{};  // id<MTLTexture>, not retained here
};
struct BoundSamplers {
    std::array<Object, kBoundTableSlots> samplers{};  // id<MTLSamplerState>, not retained here
};

}  // namespace galaxy::render::mtl
