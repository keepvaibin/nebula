// SPDX-License-Identifier: GPL-3.0-only
// The single translation units of volk (MIT) and the Vulkan Memory Allocator (MIT).
#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
