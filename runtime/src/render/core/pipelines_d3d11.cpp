// SPDX-License-Identifier: GPL-3.0-only
// core::Pipelines on Direct3D 11 (core/pipelines.h; pipelines_d3d12.cpp is the model, state for state). A pipeline is the
// two programs' shader objects, the input layout, and the blend, rasterizer and depth-stencil state objects; the encoder's
// replay binds them (RendererD3D11::flush_draws). Shader objects and input layouts are shared between the pipelines that use
// the same program bytes (the driver compiles each once); Direct3D 11 itself returns the same state object for the same
// description.
#include "render/core/pipelines.h"

#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/program_table.h"

#include <nebula_model.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {
namespace {

D3D11_BLEND blend_factor(std::uint32_t f) noexcept {
    switch (f) {
    case NEBULA_FACTOR_ZERO: return D3D11_BLEND_ZERO;
    case NEBULA_FACTOR_SRC_COLOR: return D3D11_BLEND_SRC_COLOR;
    case NEBULA_FACTOR_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
    case NEBULA_FACTOR_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
    case NEBULA_FACTOR_INV_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
    case NEBULA_FACTOR_DST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
    case NEBULA_FACTOR_INV_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
    case NEBULA_FACTOR_DST_COLOR: return D3D11_BLEND_DEST_COLOR;
    case NEBULA_FACTOR_INV_DST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
    default: return D3D11_BLEND_ONE;
    }
}

// Colour factors are invalid on the alpha channel.
D3D11_BLEND alpha_factor(D3D11_BLEND b) noexcept {
    switch (b) {
    case D3D11_BLEND_SRC_COLOR: return D3D11_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR: return D3D11_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
    default: return b;
    }
}

// A pipeline's input elements (pipelines_d3d12.cpp fill_inputs: the same semantics, formats, slots and zero inputs).
struct InputList {
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    void add(const char* name, UINT index, DXGI_FORMAT format, UINT slot, UINT offset) {
        elements.push_back({name, index, format, slot, offset, D3D11_INPUT_PER_VERTEX_DATA, 0u});
    }
    [[nodiscard]] bool has(const char* name, UINT index) const noexcept {
        for (const D3D11_INPUT_ELEMENT_DESC& element : elements) {
            if (element.SemanticIndex == index && std::strcmp(element.SemanticName, name) == 0) {
                return true;
            }
        }
        return false;
    }
};

DXGI_FORMAT input_format(std::uint32_t format) noexcept {
    switch (format) {
    case 0: return DXGI_FORMAT_R32G32B32_FLOAT;
    case 1: return DXGI_FORMAT_R32G32_FLOAT;
    case 2: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default: return DXGI_FORMAT_R32_UINT;
    }
}

void fill_inputs(const PipelineEntry& entry, InputList& list) {
    const NebulaPipelineDesc& desc = entry.desc;
    const bool site = (desc.vertex & NEBULA_PROGRAM_SITE) != 0u;
    const bool wide = site && (desc.vertex & 1u) != 0u;
    list.add("POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, gpu::kStreamPosition, 0u);
    if (desc.position_stride == 0u) {
        list.add("BLENDINDICES", 0u, DXGI_FORMAT_R32_UINT, gpu::kStreamMatrix, 0u);
    }
    for (const NebulaLayoutInput& input : entry.inputs) {
        const UINT slot = input.stream == NEBULA_STREAM_ZERO ? UINT{gpu::kStreamZero} : UINT{input.stream};
        if (slot >= gpu::kStreamCount) {
            continue;  // no such buffer slot: the input is left out
        }
        const DXGI_FORMAT format = input_format(input.format);
        switch (input.semantic) {
        case NEBULA_INPUT_NORMAL: list.add("NORMAL", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_COLOR0: list.add("COLOR", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_COLOR1: list.add("COLOR", 1u, format, slot, input.offset); break;
        case NEBULA_INPUT_TANGENT: list.add("TANGENT", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_BINORMAL: list.add("BINORMAL", 0u, format, slot, input.offset); break;
        case NEBULA_INPUT_WIDEN: list.add("BLENDINDICES", 1u, format, slot, input.offset); break;
        case NEBULA_INPUT_MATRIX: list.add("BLENDINDICES", 0u, format, slot, input.offset); break;
        default:
            if (input.semantic >= NEBULA_INPUT_COORD0) {
                list.add("TEXCOORD", input.semantic - NEBULA_INPUT_COORD0, format, slot, input.offset);
            }
            break;
        }
    }
    const auto zero = [&](const char* name, UINT index, DXGI_FORMAT format) {
        if (!list.has(name, index)) {
            list.add(name, index, format, gpu::kStreamZero, 0u);
        }
    };
    // A ring's vertices name no matrix: every one is the item's first.
    zero("BLENDINDICES", 0u, DXGI_FORMAT_R32_UINT);
    if (!site) {
        return;
    }
    // A written shader's vertex declares everything: what this site doesn't send reads zero.
    zero("NORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    zero("TANGENT", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    if (wide) {
        zero("BLENDINDICES", 1u, DXGI_FORMAT_R32_UINT);
    }
    zero("BINORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT);
    zero("COLOR", 0u, DXGI_FORMAT_R8G8B8A8_UNORM);
    zero("COLOR", 1u, DXGI_FORMAT_R8G8B8A8_UNORM);
    const UINT declared = program_coords(desc.vertex);
    for (UINT i = 0; i < declared; ++i) {
        zero("TEXCOORD", i, DXGI_FORMAT_R32G32_FLOAT);
    }
}

// The device's shader objects and input layouts, shared by the building threads. A program's bytes stay where the
// programs' file put them for the run, so their address names them.
struct Shaders {
    std::mutex lock;
    std::unordered_map<const void*, d3d11::ComPtr<ID3D11VertexShader>> vertex;
    std::unordered_map<const void*, d3d11::ComPtr<ID3D11PixelShader>> pixel;
    std::unordered_map<std::string, d3d11::ComPtr<ID3D11InputLayout>> layouts;  // the vertex bytes' address and the elements
};
Shaders gShaders;

// Made outside the lock: other builds go on meanwhile; of two threads that make the same object one is kept.
template <typename Object, typename Make>
d3d11::ComPtr<Object> shared_object(std::unordered_map<const void*, d3d11::ComPtr<Object>>& map, const void* key, Make make) {
    {
        const std::lock_guard guard(gShaders.lock);
        if (const auto it = map.find(key); it != map.end()) {
            return it->second;
        }
    }
    d3d11::ComPtr<Object> made;
    if (FAILED(make(made))) {
        return nullptr;
    }
    const std::lock_guard guard(gShaders.lock);
    return map.emplace(key, std::move(made)).first->second;
}

std::string layout_key(const void* vertex, const InputList& inputs) {
    std::string key(reinterpret_cast<const char*>(&vertex), sizeof(vertex));
    for (const D3D11_INPUT_ELEMENT_DESC& element : inputs.elements) {
        key.append(element.SemanticName);
        const std::uint32_t words[4] = {element.SemanticIndex, static_cast<std::uint32_t>(element.Format), element.InputSlot,
                                        element.AlignedByteOffset};
        key.append(reinterpret_cast<const char*>(words), sizeof(words));
    }
    return key;
}

}  // namespace

gpu::Pipeline* Pipelines::build(const PipelineEntry& entry, unsigned variant, unsigned aux) {
    // Direct3D 11 draws no aux targets (no temporal upscaler, frame generation or tracer): the programs' file holds the
    // base and per-sample forms only (compile_programs.py DXBC_FORMS).
    if (aux != 0u) {
        return nullptr;
    }
    const NebulaPipelineDesc& desc = entry.desc;
    const unsigned samples_log2 = variant & kSceneVariantSamplesMask;
    const unsigned program = program_variant(desc.pixel, aux, (desc.state & NEBULA_STATE_PIXEL_SHADING) != 0u ? 0u : samples_log2);
    const ProgramBytes vs = program_bytes(desc.vertex, program, false);
    const ProgramBytes ps = program_bytes(desc.pixel, program, true);
    if (vs.size == 0u || ps.size == 0u) {
        std::fprintf(stderr, "[pipelines] no program %llx %llx variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    ID3D11Device1* const device = device_;
    const d3d11::ComPtr<ID3D11VertexShader> vertex = shared_object(gShaders.vertex, vs.data, [&](d3d11::ComPtr<ID3D11VertexShader>& out) {
        return device->CreateVertexShader(vs.data, vs.size, nullptr, &out);
    });
    const d3d11::ComPtr<ID3D11PixelShader> pixel = shared_object(gShaders.pixel, ps.data, [&](d3d11::ComPtr<ID3D11PixelShader>& out) {
        return device->CreatePixelShader(ps.data, ps.size, nullptr, &out);
    });
    if (!vertex || !pixel) {
        std::fprintf(stderr, "[pipelines] the driver refused a program: %llx %llx variant %u\n",
                     static_cast<unsigned long long>(desc.vertex), static_cast<unsigned long long>(desc.pixel), program);
        return nullptr;
    }
    InputList inputs;
    fill_inputs(entry, inputs);
    if (inputs.elements.size() > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) {
        std::fprintf(stderr, "[pipelines] %zu vertex inputs: the device takes %u\n", inputs.elements.size(),
                     static_cast<unsigned>(D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT));
        return nullptr;
    }
    d3d11::ComPtr<ID3D11InputLayout> input;
    {
        const std::string key = layout_key(vs.data, inputs);
        {
            const std::lock_guard guard(gShaders.lock);
            if (const auto it = gShaders.layouts.find(key); it != gShaders.layouts.end()) {
                input = it->second;
            }
        }
        if (!input) {
            if (FAILED(device->CreateInputLayout(inputs.elements.data(), static_cast<UINT>(inputs.elements.size()), vs.data, vs.size,
                                                 &input))) {
                std::fprintf(stderr, "[pipelines] input layout refused: %llx %llx\n", static_cast<unsigned long long>(desc.vertex),
                             static_cast<unsigned long long>(desc.pixel));
                return nullptr;
            }
            const std::lock_guard guard(gShaders.lock);
            input = gShaders.layouts.emplace(key, input).first->second;
        }
    }

    const std::uint32_t state = desc.state;
    const bool target = (variant & kSceneVariantTarget) != 0u;
    D3D11_BLEND_DESC1 blend{};
    D3D11_RENDER_TARGET_BLEND_DESC1& rt = blend.RenderTarget[0];
    rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.LogicOp = D3D11_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = static_cast<UINT8>(
        ((state & NEBULA_STATE_COLOR_WRITE) != 0u
             ? D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE
             : 0) |
        ((state & (NEBULA_STATE_ALPHA_WRITE | NEBULA_STATE_ALPHA_PASS)) != 0u && target ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0));
    const std::uint32_t op = state & 3u;
    if (op != NEBULA_BLEND_REPLACE) {
        rt.BlendEnable = TRUE;
        if (op == NEBULA_BLEND_SUBTRACT) {
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_ONE;
            rt.BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
        } else {
            rt.SrcBlend = blend_factor((state >> 2u) & 15u);
            rt.DestBlend = blend_factor((state >> 6u) & 15u);
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            if (!target) {
                // No destination alpha in the scene: it reads as one.
                if (rt.SrcBlend == D3D11_BLEND_DEST_ALPHA) rt.SrcBlend = D3D11_BLEND_ONE;
                else if (rt.SrcBlend == D3D11_BLEND_INV_DEST_ALPHA) rt.SrcBlend = D3D11_BLEND_ZERO;
                if (rt.DestBlend == D3D11_BLEND_DEST_ALPHA) rt.DestBlend = D3D11_BLEND_ONE;
                else if (rt.DestBlend == D3D11_BLEND_INV_DEST_ALPHA) rt.DestBlend = D3D11_BLEND_ZERO;
            }
        }
        rt.SrcBlendAlpha = alpha_factor(rt.SrcBlend);
        rt.DestBlendAlpha = alpha_factor(rt.DestBlend);
        rt.BlendOpAlpha = rt.BlendOp;
    }

    D3D11_RASTERIZER_DESC1 raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    // Culling all has no state: the walker draws nothing.
    switch ((state >> 15u) & 3u) {
    case NEBULA_CULL_BACK: raster.CullMode = D3D11_CULL_BACK; break;
    case NEBULA_CULL_FRONT: raster.CullMode = D3D11_CULL_FRONT; break;
    default: raster.CullMode = D3D11_CULL_NONE; break;
    }
    raster.FrontCounterClockwise = FALSE;
    raster.DepthClipEnable = TRUE;
    // Direct3D 12's scissor is always on; here it is the rasterizer's choice, and every draw has one (bind_scene).
    raster.ScissorEnable = TRUE;
    raster.MultisampleEnable = samples_log2 != 0u;

    // Depth is reversed: the game's less is greater.
    static constexpr D3D11_COMPARISON_FUNC kDepth[8] = {
        D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_GREATER,   D3D11_COMPARISON_EQUAL,
        D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_LESS,      D3D11_COMPARISON_NOT_EQUAL,
        D3D11_COMPARISON_LESS_EQUAL,    D3D11_COMPARISON_ALWAYS,
    };
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if ((state & (1u << 10u)) != 0u) {
        depth.DepthEnable = TRUE;
        depth.DepthFunc = kDepth[(state >> 11u) & 7u];
        depth.DepthWriteMask = (state & (1u << 14u)) != 0u ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    }
    D3D11_DEPTH_STENCILOP_DESC face{};
    face.StencilFailOp = face.StencilDepthFailOp = face.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    face.StencilFunc = D3D11_COMPARISON_ALWAYS;
    depth.FrontFace = depth.BackFace = face;
    depth.StencilReadMask = depth.StencilWriteMask = 0xFFu;
    if (const std::uint32_t test = (state >> 20u) & 3u; test != 0u) {
        static constexpr D3D11_COMPARISON_FUNC kStencilTest[4] = {D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_ALWAYS,
                                                                  D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_NOT_EQUAL};
        static constexpr D3D11_STENCIL_OP kStencilOp[4] = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR_SAT, D3D11_STENCIL_OP_DECR_SAT,
                                                           D3D11_STENCIL_OP_ZERO};
        depth.StencilEnable = TRUE;
        face.StencilPassOp = kStencilOp[(state >> 22u) & 3u];
        face.StencilFunc = kStencilTest[test];
        depth.FrontFace = depth.BackFace = face;
    }

    auto pipeline = std::make_unique<gpu::Pipeline>();
    pipeline->vertex = vertex;
    pipeline->pixel = pixel;
    pipeline->input = input;
    if (FAILED(device->CreateBlendState1(&blend, &pipeline->blend)) || FAILED(device->CreateRasterizerState1(&raster, &pipeline->rasterizer)) ||
        FAILED(device->CreateDepthStencilState(&depth, &pipeline->depth_stencil))) {
        std::fprintf(stderr, "[pipelines] state refused: %llx %llx state %08x variant %u\n", static_cast<unsigned long long>(desc.vertex),
                     static_cast<unsigned long long>(desc.pixel), state, variant);
        return nullptr;
    }
    pipeline->topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gpu::Pipeline* const raw = pipeline.get();
    const std::lock_guard guard(lock_);
    owned_.push_back(pipeline.release());
    return raw;
}

// Direct3D 11 links nothing from parts: a draw that waits builds the whole pipeline.
gpu::Pipeline* Pipelines::build_fast(const PipelineEntry&, unsigned, unsigned) {
    return nullptr;
}

void Pipelines::destroy_all() noexcept {
    // The immediate context keeps what frames in flight bound until the GPU is done with them.
    for (gpu::Pipeline* pipeline : owned_) {
        delete pipeline;
    }
    owned_.clear();
    const std::lock_guard guard(gShaders.lock);
    gShaders.vertex.clear();
    gShaders.pixel.clear();
    gShaders.layouts.clear();
}

// Nothing of the renderer's own to write: Direct3D 11 drivers keep the shaders they compile in their own disk caches.
std::size_t Pipelines::save_cache() noexcept {
    return 0u;
}

// No file of the renderer's own, so nothing says what the driver's cache holds: every texture target form is prepared.
bool Pipelines::cache_file_state(std::filesystem::path&, std::uint64_t&) {
    return false;
}

std::uint64_t Pipelines::cache_file_hash() noexcept {
    return 0u;
}

}  // namespace galaxy::render::core
