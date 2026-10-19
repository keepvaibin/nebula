// SPDX-License-Identifier: GPL-3.0-only
// The Win32 game window's Vulkan surface (host_window.h).

#include "galaxy/gx/host_window_win32.h"

#include <volk.h>

namespace galaxy::gx::host_window {

std::vector<const char*> vulkan_instance_extensions() {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
}

VkSurfaceKHR create_vulkan_surface(VkInstance instance, NativeWindow window) {
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return surface;
}

// The Windows drivers report through the validation layers, not a log of their own.
std::string driver_log() {
    return {};
}

}  // namespace galaxy::gx::host_window
