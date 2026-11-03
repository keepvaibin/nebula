#pragma once

#include <atomic>
#include <mutex>
#include <thread>

// PipelineCache: native materials' pipelines on the D3D12 renderer (E-04). A material's shaders are compiled at build
// (native_shaders.h); its pipeline under a render state is created on the render thread at its first draw and kept for
// the session.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include "galaxy/render/render_state_key.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy::render {

// The PSO descriptor of a render state; a
// curved surface's patches its variant's hull and domain shaders. Public for the WARP tests.
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC make_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    D3D12_SHADER_BYTECODE vertex_shader,
    D3D12_SHADER_BYTECODE pixel_shader,
    D3D12_SHADER_BYTECODE hull_shader = {},
    D3D12_SHADER_BYTECODE domain_shader = {});

class PipelineCache {
public:
    PipelineCache() = default;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    ~PipelineCache() noexcept;

    // `root_signature` is the renderer's shared root signature.
    bool initialize(ID3D12Device* device, ID3D12RootSignature* root_signature);
    void shutdown() noexcept;

    // RENDER THREAD ONLY. A native material's pipeline: its build-compiled shaders under the draw's render state; null
    // when the build has none or the driver refuses it.
    [[nodiscard]] ID3D12PipelineState* get_native(std::uint32_t shader, const RenderStateKey& render_state, bool pixel_lit = false);
    // Starts building the seed pipelines on background threads; merge_prepared takes in what they finished.
    void start_prepare();
    void merge_prepared();

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    // Native materials' pipelines by shader, output variant and render state.
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12PipelineState> create_native(std::uint32_t shader, const RenderStateKey& rs,
                                                                             unsigned variant) const;
    struct Prepared {
        std::uint32_t shader;
        RenderStateKey rs;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    };
    bool prepare_started_ = false;
    std::vector<std::thread> prepare_threads_;
    std::atomic<std::size_t> prepare_next_{0u};
    std::atomic<bool> prepare_stop_{false};
    std::atomic<bool> prepare_ready_{false};
    std::mutex prepared_lock_;
    std::vector<Prepared> prepared_;
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::pair<std::uint32_t, RenderStateKey>,
                                                         Microsoft::WRL::ComPtr<ID3D12PipelineState>>>> native_psos_;
};

}  // namespace galaxy::render
