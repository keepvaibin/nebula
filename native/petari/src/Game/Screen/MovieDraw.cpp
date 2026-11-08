#include "Game/Screen/MovieDraw.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>

namespace {
    bool sLinear = false;

    // A movie plane (one channel, 8 bits a texel) as the movie material's texture `slot`: clamped, filtered as
    // setLinearFilter chose, one level.
    void setPlane(SiteMaterial& rMaterial, u32 slot, u32 id, u16 width, u16 height) {
        SiteTexture texture;
        SiteTextures::init(&texture, nullptr, width, height, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
        const u8 filter = sLinear ? NEBULA_FILTER_LINEAR : NEBULA_FILTER_NEAREST;
        SiteTextures::setFilter(&texture, filter, filter, 0.0f, 0.0f, 0.0f, false, 0);
        rMaterial.setTextureId(slot, id, texture);
    }
}  // namespace

namespace MovieDraw {
    void setLinearFilter(bool linear) {
        sLinear = linear;
    }

    void drawFrame(f32 screenWidth, f32 screenHeight, u16 textureWidth, u16 textureHeight, f32 x, f32 y, f32 width,
                   f32 height) {
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
        setPlane(material, 0, NEBULA_TEXTURE_MOVIE_Y, textureWidth, textureHeight);
        setPlane(material, 1, NEBULA_TEXTURE_MOVIE_U, chromaWidth, chromaHeight);
        setPlane(material, 2, NEBULA_TEXTURE_MOVIE_V, chromaWidth, chromaHeight);
        TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {x, y, 0.0f, 0.0f, 0.0f};
        *vtx++ = {x + width, y, 0.0f, 1.0f, 0.0f};
        *vtx++ = {x + width, y + height, 0.0f, 1.0f, 1.0f};
        *vtx++ = {x, y + height, 0.0f, 0.0f, 1.0f};
    }
};  // namespace MovieDraw
