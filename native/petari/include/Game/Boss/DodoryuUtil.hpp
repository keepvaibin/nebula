#pragma once

#include <nebula_geometry.hpp>

class Dodoryu;

namespace DodoryuUtil {
    bool calcVerticalizedDir(Dodoryu* pDodoryu, Vec3f* pVec, const Vec3f& rVec);
    void turnToward(Dodoryu* pDodoryu, const Vec3f& rDirection, f32 maxAngle);
    void accelerate(Dodoryu* pDodoryu, const Vec3f& rDirection, f32 acceleration, f32 lateralDamping, f32 maxSpeed);
    void addVelocity(Dodoryu* pDodoryu, bool isSnap);
    void rotateVelocityByWall(Dodoryu* pDodoryu);
    void calcEscapeDir(Dodoryu* pDodoryu, Vec3f* pDirection, f32 maxAngle);
};  // namespace DodoryuUtil
