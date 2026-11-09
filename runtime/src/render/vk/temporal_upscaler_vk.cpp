// SPDX-License-Identifier: GPL-3.0-only
// FSR 3.1 on Vulkan (temporal_upscaler_vk.h).
#include "galaxy/render/vk/temporal_upscaler_vk.h"

#include "barriers_vk.h"

#if defined(_WIN32)
#include "galaxy/render/xess_library.h"

#include <xess/xess_vk.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(GALAXY_WITH_FSR3_VK)
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#endif

namespace galaxy::render::vk {

namespace {

float halton(unsigned index, unsigned base) {
    float f = 1.0f;
    float r = 0.0f;
    for (; index > 0u; index /= base) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
    }
    return r;
}

#if defined(_WIN32)
class XessUpscaler final : public TemporalUpscaler {
public:
    ~XessUpscaler() override {
        if (context_ != nullptr && destroy_ != nullptr) {
            destroy_(context_);
        }
    }

    bool create(VkInstance instance, VkPhysicalDevice physical, VkDevice device, unsigned output_width, unsigned output_height,
                unsigned quality) {
        const auto create_context = xess_library::get<decltype(xessVKCreateContext)>("xessVKCreateContext");
        const auto init = xess_library::get<decltype(xessVKInit)>("xessVKInit");
        execute_ = xess_library::get<decltype(xessVKExecute)>("xessVKExecute");
        destroy_ = xess_library::get<decltype(xessDestroyContext)>("xessDestroyContext");
        if (create_context == nullptr || init == nullptr || execute_ == nullptr || destroy_ == nullptr ||
            create_context(instance, physical, device, &context_) != XESS_RESULT_SUCCESS) {
            return false;
        }
        xess_vk_init_params_t params{};
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
        if (in.commands == VK_NULL_HANDLE) {
            return false;
        }
        const auto view = [](const TemporalImage& image, VkImageAspectFlags aspect) {
            xess_vk_image_view_info info{};
            info.imageView = image.view;
            info.image = image.image;
            info.subresourceRange = {aspect, 0u, 1u, 0u, 1u};
            info.format = image.format;
            info.width = image.width;
            info.height = image.height;
            return info;
        };
        temporal_barriers(in, false);
        xess_vk_execute_params_t params{};
        params.colorTexture = view(in.color, VK_IMAGE_ASPECT_COLOR_BIT);
        params.velocityTexture = view(in.motion, VK_IMAGE_ASPECT_COLOR_BIT);
        params.depthTexture = view(in.depth, VK_IMAGE_ASPECT_DEPTH_BIT);
        params.responsivePixelMaskTexture = view(in.reactive, VK_IMAGE_ASPECT_COLOR_BIT);
        params.outputTexture = view(in.output, VK_IMAGE_ASPECT_COLOR_BIT);
        params.jitterOffsetX = in.jitter_x;
        params.jitterOffsetY = in.jitter_y;
        params.exposureScale = 1.0f;
        params.resetHistory = in.reset ? 1u : 0u;
        params.inputWidth = in.render_width;
        params.inputHeight = in.render_height;
        const xess_result_t result = execute_(context_, in.commands, &params);
        temporal_barriers(in, true);
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
    decltype(xessVKExecute)* execute_ = nullptr;
    decltype(xessDestroyContext)* destroy_ = nullptr;
    bool reported_ = false;
};
#endif

}  // namespace

void temporal_barriers(const TemporalUpscaleInputs& in, bool back) {
    constexpr VkPipelineStageFlags2 kColor = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    constexpr VkPipelineStageFlags2 kDepth = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    constexpr VkAccessFlags2 kColorWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kColorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kDepthWrite = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kDepthAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kSampled = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    constexpr VkAccessFlags2 kStorage = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    constexpr VkImageLayout attachment = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    constexpr VkImageLayout read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // The attachments to the compute passes' layouts, or back, in one call. Reads before a write need no access: the
    // way back makes available only the output's storage writes.
    BarriersVk barriers(in.commands);
    const auto change = [&](const TemporalImage& image, VkImageLayout attached, VkImageLayout computed,
                            VkPipelineStageFlags2 stage, VkAccessFlags2 attached_write, VkAccessFlags2 attached_access,
                            VkAccessFlags2 compute_access) {
        if (back) {
            barriers.image_scoped(image.image, image.barrier_aspects, computed, attached,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, compute_access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                  stage, attached_access, 1u);
        } else {
            barriers.image_scoped(image.image, image.barrier_aspects, attached, computed, stage, attached_write,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, compute_access, 1u);
        }
    };
    change(in.color, attachment, read, kColor, kColorWrite, kColorAccess, kSampled);
    change(in.motion, attachment, read, kColor, kColorWrite, kColorAccess, kSampled);
    change(in.reactive, attachment, read, kColor, kColorWrite, kColorAccess, kSampled);
    change(in.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, read, kDepth, kDepthWrite, kDepthAccess, kSampled);
    change(in.output, attachment, VK_IMAGE_LAYOUT_GENERAL, kColor, kColorWrite, kColorAccess, kStorage);
    barriers.issue();
}

#if defined(_WIN32)
std::unique_ptr<TemporalUpscaler> create_xess_upscaler(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                                                       unsigned output_width, unsigned output_height, unsigned quality) {
    auto upscaler = std::make_unique<XessUpscaler>();
    if (!upscaler->create(instance, physical, device, output_width, output_height, quality)) {
        return nullptr;
    }
    return upscaler;
}
#else
std::unique_ptr<TemporalUpscaler> create_xess_upscaler(VkInstance, VkPhysicalDevice, VkDevice, unsigned, unsigned, unsigned) {
    return nullptr;
}
#endif

#if defined(GALAXY_WITH_FSR3_VK)

namespace {

FfxResource ffx_image(const TemporalImage& image, const wchar_t* name, FfxResourceStates state,
                      FfxResourceUsage extra = FFX_RESOURCE_USAGE_READ_ONLY) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = image.format;
    info.extent = {image.width, image.height, 1u};
    info.mipLevels = 1u;
    info.arrayLayers = 1u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = image.usage;
    return ffxGetResourceVK(image.image, ffxGetImageResourceDescriptionVK(image.image, info, extra), name, state);
}

// Its debug labels are called unguarded, and exist only with the debug extension: here they do nothing.
VKAPI_ATTR void VKAPI_CALL no_label_begin(VkCommandBuffer, const VkDebugUtilsLabelEXT*) {}
VKAPI_ATTR void VKAPI_CALL no_label_end(VkCommandBuffer) {}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ffx_device_proc(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(no_label_begin);
    }
    if (std::strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(no_label_end);
    }
    return vkGetDeviceProcAddr(device, name);
}

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

    bool create(VkDevice device, VkPhysicalDevice physical, unsigned render_width, unsigned render_height,
                unsigned output_width, unsigned output_height) {
        // The upscaler's context plus one for the resources it shares with the application (dilated depth and motion,
        // previous nearest depth).
        constexpr std::size_t kContexts = FFX_FSR3UPSCALER_CONTEXT_COUNT + 1u;
        scratch_.resize(ffxGetScratchMemorySizeVK(physical, kContexts));
        device_context_.vkDevice = device;
        device_context_.vkPhysicalDevice = physical;
        device_context_.vkDeviceProcAddr = ffx_device_proc;
        if (ffxGetInterfaceVK(&backend_, ffxGetDeviceVK(&device_context_), scratch_.data(), scratch_.size(), kContexts) !=
            FFX_OK) {
            return false;
        }
        FfxFsr3UpscalerContextDescription description{};
        // The scene target is reversed-Z display values without an exposure source.
        description.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED | FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
        description.maxRenderSize = {render_width, render_height};
        description.maxUpscaleSize = {output_width, output_height};
        description.backendInterface = backend_;
        FfxErrorCode result = ffxFsr3UpscalerContextCreate(&context_, &description);
        if (result != FFX_OK) {
            std::cerr << "[fsr3] context creation failed: " << result << '\n';
            return false;
        }
        context_created_ = true;
        result = backend_.fpCreateBackendContext(&backend_, FFX_EFFECT_SHAREDRESOURCES, nullptr, &shared_context_);
        if (result != FFX_OK) {
            std::cerr << "[fsr3] shared resource context failed: " << result << '\n';
            return false;
        }
        shared_context_created_ = true;
        FfxFsr3UpscalerSharedResourceDescriptions shared{};
        result = ffxFsr3UpscalerGetSharedResourceDescriptions(&context_, &shared);
        const FfxCreateResourceDescription* const descriptions[] = {&shared.dilatedDepth, &shared.dilatedMotionVectors,
                                                                    &shared.reconstructedPrevNearestDepth};
        for (std::size_t i = 0; result == FFX_OK && i < shared_.size(); ++i) {
            result = backend_.fpCreateResource(&backend_, descriptions[i], shared_context_, &shared_[i]);
        }
        if (result != FFX_OK) {
            std::cerr << "[fsr3] shared resources failed: " << result << '\n';
            return false;
        }
        return true;
    }

    bool evaluate(const TemporalUpscaleInputs& in) override {
        if (!context_created_ || in.commands == VK_NULL_HANDLE) {
            return false;
        }
        FfxFsr3UpscalerDispatchDescription d{};
        d.commandList = ffxGetCommandListVK(in.commands);
        d.color = ffx_image(in.color, L"Nebula scene target color", FFX_RESOURCE_STATE_RENDER_TARGET);
        d.depth = ffx_image(in.depth, L"Nebula scene target depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT);
        d.motionVectors = ffx_image(in.motion, L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET);
        d.reactive = ffx_image(in.reactive, L"Nebula reactive mask", FFX_RESOURCE_STATE_RENDER_TARGET);
        d.output = ffx_image(in.output, L"Nebula upscaled scene", FFX_RESOURCE_STATE_RENDER_TARGET, FFX_RESOURCE_USAGE_UAV);
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
        return static_cast<unsigned>(
            ffxFsr3UpscalerGetJitterPhaseCount(static_cast<int32_t>(render_width), static_cast<int32_t>(output_width)));
    }

    void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const override {
        x = 0.0f;
        y = 0.0f;
        ffxFsr3UpscalerGetJitterOffset(&x, &y, static_cast<int32_t>(index), static_cast<int32_t>(phase_count));
    }

    const char* name() const noexcept override { return "FSR 3.1"; }

private:
    std::vector<unsigned char> scratch_;
    VkDeviceContext device_context_{};
    FfxInterface backend_{};
    FfxFsr3UpscalerContext context_{};
    std::array<FfxResourceInternal, 3> shared_{};
    FfxUInt32 shared_context_ = 0;
    bool context_created_ = false;
    bool shared_context_created_ = false;
    bool reported_failure_ = false;
};

}  // namespace

bool fsr3_compiled() noexcept { return true; }

std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(VkDevice device, VkPhysicalDevice physical, unsigned render_width,
                                                       unsigned render_height, unsigned output_width,
                                                       unsigned output_height) {
    auto upscaler = std::make_unique<Fsr3Upscaler>();
    if (!upscaler->create(device, physical, render_width, render_height, output_width, output_height)) {
        return nullptr;
    }
    return upscaler;
}

#else

bool fsr3_compiled() noexcept { return false; }

std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(VkDevice, VkPhysicalDevice, unsigned, unsigned, unsigned, unsigned) {
    return nullptr;
}

#endif

}  // namespace galaxy::render::vk
