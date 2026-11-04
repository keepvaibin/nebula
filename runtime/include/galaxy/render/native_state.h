// SPDX-License-Identifier: GPL-3.0-only
// The drawing state changes the game makes outside materials (host_main.cpp fills them from its typed calls),
// applied to the renderer's drawing state (drawing_state.h NativeDrawState).
#pragma once

#include <cstdint>
#include <span>

namespace galaxy::render::state {

enum Kind : std::uint8_t {
    kTexCoordGen = 1,
    kProjection,
    kViewport,
    kScissor,
    kLightAttenuation,
    kLightPosition,
    kTargetAlpha,
};

struct Op {
    std::uint8_t kind;
    std::uint8_t a[13];
    std::uint16_t size[2];
    float f[6];
    std::uint64_t image;  // host-side: the image's physical address
};


}  // namespace galaxy::render::state
