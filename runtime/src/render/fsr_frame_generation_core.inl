// SPDX-License-Identifier: GPL-3.0-only
// FSR 3 frame generation from AMD's optical flow and frame interpolation contexts, the same on both renderers: the renderers' files
// give it their backend's interfaces and resources.

#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace galaxy::render {

class FsrGenerationCore {
public:
    FfxInterface shared{};
    FfxInterface generation{};
    std::vector<unsigned char> shared_scratch;
    std::vector<unsigned char> generation_scratch;

    [[nodiscard]] bool created() const noexcept { return created_; }
    [[nodiscard]] bool matches(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height) const noexcept {
        return created_ && render_width_ == render_width && render_height_ == render_height && display_width_ == display_width &&
               display_height_ == display_height;
    }

    // `shared` and `generation` are set. `format` is the swap chain's; `source_format` the HUD-less picture's, which
    // the context copies byte for byte between frames.
    bool create(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height, FfxSurfaceFormat format,
                FfxSurfaceFormat source_format) noexcept {
        FfxErrorCode result = shared.fpCreateBackendContext(&shared, FFX_EFFECT_SHAREDRESOURCES, nullptr, &shared_context_);
        if (result != FFX_OK) {
            return failed("shared resources", result);
        }
        has_shared_context_ = true;
        FfxOpticalflowContextDescription flow{};
        flow.backendInterface = generation;
        flow.resolution = {display_width, display_height};
        result = ffxOpticalflowContextCreate(&optical_flow_, &flow);
        if (result != FFX_OK) {
            return failed("optical flow", result);
        }
        has_optical_flow_ = true;
        FfxFrameInterpolationContextDescription description{};
        description.backendInterface = generation;
        // Reversed depth.
        description.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED;
        description.maxRenderSize = {render_width, render_height};
        description.displaySize = {display_width, display_height};
        description.backBufferFormat = format;
        description.previousInterpolationSourceFormat = source_format;
        result = ffxFrameInterpolationContextCreate(&interpolation_, &description);
        if (result != FFX_OK) {
            return failed("frame interpolation", result);
        }
        has_interpolation_ = true;
        FfxOpticalflowSharedResourceDescriptions flow_resources{};
        FfxFrameInterpolationSharedResourceDescriptions resources{};
        result = ffxOpticalflowGetSharedResourceDescriptions(&optical_flow_, &flow_resources);
        if (result == FFX_OK) {
            result = ffxFrameInterpolationGetSharedResourceDescriptions(&interpolation_, &resources);
        }
        const FfxCreateResourceDescription* const descriptions[kResources] = {&flow_resources.opticalFlowVector, &flow_resources.opticalFlowSCD,
                                                                              &resources.dilatedDepth, &resources.dilatedMotionVectors,
                                                                              &resources.reconstructedPrevNearestDepth};
        for (unsigned i = 0u; result == FFX_OK && i < kResources; ++i) {
            result = shared.fpCreateResource(&shared, descriptions[i], shared_context_, &resources_[i]);
            has_resource_[i] = result == FFX_OK;
        }
        if (result != FFX_OK) {
            return failed("resources", result);
        }
        created_ = true;
        render_width_ = render_width;
        render_height_ = render_height;
        display_width_ = display_width;
        display_height_ = display_height;
        std::cerr << "[fsr3-fg] " << render_width << 'x' << render_height << " -> " << display_width << 'x' << display_height << '\n';
        return true;
    }

    // With the GPU idle.
    void destroy() noexcept {
        for (unsigned i = 0u; i < kResources; ++i) {
            if (has_resource_[i]) {
                (void)shared.fpDestroyResource(&shared, resources_[i], shared_context_);
                has_resource_[i] = false;
            }
        }
        if (has_interpolation_) {
            (void)ffxFrameInterpolationContextDestroy(&interpolation_);
            has_interpolation_ = false;
        }
        if (has_optical_flow_) {
            (void)ffxOpticalflowContextDestroy(&optical_flow_);
            has_optical_flow_ = false;
        }
        if (has_shared_context_) {
            (void)shared.fpDestroyBackendContext(&shared, shared_context_);
            has_shared_context_ = false;
        }
        created_ = false;
        enabled_ = false;
        prepared_ = false;
    }

    // The scene's dilated depth and motion, for the frame generated at the next present. Motion is in render pixels.
    bool prepare(FfxCommandList commands, const FfxResource& depth, const FfxResource& motion, float jitter_x, float jitter_y, unsigned render_width,
                 unsigned render_height, float frame_ms, float camera_near, float camera_far, float fov_y) noexcept {
        if (!created_) {
            return false;
        }
        ++frame_id_;
        FfxFrameInterpolationPrepareDescription& d = prepare_[frame_id_ & 1u];
        d = {};
        d.commandList = commands;
        d.renderSize = {render_width, render_height};
        d.jitterOffset = {jitter_x, jitter_y};
        d.motionVectorScale = {1.0f, 1.0f};
        d.frameTimeDelta = frame_ms;
        d.cameraNear = camera_near;
        d.cameraFar = camera_far;
        d.viewSpaceToMetersFactor = 1.0f;
        d.cameraFovAngleVertical = fov_y;
        d.depth = depth;
        d.motionVectors = motion;
        d.frameID = frame_id_;
        d.dilatedDepth = shared.fpGetResource(&shared, resources_[kDilatedDepth]);
        d.dilatedMotionVectors = shared.fpGetResource(&shared, resources_[kDilatedMotion]);
        d.reconstructedPrevDepth = shared.fpGetResource(&shared, resources_[kPreviousDepth]);
        const FfxErrorCode result = ffxFrameInterpolationPrepare(&interpolation_, &d);
        if (result != FFX_OK) {
            report("prepare", result);
            return false;
        }
        prepared_ = true;
        return true;
    }

    // At the present. `swap_chain`: its pacing swap chain.
    void configure(FfxSwapchain swap_chain, bool enabled, const FfxResource& hudless, unsigned left, unsigned top, unsigned width, unsigned height) noexcept {
        if (!created_) {
            return;
        }
        const bool on = enabled && prepared_;
        prepared_ = false;
        hudless_ = hudless;
        FfxFrameGenerationConfig config{};
        config.swapChain = swap_chain;
        config.frameGenerationEnabled = on;
        config.frameGenerationCallback = &FsrGenerationCore::dispatch;
        config.frameGenerationCallbackContext = this;
        config.HUDLessColor = hudless;
        config.interpolationRect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width), static_cast<int32_t>(height)};
        config.frameID = frame_id_;
        const FfxErrorCode result = generation.fpSwapChainConfigureFrameGeneration(&config);
        if (result != FFX_OK) {
            report("configure", result);
        }
        if (on != enabled_) {
            enabled_ = on;
            std::cerr << "[fsr3-fg] " << (on ? "on" : "off") << '\n';
        }
    }

private:
    enum : unsigned { kFlowVector, kFlowSceneChange, kDilatedDepth, kDilatedMotion, kPreviousDepth, kResources };

    // Called inside the present, for a frame configured on.
    static FfxErrorCode dispatch(const FfxFrameGenerationDispatchDescription* description, void* context) {
        return static_cast<FsrGenerationCore*>(context)->generate(*description);
    }

    FfxErrorCode generate(const FfxFrameGenerationDispatchDescription& in) noexcept {
        const FfxFrameInterpolationPrepareDescription& prepared = prepare_[in.frameID & 1u];
        const FfxResource flow_vector = shared.fpGetResource(&shared, resources_[kFlowVector]);
        const FfxResource scene_change = shared.fpGetResource(&shared, resources_[kFlowSceneChange]);
        FfxOpticalflowDispatchDescription flow{};
        flow.commandList = in.commandList;
        flow.color = hudless_.resource != nullptr ? hudless_ : in.presentColor;
        flow.reset = in.reset;
        flow.backbufferTransferFunction = in.backBufferTransferFunction;
        flow.minMaxLuminance.x = in.minMaxLuminance[0];
        flow.minMaxLuminance.y = in.minMaxLuminance[1];
        flow.opticalFlowVector = flow_vector;
        flow.opticalFlowSCD = scene_change;
        FfxErrorCode result = ffxOpticalflowContextDispatch(&optical_flow_, &flow);
        FfxFrameInterpolationDispatchDescription d{};
        d.commandList = in.commandList;
        d.displaySize.width = in.presentColor.description.width;
        d.displaySize.height = in.presentColor.description.height;
        d.currentBackBuffer = in.presentColor;
        d.currentBackBuffer_HUDLess = hudless_;
        d.renderSize = prepared.renderSize;
        d.output = in.outputs[0];
        d.opticalFlowVector = flow_vector;
        d.opticalFlowSceneChangeDetection = scene_change;
        d.opticalFlowBlockSize = 8;
        d.opticalFlowScale = {1.0f / static_cast<float>(d.displaySize.width), 1.0f / static_cast<float>(d.displaySize.height)};
        d.frameTimeDelta = prepared.frameTimeDelta;
        d.reset = in.reset;
        d.cameraNear = prepared.cameraNear;
        d.cameraFar = prepared.cameraFar;
        d.viewSpaceToMetersFactor = prepared.viewSpaceToMetersFactor;
        d.cameraFovAngleVertical = prepared.cameraFovAngleVertical;
        d.interpolationRect.left = in.interpolationRect.left;
        d.interpolationRect.top = in.interpolationRect.top;
        d.interpolationRect.width = in.interpolationRect.width;
        d.interpolationRect.height = in.interpolationRect.height;
        d.frameID = in.frameID;
        d.dilatedDepth = prepared.dilatedDepth;
        d.dilatedMotionVectors = prepared.dilatedMotionVectors;
        d.reconstructedPrevDepth = prepared.reconstructedPrevDepth;
        d.backBufferTransferFunction = in.backBufferTransferFunction;
        d.minMaxLuminance[0] = in.minMaxLuminance[0];
        d.minMaxLuminance[1] = in.minMaxLuminance[1];
        result |= ffxFrameInterpolationDispatch(&interpolation_, &d);
        return result;
    }

    bool failed(const char* what, FfxErrorCode result) noexcept {
        std::cerr << "[fsr3-fg] " << what << " failed: " << std::hex << result << std::dec << '\n';
        destroy();
        return false;
    }

    void report(const char* what, FfxErrorCode result) noexcept {
        if (!reported_) {
            reported_ = true;
            std::cerr << "[fsr3-fg] " << what << " failed: " << std::hex << result << std::dec << '\n';
        }
    }

    FfxOpticalflowContext optical_flow_{};
    FfxFrameInterpolationContext interpolation_{};
    FfxFrameInterpolationPrepareDescription prepare_[2]{};
    FfxUInt32 shared_context_ = 0u;
    FfxResourceInternal resources_[kResources]{};
    bool has_resource_[kResources]{};
    FfxResource hudless_{};
    bool has_shared_context_ = false;
    bool has_optical_flow_ = false;
    bool has_interpolation_ = false;
    bool created_ = false;
    bool enabled_ = false;
    bool prepared_ = false;
    bool reported_ = false;
    std::uint64_t frame_id_ = 0u;
    std::uint64_t generated_ = 0u;
    std::uint64_t generate_failures_ = 0u;
    unsigned render_width_ = 0u;
    unsigned render_height_ = 0u;
    unsigned display_width_ = 0u;
    unsigned display_height_ = 0u;
};

}  // namespace galaxy::render
