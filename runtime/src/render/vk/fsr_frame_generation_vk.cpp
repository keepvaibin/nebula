// SPDX-License-Identifier: GPL-3.0-only
// FSR 3 frame generation on Vulkan (fsr_frame_generation_vk.h).
#include "galaxy/render/vk/fsr_frame_generation_vk.h"

#include "galaxy/render/render_config.h"
#include "galaxy/render/vk/vk_context.h"

#include <cstring>
#include <iostream>
#include <vector>

#if defined(GALAXY_WITH_FSR3_FG)
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#endif

namespace galaxy::render {

#if defined(GALAXY_WITH_FSR3_FG)

bool fsr_frame_generation_compiled() noexcept { return true; }

namespace vk {

namespace {

FfxResource ffx_image(const TemporalImage& image, const wchar_t* name, FfxResourceStates state) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = image.format;
    info.extent = {image.width, image.height, 1u};
    info.mipLevels = 1u;
    info.arrayLayers = 1u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = image.usage;
    return ffxGetResourceVK(image.image, ffxGetImageResourceDescriptionVK(image.image, info, FFX_RESOURCE_USAGE_READ_ONLY), name, state);
}

VKAPI_ATTR void VKAPI_CALL no_label_begin(VkCommandBuffer, const VkDebugUtilsLabelEXT*) {}
VKAPI_ATTR void VKAPI_CALL no_label_end(VkCommandBuffer) {}

// Its debug labels are called unguarded, and exist only with the debug extension.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ffx_device_proc(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(no_label_begin);
    }
    if (std::strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(no_label_end);
    }
    return vkGetDeviceProcAddr(device, name);
}

VkDeviceContext g_device_context{};

FfxDevice ffx_device() {
    const Context& context = Context::get();
    g_device_context.vkDevice = context.device();
    g_device_context.vkPhysicalDevice = context.physical_device();
    g_device_context.vkDeviceProcAddr = ffx_device_proc;
    return ffxGetDeviceVK(&g_device_context);
}

}  // namespace

struct FsrFrameGenerationVk::Impl {
    FfxFsr3Context context{};
    FfxInterface shared{};
    FfxInterface generation{};
    std::vector<unsigned char> shared_scratch;
    std::vector<unsigned char> generation_scratch;
    bool created = false;
    bool enabled = false;
    bool prepared = false;
    bool reported = false;
    std::uint64_t frame_id = 0u;
    unsigned render_width = 0u;
    unsigned render_height = 0u;
    unsigned display_width = 0u;
    unsigned display_height = 0u;
};

FsrFrameGenerationVk::FsrFrameGenerationVk() : impl_(std::make_unique<Impl>()) {}

FsrFrameGenerationVk::~FsrFrameGenerationVk() = default;

bool FsrFrameGenerationVk::create_swap_chain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR& swap_chain, SwapchainCalls& calls) noexcept {
    const Context& context = Context::get();
    if (context.generation_present_queue() == VK_NULL_HANDLE || context.generation_acquire_queue() == VK_NULL_HANDLE) {
        return false;
    }
    VkFrameInterpolationInfoFFX generation{};
    generation.physicalDevice = context.physical_device();
    generation.device = context.device();
    generation.gameQueue = {context.queue(), context.queue_family(), nullptr};
    generation.asyncComputeQueue = {VK_NULL_HANDLE, 0u, nullptr};
    generation.presentQueue = {context.generation_present_queue(), context.queue_family(), nullptr};
    generation.imageAcquireQueue = {context.generation_acquire_queue(), context.queue_family(), nullptr};
    generation.compositionMode = VK_COMPOSITION_MODE_NOT_FORCED_FFX;
    FfxSwapchain ffx = ffxGetSwapchainVK(swap_chain);
    const FfxErrorCode result = ffxReplaceSwapchainForFrameinterpolationVK(ffxGetCommandQueueVK(context.queue()), ffx, &info, &generation);
    if (result != FFX_OK) {
        std::cerr << "[fsr3-fg] swap chain: " << result << '\n';
        swap_chain = VK_NULL_HANDLE;
        return false;
    }
    swap_chain = ffxGetVKSwapchain(ffx);
    FfxSwapchainReplacementFunctions functions{};
    if (ffxGetSwapchainReplacementFunctionsVK(ffx_device(), &functions) != FFX_OK) {
        return false;
    }
    calls.destroy = functions.destroySwapchainKHR;
    calls.get_images = functions.getSwapchainImagesKHR;
    calls.acquire = functions.acquireNextImageKHR;
    calls.present = functions.queuePresentKHR;
    return true;
}

bool FsrFrameGenerationVk::matches(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height) const noexcept {
    return impl_->created && impl_->render_width == render_width && impl_->render_height == render_height &&
           impl_->display_width == display_width && impl_->display_height == display_height;
}

bool FsrFrameGenerationVk::create(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height,
                                  VkFormat backbuffer_format) noexcept {
    Impl& s = *impl_;
    const VkPhysicalDevice physical = Context::get().physical_device();
    try {
        const std::size_t size = ffxGetScratchMemorySizeVK(physical, FFX_FSR3_CONTEXT_COUNT);
        s.shared_scratch.assign(size, 0u);
        s.generation_scratch.assign(size, 0u);
    } catch (...) {
        return false;
    }
    const FfxDevice device = ffx_device();
    if (ffxGetInterfaceVK(&s.shared, device, s.shared_scratch.data(), s.shared_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
        ffxGetInterfaceVK(&s.generation, device, s.generation_scratch.data(), s.generation_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK) {
        return false;
    }
    FfxFsr3ContextDescription description{};
    description.flags = FFX_FSR3_ENABLE_INTERPOLATION_ONLY | FFX_FSR3_ENABLE_DEPTH_INVERTED;
    description.maxRenderSize = {render_width, render_height};
    description.maxUpscaleSize = {display_width, display_height};
    description.displaySize = {display_width, display_height};
    description.backendInterfaceSharedResources = s.shared;
    description.backendInterfaceFrameInterpolation = s.generation;
    description.backBufferFormat = ffxGetSurfaceFormatVK(backbuffer_format);
    const FfxErrorCode result = ffxFsr3ContextCreate(&s.context, &description);
    if (result != FFX_OK) {
        std::cerr << "[fsr3-fg] context: " << result << '\n';
        return false;
    }
    s.created = true;
    s.render_width = render_width;
    s.render_height = render_height;
    s.display_width = display_width;
    s.display_height = display_height;
    std::cerr << "[fsr3-fg] " << render_width << 'x' << render_height << " -> " << display_width << 'x' << display_height << '\n';
    return true;
}

void FsrFrameGenerationVk::destroy(VkSwapchainKHR swap_chain) noexcept {
    Impl& s = *impl_;
    if (!s.created) {
        return;
    }
    if (swap_chain != VK_NULL_HANDLE) {
        configure(swap_chain, false, nullptr, 0u, 0u, 0u, 0u);
        (void)ffxWaitForPresents(ffxGetSwapchainVK(swap_chain));
    }
    (void)ffxFsr3ContextDestroy(&s.context);
    s.created = false;
    s.enabled = false;
    s.prepared = false;
}

bool FsrFrameGenerationVk::prepare(VkCommandBuffer commands, const TemporalUpscaleInputs& in) noexcept {
    Impl& s = *impl_;
    if (!s.created || commands == VK_NULL_HANDLE) {
        return false;
    }
    ++s.frame_id;
    FfxFsr3DispatchFrameGenerationPrepareDescription d{};
    d.commandList = ffxGetCommandListVK(commands);
    d.depth = ffx_image(in.depth, L"Nebula scene depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT);
    d.motionVectors = ffx_image(in.motion, L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET);
    d.jitterOffset = {in.jitter_x, in.jitter_y};
    d.motionVectorScale = {1.0f, 1.0f};
    d.renderSize = {in.render_width, in.render_height};
    d.frameTimeDelta = in.frame_ms;
    d.cameraNear = in.camera_near;
    d.cameraFar = in.camera_far;
    d.viewSpaceToMetersFactor = 1.0f;
    d.cameraFovAngleVertical = in.fov_y;
    d.frameID = s.frame_id;
    const FfxErrorCode result = ffxFsr3ContextDispatchFrameGenerationPrepare(&s.context, &d);
    if (result != FFX_OK) {
        if (!s.reported) {
            s.reported = true;
            std::cerr << "[fsr3-fg] prepare: " << result << '\n';
        }
        return false;
    }
    s.prepared = true;
    return true;
}

void FsrFrameGenerationVk::configure(VkSwapchainKHR swap_chain, bool enabled, const TemporalImage* hudless, unsigned left, unsigned top,
                                     unsigned width, unsigned height) noexcept {
    Impl& s = *impl_;
    if (!s.created || swap_chain == VK_NULL_HANDLE) {
        return;
    }
    const bool on = enabled && s.prepared;
    s.prepared = false;
    FfxFrameGenerationConfig config{};
    config.swapChain = ffxGetSwapchainVK(swap_chain);
    config.frameGenerationEnabled = on;
    config.frameGenerationCallback = ffxFsr3DispatchFrameGeneration;
    config.frameGenerationCallbackContext = &s.context;
    if (on && hudless != nullptr) {
        config.HUDLessColor = ffx_image(*hudless, L"Nebula HUD-less", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    config.interpolationRect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width), static_cast<int32_t>(height)};
    config.frameID = s.frame_id;
    const FfxErrorCode result = ffxFsr3ConfigureFrameGeneration(&s.context, &config);
    if (result != FFX_OK && !s.reported) {
        s.reported = true;
        std::cerr << "[fsr3-fg] configure: " << result << '\n';
    }
    if (on != s.enabled) {
        s.enabled = on;
        std::cerr << "[fsr3-fg] " << (on ? "on" : "off") << '\n';
    }
}

}  // namespace vk

#else

bool fsr_frame_generation_compiled() noexcept { return false; }

namespace vk {

struct FsrFrameGenerationVk::Impl {};

FsrFrameGenerationVk::FsrFrameGenerationVk() = default;
FsrFrameGenerationVk::~FsrFrameGenerationVk() = default;
bool FsrFrameGenerationVk::create_swap_chain(const VkSwapchainCreateInfoKHR&, VkSwapchainKHR&, SwapchainCalls&) noexcept { return false; }
bool FsrFrameGenerationVk::matches(unsigned, unsigned, unsigned, unsigned) const noexcept { return false; }
bool FsrFrameGenerationVk::create(unsigned, unsigned, unsigned, unsigned, VkFormat) noexcept { return false; }
void FsrFrameGenerationVk::destroy(VkSwapchainKHR) noexcept {}
bool FsrFrameGenerationVk::prepare(VkCommandBuffer, const TemporalUpscaleInputs&) noexcept { return false; }
void FsrFrameGenerationVk::configure(VkSwapchainKHR, bool, const TemporalImage*, unsigned, unsigned, unsigned, unsigned) noexcept {}

}  // namespace vk

#endif

}  // namespace galaxy::render
