#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <nebula_geometry.hpp>

#include <NebulaGpu.h>

class ModelData;
class Model;
class Material;
class JUTTexture;


namespace MR {
    void setupScreen2D();
    void drawScreenColor(const NebulaColor8& rColor);
    void drawScreenPicture(const JUTTexture& rTexture);
    void resetDrawState();
    void loadViewMtxFor2DModel();
    void drawInitFor2DModel();
    void setDefaultViewportAndScissor();
    void loadProjectionMtxFor2D();
    void setupDrawForNW4RLayout(f32, bool);
    void clearZBuffer();
    void clearStencil();
    // A fill's material, under an orthographic projection of the screen.
    SiteMaterial fillScreenSetup(const NebulaColor8&, bool opaque = false);
    void fillScreenArea(TypedDraw&, const Vec2s&, const Vec2s&);
    void fillScreen(const NebulaColor8&, bool opaque = false);
    // The volume draws' colour (runtime/src/render/materials/flat_color.hlsl), their blending, depth, culling and
    // writes as the drawing state has them (each pass sets them).
    void initShadowVolumeMaterial(SiteMaterial& rMaterial, const NebulaColor8& rColor);
    void fillSilhouetteColor();
    const JUTTexture* getMarioShadowTex();
    const JUTTexture* getMarioShadowTexForLoad();
    const Vec3f& getMarioShadowVec();
    void setMarioShadowTex(const JUTTexture*);
    void setMarioShadowVec(const Vec3f&);
    void loadTexProjectionMtx(u32);
    // The camera's projection to screen texture coordinates (s, t, q from a world position) loadTexProjectionMtx loads.
    void calcTexProjectionMtx(Pos3f*);
    void simpleDraw(Model*, Material*);
    void activateGameSceneDraw3D();
    void deactivateGameSceneDraw3D();
};  // namespace MR
