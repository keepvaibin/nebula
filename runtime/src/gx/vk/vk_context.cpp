// SPDX-License-Identifier: GPL-3.0-only
// The shared Vulkan device, its submissions and deferred destruction (vk_context.h).
#include "galaxy/gx/vk/vk_context.h"

#include "galaxy/gx/gpu_types.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#if !defined(_WIN32)
#include <time.h>
#endif

namespace galaxy::gx::vk {

namespace {
std::array<std::atomic<std::int64_t>, static_cast<unsigned>(DriverCall::Count)> g_driver_cpu{};
}  // namespace

std::int64_t thread_cpu_ns() noexcept {
#if defined(_WIN32)
    return 0;
#else
    timespec now{};
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : 0;
#endif
}

void note_driver_cpu(DriverCall call, std::int64_t ns) noexcept {
    g_driver_cpu[static_cast<unsigned>(call)].fetch_add(ns, std::memory_order_relaxed);
}

std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> take_driver_cpu() noexcept {
    std::array<std::int64_t, static_cast<unsigned>(DriverCall::Count)> taken{};
    for (unsigned i = 0; i < taken.size(); ++i) {
        taken[i] = g_driver_cpu[i].exchange(0, std::memory_order_relaxed);
    }
    return taken;
}

void check(VkResult result, const char* what) {
    if (result < VK_SUCCESS) {
        throw std::runtime_error(std::string("[vulkan] ") + what + " failed: VkResult " + std::to_string(result));
    }
}

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data != nullptr && data->pMessage != nullptr) {
        std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [name](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool has_instance_extension(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool has_device_extension(VkPhysicalDevice device, const char* name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

}  // namespace

Texture::~Texture() {
    Context& context = Context::get();
    if (!context.ready()) {
        return;
    }
    const VkDevice device = context.device();
    VmaAllocator_T* const allocator = context.allocator();
    const VkImage image_handle = image;
    VmaAllocation_T* const allocation_handle = allocation;
    const VkImageView views[2] = {view, target_view};
    const VkFramebuffer framebuffer_handle = framebuffer;
    context.defer([=] {
        if (framebuffer_handle != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer_handle, nullptr);
        }
        for (const VkImageView v : views) {
            if (v != VK_NULL_HANDLE) {
                vkDestroyImageView(device, v, nullptr);
            }
        }
        if (image_handle != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image_handle, allocation_handle);
        }
    });
}

Buffer::~Buffer() {
    Context& context = Context::get();
    if (!context.ready() || buffer == VK_NULL_HANDLE) {
        return;
    }
    VmaAllocator_T* const allocator = context.allocator();
    const VkBuffer buffer_handle = buffer;
    VmaAllocation_T* const allocation_handle = allocation;
    const std::uint64_t address_base = address;
    Context* const owner = &context;
    context.defer([=] {
        owner->release_address(address_base);
        vmaDestroyBuffer(allocator, buffer_handle, allocation_handle);
    });
}

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(bool prefer_integrated, bool validation, std::vector<const char*> instance_extensions,
                         const std::function<VkSurfaceKHR(VkInstance)>& create_surface, VkSurfaceKHR* surface) {
    if (device_ != VK_NULL_HANDLE) {
        return true;
    }
    failure_.clear();
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] no Vulkan loader\n");
        failure_ = "no Vulkan driver";
        return false;
    }
    std::vector<const char*> layers;
    if (validation && has_layer("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    // Object names and the validation messages only with the validation layer: a name is a driver call per object,
    // and the Android emulator's driver crashes on them.
    const bool debug_utils = !layers.empty() && has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug_utils) {
        instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Nebula";
    app.pEngineName = "Nebula";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions.size());
    instance_info.ppEnabledExtensionNames = instance_extensions.data();
    instance_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkCreateInstance failed\n");
        return false;
    }
    volkLoadInstance(instance_);
    if (debug_utils && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = debug_message;
        vkCreateDebugUtilsMessengerEXT(instance_, &messenger, nullptr, &messenger_);
    }
    debug_names_ = debug_utils;
    VkSurfaceKHR window_surface = VK_NULL_HANDLE;
    // Every failure from here leaves no instance, device or surface behind.
    const auto fail = [&](const char* message) {
        std::fprintf(stderr, "[vulkan] %s\n", message);
        failure_ = message;
        if (window_surface != VK_NULL_HANDLE) {
            if (device_ != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device_);
            }
            vkDestroySurfaceKHR(instance_, window_surface, nullptr);
            if (surface != nullptr) {
                *surface = VK_NULL_HANDLE;
            }
        }
        shutdown();
        return false;
    };
    if (create_surface) {
        window_surface = create_surface(instance_);
        if (window_surface == VK_NULL_HANDLE) {
            return fail("the window's surface could not be created");
        }
        if (surface != nullptr) {
            *surface = window_surface;
        }
    }

    // The first device of the preferred kind with a graphics queue (that presents to the surface) and the swapchain
    // extension.
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    const VkPhysicalDeviceType wanted =
        prefer_integrated ? VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    std::stable_sort(devices.begin(), devices.end(), [wanted](VkPhysicalDevice a, VkPhysicalDevice b) {
        VkPhysicalDeviceProperties pa{}, pb{};
        vkGetPhysicalDeviceProperties(a, &pa);
        vkGetPhysicalDeviceProperties(b, &pb);
        return (pa.deviceType == wanted) > (pb.deviceType == wanted);
    });
    bool vulkan_1_0_only = false;
    for (const VkPhysicalDevice candidate : devices) {
        if (!has_device_extension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        // Vulkan 1.1 at least (the memory allocator's and the device queries' entry points).
        VkPhysicalDeviceProperties candidate_properties{};
        vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
        if (candidate_properties.apiVersion < VK_API_VERSION_1_1) {
            vulkan_1_0_only = true;
            continue;
        }
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
        for (std::uint32_t i = 0; i < families; ++i) {
            VkBool32 presents = VK_TRUE;
            if (window_surface != VK_NULL_HANDLE &&
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, window_surface, &presents) != VK_SUCCESS) {
                presents = VK_FALSE;
            }
            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && presents == VK_TRUE) {
                physical_ = candidate;
                queue_family_ = i;
                break;
            }
        }
        if (physical_ != VK_NULL_HANDLE) {
            break;
        }
    }
    if (physical_ == VK_NULL_HANDLE) {
        return fail(vulkan_1_0_only ? "the GPU's Vulkan driver is version 1.0; Nebula needs Vulkan 1.1"
                                    : "no device with a graphics queue that presents to the window, and swapchains");
    }
    vkGetPhysicalDeviceProperties(physical_, &properties_);
    device_name_ = properties_.deviceName;
    identity_ = identify(physical_, has_device_extension(physical_, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME));
    quirks_ = quirks_for(identity_);
    VkPhysicalDeviceFeatures available{};
    vkGetPhysicalDeviceFeatures(physical_, &available);
    VkPhysicalDeviceFeatures enabled{};
    enabled.dualSrcBlend = available.dualSrcBlend;
    enabled.logicOp = available.logicOp;
    enabled.geometryShader = available.geometryShader;
    // Curved surfaces: the vertex stage's 18 interpolant locations into the hull stage, 16 segments an edge.
    const VkPhysicalDeviceLimits& limits = properties_.limits;
    tessellation_shader_ = available.tessellationShader == VK_TRUE && limits.maxTessellationGenerationLevel >= 16u &&
                           limits.maxVertexOutputComponents >= 72u &&
                           limits.maxTessellationControlPerVertexInputComponents >= 72u &&
                           limits.maxTessellationEvaluationOutputComponents >= 64u;
    enabled.tessellationShader = tessellation_shader_ ? VK_TRUE : VK_FALSE;
    enabled.samplerAnisotropy = available.samplerAnisotropy;
    enabled.textureCompressionBC = available.textureCompressionBC;
    enabled.depthClamp = available.depthClamp;
    enabled.independentBlend = available.independentBlend;
    enabled.shaderImageGatherExtended = available.shaderImageGatherExtended;
    dual_source_blend_ = available.dualSrcBlend == VK_TRUE;
    logic_op_ = available.logicOp == VK_TRUE;
    geometry_shader_ = available.geometryShader == VK_TRUE;
    sampler_anisotropy_ = available.samplerAnisotropy == VK_TRUE;
    bc_textures_ = available.textureCompressionBC == VK_TRUE;
    gather_extended_ = available.shaderImageGatherExtended == VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // A device that is a portability subset (MoltenVK) must have it enabled; Nebula uses nothing it leaves out.
    if (has_device_extension(physical_, "VK_KHR_portability_subset")) {
        device_extensions.push_back("VK_KHR_portability_subset");
    }
    // Presents at a requested display time (Android's swapchain implements it for every driver): the frame pacing of
    // RendererVk::end_frame.
    display_timing_ = has_device_extension(physical_, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    if (display_timing_) {
        device_extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    }
    // The driver's per-heap budget (the internal-resolution memory check), where it reports one.
    memory_budget_ = has_device_extension(physical_, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (memory_budget_) {
        device_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
    // Depth clipping apart from depth clamping: with both, fragment depth (shader-written too) is clamped to the
    // viewport's range while primitives are still clipped, as D3D does.
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
    if (available.depthClamp == VK_TRUE && has_device_extension(physical_, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &depth_clip;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        depth_clip_enable_ = depth_clip.depthClipEnable == VK_TRUE;
        if (depth_clip_enable_) {
            device_extensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
        }
    }
    depth_clamp_ = available.depthClamp == VK_TRUE;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    if (depth_clip_enable_) {
        depth_clip.pNext = nullptr;
        depth_clip.depthClipEnable = VK_TRUE;
        device_info.pNext = &depth_clip;
    }
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    device_info.pEnabledFeatures = &enabled;
    if (vkCreateDevice(physical_, &device_info, nullptr, &device_) != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        return fail("vkCreateDevice failed");
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_1;
    allocator_info.physicalDevice = physical_;
    allocator_info.device = device_;
    allocator_info.instance = instance_;
    allocator_info.pVulkanFunctions = &functions;
    if (memory_budget_) {
        allocator_info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if (vmaCreateAllocator(&allocator_info, &allocator_) != VK_SUCCESS) {
        allocator_ = nullptr;
        return fail("vmaCreateAllocator failed");
    }
    std::fprintf(stderr, "[vulkan] device=\"%s\" api=%u.%u driver=0x%x validation=%d\n", device_name_.c_str(),
                 VK_API_VERSION_MAJOR(properties_.apiVersion), VK_API_VERSION_MINOR(properties_.apiVersion),
                 properties_.driverVersion, layers.empty() ? 0 : 1);
    std::fprintf(stderr, "[vulkan] driver: %s; workarounds: %s\n", describe(identity_).c_str(),
                 describe(quirks_).c_str());
    std::fprintf(stderr,
                 "[vulkan] features: dual-source=%d logic-op=%d geometry=%d depth-clamp=%d depth-clip=%d bc=%d "
                 "anisotropy=%d display-timing=%d\n",
                 dual_source_blend_, logic_op_, geometry_shader_, depth_clamp_, depth_clip_enable_, bc_textures_,
                 sampler_anisotropy_, display_timing_);
    return true;
}

void Context::shutdown() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        for (Deferred& deferred : deferred_) {
            deferred.destroy();
        }
        deferred_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        for (const Pending& pending : pending_) {
            vkDestroyFence(device_, pending.fence, nullptr);
        }
        for (const VkFence fence : free_fences_) {
            vkDestroyFence(device_, fence, nullptr);
        }
        pending_.clear();
        free_fences_.clear();
    }
    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (messenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physical_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
}

std::uint64_t Context::submit(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore,
                              VkPipelineStageFlags wait_stage, VkSemaphore signal_semaphore) {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    VkFence fence = VK_NULL_HANDLE;
    // A free fence may still be in a wait() that read it before it signalled; reset none while one is.
    if (!free_fences_.empty() && waiters_ == 0) {
        fence = free_fences_.back();
        free_fences_.pop_back();
        check(vkResetFences(device_, 1, &fence), "vkResetFences");
    } else {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence");
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = static_cast<std::uint32_t>(commands.size());
    submit.pCommandBuffers = commands.data();
    if (wait_semaphore != VK_NULL_HANDLE) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &wait_semaphore;
        submit.pWaitDstStageMask = &wait_stage;
    }
    if (signal_semaphore != VK_NULL_HANDLE) {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signal_semaphore;
    }
    const std::int64_t cpu_start = thread_cpu_ns();
    const VkResult result = vkQueueSubmit(queue_, 1, &submit, fence);
    note_driver_cpu(DriverCall::Submit, thread_cpu_ns() - cpu_start);
    if (result != VK_SUCCESS) {
        free_fences_.push_back(fence);
        check(result, "vkQueueSubmit");
    }
    const std::uint64_t value = submitted_.load(std::memory_order_relaxed) + 1u;
    pending_.push_back({value, fence});
    submitted_.store(value, std::memory_order_release);
    return value;
}

std::uint64_t Context::completed_value() {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    std::size_t done = 0;
    const std::int64_t cpu_start = thread_cpu_ns();
    while (done < pending_.size() && vkGetFenceStatus(device_, pending_[done].fence) == VK_SUCCESS) {
        completed_ = pending_[done].value;
        free_fences_.push_back(pending_[done].fence);
        ++done;
    }
    note_driver_cpu(DriverCall::Poll, thread_cpu_ns() - cpu_start);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(done));
    return completed_;
}

bool Context::wait(std::uint64_t value, std::uint64_t timeout_ns) {
    VkFence fence = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (value <= completed_) {
            return true;
        }
        for (const Pending& pending : pending_) {
            if (pending.value >= value) {
                fence = pending.fence;
                break;
            }
        }
        if (fence == VK_NULL_HANDLE) {
            return false;  // never submitted
        }
        ++waiters_;
    }
    const std::int64_t cpu_start = thread_cpu_ns();
    const VkResult result = vkWaitForFences(device_, 1, &fence, VK_TRUE, timeout_ns);
    note_driver_cpu(DriverCall::Wait, thread_cpu_ns() - cpu_start);
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        --waiters_;
    }
    if (result != VK_SUCCESS) {
        return false;
    }
    static_cast<void>(completed_value());
    return true;
}

std::uint64_t Context::submit_frame(std::span<const VkCommandBuffer> commands, VkSemaphore wait_semaphore,
                                    VkPipelineStageFlags wait_stage, VkSemaphore signal_semaphore) {
    const std::uint64_t value = submit(commands, wait_semaphore, wait_stage, signal_semaphore);
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    for (Deferred& deferred : deferred_) {
        if (deferred.value == 0u) {
            deferred.value = value;
        }
    }
    return value;
}

void Context::defer(std::function<void()> destroy) {
    // Commands recorded into the open frame may name the object: it waits for that frame's submission (a peek or
    // one-time submission in between carries none of them).
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    deferred_.push_back({0u, std::move(destroy)});
}

void Context::collect() {
    const std::uint64_t completed = completed_value();
    std::vector<Deferred> due;
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        auto split = std::stable_partition(deferred_.begin(), deferred_.end(), [completed](const Deferred& d) {
            return d.value == 0u || d.value > completed;
        });
        due.assign(std::make_move_iterator(split), std::make_move_iterator(deferred_.end()));
        deferred_.erase(split, deferred_.end());
    }
    for (Deferred& deferred : due) {
        deferred.destroy();
    }
}

std::shared_ptr<Texture> Context::create_linear_image(std::uint32_t width, std::uint32_t height, VkFormat format,
                                                     const char* name) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
    constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((properties.linearTilingFeatures & kNeeded) != kNeeded) {
        return {};
    }
    auto texture = std::make_shared<Texture>();
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocationInfo allocated{};
    if (vmaCreateImage(allocator_, &image_info, &alloc_info, &texture->image, &texture->allocation, &allocated) !=
        VK_SUCCESS) {
        return {};
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->allocation_bytes = allocated.size;
    set_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(texture->image), name);
    return texture;
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 VkFormat format, VkImageUsageFlags usage, bool render_target,
                                                 const char* name, VkSampleCountFlagBits samples) {
    auto texture = std::make_shared<Texture>();
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = levels;
    image_info.arrayLayers = 1;
    image_info.samples = samples;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocationInfo allocated{};
    if (vmaCreateImage(allocator_, &image_info, &alloc_info, &texture->image, &texture->allocation, &allocated) !=
        VK_SUCCESS) {
        return {};
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->levels = levels;
    texture->allocation_bytes = allocated.size;
    const bool depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D16_UNORM;
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                         : VK_IMAGE_ASPECT_COLOR_BIT),
                                  0, levels, 0, 1};
    if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u &&
        vkCreateImageView(device_, &view_info, nullptr, &texture->view) != VK_SUCCESS) {
        return {};
    }
    if (render_target) {
        view_info.subresourceRange.levelCount = 1;
        if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (vkCreateImageView(device_, &view_info, nullptr, &texture->target_view) != VK_SUCCESS) {
            return {};
        }
    }
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(texture->image), name);
    }
    return texture;
}

bool Context::device_memory_budget(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    usage = budget = 0u;
    if (allocator_ == nullptr) {
        return false;
    }
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(allocator_, budgets.data());
    // The largest device-local heap holds the render targets (the only one on most devices).
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u && budgets[i].budget > budget) {
            budget = budgets[i].budget;
            usage = budgets[i].usage;
        }
    }
    return budget != 0u;
}

MappedBuffer Context::create_mapped_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name) {
    MappedBuffer out;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Host memory the GPU reads across the bus, as D3D12's upload heap: written once, read once per frame. Coherent:
    // the shared ring code never flushes (every Vulkan device has a host-visible coherent type).
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &out.buffer, &out.allocation, &allocated) != VK_SUCCESS) {
        return {};
    }
    out.mapped = static_cast<std::byte*>(allocated.pMappedData);
    out.size = size;
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(out.buffer), name);
    }
    return out;
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, VkBufferUsageFlags usage, const char* name) {
    MappedBuffer made = create_mapped_buffer(size, usage, name);
    if (made.buffer == VK_NULL_HANDLE) {
        return {};
    }
    auto buffer = std::make_shared<Buffer>();
    buffer->buffer = made.buffer;
    buffer->allocation = made.allocation;
    buffer->mapped = made.mapped;
    buffer->size = made.size;
    buffer->address = register_address(made.buffer);
    if (buffer->address == 0u) {
        return {};  // the reference's release destroys the buffer
    }
    return buffer;
}

std::shared_ptr<Buffer> Context::create_readback_buffer(std::uint64_t size, const char* name) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Cached host memory: the CPU reads every byte, which uncached write-combined memory makes slow. Where cached
    // memory's invalidation costs more (Mali, Adreno), coherent memory, cached as well where the device has such a type.
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (quirks_.coherent_readback) {
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    auto buffer = std::make_shared<Buffer>();
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer->buffer, &buffer->allocation, &allocated) !=
        VK_SUCCESS) {
        buffer->buffer = VK_NULL_HANDLE;
        return {};
    }
    buffer->mapped = static_cast<std::byte*>(allocated.pMappedData);
    buffer->size = size;
    if (name != nullptr) {
        set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer->buffer), name);
    }
    return buffer;
}

void Context::invalidate(const Buffer& buffer) const noexcept {
    vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

std::uint64_t Context::register_address(VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(address_mutex_);
    std::uint32_t id = 0;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else if (next_address_id_ < kAddressSlots) {
        id = next_address_id_++;
    } else {
        std::fprintf(stderr, "[vulkan] GX address space exhausted (%zu buffers)\n", kAddressSlots - 1u);
        return 0u;
    }
    address_slots_[id].store(buffer, std::memory_order_release);
    return static_cast<std::uint64_t>(id) << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const std::uint64_t id = address >> kAddressShift;
    if (id == 0u || id >= kAddressSlots) {
        return;
    }
    std::lock_guard<std::mutex> lock(address_mutex_);
    address_slots_[id].store(VK_NULL_HANDLE, std::memory_order_release);
    free_address_ids_.push_back(static_cast<std::uint32_t>(id));
}

void Context::set_name(VkObjectType type, std::uint64_t handle, const char* name) const noexcept {
    if (!debug_names_ || vkSetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

}  // namespace galaxy::gx::vk

namespace galaxy::gx::gpu {

TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}

Resource* raw(const BufferRef& buffer) noexcept {
    return buffer != nullptr ? buffer->buffer : VK_NULL_HANDLE;
}

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name) {
    vk::Context& context = vk::Context::get();
    if (!context.ready() || bytes == 0u) {
        return {};
    }
    // One buffer serves any ring: vertices, indices, constants (uniform) and matrix palettes (storage).
    std::shared_ptr<vk::Buffer> buffer = context.create_buffer(
        bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        name);
    if (!buffer) {
        return {};
    }
    UploadMemory out;
    out.cpu = buffer->mapped;
    out.gpu = buffer->address;
    out.buffer = std::move(buffer);
    return out;
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return vk::Context::get().create_texture(
        width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        true, "XFB copy");
}

}  // namespace galaxy::gx::gpu
