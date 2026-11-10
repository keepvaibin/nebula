// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Direct3D 12 (rt_backend.h): DXR 1.1 inline ray queries in compute shaders.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/rt/rt_shaders.h"

#include <cstdio>

namespace galaxy::render::rt {

bool shaders_load(gpu::Device* device) {
    const RtShaderBytes probe = rt_shader(RtShader::Probe);
    if (device == nullptr || probe.size == 0u) {
        std::fprintf(stderr, "[rt] shaders: none in this build\n");
        return false;
    }
    // rt_probe.hlsl: the scene at t0, its output at u0, its constants at b0, each a root descriptor.
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = params;
    Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) {
        std::fprintf(stderr, "[rt] shaders: the root signature failed\n");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root.Get();
    pipeline_desc.CS = {probe.data, probe.size};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT made = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline));
    if (FAILED(made)) {
        std::fprintf(stderr, "[rt] shaders: the driver refused the measuring pipeline (%08lx)\n", static_cast<unsigned long>(made));
        return false;
    }
    std::fprintf(stderr, "[rt] shaders: loaded (d3d12, %zu bytes)\n", probe.size);
    return true;
}

}  // namespace galaxy::render::rt
