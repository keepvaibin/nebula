#include "Game/Camera/CameraCalc.hpp"

namespace MR {
    void crossToPolar(const Vec3f& rA, const Vec3f& rB, f32* pLength, f32* pAngleX, f32* pAngleY) {
        f32 dx = rB.x - rA.x;
        f32 dy = rB.y - rA.y;
        f32 dz = rB.z - rA.z;
        *pLength = MR::sqrt(dx * dx + dy * dy + dz * dz);
        *pAngleX = MR::atan2(dy, MR::sqrt(dx * dx + dz * dz));
        *pAngleY = MR::atan2(dx, dz);
    }

    void polarToCross(const Vec3f& rSrc, Vec3f* pDst, f32 length, f32 angleX, f32 angleY) {
        pDst->x = rSrc.x + length * MR::cos(angleX) * MR::sin(angleY);
        pDst->y = rSrc.y + length * MR::sin(angleX);
        pDst->z = rSrc.z + length * MR::cos(angleX) * MR::cos(angleY);
    }

    void polarToCrossDegree(const Vec3f& rSrc, Vec3f* pDst, f32 length, f32 angleX, f32 angleY) {
        pDst->x = rSrc.x + length * MR::cosDegree(angleX) * MR::sinDegree(angleY);
        pDst->y = rSrc.y + length * MR::sinDegree(angleX);
        pDst->z = rSrc.z + length * MR::cosDegree(angleX) * MR::cosDegree(angleY);
    }

    f32 diffRadian(f32 angleA, f32 angleB) {
        f32 diff = MR::repeat(angleB, 0.0f, TWO_PI) - MR::repeat(angleA, 0.0f, TWO_PI);
        if (diff > 0.0f) {
            if (diff > MR::pi()) {
                diff = -(MR::pi() * 2.0f - diff);
            }
        } else {
            if (diff < -MR::pi()) {
                diff = diff + MR::pi() * 2.0f;
            }
        }
        return diff;
    }

    bool diffRadianAroundAxis(f32* pAngle, const Vec3f& rAxis, const Vec3f& rA, const Vec3f& rB) {
        Vec3f dirA = rA.killElement(rAxis);
        if (MR::isNearZero(dirA)) {
            return false;
        }

        Vec3f dirB = rB.killElement(rAxis);
        if (MR::isNearZero(dirB)) {
            return false;
        }

        MR::normalize(&dirA);
        MR::normalize(&dirB);
        s32 sign = rA.cross(rB).dot(rAxis) < 0.0f ? -1 : 1;
        *pAngle = MR::acos(dirA.dot(dirB)) * sign;
        return true;
    }

    void projectVecOnPlane(Vec3f* pDst, const Vec3f& rSrc, const Vec3f& rNorm) {
        pDst->set(rSrc - rNorm * rNorm.dot(rSrc));
    }
};  // namespace MR

