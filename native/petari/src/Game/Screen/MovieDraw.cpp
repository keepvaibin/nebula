#include "Game/Screen/MovieDraw.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>

namespace {
    bool sLinear = false;

    // A plane (one channel, 8 bits a texel) as the movie material's texture `slot`: clamped, filtered as setLinearFilter
    // chose, one level.
    void setPlane(SiteMaterial& rMaterial, u32 slot, u8* pImage, u16 width, u16 height) {
        SiteTexture texture;
        SiteTextures::init(&texture, pImage, width, height, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
        const u8 filter = sLinear ? NEBULA_FILTER_LINEAR : NEBULA_FILTER_NEAREST;
        SiteTextures::setFilter(&texture, filter, filter, 0.0f, 0.0f, 0.0f, false, 0);
        rMaterial.setTexture(slot, texture);
    }
}  // namespace

namespace MovieDraw {
    void setLinearFilter(bool linear) {
        sLinear = linear;
    }

    void uploadPlanes(u8* pY, u8* pU, u8* pV, s16 textureWidth, s16 textureHeight) {
        const u16 chromaWidth = static_cast< u16 >(textureWidth >> 1);
        const u16 chromaHeight = static_cast< u16 >(textureHeight >> 1);
        RenderTexture::linear(pY, NEBULA_TEXTURE_LINEAR_L8, textureWidth, textureHeight, 1);
        RenderTexture::linear(pU, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);
        RenderTexture::linear(pV, NEBULA_TEXTURE_LINEAR_L8, chromaWidth, chromaHeight, 1);
    }

    void drawFrame(u16 screenWidth, u16 screenHeight, u8* pY, u8* pU, u8* pV, s16 x, s16 y, s16 textureWidth,
                   s16 textureHeight, s16 width, s16 height) {
        // The screen in its own pixels, the frame as it is.
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, screenHeight, 0.0f, screenWidth, 0.0f, -1.0f);
        Frame::setOrthographic(projection);
        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);

        const u16 chromaWidth = static_cast< u16 >(textureWidth >> 1);
        const u16 chromaHeight = static_cast< u16 >(textureHeight >> 1);

        // runtime/src/render/materials/movie.hlsl: Y, U and V to RGB; opaque, depth neither tested nor written.
        SiteMaterial material;
        material.init(NEBULA_SHADER_MOVIE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
        setPlane(material, 0, pY, textureWidth, textureHeight);
        setPlane(material, 1, pU, chromaWidth, chromaHeight);
        setPlane(material, 2, pV, chromaWidth, chromaHeight);
        TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {static_cast< f32 >(x), static_cast< f32 >(y), 0.0f, 0.0f, 0.0f};
        *vtx++ = {static_cast< f32 >(static_cast< s16 >(x + width)), static_cast< f32 >(y), 0.0f, 1.0f, 0.0f};
        *vtx++ = {static_cast< f32 >(static_cast< s16 >(x + width)), static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f, 1.0f, 1.0f};
        *vtx++ = {static_cast< f32 >(x), static_cast< f32 >(static_cast< s16 >(y + height)), 0.0f, 0.0f, 1.0f};
    }
};  // namespace MovieDraw
