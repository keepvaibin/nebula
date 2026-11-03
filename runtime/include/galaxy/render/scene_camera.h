#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace galaxy::render {

class DescriptorRing;

// The wide 3D scene's camera (scene_camera.hlsl; numbers in scene_camera_shape.h). While the enhanced lighting is on the
// scene's colour stays past white in a float target; at the scene's end this meters the picture for the eye's exposure
// and writes it to the scene target through the exposure and the shoulder, every sample of a multisampled one. Widen is
// the other way, at the scene's begin: what the scene target already holds, into the wide target.
class SceneCamera {
public:
    SceneCamera() = default;
    SceneCamera(const SceneCamera&) = delete;
    SceneCamera& operator=(const SceneCamera&) = delete;
    ~SceneCamera() { shutdown(); }

    bool initialize(ID3D12Device* device);
    void shutdown();
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    struct Inputs {
        ID3D12Resource* wide = nullptr;     // single-sample, resolved, in RENDER_TARGET
        ID3D12Resource* wide_ms = nullptr;  // the multisampled target, or null, in RENDER_TARGET
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};  // the scene target
        unsigned width = 0u, height = 0u, samples = 1u;
        float seconds = 0.0f;  // since the last meter; below zero the eye starts open
    };
    // The pipeline state is left set for this pass; the caller binds its own again.
    bool record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in);

    struct WidenInputs {
        ID3D12Resource* picture = nullptr;  // the scene target's single-sample colour, in RENDER_TARGET
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};  // the wide target
        unsigned width = 0u, height = 0u, samples = 1u;
    };
    bool widen(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const WidenInputs& in);

private:
    [[nodiscard]] ID3D12PipelineState* camera_pipeline(unsigned samples);
    [[nodiscard]] ID3D12PipelineState* widen_pipeline(unsigned samples);

    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> meter_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> camera_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 4> widen_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> exposure_;
    bool ready_ = false;
};

}  // namespace galaxy::render
