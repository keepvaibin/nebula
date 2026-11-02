#include "Game/Screen/MovieDraw.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Util/NativeShader.hpp"
#include <nebula_game_api.h>

namespace {
    bool sLinear = false;

    // A plane (one channel, 8 bits a texel) as the movie material's texture `slot`: clamped, filtered as setLinearFilter
    // chose, one level.
    void setPlane(NebulaNativeMaterial& rMaterial, u32 slot, u8* pImage) {
        rMaterial.textures[slot].image = reinterpret_cast< uintptr_t >(pImage);
        rMaterial.textures[slot].repeat_s = rMaterial.textures[slot].repeat_t = 0;
        rMaterial.textures[slot].mag_linear = rMaterial.textures[slot].min_linear = sLinear ? 1 : 0;
    }
}  // namespace

namespace MovieDraw {
    void setLinearFilter(bool linear) {
        sLinear = linear;
    }

    void drawFrame(u16 screenWidth, u16 screenHeight, u8* pY, u8* pU, u8* pV, s16 x, s16 y, s16 textureWidth,
                   s16 textureHeight, s16 width, s16 height) {
        // The screen in its own pixels, the frame as it is.
        J3DNativeState::setFrameAlpha(false);
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, screenHeight, 0.0f, screenWidth, 0.0f, -1.0f);
        J3DNativeView::setOrthographic(projection);
        J3DNativeView::setViewport(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 1.0f);
        J3DNativeView::setScissor(0, 0, screenWidth, screenHeight);
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DPass::positionMatrix(0);
        J3DPass::writeColor(true);
        J3DPass::writeAlpha(false);

        // The frame's planes, decoded into these buffers since the last frame as rows of texels (nebula_cooked_texture.h).
        const u16 chromaWidth = static_cast< u16 >(textureWidth >> 1);
        const u16 chromaHeight = static_cast< u16 >(textureHeight >> 1);
        J3DNativeTexture::registerLinear(pY, NEBULA_TEXTURE_LINEAR_L8, textureWidth, textureHeight, 1);
        J3DNativeTexture::registerLinear(pU, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);
        J3DNativeTexture::registerLinear(pV, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);

        // runtime/src/render/materials/movie.hlsl: Y, U and V to RGB; opaque, depth neither tested nor written.
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_MOVIE, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
        setPlane(material, 0, pY);
        setPlane(material, 1, pU);
        setPlane(material, 2, pV);
        NativeShader::send(material);
        // The coordinates' texture matrices: the identity's place.
        J3DNativeShape::setCurrentIdentity();

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(static_cast< f32 >(x), static_cast< f32 >(y), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + width)), static_cast< f32 >(y), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + width)),
                                static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
        J3DNativeDraw::position(static_cast< f32 >(x), static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);
    }
};  // namespace MovieDraw
