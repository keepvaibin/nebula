// SPDX-License-Identifier: GPL-3.0-only
// Native state changes: drawing state the game sets outside materials (nebula_game_api.h NebulaStateOp, the same
// layout: the host copies it), applied to the renderer's drawing state as the GX functions they name set it.
#pragma once

#include <cstdint>
#include <span>

namespace galaxy::render::state {

enum Kind : std::uint8_t {
    kBlendEtc = 1,
    kBlend,
    kZMode,
    kAlphaCompare,
    kDstAlpha,
    kCull,
    kGenMode,
    kGenMode2,
    kChannel,
    kTevColorCalc,
    kTevAlphaCalc,
    kTevDirect,
    kTevOrder,
    kTevColor,
    kMaterialColor,
    kAmbientColor,
    kTexCoordGen,
    kTexLookup,
    kTexImageAttr,
    kTexImage,
    kFog,
    kTevColorIn,
    kTevAlphaIn,
    kTevColorOp,
    kTevAlphaOp,
    kTevKColorSel,
    kTevKAlphaSel,
    kTevSwapMode,
    kTevStageOrder,
    kColorUpdate,
    kAlphaUpdate,
    kDither,
    kProjection,
    kViewport,
    kScissor,
    kScissorOffset,
    kTevKColor,
    kTevColorS10,
    kClipMode,
    kIndTexMatrix,
    kTevSwapTable,
    kNumTexGens,
    kNumChans,
    kNumTevStages,
    kNumIndStages,
    kCoPlanar,
    kZCompLoc,
    kCurrentMatrix,
    kTevIndirect,
    kIndTexScale,
    kLineWidth,
    kPointSize,
    kLightAttenuation,
    kLightPosition,
    kPixelFormat,
    kInvalidateTextures,
    kZTexture,
    kCopyClear,
    kCopySamples,
    kCopyVfilter,
    kCopyYScale,
    kCopy,
    kFogRange,
    kFogRangeTable,
    kTexTlut,
    kTlutLoad,
    kIndTexOrder,
    kTexOffsets,
};

struct Op {
    std::uint8_t kind;
    std::uint8_t a[13];
    std::uint16_t size[2];
    float f[6];
    std::uint64_t image;  // host-side: the image's physical address
};


}  // namespace galaxy::render::state
