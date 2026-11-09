// SPDX-License-Identifier: GPL-3.0-only
// XeSS Super Resolution behind TemporalUpscaler (temporal_upscaler.h).
#include "galaxy/render/halton.h"
#include "galaxy/render/temporal_upscaler.h"
#include "galaxy/render/xess_library.h"
#include "barriers_d3d12.h"

#include <xess/xess_d3d12.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace galaxy::render {

namespace {

class XessUpscaler final : public TemporalUpscaler {
public:
    ~XessUpscaler() override {
        if (context_ != nullptr && destroy_ != nullptr) {
            destroy_(context_);
        }
    }

    bool create(ID3D12Device* device, unsigned output_width, unsigned output_height, unsigned quality) {
        const auto create_context = xess_library::get<decltype(xessD3D12CreateContext)>("xessD3D12CreateContext");
        const auto init = xess_library::get<decltype(xessD3D12Init)>("xessD3D12Init");
        execute_ = xess_library::get<decltype(xessD3D12Execute)>("xessD3D12Execute");
        destroy_ = xess_library::get<decltype(xessDestroyContext)>("xessDestroyContext");
        if (create_context == nullptr || init == nullptr || execute_ == nullptr || destroy_ == nullptr ||
            create_context(device, &context_) != XESS_RESULT_SUCCESS) {
            return false;
        }
        xess_d3d12_init_params_t params{};
        params.outputResolution = {output_width, output_height};
        params.qualitySetting = static_cast<xess_quality_settings_t>(xess_library::quality_setting(quality));
        // The scene target is reversed-Z display values, at exposure 1.
        params.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK | XESS_INIT_FLAG_LDR_INPUT_COLOR;
        const xess_result_t result = init(context_, &params);
        if (result != XESS_RESULT_SUCCESS) {
            std::cerr << "[xess] init failed: " << static_cast<int>(result) << '\n';
            return false;
        }
        return true;
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (in.list == nullptr || in.color == nullptr || in.depth == nullptr || in.motion == nullptr ||
            in.output == nullptr) {
            return false;
        }
        // XeSS reads its inputs from compute and writes the output as unordered access (no reactive mask: skipped).
        BarriersD3D12 uses{in.list};
        uses.texture(in.color, gpu::Use::ColorWrite, gpu::Use::ComputeRead);
        uses.texture(in.depth, gpu::Use::DepthWrite, gpu::Use::ComputeRead);
        uses.texture(in.motion, gpu::Use::ColorWrite, gpu::Use::ComputeRead);
        uses.texture(in.output, gpu::Use::ColorWrite, gpu::Use::ComputeWrite);
        uses.texture(in.reactive, gpu::Use::ColorWrite, gpu::Use::ComputeRead);
        uses.issue();
        xess_d3d12_execute_params_t params{};
        params.pColorTexture = in.color;
        params.pVelocityTexture = in.motion;
        params.pDepthTexture = in.depth;
        params.pResponsivePixelMaskTexture = in.reactive;
        params.pOutputTexture = in.output;
        params.jitterOffsetX = in.jitter_x;
        params.jitterOffsetY = in.jitter_y;
        params.exposureScale = 1.0f;
        params.resetHistory = in.reset ? 1u : 0u;
        params.inputWidth = in.render_width;
        params.inputHeight = in.render_height;
        const xess_result_t result = execute_(context_, in.list, &params);
        uses.undo();
        if (result != XESS_RESULT_SUCCESS) {
            if (!reported_) {
                reported_ = true;
                std::cerr << "[xess] execute failed: " << static_cast<int>(result) << '\n';
            }
            return false;
        }
        return true;
    }

    // XeSS's guide: at least 8 phases times the area ratio.
    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        const float ratio = static_cast<float>(output_width) / static_cast<float>(std::max(render_width, 1u));
        return std::max(8u, static_cast<unsigned>(std::ceil(8.0f * ratio * ratio)));
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
        x = halton(i, 2u) - 0.5f;
        y = halton(i, 3u) - 0.5f;
    }

    const char* name() const noexcept override { return "XeSS"; }

private:
    xess_context_handle_t context_ = nullptr;
    decltype(xessD3D12Execute)* execute_ = nullptr;
    decltype(xessDestroyContext)* destroy_ = nullptr;
    bool reported_ = false;
};

}  // namespace

std::unique_ptr<TemporalUpscaler> create_xess_upscaler(ID3D12Device* device, unsigned output_width, unsigned output_height,
                                                       unsigned quality) {
    auto upscaler = std::make_unique<XessUpscaler>();
    if (device == nullptr || !upscaler->create(device, output_width, output_height, quality)) {
        return nullptr;
    }
    return upscaler;
}

}  // namespace galaxy::render
