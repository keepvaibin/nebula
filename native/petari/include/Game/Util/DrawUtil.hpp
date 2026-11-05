#pragma once

#include <nebula_geometry.hpp>

#include <NebulaGpu.h>

class J3DModelData;
class J3DModel;
class J3DMaterial;
class JUTTexture;


namespace MR {
    void setupScreen2D();
    void drawScreenColor(const NebulaColor8& rColor);
    void drawScreenPicture(const JUTTexture& rTexture);
    void drawInit();
    void resetDrawState();
    void loadViewMtxFor2DModel();
    void drawInitFor2DModel();
    void setDefaultViewportAndScissor();
    void loadProjectionMtxFor2D();
    void setupDrawForNW4RLayout(f32, bool);
    void clearZBuffer();
    void clearStencil();
    void fillScreenSetup(const NebulaColor8&);
    void fillScreenArea(const Vec2s&, const Vec2s&);
    void fillScreen(const NebulaColor8&);
    void setupShadowVolumeDraw();
    // The volume draws' colour (runtime/src/render/materials/flat_color.hlsl), their blending, depth, culling and
    // writes as the drawing state has them (each pass sets them).
    void setShadowVolumeColor(const NebulaColor8& rColor);
    void drawSimpleModel(J3DModelData*);
    void fillSilhouetteColor();
    const JUTTexture* getMarioShadowTex();
    const JUTTexture* getMarioShadowTexForLoad();
    const Vec3f& getMarioShadowVec();
    void setMarioShadowTex(const JUTTexture*);
    void setMarioShadowVec(const Vec3f&);
    void loadTexProjectionMtx(u32);
    // The camera's projection to screen texture coordinates (s, t, q from a world position) loadTexProjectionMtx loads.
    void calcTexProjectionMtx(Pos3f*);
    void simpleDraw(J3DModel*, J3DMaterial*);
    void activateGameSceneDraw3D();
    void deactivateGameSceneDraw3D();
};  // namespace MR
