#pragma once

#include "Game/Util/MathUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include <nebula_geometry.hpp>

namespace MR {
    void crossToPolar(const Vec3f& rA, const Vec3f& rB, f32* pLength, f32* pAngleX, f32* pAngleY);
    void polarToCross(const Vec3f& rSrc, Vec3f* pDst, f32 length, f32 angleX, f32 angleY);
    void polarToCrossDegree(const Vec3f& rSrc, Vec3f* pDst, f32 length, f32 angleX, f32 angleY);
    f32 diffRadian(f32 angleA, f32 angleB);
    bool diffRadianAroundAxis(f32* pAngle, const Vec3f& rAxis, const Vec3f& rA, const Vec3f& rB);
    void projectVecOnPlane(Vec3f* pDst, const Vec3f& rSrc, const Vec3f& rNorm);
};  // namespace MR
