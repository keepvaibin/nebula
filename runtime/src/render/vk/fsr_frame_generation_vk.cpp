// SPDX-License-Identifier: GPL-3.0-only
// FSR 3 frame generation on Vulkan (fsr_frame_generation_vk.h).
#include "galaxy/render/vk/fsr_frame_generation_vk.h"

#include "galaxy/render/render_config.h"
#include "galaxy/render/vk/vk_context.h"

#include "barriers_vk.h"

#include <cstring>
#include <iostream>
#include <vector>

#if defined(GALAXY_WITH_FSR3_FG)
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>

#include "../fsr_frame_generation_core.inl"
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
    FsrGenerationCore core;
    bool unavailable = false;
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
    return impl_->core.matches(render_width, render_height, display_width, display_height);
}

bool FsrFrameGenerationVk::create(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height,
                                  VkFormat backbuffer_format) noexcept {
    Impl& s = *impl_;
    if (s.unavailable) {
        return false;
    }
    FsrGenerationCore& core = s.core;
    const VkPhysicalDevice physical = Context::get().physical_device();
    try {
        const std::size_t size = ffxGetScratchMemorySizeVK(physical, FFX_FSR3_CONTEXT_COUNT);
        core.shared_scratch.assign(size, 0u);
        core.generation_scratch.assign(size, 0u);
    } catch (...) {
        return false;
    }
    const FfxDevice device = ffx_device();
    s.unavailable = ffxGetInterfaceVK(&core.shared, device, core.shared_scratch.data(), core.shared_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
                    ffxGetInterfaceVK(&core.generation, device, core.generation_scratch.data(), core.generation_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
                    !core.create(render_width, render_height, display_width, display_height, ffxGetSurfaceFormatVK(backbuffer_format),
                                 ffxGetSurfaceFormatVK(VK_FORMAT_R8G8B8A8_UNORM));
    return !s.unavailable;
}

void FsrFrameGenerationVk::destroy(VkSwapchainKHR swap_chain) noexcept {
    FsrGenerationCore& core = impl_->core;
    if (!core.created()) {
        return;
    }
    if (swap_chain != VK_NULL_HANDLE) {
        core.configure(ffxGetSwapchainVK(swap_chain), false, FfxResource{}, 0u, 0u, 0u, 0u);
        (void)ffxWaitForPresents(ffxGetSwapchainVK(swap_chain));
    }
    core.destroy();
}

bool FsrFrameGenerationVk::prepare(VkCommandBuffer commands, const TemporalUpscaleInputs& in) noexcept {
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    return impl_->core.prepare(ffxGetCommandListVK(commands), ffx_image(in.depth, L"Nebula scene depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT),
                               ffx_image(in.motion, L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET), in.jitter_x, in.jitter_y, in.render_width,
                               in.render_height, in.frame_ms, in.camera_near, in.camera_far, in.fov_y);
}

void FsrFrameGenerationVk::configure(VkSwapchainKHR swap_chain, bool enabled, const TemporalImage* hudless, unsigned left, unsigned top,
                                     unsigned width, unsigned height) noexcept {
    if (swap_chain == VK_NULL_HANDLE) {
        return;
    }
    FfxResource picture{};
    if (hudless != nullptr) {
        picture = ffx_image(*hudless, L"Nebula HUD-less", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    impl_->core.configure(ffxGetSwapchainVK(swap_chain), enabled, picture, left, top, width, height);
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
