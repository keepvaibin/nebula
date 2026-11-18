#pragma once
// The renderer's own pipelines on Direct3D 12 (post-process, bloom, ambient occlusion, the scene camera, the mod post pass,
// present and conversion) through the driver cache the game's pipelines use (pipelines_d3d12.cpp, ID3D12PipelineLibrary):
// loaded when a run on this driver made the same pipeline before, made and stored otherwise. A pipeline is named by a hash
// of every field of its description and of its programs' bytes (not its root signature: a pipeline whose root signature
// alone changed fails to load, is made, and its store is refused, as for any name already stored). Safe from any thread.
#include "galaxy/windows_api.h"
#include <d3d12.h>

#include <cstdint>

namespace galaxy::render::core {

HRESULT utility_pipeline(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                         ID3D12PipelineState** out);
HRESULT utility_pipeline(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,
                         ID3D12PipelineState** out);

// Pipelines loaded from the cache and made (compiled) by utility_pipeline since the start: tests and diagnostics.
struct UtilityPipelineCounts {
    std::uint64_t loaded = 0u;
    std::uint64_t made = 0u;
};
[[nodiscard]] UtilityPipelineCounts utility_pipeline_counts() noexcept;

}  // namespace galaxy::render::core
