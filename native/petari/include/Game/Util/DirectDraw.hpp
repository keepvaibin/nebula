#pragma once

#include <nebula_geometry.hpp>
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
    void sendPoint(const Vec3f&, u32);
    void sendPoint(const Vec3f&);
    void drawLine(const Vec3f&, const Vec3f&, u32);
    void drawCircle(const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawCircle(const Vec3f&, const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawFillCircle(const Vec3f&, f32, u32, u32, u32);
    void drawFillCircle(const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawFillFan(const Vec3f&, const Vec3f&, const Vec3f&, u32, f32, f32, u32);
    void drawCylinder(const Vec3f&, const Vec3f&, f32, u32, u32, u32);
    void drawSpherePart(const Pos3f&, f32, f32, f32, f32, f32, u32, u32, u32);
    void drawSphere(const Vec3f&, f32, u32, u32);
    void drawSphere3D(Vec3f, f32, u32, u32);
    void drawTexture(const Vec2f&, JUTTexture*, const Vec2f&);
    void drawTexture3D(const Vec3f&, const Vec3f&, const Vec3f&, f32, f32, JUTTexture*, bool, bool);
    void drawFillBox(const Vec3f&, const Vec3f&, u32);
    void drawFillBox(const Vec2f&, const Vec2f&, u32);
    void drawFillBox3D(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&, u32);
    void cameraInit3D();
    void cameraInit2D();
    void mixFogColor(Vec3f, f32, u32);
    void invProject(Vec3f*, const Vec3f&, MtxPtr, const f32*, const f32*, bool);
    void project2D(Vec3f*, const Vec3f&);
    void project2D(Vec2f*, const Vec3f&);

    inline void setColor8(u32 color, NebulaColor8* pColor) {
        pColor->r = (color >> 24) & 0xFF;
        pColor->g = (color >> 16) & 0xFF;
        pColor->b = (color >> 8) & 0xFF;
        pColor->a = color & 0xFF;
    }
};  // namespace TDDraw
