// SPDX-License-Identifier: GPL-3.0-only
// Native state changes: drawing state the game sets outside materials (nebula_game_api.h NebulaStateOp, the same
// layout: the host copies it), applied to the renderer's drawing state (drawing_state.h NativeDrawState).
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
    kFrameAlpha,
};

struct Op {
    std::uint8_t kind;
    std::uint8_t a[13];
    std::uint16_t size[2];
    float f[6];
    std::uint64_t image;  // host-side: the image's physical address
};


}  // namespace galaxy::render::state
