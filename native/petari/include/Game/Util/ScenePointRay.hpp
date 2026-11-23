#pragma once

// The ray of the scene query (docs/engine/CONSOLE-FREE.md, section 3) in the camera's view space: the direction
// through a screen point and where it first meets a sphere. MR::calcScenePointUnderScreen (StarPointerUtil.cpp) walks
// the map and the pointer targets with it; native/tests/scene_point_ray_tests.cpp proves it.
#include <nebula_geometry.hpp>

namespace ScenePointRay {
    // The view-space direction through the screen point (x right, y down, in a screen of width by height) at a view
    // depth of 1 (z = -1), for a perspective projection whose rows 0 and 1 are (p00, 0, p02, 0) and (0, p11, p12, 0):
    // StarPointerTarget::calcScreenPositionFromView read backwards. The point t * dir has the view depth t.
    inline Vec3f calcViewDir(f32 x, f32 y, f32 width, f32 height, f32 p00, f32 p02, f32 p11, f32 p12) {
        const f32 ndcX = x * 2.0f / width - 1.0f;
        const f32 ndcY = 1.0f - y * 2.0f / height;
        return Vec3f((ndcX + p02) / p00, (ndcY + p12) / p11, -1.0f);
    }

    // The view depth where the ray t * rDir (t > 0) first enters the sphere of radius around rCenter (view space);
    // false when it misses the sphere, when the sphere lies behind the camera, or when it holds the camera (the near
    // plane cuts what is drawn there).
    inline bool calcDepthToSphere(const Vec3f& rDir, const Vec3f& rCenter, f32 radius, f32* pDepth) {
        const f32 outside = rCenter.dot(rCenter) - radius * radius;
        if (outside <= 0.0f) {
            return false;
        }

        const f32 dirDot = rDir.dot(rDir);
        const f32 along = rDir.dot(rCenter);
        const f32 disc = along * along - dirDot * outside;
        if (along <= 0.0f || disc < 0.0f) {
            return false;
        }

        *pDepth = (along - NebulaSqrt(disc)) / dirDot;
        return true;
    }
};  // namespace ScenePointRay
