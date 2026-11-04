#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>
#include <nebula_game_api.h>
#include <NebulaGpu.h>

class JUTTexture;

namespace TDDraw {
    void setViewMtx(MtxPtr);
    void loadViewMtx(MtxPtr);
    void setModelMtx(MtxPtr);
    void resetViewMtx();
    void close();
    // E-04: setup's view, and its draw (0 vertex colour, 1 a texture, 2 a texture plus colour 0) and blend modes (0
    // none, 1 by alpha, 2 added by alpha) as a native material: depth off, no culling, colour written and alpha not,
    // passing every alpha. The site adjusts it, sets its textures and colour, then sends it (NativeShader::send).
    void setupNative(NebulaNativeMaterial* pMaterial, u32 drawMode, u32 blendMode, u8 viewMode);
    void sendPoint(const TVec3f&, u32);
    void sendPoint(const TVec3f&);
    void drawLine(const TVec3f&, const TVec3f&, u32);
    void drawCircle(const TVec3f&, const TVec3f&, f32, u32, u32);
    void drawCircle(const TVec3f&, const TVec3f&, const TVec3f&, f32, u32, u32);
    void drawFillCircle(const TVec3f&, f32, u32, u32, u32);
    void drawFillCircle(const TVec3f&, const TVec3f&, f32, u32, u32);
    void drawFillFan(const TVec3f&, const TVec3f&, const TVec3f&, u32, f32, f32, u32);
    void drawCylinder(const TVec3f&, const TVec3f&, f32, u32, u32, u32);
    void drawSpherePart(const TPos3f&, f32, f32, f32, f32, f32, u32, u32, u32);
    void drawSphere(const TVec3f&, f32, u32, u32);
    void drawSphere3D(TVec3f, f32, u32, u32);
    void drawTexture(const TVec2f&, JUTTexture*, const TVec2f&);
    void drawTexture3D(const TVec3f&, const TVec3f&, const TVec3f&, f32, f32, JUTTexture*, bool, bool);
    void drawFillBox(const TVec3f&, const TVec3f&, u32);
    void drawFillBox(const TVec2f&, const TVec2f&, u32);
    void drawFillBox3D(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&, u32);
    void cameraInit3D();
    void cameraInit2D();
    void mixFogColor(TVec3f, f32, u32);
    void invProject(TVec3f*, const TVec3f&, MtxPtr, const f32*, const f32*, bool);
    void project2D(TVec3f*, const TVec3f&);
    void project2D(TVec2f*, const TVec3f&);

    inline void setColor8(u32 color, NebulaColor8* pColor) {
        pColor->r = (color >> 24) & 0xFF;
        pColor->g = (color >> 16) & 0xFF;
        pColor->b = (color >> 8) & 0xFF;
        pColor->a = color & 0xFF;
    }
};  // namespace TDDraw
