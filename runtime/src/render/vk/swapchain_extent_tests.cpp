// SPDX-License-Identifier: GPL-3.0-only
// The swap chain's size (galaxy/render/vk/swapchain_extent.h): a surface that reports its size keeps it, a surface that does not
// (Wayland) follows the window.
#include "galaxy/render/vk/swapchain_extent.h"

#include <iostream>

namespace {

using galaxy::render::vk::Extent2;
using galaxy::render::vk::kExtentUndefined;
using galaxy::render::vk::swapchain_extent;
using galaxy::render::vk::unpack_window_extent;

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    bool ok = true;
    const Extent2 none{kExtentUndefined, kExtentUndefined};
    const Extent2 first{1280u, 720u};
    ok &= expect(swapchain_extent({1920u, 1080u}, {800u, 600u}, first) == Extent2{1920u, 1080u},
                 "a surface that reports its size keeps it, whatever the window said (Windows, Android, X11)");
    ok &= expect(swapchain_extent(none, {}, first) == first, "no size from the surface and none from the window yet: the window's first size");
    ok &= expect(swapchain_extent(none, {1920u, 1080u}, first) == Extent2{1920u, 1080u},
                 "no size from the surface: the size the window reported at its resize, not its first size");
    ok &= expect(swapchain_extent(none, {1920u, 0u}, first) == first && swapchain_extent(none, {0u, 1080u}, first) == first,
                 "a window size with a zero side is no size");
    const Extent2 resized = unpack_window_extent((std::uint64_t{2560u} << 32u) | 1440u);
    ok &= expect(resized == Extent2{2560u, 1440u}, "the resize message holds the width in its high half");
    ok &= expect(swapchain_extent(none, resized, {1920u, 1080u}) == Extent2{2560u, 1440u},
                 "after a resize the swap chain is made at the new size, then the backbuffer follows it");
    if (!ok) return 1;
    std::cout << "swapchain extent tests passed\n";
    return 0;
}
