#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <nebula_geometry.hpp>
#include <nebula_game_api.h>
#include <NebulaGpu.h>

class GameTexture;

namespace TDDraw {
    void close();
    // Its draw (0 vertex colour, 1 a texture, 2 a texture plus colour 0) and blend modes (0 none, 1 by alpha, 2 added by
    // alpha) as a material: depth off, no culling, colour written and alpha not, passing every alpha (constant 0 for modes
    // 0 and 1, constant 4 after the colour for mode 2); the projection for view mode 1 (the camera's) and 2 (the screen). The
    // site adjusts it and sets its textures and colour; a site that takes another written shader sets it with setShader.
    void setupNative(SiteMaterial* pMaterial, u32 drawMode, u32 blendMode, u8 viewMode);
    // A site under the camera, or the screen for view mode 2.
    TypedDraw site(const SiteMaterial& rMaterial, u8 viewMode);
    void drawLine(TypedDraw&, const Vec3f&, const Vec3f&, u32);
    void drawCircle(TypedDraw&, const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawCircle(TypedDraw&, const Vec3f&, const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawFillCircle(TypedDraw&, const Vec3f&, f32, u32, u32, u32);
    void drawFillCircle(TypedDraw&, const Vec3f&, const Vec3f&, f32, u32, u32);
    void drawFillFan(TypedDraw&, const Vec3f&, const Vec3f&, const Vec3f&, u32, f32, f32, u32);
    void drawCylinder(TypedDraw&, const Vec3f&, const Vec3f&, f32, u32, u32, u32);
    void drawSpherePart(TypedDraw&, const Pos3f&, f32, f32, f32, f32, f32, u32, u32, u32);
    void drawSphere(TypedDraw&, const Vec3f&, f32, u32, u32);
    void drawSphere3D(TypedDraw&, Vec3f, f32, u32, u32);
    void drawTexture(TypedDraw&, const Vec2f&, GameTexture*, const Vec2f&);
    void drawTexture3D(TypedDraw&, const Vec3f&, const Vec3f&, const Vec3f&, f32, f32, GameTexture*, bool, bool);
    void drawFillBox(TypedDraw&, const Vec3f&, const Vec3f&, u32);
    void drawFillBox(TypedDraw&, const Vec2f&, const Vec2f&, u32);
    void drawFillBox3D(TypedDraw&, const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&, u32);
    void cameraInit3D();
    void cameraInit2D();
    void invProject(Vec3f*, const Vec3f&, MtxPtr, const f32*, const f32*, bool);
    void project2D(Vec3f*, const Vec3f&);
    void project2D(Vec2f*, const Vec3f&);
};  // namespace TDDraw
