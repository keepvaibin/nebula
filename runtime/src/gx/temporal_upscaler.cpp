// FSR 3.1 behind TemporalUpscaler (temporal_upscaler.h).
#include "galaxy/gx/temporal_upscaler.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#if defined(GALAXY_WITH_FSR3)
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#endif

namespace galaxy::gx {

#if defined(GALAXY_WITH_FSR3)

namespace {

class Fsr3Upscaler final : public TemporalUpscaler {
public:
    ~Fsr3Upscaler() override {
        for (FfxResourceInternal& resource : shared_) {
            if (resource.internalIndex != 0) {
                backend_.fpDestroyResource(&backend_, resource, shared_context_);
            }
        }
        if (shared_context_created_) {
            backend_.fpDestroyBackendContext(&backend_, shared_context_);
        }
        if (context_created_) {
            ffxFsr3UpscalerContextDestroy(&context_);
        }
    }

    bool create(ID3D12Device* device,
                unsigned max_render_width, unsigned max_render_height,
                unsigned max_output_width, unsigned max_output_height) {
        // The upscaler's context plus one for the resources it shares with
        // the application (dilated depth and motion, previous nearest depth).
        constexpr std::size_t kContexts = FFX_FSR3UPSCALER_CONTEXT_COUNT + 1u;
        scratch_.resize(ffxGetScratchMemorySizeDX12(kContexts));
        if (ffxGetInterfaceDX12(&backend_, ffxGetDeviceDX12(device), scratch_.data(),
                                scratch_.size(), kContexts) != FFX_OK) {
            return false;
        }
        FfxFsr3UpscalerContextDescription description{};
        // The EFB is reversed-Z gamma RGBA8 without an exposure source.
        description.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED |
                            FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
        description.maxRenderSize = {max_render_width, max_render_height};
        description.maxUpscaleSize = {max_output_width, max_output_height};
        description.backendInterface = backend_;
        FfxErrorCode result = ffxFsr3UpscalerContextCreate(&context_, &description);
        if (result != FFX_OK) {
            std::cerr << "[fsr3] context creation failed: " << result << '\n';
            return false;
        }
        context_created_ = true;
        result = backend_.fpCreateBackendContext(
            &backend_, FFX_EFFECT_SHAREDRESOURCES, nullptr, &shared_context_);
        if (result != FFX_OK) {
            std::cerr << "[fsr3] shared resource context failed: " << result << '\n';
            return false;
        }
        shared_context_created_ = true;
        FfxFsr3UpscalerSharedResourceDescriptions shared{};
        result = ffxFsr3UpscalerGetSharedResourceDescriptions(&context_, &shared);
        const FfxCreateResourceDescription* const descriptions[] = {
            &shared.dilatedDepth, &shared.dilatedMotionVectors,
            &shared.reconstructedPrevNearestDepth};
        for (std::size_t i = 0; result == FFX_OK && i < shared_.size(); ++i) {
            result = backend_.fpCreateResource(
                &backend_, descriptions[i], shared_context_, &shared_[i]);
        }
        if (result != FFX_OK) {
            std::cerr << "[fsr3] shared resources failed: " << result << '\n';
            return false;
        }
        return true;
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (!context_created_ || in.list == nullptr || in.color == nullptr ||
            in.depth == nullptr || in.output == nullptr) {
            return false;
        }
        FfxFsr3UpscalerDispatchDescription d{};
        d.commandList = ffxGetCommandListDX12(in.list);
        d.color = ffxGetResourceDX12(in.color, ffxGetResourceDescriptionDX12(in.color),
                                     L"Nebula EFB color", FFX_RESOURCE_STATE_RENDER_TARGET);
        d.depth = ffxGetResourceDX12(in.depth, ffxGetResourceDescriptionDX12(in.depth),
                                     L"Nebula EFB depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT);
        if (in.motion != nullptr) {
            d.motionVectors = ffxGetResourceDX12(
                in.motion, ffxGetResourceDescriptionDX12(in.motion),
                L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET);
        }
        if (in.reactive != nullptr) {
            d.reactive = ffxGetResourceDX12(
                in.reactive, ffxGetResourceDescriptionDX12(in.reactive),
                L"Nebula reactive mask", FFX_RESOURCE_STATE_RENDER_TARGET);
        }
        d.output = ffxGetResourceDX12(
            in.output,
            ffxGetResourceDescriptionDX12(in.output, FFX_RESOURCE_USAGE_UAV),
            L"Nebula upscaled scene", FFX_RESOURCE_STATE_RENDER_TARGET);
        d.dilatedDepth = backend_.fpGetResource(&backend_, shared_[0]);
        d.dilatedMotionVectors = backend_.fpGetResource(&backend_, shared_[1]);
        d.reconstructedPrevNearestDepth = backend_.fpGetResource(&backend_, shared_[2]);
        d.jitterOffset = {in.jitter_x, in.jitter_y};
        d.motionVectorScale = {1.0f, 1.0f};
        d.renderSize = {in.render_width, in.render_height};
        d.upscaleSize = {in.output_width, in.output_height};
        d.enableSharpening = false;
        d.sharpness = 0.0f;
        d.frameTimeDelta = in.frame_ms;
        d.preExposure = 1.0f;
        d.reset = in.reset;
        d.cameraNear = in.camera_near;
        d.cameraFar = in.camera_far;
        d.cameraFovAngleVertical = in.fov_y;
        d.viewSpaceToMetersFactor = 1.0f;
        const FfxErrorCode result = ffxFsr3UpscalerContextDispatch(&context_, &d);
        if (result != FFX_OK) {
            if (!reported_failure_) {
                reported_failure_ = true;
                std::cerr << "[fsr3] dispatch failed: " << result << '\n';
            }
            return false;
        }
        return true;
    }

    unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const override {
        return static_cast<unsigned>(ffxFsr3UpscalerGetJitterPhaseCount(
            static_cast<int32_t>(render_width), static_cast<int32_t>(output_width)));
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        x = 0.0f;
        y = 0.0f;
        ffxFsr3UpscalerGetJitterOffset(&x, &y, static_cast<int32_t>(index),
                                       static_cast<int32_t>(phase_count));
    }

    const char* name() const noexcept override { return "FSR 3.1"; }

private:
    std::vector<unsigned char> scratch_;
    FfxInterface backend_{};
    FfxFsr3UpscalerContext context_{};
    std::array<FfxResourceInternal, 3> shared_{};
    FfxUInt32 shared_context_ = 0;
    bool context_created_ = false;
    bool shared_context_created_ = false;
    bool reported_failure_ = false;
};

}  // namespace

std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(
    ID3D12Device* device,
    unsigned max_render_width, unsigned max_render_height,
    unsigned max_output_width, unsigned max_output_height) {
    if (device == nullptr) {
        return nullptr;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_2};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_2) {
        std::cerr << "[fsr3] shader model 6.2 unavailable\n";
        return nullptr;
    }
    auto upscaler = std::make_unique<Fsr3Upscaler>();
    if (!upscaler->create(device, max_render_width, max_render_height,
                          max_output_width, max_output_height)) {
        return nullptr;
    }
    return upscaler;
}

bool fsr3_available() noexcept { return true; }

#else

std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(
    ID3D12Device*, unsigned, unsigned, unsigned, unsigned) {
    return nullptr;
}

bool fsr3_available() noexcept { return false; }

#endif

}  // namespace galaxy::gx
