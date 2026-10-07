// pipeline_cache.cpp — Lock-free PSO completion queue (Vyukov MPSC) and
// PipelineCache with background shader specialization.
//
// Threading contract:
//   - get(), drain_completions() — render thread only.
//   - push() — any compile worker thread, lock-free.
//   - try_pop() — render thread only.
//
// The published_ map and in_flight_ set are render-thread-only; no locks
// are required for them.  The job intake (jobs_, job_mutex_, job_cv_) uses a
// plain mutex/condvar — the hot path back to the render thread (completion
// queue) remains lock-free.

#include "galaxy/gx/pipeline_cache.h"

#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/shader_pair_compile.h"

#pragma warning(push, 0)
#include <d3dcompiler.h>
#include <d3d12shader.h>
#pragma warning(pop)

#include <atomic>
#include <array>
#include <limits>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace galaxy::gx {

namespace {

bool trace_gx_stalls_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_GX_STALLS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length == 0) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : parsed;
}

std::uint64_t trace_gx_stall_threshold_us() {
    static const std::uint64_t threshold =
        read_env_u64("GALAXY_TRACE_GX_STALL_US", 1000u);
    return threshold;
}

bool pso_prewarm_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value), "GALAXY_PSO_PREWARM") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool pipeline_library_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_D3D12_PIPELINE_LIBRARY") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t pso_prewarm_limit() {
    static const std::uint64_t limit =
        read_env_u64("GALAXY_PSO_PREWARM_LIMIT", 4096u);
    return limit == 0u ? 4096u : limit;
}

bool pso_prewarm_blocking_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_PSO_PREWARM_BLOCKING") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t pso_prewarm_blocking_timeout_ms() {
    static const std::uint64_t timeout =
        read_env_u64("GALAXY_PSO_PREWARM_BLOCKING_MS", 30000u);
    return timeout;
}

std::uint64_t pso_inflight_wait_ms() {
    static const std::uint64_t timeout =
        read_env_u64("GALAXY_PSO_INFLIGHT_WAIT_MS", 4u);
    return timeout;
}

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}

// Input layout for the fixed 144-byte GxVertexOut stream (vertex_loader.h).
constexpr D3D12_INPUT_ELEMENT_DESC kGxInputLayout[] = {
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
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 120, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// GX BlendFactor -> D3D12, src side (GX factor 2/3 reference the destination).
D3D12_BLEND gx_src_blend(std::uint32_t f) {
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

// GX BlendFactor -> D3D12, dst side (GX factor 2/3 reference the source).
D3D12_BLEND gx_dst_blend(std::uint32_t f) {
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

bool force_depth_always_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_FORCE_DEPTH_ALWAYS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// GX LogicOp 0-15 -> D3D12.
D3D12_LOGIC_OP gx_logic_op(std::uint32_t m) {
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
Microsoft::WRL::ComPtr<ID3DBlob> compile_shader(
    const std::string& src,
    const char* target,
    const char* label) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(
        src.data(), src.size(), label, nullptr, nullptr, "main", target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (FAILED(hr)) {
        static std::atomic<int> s_err_logs{0};
        if (s_err_logs.fetch_add(1) < 4) {
            std::cerr << "[PipelineCache] D3DCompile(" << target << ") failed for "
                      << label << ":\n"
                      << (errors ? static_cast<const char*>(
                                       errors->GetBufferPointer())
                                 : "(no error blob)")
                      << '\n';
        }
        return nullptr;
    }
    return code;
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc.
D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc_impl(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    ID3DBlob* vs,
    ID3DBlob* ps,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {kGxInputLayout,
                      static_cast<UINT>(std::size(kGxInputLayout))};

    // ---- Blend state from blend_bits -------------------------------------
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    const std::uint32_t bb = rs.blend_bits;
    // GX EFB write masks (key bits 25/26 = cmode0 color_update/alpha_update).
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
            // GX subtract: dst = dst - src, factors forced to one.
            rt.SrcBlend  = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp   = D3D12_BLEND_OP_REV_SUBTRACT;
        } else {
            rt.SrcBlend  = gx_src_blend((bb >> 2) & 0x7u);
            rt.DestBlend = gx_dst_blend((bb >> 5) & 0x7u);
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
            // The PS exports the TEV alpha on SV_Target1 (the matching
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
    } else if (bb & (1u << 8)) {         // logic_op_enable
        rt.LogicOpEnable = TRUE;
        rt.LogicOp = gx_logic_op((bb >> 9) & 0xFu);
    }

    // ---- Rasterizer -------------------------------------------------------
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // GX cull → D3D12.  GX screen space is Y-down relative to D3D NDC (the
    // EFB viewport does not flip), so GX "front" lands as a D3D back face
    // and vice versa — the same front/back swap Dolphin's D3D backends use.
    // CullMode::All (rasterize nothing) cannot be expressed in one PSO; GX
    // titles use it to suppress geometry, so an empty scissor would be the
    // faithful path — not observed in the SMG boot, mapped to None.
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

    // ---- Depth from zmode_bits -------------------------------------------
    // FLIPPED inequality table (Dolphin D3D backends): our depth buffer is
    // REVERSED-Z relative to the GX z24 values (near=1, far=0; see the
    // projection negation in gx_backend.cpp fill_projection), so GX LESS
    // must become D3D GREATER, LEQUAL → GREATER_EQUAL, etc.  The old
    // straight n→n+1 mapping was only valid for a non-inverted buffer.
    static constexpr D3D12_COMPARISON_FUNC kGxDepthFunc[8] = {
        D3D12_COMPARISON_FUNC_NEVER,           // GX_NEVER
        D3D12_COMPARISON_FUNC_GREATER,         // GX_LESS     (reversed-Z)
        D3D12_COMPARISON_FUNC_EQUAL,           // GX_EQUAL
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,   // GX_LEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_LESS,            // GX_GREATER  (reversed-Z)
        D3D12_COMPARISON_FUNC_NOT_EQUAL,       // GX_NEQUAL
        D3D12_COMPARISON_FUNC_LESS_EQUAL,      // GX_GEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_ALWAYS,          // GX_ALWAYS
    };
    const std::uint32_t zb = rs.zmode_bits;
    if (zb & 1u) {
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthFunc = force_depth_always_enabled()
            ? D3D12_COMPARISON_FUNC_ALWAYS
            : kGxDepthFunc[(zb >> 1) & 0x7u];
        pd.DepthStencilState.DepthWriteMask = (zb & (1u << 4))
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
    }

    pd.SampleMask            = UINT_MAX;
    switch (static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
        rs.primitive_topology)) {
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
        if (point_geometry_shader == nullptr) {
            throw std::invalid_argument(
                "point PSO requires the GX point geometry shader");
        }
        pd.GS = {
            point_geometry_shader->GetBufferPointer(),
            point_geometry_shader->GetBufferSize()};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        // GX culling applies to polygons, not points expanded by the host.
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
        if (line_geometry_shader == nullptr) {
            throw std::invalid_argument(
                "line PSO requires the GX line geometry shader");
        }
        pd.GS = {
            line_geometry_shader->GetBufferPointer(),
            line_geometry_shader->GetBufferSize()};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        // GX culling applies to polygons, not lines expanded by the host.
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
        pd.PrimitiveTopologyType =
            static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
                rs.primitive_topology);
        break;
    default:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    }
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = kEfbColorFormat;
    pd.DSVFormat             = kEfbDepthFormat;
    pd.SampleDesc.Count      = 1;

    return pd;
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc and
// create the pipeline.  Returns null on failure (logged, capped).
Microsoft::WRL::ComPtr<ID3D12PipelineState> create_gx_pso(
    ID3D12Device* device,
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    ID3DBlob* vs,
    ID3DBlob* ps,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd =
        make_gx_pso_desc_impl(
            root_signature,
            rs,
            vs,
            ps,
            line_geometry_shader,
            point_geometry_shader);

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

std::wstring pso_library_name(const PsoKey& key) {
    wchar_t name[160]{};
    swprintf_s(
        name,
        L"gx_%016llx_%016llx_%08x_%08x_%02x_%02x_%02x",
        static_cast<unsigned long long>(key.vs_hash),
        static_cast<unsigned long long>(key.ps_hash),
        key.render_state.blend_bits,
        key.render_state.zmode_bits,
        static_cast<unsigned>(key.render_state.cull),
        static_cast<unsigned>(key.render_state.pixfmt),
        static_cast<unsigned>(key.render_state.primitive_topology));
    return name;
}

}  // anonymous namespace

D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    ID3DBlob* vertex_shader,
    ID3DBlob* pixel_shader,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader) {
    return make_gx_pso_desc_impl(
        root_signature,
        render_state,
        vertex_shader,
        pixel_shader,
        line_geometry_shader,
        point_geometry_shader);
}

// ===========================================================================
// PsoCompletionQueue — Vyukov MPSC intrusive list
// ===========================================================================
//
// The queue uses a sentinel node so the consumer never needs to call
// new/delete in the steady-state pop path.
//
// Producer (push, any thread):
//   1. Allocate a Node on the heap.
//   2. Set node->next = nullptr.
//   3. Swing head_ to node (atomic exchange), get prev.
//   4. Store node into prev->next (the only store the consumer waits on).
//
// Consumer (try_pop, render thread only):
//   Tail cursor points to the sentinel.  If sentinel->next is null the queue is
//   empty.  Otherwise advance tail_ = sentinel->next, making the new tail the
//   new sentinel, and return the old tail's item.

struct PsoCompletionQueue::Node {
    PsoCompletionQueue::Item item;
    std::atomic<Node*> next{nullptr};
};

PsoCompletionQueue::PsoCompletionQueue() {
    // Allocate the sentinel on the heap.
    // tail_ always points to the current sentinel node (heap-owned).
    Node* sentinel = new Node{};
    head_.store(sentinel, std::memory_order_relaxed);
    tail_ = sentinel;
}

PsoCompletionQueue::~PsoCompletionQueue() {
    // Drain any remaining nodes (should be none after shutdown joins workers).
    Item discard;
    while (try_pop(discard)) {}
    // tail_ is the last sentinel; always heap-allocated, always delete it.
    delete tail_;
}

void PsoCompletionQueue::push(Item item) {
    // Allocated on the heap — the consumer frees it after popping.
    Node* node = new Node{std::move(item), nullptr};
    // Swing head_ to the new node; prev is whoever was there before.
    Node* prev = head_.exchange(node, std::memory_order_acq_rel);
    // Link: store into prev->next.  The consumer spins on this store if it
    // races the exchange, but that window is tiny.
    prev->next.store(node, std::memory_order_release);
}

bool PsoCompletionQueue::try_pop(Item& out) {
    // tail_ is the sentinel (or the last consumed node acting as sentinel).
    Node* next = tail_->next.load(std::memory_order_acquire);
    if (next == nullptr) {
        return false;  // queue is empty
    }
    // Advance the sentinel: next becomes the new sentinel; delete the old one.
    out = std::move(next->item);
    delete tail_;
    tail_ = next;
    return true;
}

// ===========================================================================
// PipelineCache
// ===========================================================================

bool PipelineCache::initialize(
    ID3D12Device* device,
    ID3D12RootSignature* root_signature,
    const std::filesystem::path& cache_dir,
    unsigned worker_count) {

    if (device == nullptr || root_signature == nullptr || device_ || !workers_.empty()) {
        return false;
    }
    worker_error_ = nullptr;
    worker_failed_.store(false, std::memory_order_relaxed);
    device_         = device;
    root_signature_ = root_signature;
    cache_dir_      = cache_dir;

    const std::string line_geometry_source{
        ShaderGenerator::line_geometry_shader_source()};
    const std::string point_geometry_source{
        ShaderGenerator::point_geometry_shader_source()};
    line_geometry_shader_ = compile_shader(
        line_geometry_source, "gs_5_1", "gx_line_gs");
    point_geometry_shader_ = compile_shader(
        point_geometry_source, "gs_5_1", "gx_point_gs");
    if (!line_geometry_shader_ || !point_geometry_shader_) {
        return false;
    }

    // worker_count retained in the signature for the M8 hybrid-uber plan;
    // the current policy compiles synchronously on first miss (a one-time
    // hitch per material instead of invisible geometry — see get()).
    stopping_.store(false, std::memory_order_relaxed);

    // Create the cache directory if it does not exist.
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    // A failure here is non-fatal — we simply won't have disk persistence.

    // Warm valid saved stages. This cannot prepare genuinely new shader
    // configurations or recover stages that never persisted successfully.
    load_disk_cache();
    load_pso_key_cache();
    if (pipeline_library_enabled()) {
        load_pipeline_library();
    }
    const std::uint64_t prewarm_jobs = enqueue_cached_pso_prewarm(worker_count);
    if (prewarm_jobs != 0 && pso_prewarm_blocking_enabled()) {
        wait_for_cached_pso_prewarm(prewarm_jobs);
    }

    // One bounded startup record is useful even when gameplay diagnostics
    // are off. It reports preparation, not an assertion of future coverage.
    std::ostringstream ready;
    ready << "[pipeline-cache-ready] pairs=" << shader_blobs_.size()
          << " vertex-stages=" << vertex_shader_blobs_.size()
          << " pixel-stages=" << pixel_shader_blobs_.size()
          << " known-keys=" << warm_pso_keys_.size()
          << " prewarm-enqueued=" << prewarm_jobs
          << " published=" << published_.size()
          << " pending=" << in_flight_.size()
          << " shader-repair=" << shader_cache_repair_pending_
          << " key-repair=" << pso_cache_repair_pending_
          << " driver-library=" << (pipeline_library_ ? "available" : "unavailable")
          << " loaded-library-bytes=" << pipeline_library_blob_.size() << '\n';
    std::cerr << ready.str();

    return true;
}

PipelineCache::~PipelineCache() noexcept {
    try {
        shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[PipelineCache] shutdown failed: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "[PipelineCache] shutdown failed\n";
    }
}

void PipelineCache::shutdown() {
    // Signal workers to stop.
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        stopping_.store(true, std::memory_order_relaxed);
        // Queued prewarm is optional. Only already running work must retire.
        jobs_.clear();
    }
    job_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
    drain_completions();
    flush_disk();

    published_.clear();
    uber_.clear();
    in_flight_.clear();
    vertex_shader_blobs_.clear();
    pixel_shader_blobs_.clear();
    // Failed persistence still owns these CPU DXBC blobs. Keep them usable
    // by a later flush_disk() retry after render resources have shut down.
    if (unsaved_blobs_.empty()) {
        shader_blobs_.clear();
    }
    line_geometry_shader_.Reset();
    point_geometry_shader_.Reset();
    persisted_pso_keys_.clear();
    warm_pso_keys_.clear();
    // Pending keys are retired only by successful persistence in flush_disk().
    // A failed save must remain retryable after render resources shut down.
    pipeline_library_.Reset();
    pipeline_library_blob_.clear();
    pipeline_library_dirty_ = false;
    root_signature_.Reset();
    device_.Reset();
}

// ---------------------------------------------------------------------------
// get — render thread only
// ---------------------------------------------------------------------------

ID3D12PipelineState* PipelineCache::get(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {

    if (worker_failed_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
    }
    // 1. Check the published specialized map (render-thread-only, no lock).
    {
        const auto it = published_.find(key);
        if (it != published_.end() && it->second) {
            specialized_hits_.fetch_add(1, std::memory_order_relaxed);
            return it->second.Get();
        }
    }
    const std::uint64_t key_hash = key.hash();
    if (in_flight_.find(key) != in_flight_.end()) {
        ID3D12PipelineState* in_flight_pso = nullptr;
        if (wait_for_in_flight_pso(key, in_flight_pso) &&
            in_flight_pso != nullptr) {
            specialized_hits_.fetch_add(1, std::memory_order_relaxed);
            return in_flight_pso;
        }
    }

    // 2. Compile SYNCHRONOUSLY on first sight.  Rationale (review note): the
    // previous policy returned the not-yet-built uber fallback — effectively
    // no pipeline — so every first-seen material (and every new blend/zmode
    // variant of a known material) was SKIPPED for the frames its async
    // compile spent in flight: structural pop-in/flicker at every scene
    // change.  Until a real uber pipeline exists (M8), a one-time per-shader
    // hitch is strictly better than invisible geometry, and the DXBC cache
    // below (in-memory by shader hash + shaders.bin on disk) limits FXC to
    // the FIRST run that ever sees the shader; render-state variants reuse
    // the blobs and pay only the driver PSO build.  This mirrors the
    // trade-off space described in Dolphin's hybrid-ubershader design.
    uber_fallbacks_.fetch_add(1, std::memory_order_relaxed);

    const auto miss_start = std::chrono::steady_clock::now();
    const ShaderPairKey shader_pair_hash{key.vs_hash, key.ps_hash};
    const bool blob_cache_hit =
        shader_blobs_.find(shader_pair_hash) != shader_blobs_.end();
    const bool vs_cache_hit = vertex_shader_blobs_.contains(key.vs_hash);
    const bool ps_cache_hit = pixel_shader_blobs_.contains(key.ps_hash);
    const bool known_key = persisted_pso_keys_.contains(key);
    const ShaderPair* blobs = get_or_compile_blobs(key, ps_key, vs_key);
    const auto blobs_end = std::chrono::steady_clock::now();

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const auto pso_start = std::chrono::steady_clock::now();
    if (blobs != nullptr && blobs->vs && blobs->ps) {
        pso = create_or_load_pso(key, blobs->vs.Get(), blobs->ps.Get());
    }
    const auto pso_end = std::chrono::steady_clock::now();
    // Preserve map ownership if a completion populated this key during the
    // in-flight wait. Upgrade only a null entry; never discard a successful
    // PSO or return a pointer from this temporary ComPtr.
    auto published_it = published_.find(key);
    if (published_it == published_.end() && pso) {
        published_it = published_.emplace(key, std::move(pso)).first;
    } else if (published_it != published_.end() && !published_it->second && pso) {
        published_it->second = std::move(pso);
    }
    ID3D12PipelineState* raw = published_it == published_.end()
        ? nullptr : published_it->second.Get();
    if (raw != nullptr) {
        remember_pso_key(key);
    }
    const std::uint64_t total_us = elapsed_us(miss_start, pso_end);
    if (trace_gx_stalls_enabled() &&
        (total_us >= trace_gx_stall_threshold_us() || raw == nullptr)) {
        std::ostringstream message;
        message << "[gx-pso-miss] key=0x" << std::hex << key_hash
                  << " vs=0x" << key.vs_hash
                  << " ps=0x" << key.ps_hash
                  << " blend=0x" << key.render_state.blend_bits
                  << " z=0x" << key.render_state.zmode_bits
                  << std::dec
                  << " topo="
                  << static_cast<unsigned>(
                         key.render_state.primitive_topology)
                  << " cull=" << static_cast<unsigned>(key.render_state.cull)
                  << " pixfmt=" << static_cast<unsigned>(key.render_state.pixfmt)
                  << " blob-cache-hit=" << (blob_cache_hit ? 1 : 0)
                  << " vs-cache-hit=" << (vs_cache_hit ? 1 : 0)
                  << " ps-cache-hit=" << (ps_cache_hit ? 1 : 0)
                  << " key-known=" << (known_key ? 1 : 0)
                  << " blob-us=" << elapsed_us(miss_start, blobs_end)
                  << " pso-us=" << elapsed_us(pso_start, pso_end)
                  << " total-us=" << total_us
                  << " success=" << (raw != nullptr ? 1 : 0)
                  << '\n';
        std::cerr << message.str();
    }
    return raw;
}

// ---------------------------------------------------------------------------
// drain_completions — render thread only, called from begin_frame()
// ---------------------------------------------------------------------------

void PipelineCache::drain_completions() {
    PsoCompletionQueue::Item item;
    while (completions_.try_pop(item)) {
        // A failed prewarm must leave this key available for a synchronous
        // first-use attempt. A successful completion can replace a null
        // synchronous result but must never replace another successful PSO.
        if (item.pipeline) {
            auto published_it = published_.find(item.key);
            if (published_it == published_.end()) {
                published_it = published_
                    .emplace(item.key, std::move(item.pipeline)).first;
            } else if (!published_it->second) {
                published_it->second = std::move(item.pipeline);
            }
            remember_pso_key(item.key);
        }
        in_flight_.erase(item.key);
    }
}

bool PipelineCache::wait_for_in_flight_pso(
    const PsoKey& key,
    ID3D12PipelineState*& out) {
    out = nullptr;
    const std::uint64_t key_hash = key.hash();
    // A queued job can be claimed by first use. A running job has one owner;
    // timing out must never cause a duplicate driver creation for the same key.
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        const auto queued = std::find_if(jobs_.begin(), jobs_.end(),
            [&](const CompileJob& job) { return job.key == key; });
        if (queued != jobs_.end()) {
            jobs_.erase(queued);
            in_flight_.erase(key);
            return false;
        }
    }
    const std::uint64_t timeout_ms = std::max<std::uint64_t>(
        30000u, std::min<std::uint64_t>(pso_inflight_wait_ms(), 300000u));
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        drain_completions();
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (worker_error_) std::rethrow_exception(worker_error_);
        }
        if (const auto it = published_.find(key); it != published_.end()) {
            out = it->second.Get();
            return true;
        }
        if (in_flight_.find(key) == in_flight_.end()) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
        if (elapsed_ms >= static_cast<long long>(timeout_ms)) {
            if (trace_gx_stalls_enabled() &&
                elapsed_us(start, now) >= trace_gx_stall_threshold_us()) {
                std::cerr << "[gx-pso-inflight-wait] key=0x" << std::hex
                          << key_hash << std::dec
                          << " elapsed-us=" << elapsed_us(start, now)
                          << " timeout-ms=" << timeout_ms << '\n';
            }
            throw std::runtime_error("timed out waiting for a running GX pipeline job");
        }

        std::unique_lock<std::mutex> lock(job_mutex_);
        completion_cv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

// ---------------------------------------------------------------------------
// get_or_compile_blobs — DXBC pair lookup / synchronous FXC
// ---------------------------------------------------------------------------

const PipelineCache::ShaderPair* PipelineCache::get_or_compile_blobs(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {
    const ShaderPairKey ph{key.vs_hash, key.ps_hash};
    {
        const auto it = shader_blobs_.find(ph);
        if (it != shader_blobs_.end() &&
            it->second.vs_hash == key.vs_hash &&
            it->second.ps_hash == key.ps_hash && it->second.vs && it->second.ps) {
            return &it->second;
        }
    }

    ShaderPair pair{};
    pair.vs_hash = key.vs_hash;
    pair.ps_hash = key.ps_hash;
    if (const auto it = vertex_shader_blobs_.find(key.vs_hash);
        it != vertex_shader_blobs_.end()) {
        pair.vs = it->second;
    }
    if (const auto it = pixel_shader_blobs_.find(key.ps_hash);
        it != pixel_shader_blobs_.end()) {
        pair.ps = it->second;
    }

    const bool need_vs = !pair.vs;
    const bool need_ps = !pair.ps;
    if (need_vs && need_ps) {
        const std::string vs_src = generator_.generate_vs(vs_key);
        const std::string ps_src = generator_.generate_ps(ps_key);
        auto compiled = compile_independent_shader_pair(
            vs_src, ps_src, compile_shader);
        pair.vs = std::move(compiled.first);
        pair.ps = std::move(compiled.second);
    } else if (need_vs) {
        const std::string vs_src = generator_.generate_vs(vs_key);
        pair.vs = compile_shader(vs_src, "vs_5_1", "gx_vs");
    } else if (need_ps) {
        const std::string ps_src = generator_.generate_ps(ps_key);
        pair.ps = compile_shader(ps_src, "ps_5_1", "gx_ps");
    }

    if (pair.vs) {
        vertex_shader_blobs_.try_emplace(key.vs_hash, pair.vs);
    }
    if (pair.ps) {
        pixel_shader_blobs_.try_emplace(key.ps_hash, pair.ps);
    }

    if (pair.vs && pair.ps) {
        unsaved_blobs_.push_back(ph);
    } else {
        // A transient compiler failure must remain retryable.
        return nullptr;
    }
    const auto [it, inserted] = shader_blobs_.emplace(ph, std::move(pair));
    (void)inserted;
    return &it->second;
}

// ---------------------------------------------------------------------------
// Disk cache: shaders.bin
//   header: u32 magic 'GXSC', u32 version
//   record: u64 vs_hash, u64 ps_hash, u32 vs_size, u32 ps_size,
//           vs DXBC bytes, ps DXBC bytes
// ---------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kShaderCacheMagic   = 0x43535847u;  // "GXSC"
// Version GATES GENERATOR OUTPUT, not just the file format: bump whenever
// generate_vs/generate_ps emit different HLSL for the same key, or stale
// DXBC with old semantics is served for hash-identical keys.
    // v17 keeps TEV registers as Dolphin-style signed ints. v16 matches
    // Dolphin's diffuse-light color multiplication order. v15 wraps
    // dual-tex post-matrix rows like XF hardware. v13 includes integer XF
    // lighting and the TEV alpha=1 blend quirk. v18 widens GxVertexOut from
    // 64 to 112 bytes, carries TEXCOORD2-7, and changes VS BLENDINDICES to
    // uint4. v19 changes PS texture sampling to use BP texcoord scale in
    // tex_dims.zw instead of inverse image size. v20 copies the final TEV
    // stage's REG0-2 destination into PREV before PE output. v21 applies an
    // active indirect-coordinate op even when the stage's regular texture
    // lookup is disabled. v22 widens GxVertexOut to carry NBT
    // tangent/binormal and implements emboss texgens from those attributes.
    // v23 reverts emboss texgen to a clean source-coord passthrough (the v22
    // NBT-based offset scrambled texcoords into fabric noise on emboss-mapped
    // surfaces because RMGE01 vertices lack authored tangent/binormal).
constexpr std::uint32_t kShaderCacheVersion = 24u;
constexpr std::uint32_t kPsoKeyCacheMagic = 0x43504B47u;  // "GKPC"
constexpr std::uint32_t kPsoKeyCacheVersion = 5u;
constexpr std::uint32_t kPipelineLibraryMagic = 0x4C505847u; // "GXPL"
constexpr std::uint32_t kPipelineLibraryVersion = 6u;
constexpr std::uint64_t kMaxPipelineLibraryBytes = 64ull << 20;
constexpr std::uint64_t kMaxShaderCacheBytes = 128ull << 20;
constexpr std::uint32_t kMaxCacheRecords = 65536u;

bool valid_pso_key(const PsoKey& key) {
    const auto& state = key.render_state;
    return state.primitive_topology >= 1u && state.primitive_topology <= 3u &&
        state.cull <= 3u && state.pixfmt <= 7u &&
        (state.blend_bits & 0xe0000000u) == 0u && (state.zmode_bits & ~0x1fu) == 0u &&
        std::all_of(std::begin(state.reserved), std::end(state.reserved),
            [](std::uint8_t value) { return value == 0u; });
}

// Reflection checks the container and shader stage. Per-record checksums reject
// accidental payload corruption; this local cache is not a trust boundary.
bool valid_shader_blob(ID3DBlob* blob, bool vertex) {
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    if (blob == nullptr || FAILED(D3DReflect(blob->GetBufferPointer(),
            blob->GetBufferSize(), IID_PPV_ARGS(&reflection)))) return false;
    D3D12_SHADER_DESC desc{};
    if (FAILED(reflection->GetDesc(&desc))) return false;
    // DXBC shader version tokens encode the program type in their high word:
    // pixel = 0, vertex = 1 (D3D12_SHVER_*).
    return (desc.Version >> 16) == (vertex ? 1u : 0u);
}

bool read_shader_record(FILE* f, std::uint64_t& vs_hash, std::uint64_t& ps_hash,
    Microsoft::WRL::ComPtr<ID3DBlob>& vs, Microsoft::WRL::ComPtr<ID3DBlob>& ps) {
    std::uint32_t vs_size = 0u, ps_size = 0u;
    std::uint64_t vs_checksum = 0u, ps_checksum = 0u;
    if (std::fread(&vs_hash, sizeof(vs_hash), 1, f) != 1 ||
        std::fread(&ps_hash, sizeof(ps_hash), 1, f) != 1 ||
        std::fread(&vs_size, sizeof(vs_size), 1, f) != 1 ||
        std::fread(&ps_size, sizeof(ps_size), 1, f) != 1 ||
        std::fread(&vs_checksum, sizeof(vs_checksum), 1, f) != 1 ||
        std::fread(&ps_checksum, sizeof(ps_checksum), 1, f) != 1 ||
        vs_size == 0u || ps_size == 0u ||
        vs_size > (16u << 20) || ps_size > (16u << 20)) return false;
    const __int64 payload_start = _ftelli64(f);
    if (payload_start < 0 || _fseeki64(f, 0, SEEK_END) != 0) return false;
    const __int64 end = _ftelli64(f);
    if (end < payload_start ||
        static_cast<std::uint64_t>(end - payload_start) <
            static_cast<std::uint64_t>(vs_size) + ps_size ||
        _fseeki64(f, payload_start, SEEK_SET) != 0) return false;
    if (FAILED(D3DCreateBlob(vs_size, &vs)) || FAILED(D3DCreateBlob(ps_size, &ps)) ||
        std::fread(vs->GetBufferPointer(), 1, vs_size, f) != vs_size ||
        std::fread(ps->GetBufferPointer(), 1, ps_size, f) != ps_size) return false;
    return fnv1a64(vs->GetBufferPointer(), vs_size) == vs_checksum &&
        fnv1a64(ps->GetBufferPointer(), ps_size) == ps_checksum &&
        valid_shader_blob(vs.Get(), true) && valid_shader_blob(ps.Get(), false);
}

// Serializes replacement writers across processes. The cache remains optional:
// a busy or inaccessible lock leaves pending records for a later retry.
class CacheWriterLease {
public:
    explicit CacheWriterLease(const std::filesystem::path& path) {
        auto lock_path = path;
        lock_path += L".lock";
        handle_ = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~CacheWriterLease() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};


// The two append-record formats retain their valid byte prefix. Persistence
// writes a replacement beside the original so an interrupted/failed append
// cannot poison the next attempt, including a retry in this process.
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
std::size_t persistence_test_write_budget = std::numeric_limits<std::size_t>::max();
bool persistence_test_fail_close = false;
#endif

bool write_cache_bytes(FILE* f, const void* data, std::size_t bytes) {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (bytes > persistence_test_write_budget) {
        if (persistence_test_write_budget != 0u) {
            (void)std::fwrite(data, 1, persistence_test_write_budget, f);
        }
        persistence_test_write_budget = 0u;
        return false;
    }
    persistence_test_write_budget -= bytes;
#endif
    return std::fwrite(data, 1, bytes, f) == bytes;
}

bool close_cache_output(FILE* f) {
    const bool closed = std::fclose(f) == 0;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    return closed && !persistence_test_fail_close;
#else
    return closed;
#endif
}

bool find_cache_prefix(
    FILE* f, std::uint32_t expected_magic, std::uint32_t expected_version,
    bool shader_records, std::uint64_t& prefix_bytes, std::uint32_t& retained_records) {
    prefix_bytes = 0u;
    retained_records = 0u;
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const __int64 signed_size = _ftelli64(f);
    if (signed_size < 0 || _fseeki64(f, 0, SEEK_SET) != 0) return false;
    const auto size = static_cast<std::uint64_t>(signed_size);
    if (size < 8u) return true;
    std::uint32_t magic = 0u, version = 0u;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1) return false;
    if (magic != expected_magic || version != expected_version) return true;
    prefix_bytes = 8u;
    if (!shader_records) {
        PsoKey key{};
        std::uint32_t records = 0u;
        while (records++ < kMaxCacheRecords && std::fread(&key, sizeof(key), 1, f) == 1 &&
               valid_pso_key(key)) {
            prefix_bytes += sizeof(key);
            ++retained_records;
        }
        return std::ferror(f) == 0;
    }
    std::uint32_t records = 0u;
    while (size - prefix_bytes >= 40u && prefix_bytes < kMaxShaderCacheBytes &&
           records++ < kMaxCacheRecords) {
        std::uint64_t vs_hash = 0u, ps_hash = 0u;
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
        if (!read_shader_record(f, vs_hash, ps_hash, vs, ps)) break;
        const auto end = _ftelli64(f);
        if (end < 0 || static_cast<std::uint64_t>(end) > kMaxShaderCacheBytes) break;
        prefix_bytes = static_cast<std::uint64_t>(end);
        ++retained_records;
    }
    return std::ferror(f) == 0;
}

bool cache_has_tail(FILE* f, __int64 last_complete) {
    if (last_complete < 0 || std::ferror(f) != 0 ||
        _fseeki64(f, 0, SEEK_END) != 0) return false;
    return _ftelli64(f) > last_complete;
}

template <typename WritePending>
bool replace_record_cache(
    const std::filesystem::path& path, std::uint32_t magic,
    std::uint32_t version, bool shader_records, WritePending write_pending) {
    CacheWriterLease lease(path);
    if (!lease) return false;
    FILE* source = nullptr;
    std::uint64_t prefix_bytes = 0u;
    std::uint32_t retained_records = 0u;
    if (_wfopen_s(&source, path.c_str(), L"rb") == 0 && source != nullptr) {
        if (!find_cache_prefix(source, magic, version, shader_records, prefix_bytes, retained_records) ||
            _fseeki64(source, 0, SEEK_SET) != 0) {
            std::fclose(source);
            return false;
        }
    } else {
        std::error_code ec;
        // A read failure for an existing cache is not proof it is corrupt.
        const bool exists = std::filesystem::exists(path, ec);
        if (ec || exists) return false;
    }
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    FILE* output = nullptr;
    if (_wfopen_s(&output, tmp.c_str(), L"wb") != 0 || output == nullptr) {
        if (source != nullptr) std::fclose(source);
        return false;
    }
    bool written = true;
    std::array<unsigned char, 64u << 10> buffer{};
    std::uint64_t remaining = prefix_bytes;
    while (written && remaining != 0u) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        written = source != nullptr &&
            std::fread(buffer.data(), 1, chunk, source) == chunk &&
            write_cache_bytes(output, buffer.data(), chunk);
        remaining -= chunk;
    }
    if (source != nullptr && std::fclose(source) != 0) written = false;
    if (written && prefix_bytes == 0u) {
        written = write_cache_bytes(output, &magic, sizeof(magic)) &&
            write_cache_bytes(output, &version, sizeof(version));
    }
    if (written) written = write_pending(output, retained_records);
    const bool closed = close_cache_output(output);
    if (written && closed && MoveFileExW(
            tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
}
}  // namespace

#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
void set_pipeline_cache_persistence_test_fault(std::size_t write_budget, bool fail_close) {
    persistence_test_write_budget = write_budget;
    persistence_test_fail_close = fail_close;
}
#endif

void PipelineCache::load_disk_cache() {
    const std::filesystem::path path = cache_dir_ / "shaders.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        return;
    }
    std::uint32_t magic = 0, version = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1 ||
        magic != kShaderCacheMagic || version != kShaderCacheVersion) {
        std::fclose(f);
        // Exclude stale/foreign shader semantics immediately. The replacement
        // writer also validates a retained file prefix before persisting
        // new records. Invalid input is left untouched until replacement succeeds.
        shader_cache_repair_pending_ = true;
        if (trace_gx_stalls_enabled()) {
            std::ostringstream message;
            message << "[shader-cache-rejected] reason=header-or-version magic=" << magic
                    << " version=" << version << " expected-version=" << kShaderCacheVersion << '\n';
            std::cerr << message.str();
        }
        return;
    }
    unsigned loaded = 0;
    __int64 last_complete = _ftelli64(f);
    for (; loaded < kMaxCacheRecords && last_complete < static_cast<__int64>(kMaxShaderCacheBytes);) {
        std::uint64_t vs_hash = 0, ps_hash = 0;
        ShaderPair pair{};
        if (!read_shader_record(f, vs_hash, ps_hash, pair.vs, pair.ps) ||
            _ftelli64(f) > static_cast<__int64>(kMaxShaderCacheBytes)) break;
        pair.vs_hash = vs_hash;
        pair.ps_hash = ps_hash;
        pair.vs = vertex_shader_blobs_.try_emplace(vs_hash, pair.vs).first->second;
        pair.ps = pixel_shader_blobs_.try_emplace(ps_hash, pair.ps).first->second;
        shader_blobs_.emplace(ShaderPairKey{vs_hash, ps_hash}, std::move(pair));
        ++loaded;
        last_complete = _ftelli64(f);
    }
    shader_cache_repair_pending_ = cache_has_tail(f, last_complete);
    std::fclose(f);
    if (loaded != 0 && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded " << loaded
                  << " DXBC pairs from shader cache\n";
    }
}

void PipelineCache::load_pso_key_cache() {
    const std::filesystem::path path = cache_dir_ / "psos.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        return;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1 ||
        magic != kPsoKeyCacheMagic || version != kPsoKeyCacheVersion) {
        std::fclose(f);
        pso_cache_repair_pending_ = true;
        if (trace_gx_stalls_enabled()) {
            std::ostringstream message;
            message << "[pso-key-cache-rejected] reason=header-or-version magic=" << magic
                    << " version=" << version << " expected-version=" << kPsoKeyCacheVersion << '\n';
            std::cerr << message.str();
        }
        return;
    }

    PsoKey key{};
    __int64 last_complete = _ftelli64(f);
    std::uint32_t records = 0;
    while (records++ < kMaxCacheRecords && std::fread(&key, sizeof(key), 1, f) == 1) {
        if (!valid_pso_key(key)) break;
        if (persisted_pso_keys_.insert(key).second) {
            warm_pso_keys_.push_back(key);
        }
        last_complete = _ftelli64(f);
    }
    pso_cache_repair_pending_ = cache_has_tail(f, last_complete);
    std::fclose(f);

    if (!warm_pso_keys_.empty() && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded " << warm_pso_keys_.size()
                  << " PSO keys from cache\n";
    }
}

void PipelineCache::remember_pso_key(const PsoKey& key) {
    if (persisted_pso_keys_.insert(key).second) {
        unsaved_pso_keys_.push_back(key);
    }
}

void PipelineCache::load_pipeline_library() {
    D3D12_FEATURE_DATA_SHADER_CACHE shader_cache{};
    if (FAILED(device_->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_CACHE,
            &shader_cache,
            sizeof(shader_cache))) ||
        (shader_cache.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) == 0) {
        if (trace_gx_stalls_enabled()) {
            std::cerr << "[PipelineCache] D3D12 pipeline library unsupported\n";
        }
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if (FAILED(device_.As(&device1))) {
        return;
    }

    const LUID adapter_luid = device_->GetAdapterLuid();
    const std::filesystem::path path = cache_dir_ / "pipelines.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f != nullptr) {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint32_t shader_version = 0;
        std::uint32_t luid_low = 0;
        std::uint32_t luid_high = 0;
        std::uint64_t blob_size = 0;
        const bool header_ok =
            std::fread(&magic, sizeof(magic), 1, f) == 1 &&
            std::fread(&version, sizeof(version), 1, f) == 1 &&
            std::fread(&shader_version, sizeof(shader_version), 1, f) == 1 &&
            std::fread(&luid_low, sizeof(luid_low), 1, f) == 1 &&
            std::fread(&luid_high, sizeof(luid_high), 1, f) == 1 &&
            std::fread(&blob_size, sizeof(blob_size), 1, f) == 1;
        const bool matches_device =
            luid_low == adapter_luid.LowPart &&
            static_cast<LONG>(luid_high) == adapter_luid.HighPart;
        if (!header_ok ||
            magic != kPipelineLibraryMagic ||
            version != kPipelineLibraryVersion ||
            shader_version != kShaderCacheVersion ||
            !matches_device ||
            blob_size == 0 ||
            blob_size > kMaxPipelineLibraryBytes) {
            std::fclose(f);
            pipeline_library_blob_.clear();
        } else {
            pipeline_library_blob_.resize(static_cast<std::size_t>(blob_size));
            const std::size_t got = std::fread(
                pipeline_library_blob_.data(),
                1,
                pipeline_library_blob_.size(),
                f);
            std::fclose(f);
            if (got != pipeline_library_blob_.size()) {
                pipeline_library_blob_.clear();
            }
        }
    }

    const void* blob_ptr = pipeline_library_blob_.empty()
        ? nullptr
        : pipeline_library_blob_.data();
    const SIZE_T blob_size = pipeline_library_blob_.size();
    HRESULT hr = device1->CreatePipelineLibrary(
        blob_ptr,
        blob_size,
        IID_PPV_ARGS(&pipeline_library_));
    if (FAILED(hr) && !pipeline_library_blob_.empty()) {
        if (trace_gx_stalls_enabled()) {
            std::cerr << "[PipelineCache] discarded stale D3D12 pipeline library"
                         " HRESULT=0x"
                      << std::hex << static_cast<unsigned>(hr) << std::dec
                      << '\n';
        }
        pipeline_library_.Reset();
        pipeline_library_blob_.clear();
        hr = device1->CreatePipelineLibrary(
            nullptr,
            0,
            IID_PPV_ARGS(&pipeline_library_));
    }

    if (FAILED(hr)) {
        pipeline_library_.Reset();
        return;
    }

    if (!pipeline_library_blob_.empty() && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded D3D12 pipeline library ("
                  << pipeline_library_blob_.size() << " bytes)\n";
    }
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::create_or_load_pso(
    const PsoKey& key,
    ID3DBlob* vs_blob,
    ID3DBlob* ps_blob) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = make_gx_pso_desc(
        root_signature_.Get(),
        key.render_state,
        vs_blob,
        ps_blob,
        line_geometry_shader_.Get(),
        point_geometry_shader_.Get());
    const std::wstring name = pso_library_name(key);

    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_) {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> cached;
            const HRESULT load_hr = pipeline_library_->LoadGraphicsPipeline(
                name.c_str(),
                &pd,
                IID_PPV_ARGS(&cached));
            if (SUCCEEDED(load_hr)) {
                return cached;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = create_gx_pso(
        device_.Get(),
        root_signature_.Get(),
        key.render_state,
        vs_blob,
        ps_blob,
        line_geometry_shader_.Get(),
        point_geometry_shader_.Get());
    if (!pso) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_) {
            const HRESULT store_hr =
                pipeline_library_->StorePipeline(name.c_str(), pso.Get());
            if (SUCCEEDED(store_hr)) {
                pipeline_library_dirty_ = true;
            }
        }
    }

    return pso;
}

void PipelineCache::flush_pipeline_library() {
    std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
    if (!pipeline_library_ || !pipeline_library_dirty_) {
        return;
    }

    const SIZE_T size = pipeline_library_->GetSerializedSize();
    if (size == 0 || size > kMaxPipelineLibraryBytes) {
        return;
    }

    std::vector<std::uint8_t> bytes(size);
    const HRESULT hr = pipeline_library_->Serialize(bytes.data(), bytes.size());
    if (FAILED(hr)) {
        return;
    }

    const std::filesystem::path path = cache_dir_ / "pipelines.bin";
    CacheWriterLease lease(path);
    if (!lease) return;
    const std::filesystem::path tmp = cache_dir_ / "pipelines.bin.tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || f == nullptr) {
        return;
    }
    const LUID adapter_luid = device_->GetAdapterLuid();
    const std::uint32_t magic = kPipelineLibraryMagic;
    const std::uint32_t version = kPipelineLibraryVersion;
    const std::uint32_t shader_version = kShaderCacheVersion;
    const std::uint32_t luid_low = adapter_luid.LowPart;
    const std::uint32_t luid_high =
        static_cast<std::uint32_t>(adapter_luid.HighPart);
    const std::uint64_t blob_size =
        static_cast<std::uint64_t>(bytes.size());
    bool wrote_header =
        write_cache_bytes(f, &magic, sizeof(magic)) &&
        write_cache_bytes(f, &version, sizeof(version)) &&
        write_cache_bytes(f, &shader_version, sizeof(shader_version)) &&
        write_cache_bytes(f, &luid_low, sizeof(luid_low)) &&
        write_cache_bytes(f, &luid_high, sizeof(luid_high)) &&
        write_cache_bytes(f, &blob_size, sizeof(blob_size));
    const bool wrote = wrote_header && write_cache_bytes(f, bytes.data(), bytes.size());
    const bool closed = close_cache_output(f);
    if (!wrote || !closed) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return;
    }

    std::error_code ec;
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return;
    }

    // Serialization output does not replace the bytes borrowed by the live library.
    pipeline_library_dirty_ = false;
}

bool PipelineCache::find_cached_shader_stages(
    const PsoKey& key, ShaderPair& out) const {
    out = {};
    const auto vs = vertex_shader_blobs_.find(key.vs_hash);
    const auto ps = pixel_shader_blobs_.find(key.ps_hash);
    if (vs == vertex_shader_blobs_.end() || !vs->second ||
        ps == pixel_shader_blobs_.end() || !ps->second) return false;
    out.vs_hash = key.vs_hash;
    out.ps_hash = key.ps_hash;
    out.vs = vs->second;
    out.ps = ps->second;
    return true;
}

std::uint64_t PipelineCache::enqueue_cached_pso_prewarm(unsigned worker_count) {
    if (!pso_prewarm_enabled() || warm_pso_keys_.empty()) {
        return 0;
    }

    published_.reserve(warm_pso_keys_.size());
    in_flight_.reserve(warm_pso_keys_.size());
    std::uint64_t enqueued = 0;
    const std::uint64_t limit = pso_prewarm_limit();
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        for (const PsoKey& key : warm_pso_keys_) {
            if (enqueued >= limit) {
                break;
            }
            // A failed/interrupted pair-file save can leave both component
            // stages under other pairs. Their hashes fully identify the same
            // DXBC needed here; exact-pair presence is not a prewarm condition.
            ShaderPair stages{};
            if (!find_cached_shader_stages(key, stages)) {
                continue;
            }
            jobs_.push_back(CompileJob{
                key,
                PixelShaderKey{},
                VertexShaderKey{},
                stages.vs,
                stages.ps,
                true});
            in_flight_.insert(key);
            ++enqueued;
        }
    }
    if (enqueued != 0 && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] queued " << enqueued
                  << " cached PSO prewarm jobs\n";
    }
    if (enqueued != 0) {
        const unsigned threads = static_cast<unsigned>(
            std::min<std::uint64_t>(std::clamp(worker_count, 1u, 4u), enqueued));
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { worker_main(); });
        }
        job_cv_.notify_all();
    }
    return enqueued;
}

void PipelineCache::wait_for_cached_pso_prewarm(std::uint64_t enqueued) {
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t timeout_ms = pso_prewarm_blocking_timeout_ms();
    for (;;) {
        drain_completions();
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            if (worker_error_) std::rethrow_exception(worker_error_);
        }
        if (in_flight_.empty()) {
            break;
        }
        if (timeout_ms != 0u) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - start).count();
            if (elapsed_ms >= static_cast<long long>(timeout_ms)) {
                if (trace_gx_stalls_enabled()) {
                    std::cerr << "[PipelineCache] blocking prewarm timed out"
                              << " enqueued=" << enqueued
                              << " pending=" << in_flight_.size()
                              << " timeout-ms=" << timeout_ms << '\n';
                }
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (trace_gx_stalls_enabled()) {
        const auto end = std::chrono::steady_clock::now();
        std::cerr << "[PipelineCache] blocking prewarm completed"
                  << " enqueued=" << enqueued
                  << " elapsed-ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         end - start).count()
                  << '\n';
    }
}

void PipelineCache::flush_disk() {
    bool pipeline_dirty = false;
    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        pipeline_dirty = pipeline_library_dirty_;
    }
    if (unsaved_blobs_.empty() && unsaved_pso_keys_.empty() &&
        !shader_cache_repair_pending_ && !pso_cache_repair_pending_ &&
        !pipeline_dirty) {
        return;
    }

    if (!unsaved_blobs_.empty() || shader_cache_repair_pending_) {
        const bool saved = replace_record_cache(
            cache_dir_ / "shaders.bin", kShaderCacheMagic, kShaderCacheVersion, true,
            [&](FILE* f, std::uint32_t records) {
                for (const ShaderPairKey& ph : unsaved_blobs_) {
                    const auto it = shader_blobs_.find(ph);
                    if (it == shader_blobs_.end() || !it->second.vs || !it->second.ps) return false;
                    const ShaderPair& pair = it->second;
                    const auto position = _ftelli64(f);
                    if (records++ >= kMaxCacheRecords || position < 0 ||
                        pair.vs->GetBufferSize() > (16u << 20u) ||
                        pair.ps->GetBufferSize() > (16u << 20u) ||
                        static_cast<std::uint64_t>(position) + 40u +
                            pair.vs->GetBufferSize() + pair.ps->GetBufferSize() > kMaxShaderCacheBytes) return false;
                    const auto vs_size = static_cast<std::uint32_t>(pair.vs->GetBufferSize());
                    const auto ps_size = static_cast<std::uint32_t>(pair.ps->GetBufferSize());
                    const auto vs_checksum = fnv1a64(pair.vs->GetBufferPointer(), vs_size);
                    const auto ps_checksum = fnv1a64(pair.ps->GetBufferPointer(), ps_size);
                    if (!write_cache_bytes(f, &pair.vs_hash, sizeof(pair.vs_hash)) ||
                        !write_cache_bytes(f, &pair.ps_hash, sizeof(pair.ps_hash)) ||
                        !write_cache_bytes(f, &vs_size, sizeof(vs_size)) ||
                        !write_cache_bytes(f, &ps_size, sizeof(ps_size)) ||
                        !write_cache_bytes(f, &vs_checksum, sizeof(vs_checksum)) ||
                        !write_cache_bytes(f, &ps_checksum, sizeof(ps_checksum)) ||
                        !write_cache_bytes(f, pair.vs->GetBufferPointer(), vs_size) ||
                        !write_cache_bytes(f, pair.ps->GetBufferPointer(), ps_size)) return false;
                }
                return true;
            });
        if (saved) {
            unsaved_blobs_.clear();
            shader_cache_repair_pending_ = false;
        }
    }

    if (!unsaved_pso_keys_.empty() || pso_cache_repair_pending_) {
        const bool saved = replace_record_cache(
            cache_dir_ / "psos.bin", kPsoKeyCacheMagic, kPsoKeyCacheVersion, false,
            [&](FILE* f, std::uint32_t records) {
                for (const PsoKey& key : unsaved_pso_keys_) {
                    if (records++ >= kMaxCacheRecords) return false;
                    if (!write_cache_bytes(f, &key, sizeof(key))) return false;
                }
                return true;
            });
        if (saved) {
            unsaved_pso_keys_.clear();
            pso_cache_repair_pending_ = false;
        }
    }

    flush_pipeline_library();
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

std::uint64_t PipelineCache::specialized_hits() const {
    return specialized_hits_.load(std::memory_order_relaxed);
}

std::uint64_t PipelineCache::uber_fallbacks() const {
    return uber_fallbacks_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// get_or_create_uber — render thread only
// ---------------------------------------------------------------------------

ID3D12PipelineState* PipelineCache::get_or_create_uber(
    const RenderStateKey& render_state) {

    const std::uint64_t rs_hash =
        fnv1a64(&render_state, sizeof(render_state));

    const auto it = uber_.find(rs_hash);
    if (it != uber_.end()) {
        return it->second.Get();
    }

    // Future native-feature work can compile an uber PSO from embedded DXBC
    // here. For now insert nullptr so subsequent lookups hit the map and do
    // not repeatedly reach this path.
    uber_.emplace(rs_hash, Microsoft::WRL::ComPtr<ID3D12PipelineState>{});
    return nullptr;
}

// ---------------------------------------------------------------------------
// worker_main — compile thread
// ---------------------------------------------------------------------------

void PipelineCache::worker_main() {
    for (;;) {
        CompileJob job{};
        {
            std::unique_lock<std::mutex> lock(job_mutex_);
            job_cv_.wait(lock, [this] {
                return !jobs_.empty() ||
                       stopping_.load(std::memory_order_relaxed);
            });
            if (jobs_.empty()) {
                // stopping_ is true and no remaining jobs.
                break;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        try {
        Microsoft::WRL::ComPtr<ID3DBlob> vs = job.vs_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> ps = job.ps_blob;
        if (!job.use_cached_blobs) {
            const std::string vs_src = generator_.generate_vs(job.vs_key);
            const std::string ps_src = generator_.generate_ps(job.ps_key);
            vs = compile_shader(vs_src, "vs_5_1", "gx_vs");
            ps = compile_shader(ps_src, "ps_5_1", "gx_ps");
        }

        PsoCompletionQueue::Item completion;
        completion.key = job.key;
        if (vs && ps) {
            completion.pipeline = create_or_load_pso(job.key, vs.Get(), ps.Get());
        }

        completions_.push(std::move(completion));
        completion_cv_.notify_all();
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                if (!worker_error_) worker_error_ = std::current_exception();
                worker_failed_.store(true, std::memory_order_release);
                stopping_.store(true, std::memory_order_relaxed);
                jobs_.clear();
            }
            completion_cv_.notify_all();
            job_cv_.notify_all();
            return;
        }
    }
}

}  // namespace galaxy::gx
