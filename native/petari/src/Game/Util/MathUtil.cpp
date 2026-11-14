#include "Game/Util/MathUtil.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <nebula_geometry.hpp>


#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getRandomDegree_f1, "getRandomDegree.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isHalfProbability_f1, "isHalfProbability.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getEaseInValue_f1, "getEaseInValue.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getEaseOutValue_f1, "getEaseOutValue.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getEaseInOutValue_f1, "getEaseInOutValue.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getScaleWithReactionValueZeroToOne_f1, "getScaleWithReactionValueZeroToOne.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getScaleWithReactionValueZeroToOne_f2, "getScaleWithReactionValueZeroToOne.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getScaleWithReactionValueZeroToOne_f3, "getScaleWithReactionValueZeroToOne.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getScaleWithReactionValueZeroToOne_f4, "getScaleWithReactionValueZeroToOne.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getReduceVibrationValue_f1, "getReduceVibrationValue.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_crossOrPerpendicular_f1, "crossOrPerpendicular.f1", 1.0e-12f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeAxisAndCosignVecToVec_f1, "makeAxisAndCosignVecToVec.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcParabolicFunctionParam_f1, "calcParabolicFunctionParam.f1", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcParabolicFunctionParam_f2, "calcParabolicFunctionParam.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcParabolicFunctionParam_f3, "calcParabolicFunctionParam.f3", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcParabolicFunctionParam_f4, "calcParabolicFunctionParam.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendQuatUpFront_f1, "blendQuatUpFront.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendQuatUpFront_f2, "blendQuatUpFront.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendQuatFrontUp_f1, "blendQuatFrontUp.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendQuatFrontUp_f2, "blendQuatFrontUp.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnQuat_f1, "turnQuat.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnQuat_f2, "turnQuat.f2", 0.015f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotateY_f1, "calcRotateY.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnVecToVecDegree_f1, "turnVecToVecDegree.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendAngle_f1, "blendAngle.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_lerp_f1, "lerp.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_vecBlendSphere_f1, "vecBlendSphere.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_diffAngleSignedHorizontal_f1, "diffAngleSignedHorizontal.f1", 0.00017453293f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNearAngleDegree_f1, "isNearAngleDegree.f1", 180.0f);


const Vec gZeroVec = {0.0f, 0.0f, 0.0f};

namespace MR {
    f32 acosEx(f32 x) {
        return Angle::acosRadian(x);
    }

    f32 getRandom() {
        u32 rand = SingletonHolder< GameSystem >::get()->mObjHolder->mRandom.rand();
        u32 value = (rand >> 9) | 0x3F800000;

        return reinterpret_cast< f32& >(value) - 1.0f;
    }

    f32 getRandom(f32 min, f32 max) {
        return min + (max - min) * getRandom();
    }

    s32 getRandom(s32 min, s32 max) {
        return getRandom(static_cast< f32 >(min), static_cast< f32 >(max));
    }

    f32 getRandomDegree() {
        return getRandom(0.0f, cNum_getRandomDegree_f1);
    }

    void calcRandomVec(Vec3f* pDst, f32 min, f32 max) {
        // Drawn z, y, x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
        const f32 z = getRandom(min, max);
        const f32 y = getRandom(min, max);
        const f32 x = getRandom(min, max);
        pDst->set< f32 >(x, y, z);
    }

    bool isHalfProbability() {
        return getRandom() < cNum_isHalfProbability_f1;
    }

    f32 getSignHalfProbability() {
        if (isHalfProbability()) {
            return -1.0f;
        } else {
            return 1.0f;
        }
    }

    void getRandomVector(Vec3f* pDst, f32 range) {
        // Drawn z, y, x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
        const f32 z = getRandom(-range, range);
        const f32 y = getRandom(-range, range);
        const f32 x = getRandom(-range, range);
        pDst->set< f32 >(x, y, z);
    }

    void addRandomVector(Vec3f* pDst, const Vec3f& rSrc, f32 range) {
        f32 x = getRandom(-range, range);
        f32 y = getRandom(-range, range);
        f32 z = getRandom(-range, range);

        pDst->set(rSrc + Vec3f(x, y, z));
    }

    void turnRandomVector(Vec3f* pDst, const Vec3f& rSrc, f32 range) {
        f32 srcLength = rSrc.length();

        addRandomVector(pDst, rSrc, range);

        if (isNearZero(*pDst)) {
            pDst->set(rSrc);
        } else {
            pDst->setLength(srcLength);
        }
    }

    f32 getInterpolateValue(f32 t, f32 start, f32 end) {
        return start + (end - start) * t;
    }

    f32 getLinerValue(f32 x, f32 start, f32 end, f32 max) {
        return getInterpolateValue(x / max, start, end);
    }

    f32 getLinerValueFromMinMax(f32 x, f32 min, f32 max, f32 start, f32 end) {
        return getInterpolateValue((FloatUtil::clamp(x, min, max) - min) / (max - min), start, end);
    }

    f32 getEaseInValue(f32 x, f32 start, f32 end, f32 max) {
        return getInterpolateValue(1.0f - Angle::cosRadian(x / max * pi() / cNum_getEaseInValue_f1), start, end);
    }

    f32 getEaseOutValue(f32 x, f32 start, f32 end, f32 max) {
        f32 t = x / max * pi() / cNum_getEaseOutValue_f1;
        return getInterpolateValue(Angle::sinRadian(t), start, end);
    }

    f32 getEaseInOutValue(f32 x, f32 start, f32 end, f32 max) {
        f32 t = x / max * pi();
        return getInterpolateValue((1.0f - Angle::cosRadian(t)) / cNum_getEaseInOutValue_f1, start, end);
    }

    f32 getScaleWithReactionValueZeroToOne(f32 x, f32 freq, f32 amplitude) {
        if (x < cNum_getScaleWithReactionValueZeroToOne_f1) {
            return getEaseOutValue(x * cNum_getScaleWithReactionValueZeroToOne_f2, 0.0f, 1.0f, 1.0f);
        } else {
            f32 t = (x - cNum_getScaleWithReactionValueZeroToOne_f3) * cNum_getScaleWithReactionValueZeroToOne_f4;
            return 1.0f + (1.0f - Angle::cosRadian(pi() * freq * t)) * (1.0f - t) * amplitude;
        }
    }

    f32 getConvergeVibrationValue(f32 x, f32 start, f32 end, f32 dampScale, f32 rate) {
        f32 vibWeight = (x * x) * (x * x);
        f32 t1 = 1.0f - x;
        f32 convergeWeight = (t1 * t1) * (t1 * t1);
        f32 dampRatio = dampScale * t1;

        f32 vibration = Angle::sinRadian(PI * (x + rate * vibWeight));
        return getInterpolateValue((1.0f - convergeWeight) + vibration * dampRatio, start, end);
    }

    f32 getReduceVibrationValue(f32 x, f32 time, f32 base, f32 amplitude, f32 freq) {
        f32 vibMax = base + amplitude;

        f32 vib = Angle::cosRadian(x * (1.0f / freq * PI));
        f32 vibration = (amplitude * cNum_getReduceVibrationValue_f1) * (vib - 1.0f);
        if (x >= time) {
            return vibration + vibMax;
        } else {
            f32 t = x - time;
            f32 reduce = (1.0f - vibMax) * (1.0f / (time * time)) * t * t;
            reduce = vibMax + reduce;
            return reduce + vibration;
        }
    }

    void crossOrPerpendicular(Vec3f* pDst, const Vec3f& rA, const Vec3f& rB) {
        const Vec3f a(rA);
        const Vec3f b(rB);
        pDst->cross(a, b);
        const f32 crossSq = pDst->dot(*pDst);
        if (!(crossSq <= cNum_crossOrPerpendicular_f1 * a.dot(a) * b.dot(b))) {
            return;
        }

        const f32 ax = a.x < 0.0f ? -a.x : a.x;
        const f32 ay = a.y < 0.0f ? -a.y : a.y;
        const f32 az = a.z < 0.0f ? -a.z : a.z;
        Vec3f axis(0.0f, 0.0f, 0.0f);
        if (ax <= ay && ax <= az) {
            axis.x = 1.0f;
        } else if (ay <= az) {
            axis.y = 1.0f;
        } else {
            axis.z = 1.0f;
        }

        pDst->cross(a, axis);
        if (!(pDst->dot(*pDst) > 0.0f)) {
            pDst->set(axis);
        }
    }

    void makeAxisFrontUp(Vec3f* pSide, Vec3f* pUp, const Vec3f& rFront, const Vec3f& rUp) {
        crossOrPerpendicular(pSide, rUp, rFront);
        normalize(pSide);
        pUp->cross(rFront, *pSide);
    }

    void makeAxisFrontSide(Vec3f* pUp, Vec3f* pSide, const Vec3f& rFront, const Vec3f& rSide) {
        crossOrPerpendicular(pUp, rFront, rSide);
        normalize(pUp);
        pSide->cross(*pUp, rFront);
    }

    void makeAxisUpFront(Vec3f* pSide, Vec3f* pFront, const Vec3f& rUp, const Vec3f& rFront) {
        crossOrPerpendicular(pSide, rUp, rFront);
        normalize(pSide);
        pFront->cross(*pSide, rUp);
    }

    void makeAxisUpSide(Vec3f* pFront, Vec3f* pSide, const Vec3f& rUp, const Vec3f& rSide) {
        crossOrPerpendicular(pFront, rSide, rUp);
        normalize(pFront);
        pSide->cross(rUp, *pFront);
    }

    void makeAxisVerticalZX(Vec3f* pVec, const Vec3f& rAxis) {
        pVec->killElement(Vec3f(0.0f, 0.0f, 1.0f), rAxis);
        if (isNearZero(*pVec)) {
            pVec->killElement(Vec3f(1.0f, 0.0f, 0.0f), rAxis);
        }

        normalize(pVec);
    }

    void makeAxisCrossPlane(Vec3f* pVec, Vec3f* pCross, const Vec3f& rNorm) {
        makeAxisVerticalZX(pVec, rNorm);
        pCross->cross(*pVec, rNorm);
        normalizeOrZero(pCross);
    }

    bool makeAxisAndCosignVecToVec(Vec3f* pAxis, f32* pCos, const Vec3f& rFrom, const Vec3f& rTo) {
        Vec3f from;
        if (isOppositeDirection(rFrom, rTo)) {
            turnRandomVector(&from, rFrom, cNum_makeAxisAndCosignVecToVec_f1);
            normalize(&from);
        } else {
            from.set(rFrom);
        }

        Vec3f axis = from.cross(rTo);

        if (isNearZero(axis)) {
            pAxis->zero();
            *pCos = 1.0f;

            return false;
        } else {
            normalize(axis, pAxis);
            *pCos = FloatUtil::clamp(from.dot(rTo), -1.0f, 1.0f);

            return true;
        }
    }

    f32 calcPerpendicFootToLine(Vec3f* pDst, const Vec3f& rPos, const Vec3f& rPointA, const Vec3f& rPointB) {
        Vec3f offset = rPointB - rPointA;
        f32 ratio = rPos.dot(offset) - rPointA.dot(offset);
        ratio /= offset.squared();
        offset.scale(ratio);
        pDst->set(rPointA);
        pDst->add(offset);
        return ratio;
    }

    f32 calcPerpendicFootToLineInside(Vec3f* pDst, const Vec3f& rPos, const Vec3f& rPointA, const Vec3f& rPointB) {
        Vec3f offset = rPointB - rPointA;
        f32 ratio = rPos.dot(offset) - rPointA.dot(offset);
        ratio /= offset.squared();
        ratio = FloatUtil::clamp(ratio, 0.0f, 1.0f);
        offset.scale(ratio);
        pDst->set(rPointA);
        pDst->add(offset);
        return ratio;
    }

    bool checkHitSegmentSphere(const Vec3f& rSpherePos, const Vec3f& rPointA, const Vec3f& rPointB, f32 radius, Vec3f* pDir) {
        Vec3f pos = rSpherePos;
        Vec3f segment = rPointB;
        pos -= rPointA;
        segment -= rPointA;

        f32 dot = pos.dot(segment);
        f32 radSquared = radius * radius;
        if (dot < 0.0f) {
            if (rPointA.squared(rSpherePos) < radSquared) {
                if (pDir != nullptr) {
                    pDir->set(rSpherePos - rPointA);
                    MR::normalizeOrZero(pDir);
                }

                return true;
            }
        } else if (segment.squared() < dot) {
            if (rPointB.squared(rSpherePos) < radSquared) {
                if (pDir != nullptr) {
                    pDir->set(rSpherePos - rPointB);
                    MR::normalizeOrZero(pDir);
                }

                return true;
            }
        } else {
            Vec3f offset = segment;
            offset.scale(dot / segment.squared());
            offset.sub(pos);
            if (offset.squared() <= radSquared) {
                if (pDir != nullptr) {
                    pDir->set(-offset);
                    MR::normalizeOrZero(pDir);
                }

                return true;
            }
        }

        return false;
    }

    bool checkHitSemilinePlane(Vec3f* pHitPos, const Vec3f& rPos, const Vec3f& rDir, const Vec3f& rPlanePos, const Vec3f& rPlaneNorm) {
        f32 dot = rDir.dot(rPlaneNorm);
        Vec3f offset = rPlanePos - rPos;
        if (0.0f < dot) {
            return false;
        }

        f32 ratio = rPlaneNorm.dot(offset) / dot;
        pHitPos->set(rPos);
        pHitPos->add(rDir * ratio);

        return true;
    }

    bool calcReboundVelocity(Vec3f* pVel, const Vec3f& rNormal, f32 reboundRate) {
        f32 dot = pVel->dot(rNormal);
        if (dot < 0.0f) {
            f32 rate = reboundRate + 1.0f;
            pVel->x -= rate * rNormal.x * dot;
            pVel->y -= rate * rNormal.y * dot;
            pVel->z -= rate * rNormal.z * dot;
            return true;
        }

        return false;
    }

    bool calcReboundVelocity(Vec3f* pVel, const Vec3f& rNormal, f32 reboundRateV, f32 reboundRateH) {
        f32 dot = pVel->dot(rNormal);
        if (dot < 0.0f) {
            pVel->sub(rNormal * dot);
            pVel->scale(reboundRateH);
            pVel->sub(rNormal * dot * reboundRateV);
            return true;
        }

        return false;
    }

    void calcParabolicFunctionParam(f32* pAccel, f32* pVel, f32 max, f32 end) {
        f32 discrim = max * (max - end);

        if (discrim <= 0.0f) {
            *pAccel = -max;
            *pVel = end + max;
            return;
        }

        if (MR::abs(end) < cNum_calcParabolicFunctionParam_f1) {
            *pAccel = max * -cNum_calcParabolicFunctionParam_f2;
            *pVel = max * cNum_calcParabolicFunctionParam_f3;
            return;
        }

        f32 discRoot = NebulaSqrt(discrim);

        f32 t1 = (max + discRoot) / end;
        f32 t2 = (max - discRoot) / end;

        f32 t;
        if (0.0f <= t1 && t1 <= 1.0f) {
            t = t1;
        } else if (0.0f <= t2 && t2 <= 1.0f) {
            t = t2;
        } else {
            t = 1.0f;
        }

        f32 a = -max / (t * t);
        *pAccel = a;
        *pVel = -cNum_calcParabolicFunctionParam_f4 * t * a;
    }

    void makeQuatRotateRadian(Quat4f* pQuat, const Vec3f& rRot) {
        pQuat->setEuler(rRot.x, rRot.y, rRot.z);
    }

    void makeQuatRotateDegree(Quat4f* pQuat, const Vec3f& rRot) {
        pQuat->setEulerDegree(rRot.x, rRot.y, rRot.z);
    }

    void makeQuatFromVec(Quat4f* pQuat, const Vec3f& rFront, const Vec3f& rUp) {
        Pos3f mtx;
        makeMtxFrontUp(&mtx, rFront, rUp);
        mtx.getQuat(*pQuat);
    }

    void makeQuatSideUp(Quat4f* pDst, const Vec3f& rSide, const Vec3f& rUp) {
        Pos3f mtx;
        makeMtxSideUp(&mtx, rSide, rUp);
        mtx.getQuat(*pDst);
    }

    void makeQuatUpFront(Quat4f* pQuat, const Vec3f& rUp, const Vec3f& rFront) {
        Pos3f mtx;
        makeMtxUpFront(&mtx, rUp, rFront);
        mtx.getQuat(*pQuat);
    }

    void makeQuatUpNoSupport(Quat4f* pQuat, const Vec3f& rUp) {
        Pos3f mtx;
        makeMtxUpNoSupport(&mtx, rUp);
        mtx.getQuat(*pQuat);
    }

    void blendQuatUpFront(Quat4f* pDst, const Vec3f& rUp, const Vec3f& rFront, f32 upRate, f32 frontRate) {
        blendQuatUpFront(pDst, *pDst, rUp, rFront, upRate, frontRate);
    }

    void blendQuatUpFront(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rUp, const Vec3f& rFront, f32 upRate, f32 frontRate) {
        Quat4f q;
        q.set(rSrc);
        Vec3f up;
        q.getYDir(up);

        bool isUpSame = up.dot(rUp) >= 0.0f ? false : isSameDirection(up, rUp);
        if (isUpSame) {
            turnRandomVector(&up, up, cNum_blendQuatUpFront_f1);
        }

        Quat4f upRot;
        upRot.setRotate(up, rUp, upRate);
        q.mult(upRot);

        q.getYDir(up);
        Vec3f front;
        q.getZDir(front);

        Vec3f targetFront = rFront.killElement(up);
        MR::normalizeOrZero(&targetFront);

        bool isFrontSame = front.dot(targetFront) >= 0.0f ? false : isSameDirection(front, targetFront);
        if (isFrontSame) {
            turnRandomVector(&front, front, cNum_blendQuatUpFront_f2);
        }

        Quat4f frontRot;
        frontRot.setRotate(front, targetFront, frontRate);
        q.mult(frontRot);

        q.normalize();
        pDst->set(q);
    }

    void blendQuatFrontUp(Quat4f* pDst, const Vec3f& rUp, const Vec3f& rFront, f32 upRate, f32 frontRate) {
        blendQuatFrontUp(pDst, *pDst, rUp, rFront, upRate, frontRate);
    }

    // The front (Z) toward rFront first, then the up (Y) toward rUp across the new front.
    void blendQuatFrontUp(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rUp, const Vec3f& rFront, f32 upRate, f32 frontRate) {
        Quat4f q;
        q.set(rSrc);
        Vec3f front;
        q.getZDir(front);

        bool isFrontSame = front.dot(rFront) >= 0.0f ? false : isSameDirection(front, rFront);
        if (isFrontSame) {
            turnRandomVector(&front, front, cNum_blendQuatFrontUp_f1);
        }

        Quat4f frontRot;
        frontRot.setRotate(front, rFront, frontRate);
        q.mult(frontRot);

        Vec3f up;
        q.getYDir(up);
        q.getZDir(front);

        Vec3f targetUp = rUp.killElement(front);
        MR::normalizeOrZero(&targetUp);

        bool isUpSame = up.dot(targetUp) >= 0.0f ? false : isSameDirection(up, targetUp);
        if (isUpSame) {
            turnRandomVector(&up, up, cNum_blendQuatFrontUp_f2);
        }

        Quat4f upRot;
        upRot.setRotate(up, targetUp, upRate);
        q.mult(upRot);

        q.normalize();
        pDst->set(q);
    }

    void rotateQuatMoment(Quat4f* pQuat, const Vec3f& rMoment) {
        Quat4f rot;
        f32 rate;
        Vec3f axis;
        separateScalarAndDirection(&rate, &axis, rMoment);
        rot.setRotate(axis, rate);
        pQuat->mult(rot);
        pQuat->normalize();
    }

    void rotateQuatRollBall(Quat4f* pQuat, const Vec3f& rVel, const Vec3f& rNorm, f32 radius) {
        Vec3f moment;
        calcMomentRollBall(&moment, rVel, rNorm, radius);
        rotateQuatMoment(pQuat, moment);
    }

    bool turnQuat(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rFrom, const Vec3f& rTo, f32 angle) {
        Vec3f from;
        Vec3f target = rTo;

        bool isSameDir = rFrom.dot(rTo) >= 0.0f ? false : isSameDirection(rFrom, rTo);
        if (isSameDir) {
            turnRandomVector(&from, rFrom, cNum_turnQuat_f1);
        } else {
            from.set(rFrom);
        }

        MR::normalizeOrZero(&from);
        MR::normalizeOrZero(&target);

        f32 angleBetween = acosEx(FloatUtil::clamp(from.dot(target), -1.0f, 1.0f));
        f32 rate;
        if (angleBetween <= angle) {
            rate = 1.0f;
        } else {
            rate = FloatUtil::clamp(angle / angleBetween, 0.0f, 1.0f);
        }

        Quat4f rot;
        rot.setRotate(from, target, rate);
        pDst->mult(rot, rSrc);
        pDst->normalize();

        return angleBetween < cNum_turnQuat_f2;
    }

    bool turnQuatXDirRad(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rTo, f32 angle) {
        Vec3f xDir;
        rSrc.getXDir(xDir);
        return turnQuat(pDst, rSrc, xDir, rTo, angle);
    }

    void turnQuatXDirRate(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rTo, f32 rate) {
        Vec3f xDir;
        rSrc.getXDir(xDir);
        Quat4f rot;
        rot.setRotate(xDir, rTo, rate);
        pDst->mult(rot, rSrc);
    }

    bool turnQuatYDirRad(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rTo, f32 angle) {
        Vec3f yDir;
        rSrc.getYDir(yDir);
        return turnQuat(pDst, rSrc, yDir, rTo, angle);
    }

    void turnQuatYDirRate(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rTo, f32 rate) {
        Vec3f yDir;
        rSrc.getYDir(yDir);
        Quat4f rot;
        rot.setRotate(yDir, rTo, rate);
        pDst->mult(rot, rSrc);
    }

    bool turnQuatZDirRad(Quat4f* pDst, const Quat4f& rSrc, const Vec3f& rTo, f32 angle) {
        Vec3f zDir;
        rSrc.getZDir(zDir);
        return turnQuat(pDst, rSrc, zDir, rTo, angle);
    }

    f32 sign(f32 x) {
        if (0.0f < x) {
            return 1.0f;
        }

        if (x < 0.0f) {
            return -1.0f;
        }

        return 0.0f;
    }

    void clampVecAngleDeg(Vec3f* pDst, const Vec3f& rBase, f32 angle) {
        if (pDst->angle(rBase) * _180_PI <= angle) {
            return;
        }

        Vec3f axis = rBase.cross(*pDst);
        if (MR::normalizeOrZero(&axis)) {
            return;
        }


        rotateVecDegree(pDst, rBase, axis, angle);
    }

    void clampLength(Vec3f* pDst, const Vec3f& rSrc, f32 length) {
        if (rSrc.squared() > length * length) {
            pDst->setLength(rSrc, length);
        } else {
            pDst->set(rSrc);
        }
    }

    f32 convergeRadian(f32 angle, f32 target, f32 speed) {
        f32 convergeTarget = target;
        if ((target + pi2()) - angle < pi()) {
            convergeTarget = target + pi2();
        } else if (angle - (target - pi2()) < pi()) {
            convergeTarget = target - pi2();
        }

        if (angle < convergeTarget) {
            angle += speed;
            if (angle > convergeTarget) {
                angle = convergeTarget;
            }
        } else {
            angle -= speed;
            if (angle < convergeTarget) {
                angle = convergeTarget;
            }
        }

        return repeat(angle, 0.0f, pi2());
    }

    bool isInRange(f32 x, f32 min, f32 max) {
        if (min > max) {
            if (x < max) {
                return false;
            }

            if (x > min) {
                return false;
            }

            return true;
        } else {
            if (x < min) {
                return false;
            }

            if (x > max) {
                return false;
            }

            return true;
        }
    }

    f32 calcRotateY(f32 x, f32 z) {
        return MR::repeatDegree(MR::toDegree(MR::atan2(-z, x)) + cNum_calcRotateY_f1);
    }

    f32 calcRotateZ(const Vec3f& rVecA, const Vec3f& rVecB) {
        Vec2f vec(rVecB.x, rVecB.y);
        vec.x -= rVecA.x;
        vec.y -= rVecA.y;
        return MR::atan2(vec.y, vec.x) * _180_PI;
    }

    f32 calcDistanceXY(const Vec3f& rPos1, const Vec3f& rPos2) {
        f32 xDelta = rPos1.x - rPos2.x;
        f32 yDelta = rPos1.y - rPos2.y;
        return FloatUtil::sqrt(xDelta * xDelta + yDelta * yDelta);
    }

    void rotateVecDegree(f32* pX, f32* pY, f32 degree) {
        f32 x = *pX;
        f32 y = *pY;
        f32 Sin = Angle::sinDegree(degree);
        f32 Cos = Angle::cosDegree(degree);

        *pX = (Cos * x) - (Sin * y);
        *pY = (Sin * x) + (Cos * y);
    }

    void rotateVecDegree(Vec2f* pDst, f32 degree) {
        rotateVecDegree(&pDst->x, &pDst->y, degree);
    }

    void rotateVecDegree(Vec3f* pDst, const Vec3f& rAxis, f32 degree) {
        rotateVecDegree(pDst, *pDst, rAxis, degree);
    }

    void rotateVecDegree(Vec3f* pDst, const Vec3f& rSrc, const Vec3f& rAxis, f32 degree) {
        Rot3f rotation;
        rotation.identity();
        rotation.setRotate(rAxis, degree * PI_180);
        rotation.mult(rSrc, *pDst);
    }

    void rotateVecRadian(Vec3f* pDst, const Vec3f& rAxis, f32 angle) {
        rotateVecRadian(pDst, *pDst, rAxis, angle);
    }

    void rotateVecRadian(Vec3f* pDst, const Vec3f& rSrc, const Vec3f& rAxis, f32 angle) {
        Rot3f rotation;
        rotation.identity();
        rotation.setRotate(rAxis, angle);
        rotation.mult(rSrc, *pDst);
    }

    void calcLocalVec(Vec3f* pVec, MtxPtr pMtx) {
        Pos3f inv = pMtx;
        inv.invert(inv);
        inv.mult(*pVec, *pVec);
    }

    void normalize(Vec2f* pVec) {
        Vec3f vec(pVec->x, pVec->y, 0.0f);
        normalize(&vec);
        pVec->x = vec.x;
        pVec->y = vec.y;
    }

    void normalize(Vec3f* pVec) {
        NebulaVecNormalize(pVec, pVec);
    }

    void normalize(Pos3f* pMtx) {
        Vec3f x, y, z;
        pMtx->getXYZDir(x, y, z);
        normalize(&x);
        normalize(&y);
        normalize(&z);
        pMtx->setXYZDir(x, y, z);
    }

    void normalize(const Vec2f& rSrc, Vec2f* pDst) {
        *pDst = rSrc;
        normalize(pDst);
    }

    void normalize(const Vec3f& rSrc, Vec3f* pDst) {
        pDst->set(rSrc);
        normalize(pDst);
    }

    bool normalizeOrZero(Vec2f* pVec) {
        if (isNearZero(*pVec)) {
            pVec->zero();
            return true;
        }

        normalize(pVec);
        return false;
    }

    bool normalizeOrZero(Vec3f* pVec) {
        if (isNearZero(*pVec)) {
            pVec->zero();
            return true;
        }

        normalize(pVec);
        return false;
    }

    bool normalizeOrZero(const Vec3f& rSrc, Vec3f* pDst) {
        pDst->set(rSrc);

        return normalizeOrZero(pDst);
    }

    void separateScalarAndDirection(f32* pScalar, Vec2f* pDir, const Vec2f& rVec) {
        *pScalar = rVec.length();

        if (isNearZero(rVec)) {
            pDir->zero();
        } else {
            normalize(rVec, pDir);
        }
    }

    void separateScalarAndDirection(f32* pScalar, Vec3f* pDir, const Vec3f& rVec) {
        *pScalar = rVec.length();

        if (isNearZero(rVec)) {
            pDir->zero();
        } else {
            normalize(rVec, pDir);
        }
    }

    f32 normalize(f32 x, f32 min, f32 max) {
        f32 range = max - min;

        if (!isNearZero(range)) {
        } else if (x < min) {
            return 0.0f;
        } else {
            return 1.0f;
        }

        return (FloatUtil::clamp(x, min, max) - min) / range;
    }

    f32 normalizeAbs(f32 x, f32 min, f32 max) {
        if (x >= 0.0f) {
            return normalize(x, min, max);
        } else {
            return -normalize(-x, min, max);
        }
    }

    bool turnVecToVecCos(Vec3f* pDst, const Vec3f& rFrom, const Vec3f& rTo, f32 cosAngle, const Vec3f& rAxis, f32 fallbackLength) {
        if (isNearZero(rFrom)) {
            return false;
        }

        if (isNearZero(rTo)) {
            return false;
        }

        if (rFrom.dot(rTo) > cosAngle) {
            pDst->set(rTo);
            normalize(pDst);
            return true;
        }

        f32 sinAngle = NebulaSqrt(1.0f - cosAngle * cosAngle);
        Vec3f rejectFrom = rTo.killElement(rFrom);

        if (isNearZero(rejectFrom)) {
            Vec3f fallback = rFrom.cross(rAxis);
            normalize(&fallback);
            pDst->set(rFrom + fallback * fallbackLength);
            normalize(pDst);
            return false;
        }

        normalize(&rejectFrom);
        pDst->set(rFrom * cosAngle + rejectFrom * sinAngle);
        normalize(pDst);
        return false;
    }

    bool turnVecToVecCosOnPlane(Vec3f* pDst, const Vec3f& rFrom, const Vec3f& rTo, const Vec3f& rAxis, f32 cosAngle) {
        Vec3f planarTo = rTo.killElement(rAxis);
        normalizeOrZero(&planarTo);
        if (!MR::isNearZero(planarTo)) {
            if (cosAngle > -1.0f) {
                return turnVecToVecCos(pDst, rFrom, planarTo, cosAngle, rAxis);
            }

            pDst->set(planarTo);
            return true;
        }

        return false;
    }

    bool turnVecToVecCosOnPlane(Vec3f* pVec, const Vec3f& rTo, const Vec3f& rAxis, f32 cosAngle) {
        return turnVecToVecCosOnPlane(pVec, *pVec, rTo, rAxis, cosAngle);
    }

    f32 turnVecToVecRadian(Vec3f* pDst, const Vec3f& rFrom, const Vec3f& rTo, f32 angle, const Vec3f& rAxis) {
        const f32 rawAngle = Angle::acosRadian(FloatUtil::clamp(rFrom.dot(rTo), -1.0f, 1.0f));
        f32 angleBetween = rawAngle;
        f32 angleMoved = 0.0f;
        f32 angleClamped = angleBetween;
        if (angle <= angleBetween) {
            angleMoved = angleBetween - angle;
            angleClamped = angle;
        }

        if (isNearZero(angleBetween)) {
            return 0.0f;
        }

        Quat4f rot;
        bool isSameDir = rFrom.dot(rTo) >= 0.0f ? false : isSameDirection(rFrom, rTo);
        if (isSameDir) {
            rot.setRotate(rAxis, angleClamped);
        } else {
            rot.setRotate(rFrom, rTo, angleClamped / rawAngle);
        }

        Vec3f v = rFrom;
        rot.transform(v);
        normalizeOrZero(&v);
        if (!isNearZero(v)) {
            pDst->set(v);
        }

        return angleMoved;
    }

    f32 turnVecToVecDegree(Vec3f* pDst, const Vec3f& rFrom, const Vec3f& rTo, f32 angle, const Vec3f& rAxis) {
        return turnVecToVecRadian(pDst, rFrom, rTo, angle * (PI / cNum_turnVecToVecDegree_f1), rAxis) * _180_PI;
    }

    void calcMomentRollBall(Vec3f* pMoment, const Vec3f& rVel, const Vec3f& rUp, f32 radius) {
        Vec3f up;
        up.set(rUp);
        if (normalizeOrZero(&up)) {
            pMoment->zero();
            return;
        }

        Vec3f moment = up.cross(rVel);
        moment /= radius;
        pMoment->set(moment);
    }

    bool calcReflectionVector(Vec3f* pDir, const Vec3f& rNorm, f32 rate, f32 cosAngleMin) {
        f32 dot = rNorm.dot(*pDir);
        if (dot < -cosAngleMin) {
            pDir->sub(rNorm * dot * (rate + 1.0f));
            return true;
        }

        if (dot < 0.0f) {
            pDir->sub(rNorm * dot);
            return false;
        }

        return false;
    }

    bool isSameDirection(const Vec3f& rVec1, const Vec3f& rVec2, f32 tolerance) {
        if (MR::abs(rVec1.y * rVec2.z - rVec1.z * rVec2.y) > tolerance) {
            return false;
        }

        if (MR::abs(rVec1.z * rVec2.x - rVec1.x * rVec2.z) > tolerance) {
            return false;
        }

        if (MR::abs(rVec1.x * rVec2.y - rVec1.y * rVec2.x) > tolerance) {
            return false;
        }

        return true;
    }

    bool isOppositeDirection(const Vec3f& rVec1, const Vec3f& rVec2, f32 tolerance) {
        if (rVec1.dot(rVec2) >= 0.0f) {
            return false;
        }

        return isSameDirection(rVec1, rVec2, tolerance);
    }

    bool isNearZero(f32 x, f32 tolerance) {
        if (x < 0.0f) {
            x = -x;
        }

        if (x < tolerance) {
            return true;
        }

        return false;
    }

    bool isNearZero(const Vec3f& rVec, f32 tolerance) {
        if (rVec.x > tolerance) {
            return false;
        }

        if (rVec.x < -tolerance) {
            return false;
        }

        if (rVec.y > tolerance) {
            return false;
        }

        if (rVec.y < -tolerance) {
            return false;
        }

        if (rVec.z > tolerance) {
            return false;
        }

        if (rVec.z < -tolerance) {
            return false;
        }

        return true;
    }

    bool isNearZero(const Vec2f& rVec, f32 tolerance) {
        if (rVec.x > tolerance) {
            return false;
        }

        if (rVec.x < -tolerance) {
            return false;
        }

        if (rVec.y > tolerance) {
            return false;
        }

        if (rVec.y < -tolerance) {
            return false;
        }

        return true;
    }

    f32 diffAngleAbs(f32 angleA, f32 angleB) {
        f32 normalize = normalizeAngleAbs(angleA - angleB);

        if (normalize > PI) {
            normalize = (TWO_PI - normalize);
        }

        return normalize;
    }

    f32 normalizeAngleAbs(f32 angle) {
        // One turn at a time, as the game always did; an angle many turns out (or infinite) is folded by fmod in one step
        // instead of looping once per turn.
        for (s32 turns = 0; turns < 16; turns++) {
            if (angle < 0.0f) {
                angle += TWO_PI;
            } else if (angle > TWO_PI) {
                angle -= TWO_PI;
            } else {
                return angle;
            }
        }

        angle = static_cast< f32 >(fmod(angle, TWO_PI));
        return angle < 0.0f ? angle + TWO_PI : angle;
    }

    bool isAngleBetween(f32 angle, f32 min, f32 max) {
        f32 normAngle = normalizeAngleAbs(angle);
        f32 upper = normalizeAngleAbs(min);
        f32 lower = normalizeAngleAbs(max);

        if (lower > upper) {
            f32 swapped = lower;
            lower = upper;
            upper = swapped;
        }

        // Between them along the shorter way round.
        bool isBetween = normAngle >= lower && normAngle <= upper;

        if (upper - lower > PI) {
            isBetween = !isBetween;
        }

        return isBetween;
    }

    f32 blendAngle(f32 angleA, f32 angleB, f32 rate) {
        angleA = normalizeAngleAbs(angleA);
        angleB = normalizeAngleAbs(angleB);

        if (!isAngleBetween((angleA + angleB) / cNum_blendAngle_f1, angleA, angleB)) {
            if (angleA < angleB) {
                angleA += TWO_PI;
            } else {
                angleB += TWO_PI;
            }
        }

        return normalizeAngleAbs((1.0f - rate) * angleA + rate * angleB);
    }

    u8 lerp(u8 start, u8 end, f32 t) {
        return (s32)(FloatUtil::clamp(start + (end - start) * t, 0.0f, cNum_lerp_f1));
    }

    NebulaColor8 lerp(NebulaColor8 start, NebulaColor8 end, f32 t) {
        u8 a = lerp(start.a, end.a, t);
        u8 b = lerp(start.b, end.b, t);
        u8 g = lerp(start.g, end.b, t);
        u8 r = lerp(start.r, end.r, t);

        NebulaColor8 color = {r, g, b, a};

        return color;
    }

    void sortSmall(s32 length, f32* pSortArray, s32* pIndexArray) {
        for (int i = 0; i < length; i++) {
            pIndexArray[i] = i;
        }

        for (int index = 0; index < length; index++) {
            f32 element = pSortArray[index];
            int indexOfSmallestElement = index;
            for (int i = index + 1; i < length; i++) {
                if (element > pSortArray[i]) {
                    element = pSortArray[i];
                    indexOfSmallestElement = i;
                }
            }

            s32 swappedIndex = pIndexArray[index];
            f32 swappedValue = pSortArray[index];
            pIndexArray[index] = pIndexArray[indexOfSmallestElement];
            pSortArray[index] = element;
            pIndexArray[indexOfSmallestElement] = swappedIndex;
            pSortArray[indexOfSmallestElement] = swappedValue;
        }
    };
};  // namespace MR

f32 VecKillElement(const Vec* pSrc, const Vec* pKill, const Vec* pDst) {
    const f32 sx = pSrc->x, sy = pSrc->y, sz = pSrc->z;
    const f32 kx = pKill->x, ky = pKill->y, kz = pKill->z;
    const f32 dot = (sx * kx + sy * ky) + sz * kz;
    // Stores through pDst, which the signature declares const.
    Vec* dst = const_cast< Vec* >(pDst);
    dst->x = sx - dot * kx;
    dst->y = sy - dot * ky;
    dst->z = sz - dot * kz;
    return dot;
}

namespace MR {
    f32 vecKillElement(const Vec3f& rSrc, const Vec3f& rKillDir, Vec3f* pDst) {
        if (isNearZero(rKillDir)) {
            *pDst = rSrc;

            return 0.0f;
        }

        return VecKillElement(rSrc, rKillDir, pDst);
    }

    void vecScaleAdd(Vec3f* pDst, const Vec3f* pAdd, f32 scale) {
        pDst->x = pAdd->x * scale + pDst->x;
        pDst->y = pAdd->y * scale + pDst->y;
        pDst->z = pAdd->z * scale + pDst->z;
    }

    // pDst may be rFrom or rTo: every component is read before any is written.
    void vecBlend(const Vec3f& rFrom, const Vec3f& rTo, Vec3f* pDst, f32 rate) {
        const f32 invRate = 1.0f - rate;
        const f32 x = rTo.x * rate + rFrom.x * invRate;
        const f32 y = rTo.y * rate + rFrom.y * invRate;
        const f32 z = rTo.z * rate + rFrom.z * invRate;
        pDst->x = x;
        pDst->y = y;
        pDst->z = z;
    }

    void vecBlendNormal(const Vec3f& rFrom, const Vec3f& rTo, Vec3f* pVec, f32 rate) {
        Vec3f vA;
        if (isNearZero(rFrom)) {
            vA.zero();
        } else {
            vA.set(rFrom);
            NebulaVecNormalize(vA, vA);
            NebulaVecScale(vA, vA, 1.0f - rate);
        }

        Vec3f vB;
        if (isNearZero(rTo)) {
            vB.zero();
        } else {
            vB.set(rTo);
            NebulaVecNormalize(vB, vB);
            NebulaVecScale(vB, vB, rate);
        }

        NebulaVecAdd(vA, vB, pVec);
    }

    bool vecBlendSphere(const Vec3f& rFrom, const Vec3f& rTo, Vec3f* pVec, f32 rate) {
        f32 lenFrom = rFrom.length();
        f32 lenTo = rTo.length();
        f32 diffAngle;
        if (lenFrom == 0.0f || lenTo == 0.0f) {
            diffAngle = 0.0f;
        } else {
            diffAngle = diffAngleAbs(rFrom, rTo);
        }

        if (diffAngle < cNum_vecBlendSphere_f1) {
            vecBlendNormal(rFrom, rTo, pVec, rate);
            pVec->setLength(lenFrom * (1.0f - rate) + lenTo * rate);
            return true;
        }

        if (diffAngle == 0.0f) {
            *pVec = rFrom;
            pVec->setLength(lenFrom * (1.0f - rate) + lenTo * rate);
            return false;
        }

        if (diffAngle == pi()) {
            return false;
        }

        *pVec = (rFrom * Angle::sinRadian(diffAngle * (1.0f - rate)) + rTo * Angle::sinRadian(diffAngle * rate)) / Angle::sinRadian(diffAngle);
        pVec->setLength(lenFrom * (1.0f - rate) + lenTo * rate);

        return true;
    }

    void vecRotAxis(const Vec3f& rFrom, const Vec3f& rTo, const Vec3f& rAxis, Vec3f* pVec, f32 angle) {
        f32 lenFrom = rFrom.length();
        f32 lenTo = rTo.length();
        f32 diffAngle;
        if (lenFrom == 0.0f || lenTo == 0.0f) {
            diffAngle = 0.0f;
        } else {
            diffAngle = diffAngleAbs(rFrom, rTo);
        }

        if (diffAngle == 0.0f) {
            *pVec = rTo;
            return;
        }

        if (diffAngle > angle) {
            if (diffAngle < pi() && rFrom.cross(rTo).dot(rAxis) < 0.0f) {
                angle = -angle;
            }

            Pos3f rot;
            NebulaMtxRotAxisRad(rot, rAxis, angle);
            NebulaMtxMultVec(rot, rFrom, pVec);
            return;
        }

        *pVec = rTo;
        return;
    }

    void blendColor(NebulaColor8* pDst, const NebulaColor8& rStart, const NebulaColor8& rEnd, f32 t) {
        pDst->r = (s32)(getInterpolateValue(t, rStart.r, rEnd.r));
        pDst->g = (s32)(getInterpolateValue(t, rStart.g, rEnd.g));
        pDst->b = (s32)(getInterpolateValue(t, rStart.b, rEnd.b));
        pDst->a = (s32)(getInterpolateValue(t, rStart.a, rEnd.a));
    }

    void blendVec(Vec* pDst, const Vec& rStart, const Vec& rEnd, f32 t) {
        pDst->x = getInterpolateValue(t, rStart.x, rEnd.x);
        pDst->y = getInterpolateValue(t, rStart.y, rEnd.y);
        pDst->z = getInterpolateValue(t, rStart.z, rEnd.z);
    }

    f32 turnVecToPlane(Vec3f* pDst, const Vec3f& rSrc, const Vec3f& rNorm) {
        Vec3f vec = rSrc;
        f32 length = rSrc.length();
        f32 proj = rNorm.dot(rSrc);
        vec.sub(rNorm * proj);
        vec.setLength(length);
        pDst->set(vec);
        return length;
    }

    u32 getMinAbsElementIndex(const Vec3f& rVec) {
        f64 abs_x = abs(rVec.x);
        f64 abs_y = abs(rVec.y);
        f64 abs_z = abs(rVec.z);

        if (abs_x < abs_y && abs_x < abs_z) {
            return 0;
        }

        if (abs_y < abs_z) {
            return 1;
        }

        return 2;
    }

    f32 getMaxElement(const Vec3f& rVec) {
        const u32 index = getMaxElementIndex(rVec);
        return index == 0 ? rVec.x : index == 1 ? rVec.y : rVec.z;
    }

    f32 getMaxAbsElement(const Vec3f& rVec) {
        const u32 index = getMaxAbsElementIndex(rVec);
        return index == 0 ? rVec.x : index == 1 ? rVec.y : rVec.z;
    }

    u32 getMaxElementIndex(const Vec3f& rVec) {
        if (rVec.x > rVec.y && rVec.x > rVec.z) {
            return 0;
        }

        if (rVec.y > rVec.z) {
            return 1;
        }

        return 2;
    }

    u32 getMaxAbsElementIndex(const Vec3f& rVec) {
        f64 abs_x = abs(rVec.x);
        f64 abs_y = abs(rVec.y);
        f64 abs_z = abs(rVec.z);

        if (abs_x > abs_y && abs_x > abs_z) {
            return 0;
        }

        if (abs_y > abs_z) {
            return 1;
        }

        return 2;
    }

    f32 diffAngleAbsFast(const Vec3f& rA, const Vec3f& rB) {
        return MR::acos(rA.dot(rB));
    }

    f32 diffAngleAbs(const Vec3f& rA, const Vec3f& rB) {
        f32 length1 = rA.length();
        f32 length2 = rB.length();
        f32 x = rA.dot(rB) / (length1 * length2);

        if (x >= 1.0f) {
            return 0.0f;
        } else if (x <= -1.0f) {
            return PI;
        } else {
            return acosEx(x);
        }
    }

    f32 diffAngleAbs(const Vec2f& rA, const Vec2f& rB) {
        f32 length1 = rA.length();
        f32 length2 = rB.length();
        f32 x = rA.dot(rB) / (length1 * length2);

        if (x >= 1.0f) {
            return 0.0f;
        } else if (x <= -1.0f) {
            return PI;
        } else {
            return acosEx(x);
        }
    }

    f32 diffAngleAbsHorizontal(const Vec3f& rA, const Vec3f& rB, const Vec3f& rAxis) {
        Vec3f horizonA, horizonB;

        vecKillElement(rA, rAxis, &horizonA);
        vecKillElement(rB, rAxis, &horizonB);

        return diffAngleAbs(horizonA, horizonB);
    }

    f32 diffAngleSignedHorizontal(const Vec3f& rA, const Vec3f& rB, const Vec3f& rAxis) {
        Vec3f horizonA, horizonB;
        vecKillElement(rA, rAxis, &horizonA);
        vecKillElement(rB, rAxis, &horizonB);
        f32 angleAbs = diffAngleAbs(horizonA, horizonB);
        Pos3f mtx;
        NebulaMtxRotAxisRad(mtx, rAxis, cNum_diffAngleSignedHorizontal_f1);
        f32 angle1 = horizonA.dot(horizonB);
        NebulaMtxMultVec(mtx, horizonA, horizonA);
        f32 angle2 = horizonA.dot(horizonB);

        if (angle2 > angle1) {
            return angleAbs;
        } else {
            return -angleAbs;
        }
    }

    bool isNearAngleRadian(const Vec3f& rA, const Vec3f& rB, f32 angle) {
        if (isNearZero(rA) || isNearZero(rB)) {
            return false;
        }

        Vec3f normA;
        normalize(rA, &normA);

        Vec3f normB;
        normalize(rB, &normB);

        return normA.dot(normB) >= Angle::cosRadian(angle);
    }

    bool isNearAngleDegree(const Vec3f& rA, const Vec3f& rB, f32 angle) {
        return isNearAngleRadian(rA, rB, angle * (PI / cNum_isNearAngleDegree_f1));
    }

    bool isNearAngleRadianHV(const Vec3f& rA, const Vec3f& rB, const Vec3f& rAxis, f32 angleH, f32 angleV) {
        if (angleV > HALF_PI_D) {
            angleV = HALF_PI;
        }

        Vec3f unitA;
        if (normalizeOrZero(rA, &unitA)) {
            return false;
        }

        Vec3f unitB;
        if (normalizeOrZero(rB, &unitB)) {
            return false;
        }

        Vec3f unitAxis;
        if (normalizeOrZero(rAxis, &unitAxis)) {
            return false;
        }

        Vec3f horizonA;
        horizonA.killElement(rA, rAxis);
        normalizeOrZero(&horizonA);

        if (horizonA.dot(unitB) < Angle::cosRadian(angleH)) {
            return false;
        }

        if (MR::abs(unitA.dot(unitAxis)) > MR::abs(Angle::sinRadian(angleV))) {
            return false;
        }

        return true;
    }

    bool isNearAngleDegreeHV(const Vec3f& rA, const Vec3f& rB, const Vec3f& rAxis, f32 angleH, f32 angleV) {
        angleV = PI_180 * angleV;
        angleH = PI_180 * angleH;

        return isNearAngleRadianHV(rA, rB, rAxis, angleH, angleV);
    }

    void createBoundingBox(const Vec3f* pPoints, u32 numPoints, Vec3f* pMin, Vec3f* pMax) {
        *pMin = pPoints[0];
        *pMax = pPoints[0];

        for (u32 idx = 1; idx < numPoints; idx++) {
            const Vec3f& point = pPoints[idx];

            if (point.x < pMin->x) {
                pMin->x = point.x;
            }

            if (point.y < pMin->y) {
                pMin->y = point.y;
            }

            if (point.z < pMin->z) {
                pMin->z = point.z;
            }

            if (pMax->x < point.x) {
                pMax->x = point.x;
            }

            if (pMax->y < point.y) {
                pMax->y = point.y;
            }

            if (pMax->z < point.z) {
                pMax->z = point.z;
            }
        }
    }

    bool isNormalize(const Vec3f& rVec, f32 tolerance) {
        return MR::abs(1.0f - rVec.length()) <= tolerance;
    }

    // All bits set in each component: a quiet NaN.
    void setNan(Vec3f& rDst) {
        const f32 nan = __builtin_bit_cast(f32, 0xFFFFFFFFu);
        rDst.x = nan;
        rDst.y = nan;
        rDst.z = nan;
    }

    bool isNan(const Vec3f& rVec) {
        // NaN in any component (MSL's __fpclassifyf answered 1 for NaN alone).
        if (__builtin_isnan(rVec.x) || __builtin_isnan(rVec.y) || __builtin_isnan(rVec.z)) {
            return true;
        }

        return false;
    }

    f32 getFootPoint(const Vec3f& rPointA, const Vec3f& rPointB, const Vec3f& rPos, Vec3f* pDst) {
        Vec3f segment = rPointB - rPointA;
        f32 length = segment.length();
        f32 proj = -(segment.x * (rPointA.x - rPos.x) + segment.y * (rPointA.y - rPos.y) + segment.z * (rPointA.z - rPos.z)) / (length * length);
        *pDst = rPointA + segment * proj;
        return proj;
    }

    f32 mod(f32 x, f32 y) {
        return fmod(x, y);
    }

    void getRotatedAxisY(Vec3f* pDst, const Vec3f& rPSrc) {
        f32 cosX = Angle::cosDegree(rPSrc.x);
        f32 cosY = Angle::cosDegree(rPSrc.y);
        f32 cosZ = Angle::cosDegree(rPSrc.z);
        f32 sinX = Angle::sinDegree(rPSrc.x);
        f32 sinY = Angle::sinDegree(rPSrc.y);
        f32 sinZ = Angle::sinDegree(rPSrc.z);

        pDst->set((sinX * (cosZ * sinY)) - (sinZ * cosX), (sinX * (sinZ * sinY)) + (cosZ * cosX), cosY * sinX);
    }

    void getRotatedAxisZ(Vec3f* pDst, const Vec3f& rPSrc) {
        Vec3f vec(0.0f, 0.0f, 1.0f);
        Pos3f mtx;
        MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rPSrc.x, rPSrc.y, rPSrc.z);
        NebulaMtxMultVec(mtx, &vec, pDst);
    }
};  // namespace MR
