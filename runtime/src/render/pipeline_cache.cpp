// SPDX-License-Identifier: GPL-3.0-only
// PipelineCache: native materials' pipelines (E-04). Each is a material's build-compiled shaders (native_shaders.h)
// under a render state, created on its first draw and kept for the session.

#include "galaxy/render/pipeline_cache.h"
#include "galaxy/run_options.h"
#include "galaxy/render/pipeline_seed.h"
#include "galaxy/windows_power.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "galaxy/render/native_shaders.h"
#include "galaxy/render/renderer_d3d12.h"


#include <cstring>
#include <iostream>

namespace galaxy::render {

namespace {

constexpr D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
    {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",        0, DXGI_FORMAT_R32_UINT,        0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",        1, DXGI_FORMAT_R32_UINT,        0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,    0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     2, DXGI_FORMAT_R32G32_FLOAT,    0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     3, DXGI_FORMAT_R32G32_FLOAT,    0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     4, DXGI_FORMAT_R32G32_FLOAT,    0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     5, DXGI_FORMAT_R32G32_FLOAT,    0, 96, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     6, DXGI_FORMAT_R32G32_FLOAT,    0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     7, DXGI_FORMAT_R32G32_FLOAT,    0, 112, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32_UINT, 0, 120, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BEND",         0, DXGI_FORMAT_R32G32B32_UINT,  0, 132, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// game BlendFactor -> D3D12, src side.
D3D12_BLEND src_blend(std::uint32_t f) {
    switch (f) {
    case 0: return D3D12_BLEND_ZERO;
    case 1: return D3D12_BLEND_ONE;
    case 2: return D3D12_BLEND_DEST_COLOR;
    case 3: return D3D12_BLEND_INV_DEST_COLOR;
    case 4: return D3D12_BLEND_SRC_ALPHA;
    case 5: return D3D12_BLEND_INV_SRC_ALPHA;
    case 6: return D3D12_BLEND_DEST_ALPHA;
    case 7: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return D3D12_BLEND_ONE;
    }
}

// game BlendFactor -> D3D12, dst side.
D3D12_BLEND dst_blend(std::uint32_t f) {
    switch (f) {
    case 0: return D3D12_BLEND_ZERO;
    case 1: return D3D12_BLEND_ONE;
    case 2: return D3D12_BLEND_SRC_COLOR;
    case 3: return D3D12_BLEND_INV_SRC_COLOR;
    case 4: return D3D12_BLEND_SRC_ALPHA;
    case 5: return D3D12_BLEND_INV_SRC_ALPHA;
    case 6: return D3D12_BLEND_DEST_ALPHA;
    case 7: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return D3D12_BLEND_ZERO;
    }
}

// Color-only blend factors are invalid for the alpha channel; demote them.
D3D12_BLEND to_alpha_blend(D3D12_BLEND b) {
    switch (b) {
    case D3D12_BLEND_SRC_COLOR:      return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR:  return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR:     return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    default:                         return b;
    }
}

// game LogicOp 0-15 -> D3D12.
D3D12_LOGIC_OP logic_op_mode(std::uint32_t m) {
    switch (m) {
    case 0:  return D3D12_LOGIC_OP_CLEAR;
    case 1:  return D3D12_LOGIC_OP_AND;
    case 2:  return D3D12_LOGIC_OP_AND_REVERSE;
    case 3:  return D3D12_LOGIC_OP_COPY;
    case 4:  return D3D12_LOGIC_OP_AND_INVERTED;
    case 5:  return D3D12_LOGIC_OP_NOOP;
    case 6:  return D3D12_LOGIC_OP_XOR;
    case 7:  return D3D12_LOGIC_OP_OR;
    case 8:  return D3D12_LOGIC_OP_NOR;
    case 9:  return D3D12_LOGIC_OP_EQUIV;
    case 10: return D3D12_LOGIC_OP_INVERT;
    case 11: return D3D12_LOGIC_OP_OR_REVERSE;
    case 12: return D3D12_LOGIC_OP_COPY_INVERTED;
    case 13: return D3D12_LOGIC_OP_OR_INVERTED;
    case 14: return D3D12_LOGIC_OP_NAND;
    case 15: return D3D12_LOGIC_OP_SET;
    default: return D3D12_LOGIC_OP_NOOP;
    }
}

// Compile one HLSL source; logs the compiler error (capped) on failure.
// `fast` skips FXC optimization: the first-use path publishes that pipeline
// at once and a worker replaces it with the level-1 build (see get()).
D3D12_GRAPHICS_PIPELINE_STATE_DESC make_pso_desc_impl(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps,
    D3D12_SHADER_BYTECODE hull_shader,
    D3D12_SHADER_BYTECODE domain_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature;
    pd.VS = vs;
    pd.PS = ps;
    pd.InputLayout = {kInputLayout,
                      static_cast<UINT>(std::size(kInputLayout))};

    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    const std::uint32_t bb = rs.blend_bits;
    // game scene target write masks (key bits 25/26 = cmode0 color_update/alpha_update).
    // Z-clear / z-prepass draws disable color update while writing depth.
    rt.RenderTargetWriteMask = static_cast<UINT8>(
        ((bb & (1u << 25)) ? (D3D12_COLOR_WRITE_ENABLE_RED |
                              D3D12_COLOR_WRITE_ENABLE_GREEN |
                              D3D12_COLOR_WRITE_ENABLE_BLUE) : 0) |
        ((bb & (1u << 26)) ? static_cast<int>(D3D12_COLOR_WRITE_ENABLE_ALPHA) : 0));
    if (bb & 1u) {                       // blend_enable
        rt.BlendEnable = TRUE;
        const bool subtract = (bb & 2u) != 0;
        if (subtract) {
            // game subtract: dst = dst - src, factors forced to one.
            rt.SrcBlend  = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp   = D3D12_BLEND_OP_REV_SUBTRACT;
        } else {
            rt.SrcBlend  = src_blend((bb >> 2) & 0x7u);
            rt.DestBlend = dst_blend((bb >> 5) & 0x7u);
            rt.BlendOp   = D3D12_BLEND_OP_ADD;
            if (rs.pixfmt != 1u) {
                if (rt.SrcBlend == D3D12_BLEND_DEST_ALPHA)
                    rt.SrcBlend = D3D12_BLEND_ONE;
                else if (rt.SrcBlend == D3D12_BLEND_INV_DEST_ALPHA)
                    rt.SrcBlend = D3D12_BLEND_ZERO;
                if (rt.DestBlend == D3D12_BLEND_DEST_ALPHA)
                    rt.DestBlend = D3D12_BLEND_ONE;
                else if (rt.DestBlend == D3D12_BLEND_INV_DEST_ALPHA)
                    rt.DestBlend = D3D12_BLEND_ZERO;
            }
        }
        rt.SrcBlendAlpha  = to_alpha_blend(rt.SrcBlend);
        rt.DestBlendAlpha = to_alpha_blend(rt.DestBlend);
        rt.BlendOpAlpha   = rt.BlendOp;
        if (bb & (1u << 27)) {           // dual-source dst-alpha
            // The PS exports the material alpha on SV_Target1 (the matching
            // PixelShaderKey flags bit7 is derived from the same register
            // snapshot): RGB blends against SRC1_ALPHA while the alpha
            // channel keeps its independently selected alpha equation.
            rt.SrcBlend = (rt.SrcBlend == D3D12_BLEND_SRC_ALPHA)
                ? D3D12_BLEND_SRC1_ALPHA
                : (rt.SrcBlend == D3D12_BLEND_INV_SRC_ALPHA)
                    ? D3D12_BLEND_INV_SRC1_ALPHA
                    : rt.SrcBlend;
            rt.DestBlend = (rt.DestBlend == D3D12_BLEND_SRC_ALPHA)
                ? D3D12_BLEND_SRC1_ALPHA
                : (rt.DestBlend == D3D12_BLEND_INV_SRC_ALPHA)
                    ? D3D12_BLEND_INV_SRC1_ALPHA
                    : rt.DestBlend;
        }
        if (bb & (1u << 28)) { // Constant destination alpha is independent.
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        if (bb & kRenderStateZeroAlpha) { // Constant destination alpha 0, drawn single-source.
            rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
    } else if (bb & (1u << 8)) {         // logic_op_enable
        rt.LogicOpEnable = TRUE;
        rt.LogicOp = logic_op_mode((bb >> 9) & 0xFu);
    }

    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // game screen space is Y-down relative to D3D NDC (the scene target viewport does not flip), so a game
    // front face lands as a D3D back face and vice versa.
    // CullMode::All (rasterize nothing) has no PSO equivalent and maps to None.
    switch (static_cast<CullMode>(rs.cull)) {
    case CullMode::Front:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        break;
    case CullMode::Back:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        break;
    case CullMode::None:
    case CullMode::All:
    default:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    }
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;

    // Depth is reversed-Z relative to the game depth values (near=1, far=0; the projection negation
    // in render_core.cpp fill_projection), so game LESS maps to D3D GREATER, game LEQUAL to
    // GREATER_EQUAL, and so on.
    static constexpr D3D12_COMPARISON_FUNC kDepthFunc[8] = {
        D3D12_COMPARISON_FUNC_NEVER,           // NEVER
        D3D12_COMPARISON_FUNC_GREATER,         // LESS     (reversed-Z)
        D3D12_COMPARISON_FUNC_EQUAL,           // EQUAL
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,   // LEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_LESS,            // GREATER  (reversed-Z)
        D3D12_COMPARISON_FUNC_NOT_EQUAL,       // NEQUAL
        D3D12_COMPARISON_FUNC_LESS_EQUAL,      // GEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_ALWAYS,          // ALWAYS
    };
    const std::uint32_t zb = rs.zmode_bits;
    if (zb & 1u) {
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthFunc = kDepthFunc[(zb >> 1) & 0x7u];
        pd.DepthStencilState.DepthWriteMask = (zb & (1u << 4))
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
    }

    pd.SampleMask            = UINT_MAX;
    switch (static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
        rs.primitive_topology & kTopologyTypeMask)) {
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
        // A curved surface's patches (kVsKeyCurved): the hull and domain shaders of its vertex output's variant.
        if (hull_shader.pShaderBytecode == nullptr || domain_shader.pShaderBytecode == nullptr) {
            throw std::invalid_argument(
                "curved PSO requires the curved hull and domain shaders");
        }
        pd.HS = hull_shader;
        pd.DS = domain_shader;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        break;
    default:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    }
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = (rs.samples_log2 & kSceneVariantHdr) != 0u ? kSceneHdrColorFormat : kSceneColorFormat;
    if ((bb & kRenderStateMotionTarget) != 0u) {
        // Opaque motion writer: render target 1 takes the motion vectors
        // with render target 0's write mask (independent blend off).
        pd.NumRenderTargets = 2;
        pd.RTVFormats[1] = kMotionVectorFormat;
    } else if ((bb & kRenderStateReactiveTarget) != 0u) {
        // Blended draw: render target 1 keeps the largest reactive value.
        pd.NumRenderTargets = 2;
        pd.RTVFormats[1] = kReactiveMaskFormat;
        pd.BlendState.IndependentBlendEnable = TRUE;
        D3D12_RENDER_TARGET_BLEND_DESC& mask = pd.BlendState.RenderTarget[1];
        mask.BlendEnable = TRUE;
        mask.SrcBlend = D3D12_BLEND_ONE;
        mask.DestBlend = D3D12_BLEND_ONE;
        mask.BlendOp = D3D12_BLEND_OP_MAX;
        mask.SrcBlendAlpha = D3D12_BLEND_ONE;
        mask.DestBlendAlpha = D3D12_BLEND_ONE;
        mask.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        mask.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
        // The effects layer: target 2 blends the draw's colour as the scene target does (what it adds over what lies behind),
        // target 3 takes the scene target's destination factor on its own (what of that it lets through: transmittance).
        // The backend sets the layer only for single-source blends whose factors never read the scene target.
        if ((bb & kRenderStateLayerTarget) != 0u) {
            pd.NumRenderTargets = 4;
            pd.RTVFormats[2] = kLayerFormat;
            pd.RTVFormats[3] = kLayerFormat;
            const D3D12_RENDER_TARGET_BLEND_DESC& scene_target = pd.BlendState.RenderTarget[0];
            D3D12_RENDER_TARGET_BLEND_DESC& layer = pd.BlendState.RenderTarget[2];
            layer = scene_target;
            layer.LogicOpEnable = FALSE;
            layer.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                          D3D12_COLOR_WRITE_ENABLE_BLUE;
            D3D12_RENDER_TARGET_BLEND_DESC& trans = pd.BlendState.RenderTarget[3];
            trans = layer;
            trans.SrcBlend = D3D12_BLEND_ZERO;
            trans.BlendOp = D3D12_BLEND_OP_ADD;
            trans.DestBlend = scene_target.BlendOp == D3D12_BLEND_OP_ADD ? scene_target.DestBlend : D3D12_BLEND_ONE;
        }
    }
    pd.DSVFormat             = kSceneDepthFormat;
    pd.SampleDesc.Count      = 1u << (rs.samples_log2 & kSceneVariantSamplesMask);

    return pd;
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc and
// create the pipeline.  Returns null on failure (logged, capped).
Microsoft::WRL::ComPtr<ID3D12PipelineState> create_pso(
    ID3D12Device* device,
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps,
    D3D12_SHADER_BYTECODE hull_shader,
    D3D12_SHADER_BYTECODE domain_shader) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd =
        make_pso_desc_impl(
            root_signature,
            rs,
            vs,
            ps,
            hull_shader,
            domain_shader);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const HRESULT hr =
        device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        static std::atomic<int> s_pso_logs{0};
        if (s_pso_logs.fetch_add(1) < 4) {
            std::cerr << "[PipelineCache] CreateGraphicsPipelineState failed"
                         " HRESULT=0x"
                      << std::hex << static_cast<unsigned>(hr) << std::dec
                      << '\n';
        }
        return nullptr;
    }
    return pso;
}

}  // anonymous namespace

D3D12_GRAPHICS_PIPELINE_STATE_DESC make_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    D3D12_SHADER_BYTECODE vertex_shader,
    D3D12_SHADER_BYTECODE pixel_shader,
    D3D12_SHADER_BYTECODE hull_shader,
    D3D12_SHADER_BYTECODE domain_shader) {
    return make_pso_desc_impl(
        root_signature,
        render_state,
        vertex_shader,
        pixel_shader,
        hull_shader,
        domain_shader);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::create_native(std::uint32_t shader, const RenderStateKey& rs,
                                                                          unsigned variant) const {
    const auto code = [](const NativeShaderBytes& bytes) {
        return bytes.size != 0u ? D3D12_SHADER_BYTECODE{bytes.data, bytes.size} : D3D12_SHADER_BYTECODE{};
    };
    const D3D12_SHADER_BYTECODE vs = code(native_shader_bytes(shader, variant, false));
    const D3D12_SHADER_BYTECODE ps = code(native_shader_bytes(shader, variant, true));
    // A curved surface's patches: the native patch stages for the output layout.
    const bool curved = (rs.primitive_topology & kTopologyTypeMask) == kTopologyPatch;
    const bool motion = variant == kNativeVariantMotion;
    const D3D12_SHADER_BYTECODE hs = curved ? code(native_curved_bytes(motion, false)) : D3D12_SHADER_BYTECODE{};
    const D3D12_SHADER_BYTECODE ds = curved ? code(native_curved_bytes(motion, true)) : D3D12_SHADER_BYTECODE{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    if (vs.pShaderBytecode && ps.pShaderBytecode && (!curved || (hs.pShaderBytecode && ds.pShaderBytecode))) {
        pso = create_pso(device_.Get(), root_signature_.Get(), rs, vs, ps, hs, ds);
    }
    return pso;
}

ID3D12PipelineState* PipelineCache::get_native(std::uint32_t shader, const RenderStateKey& render_state) {
    if (!device_ || !root_signature_) {
        return nullptr;
    }
    const RenderStateKey rs = native_render_state(render_state);
    const unsigned variant = native_shader_variant(rs.blend_bits);
    const std::uint32_t id = shader * kNativeVariants + variant;
    const std::uint64_t hash = combine64(hash_key_words(&rs, sizeof(rs)), id);
    auto& bucket = native_psos_[hash];
    for (const auto& entry : bucket) {
        if (entry.first.first == id && entry.first.second == rs) {
            return entry.second.Get();
        }
    }
    note_pipeline_for_seed(shader, render_state);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = create_native(shader, rs, variant);
    bucket.emplace_back(std::make_pair(id, rs), pso);
    return pso.Get();
}

void PipelineCache::start_prepare() {
    if (pipeline_seeds().empty()) {
        return;
    }
    const auto prepare_started_at = std::chrono::steady_clock::now();
    for (unsigned t = 0; t < 2u; ++t) {
        prepare_threads_.emplace_back([this, prepare_started_at] {
            galaxy::host::name_current_thread(L"Nebula Pipeline Prepare");
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto seeds = pipeline_seeds();
            while (!prepare_stop_.load(std::memory_order_relaxed)) {
                const std::size_t i = prepare_next_.fetch_add(1u, std::memory_order_relaxed);
                if (i >= seeds.size()) {
                    if (i == seeds.size()) {
                        std::fprintf(stderr, "[pipelines] %zu prepared at %llu ms\n", seeds.size(),
                                     static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - prepare_started_at).count()));
                    }
                    return;
                }
                const RenderStateKey rs = native_render_state(seed_render_state(seeds[i]));
                Prepared out{seeds[i].shader, rs, create_native(seeds[i].shader, rs, native_shader_variant(rs.blend_bits))};
                const std::lock_guard guard(prepared_lock_);
                prepared_.push_back(std::move(out));
                prepare_ready_.store(true, std::memory_order_release);
            }
        });
    }
}

void PipelineCache::merge_prepared() {
    if (!prepare_started_) {
        prepare_started_ = true;
        start_prepare();
    }
    if (!prepare_ready_.load(std::memory_order_acquire)) {
        return;
    }
    std::vector<Prepared> done;
    {
        const std::lock_guard guard(prepared_lock_);
        done.swap(prepared_);
        prepare_ready_.store(false, std::memory_order_relaxed);
    }
    for (Prepared& p : done) {
        const unsigned variant = native_shader_variant(p.rs.blend_bits);
        const std::uint32_t id = p.shader * kNativeVariants + variant;
        auto& bucket = native_psos_[combine64(hash_key_words(&p.rs, sizeof(p.rs)), id)];
        bool have = false;
        for (const auto& entry : bucket) {
            have = have || (entry.first.first == id && entry.first.second == p.rs);
        }
        if (!have) {
            bucket.emplace_back(std::make_pair(id, p.rs), std::move(p.pso));
        }
    }
}

bool PipelineCache::initialize(ID3D12Device* device, ID3D12RootSignature* root_signature) {
    if (device == nullptr || root_signature == nullptr || device_) {
        return false;
    }
    device_ = device;
    root_signature_ = root_signature;
    // From here, not the first frame: the pipelines are ready sooner and the start is not held for them.
    prepare_started_ = true;
    start_prepare();
    return true;
}

PipelineCache::~PipelineCache() noexcept {
    shutdown();
}

void PipelineCache::shutdown() noexcept {
    prepare_stop_.store(true, std::memory_order_relaxed);
    for (std::thread& t : prepare_threads_) {
        t.join();
    }
    prepare_threads_.clear();
    prepared_.clear();
    native_psos_.clear();
    root_signature_.Reset();
    device_.Reset();
}

}  // namespace galaxy::render
