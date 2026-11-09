#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <nebula_geometry.hpp>

#include <NebulaGpu.h>

class ModelData;
class Model;
class Material;
class GameTexture;


namespace MR {
    void setupScreen2D();
    void drawScreenColor(const NebulaColor8& rColor);
    void drawScreenPicture(const GameTexture& rTexture);
    void resetDrawState();
    void loadViewMtxFor2DModel();
    void drawInitFor2DModel();
    void setDefaultViewportAndScissor();
    void loadProjectionMtxFor2D();
    void setupDrawForLayout();
    void clearZBuffer();
    void clearStencil();
    // A fill's material, under an orthographic projection of the screen.
    SiteMaterial fillScreenSetup(const NebulaColor8&, bool opaque = false);
    // The rectangle from rMin to rMax, screen units.
    void fillScreenArea(TypedDraw&, const Vec2f& rMin, const Vec2f& rMax);
    void fillScreen(const NebulaColor8&, bool opaque = false);
    // The volume draws' colour (runtime/src/render/materials/flat_color.hlsl), their blending, depth, culling and
    // writes as the drawing state has them (each pass sets them).
    void initShadowVolumeMaterial(SiteMaterial& rMaterial, const NebulaColor8& rColor);
    void fillSilhouetteColor();
    const GameTexture* getMarioShadowTex();
    const GameTexture* getMarioShadowTexForLoad();
    const Vec3f& getMarioShadowVec();
    void setMarioShadowTex(const GameTexture*);
    void setMarioShadowVec(const Vec3f&);
    // The camera's projection to screen texture coordinates (s, t, q from a world position).
    void calcTexProjectionMtx(Pos3f*);
    void simpleDraw(Model*, Material*);
    void activateGameSceneDraw3D();
    void deactivateGameSceneDraw3D();
};  // namespace MR
