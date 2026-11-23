// SPDX-License-Identifier: GPL-3.0-only
// The size of a swap chain: what the surface says, else what the window last told. A surface whose size is set by the swap chain
// itself reports 0xFFFFFFFF for both (the Vulkan specification names Wayland): the swap chain then takes the window's size, which
// changes when the player resizes the window, maximises it or switches to full screen. Taken out of renderer_vk.cpp so a test
// can run it with a made-up surface.
#pragma once

#include <cstdint>

namespace galaxy::render::vk {

inline constexpr std::uint32_t kExtentUndefined = 0xFFFFFFFFu;

struct Extent2 {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    [[nodiscard]] constexpr bool operator==(const Extent2& other) const noexcept {
        return width == other.width && height == other.height;
    }
};

// `surface`: the surface's currentExtent; `window`: the last size the window reported (0 when it reported none yet);
// `backbuffer`: the size the window had when it was made, or the swap chain's last (host_window::backbuffer_width/height).
[[nodiscard]] constexpr Extent2 swapchain_extent(Extent2 surface, Extent2 window, Extent2 backbuffer) noexcept {
    if (surface.width != kExtentUndefined) return surface;
    if (window.width != 0u && window.height != 0u) return window;
    return backbuffer;
}

// A window size as the host's resize message packs it (host_window::pending_resize_extent: width in the high half).
[[nodiscard]] constexpr Extent2 unpack_window_extent(std::uint64_t packed) noexcept {
    return {static_cast<std::uint32_t>(packed >> 32u), static_cast<std::uint32_t>(packed & 0xffffffffu)};
}

}  // namespace galaxy::render::vk
