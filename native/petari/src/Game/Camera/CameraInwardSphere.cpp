#include "Game/Camera/CameraInwardSphere.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f4, "calc.f4", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f5, "calc.f5", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f6, "calc.f6", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f7, "calc.f7", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f8, "calc.f8", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f9, "calc.f9", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f10, "calc.f10", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f11, "calc.f11", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f12, "calc.f12", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcGravityCenter_f1, "calcGravityCenter.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcGravityCenter_f2, "calcGravityCenter.f2", 0.001f);

namespace {
    NEBULA_TUNABLE(f32, sBaseDist, 300.0f);
};  // namespace

CameraInwardSphere::CameraInwardSphere(const char* pName)
    : Camera(pName), mDistMax(1500.0f), mDistMin(500.0f), mDistBase(::sBaseDist), mVelocity(0.0f, 0.0f, 0.0f), mPos(0.0f, 0.0f, 0.0f) {
}

void CameraInwardSphere::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    calcGravityCenter();
    f32 dist = CameraLocalUtil::getTarget(this)->getPosition().distance(mGravityCenter);
    Vec3f offset = CameraLocalUtil::getPos(this) - mGravityCenter;
    offset.setLength(dist - mDistBase);
    CameraLocalUtil::setPos(this, mGravityCenter + offset);
    mVelocity.zero();
    mPos.set(CameraLocalUtil::getPos(this));
}

CameraTargetObj* CameraInwardSphere::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);
    CameraLocalUtil::setWatchPos(this, watchPoint);

    Vec3f camOffset = mPos - CameraLocalUtil::getWatchPos(this);
    f32 camDist = camOffset.length();
    f32 dist;
    if (camDist < 1.0f) {
        camOffset.set(-CameraLocalUtil::getTarget(this)->getFrontVec());
        dist = mDistMin;
    } else {
        MR::normalize(&camOffset);
        dist = camDist;
        if (dist < mDistMin) {
            dist = mDistMin;
        } else if (dist > mDistMax) {
            dist = mDistMax;
        }
    }
    camOffset.scale(dist);

    Vec3f camOffsetPos = camOffset + CameraLocalUtil::getWatchPos(this);

    Vec3f grav;
    MR::calcGravityVector(this, camOffsetPos, &grav, nullptr, 0);

    Triangle tri;
    TriangleFilterFunc filterFunc(&MR::isCameraCodeThrough);

    Vec3f newPos;
    Vec3f mapPos;
    if (MR::getFirstPolyOnLineToMap(&mapPos, &tri, camOffsetPos - grav * cNum_calc_f3, grav * cNum_calc_f4, nullptr, &filterFunc)) {
        newPos = mapPos - grav * mDistBase;
    } else {
        newPos = camOffsetPos;
    }

    mVelocity.add(grav * cNum_calc_f5);
    if (mVelocity.length() > cNum_calc_f6) {
        mVelocity.setLength(cNum_calc_f7);
    }
    newPos.add(mVelocity);

    f32 maxDist = 0.0f;
    s32 collisions = Collision::checkStrikeBallToMap(newPos, mDistBase, nullptr, nullptr);
    for (s32 idx = 0; idx < collisions; idx++) {
        const HitInfo* info = Collision::getStrikeInfoMap(idx);
        f32 dot = grav.dot(*info->mParentTriangle.getNormal(0) * info->mDepth);
        if (maxDist < -dot) {
            maxDist = -dot;
        }
    }
    if (maxDist > 0.0f) {
        mVelocity.zero();
    }
    newPos.add(-grav * maxDist);

    mPos.set(newPos);

    Vec3f upPos = newPos - grav * cNum_calc_f8;
    f32 oldDist = (CameraLocalUtil::getPos(this) - upPos).length();
    f32 newDist = (newPos - upPos).length();

    newDist = oldDist * cNum_calc_f9 + newDist * cNum_calc_f10;
    CameraLocalUtil::setPos(this, upPos + grav * newDist);

    Vec3f up = CameraLocalUtil::getUpVec(this) * cNum_calc_f11 - grav * cNum_calc_f12;
    MR::normalize(&up);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    return CameraLocalUtil::getTarget(this);
}

void CameraInwardSphere::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAngleA, general->mDist, general->mAngleB);
}

void CameraInwardSphere::calcGravityCenter() {
    // Sample two (close) points along the gravity field, and the intersections
    // of the two gravity vectors will produce the center of gravity
    Vec3f up1;
    Vec3f pos1 = CameraLocalUtil::getTarget(this)->getPosition();
    MR::calcGravityVector(this, pos1, &up1, nullptr, 0);
    up1.negate();

    Vec3f pos2 = CameraLocalUtil::getTarget(this)->getFrontVec();
    pos2.orthogonalize(up1);

    if (MR::isNearZero(pos2)) {
        pos2 = CameraLocalUtil::getTarget(this)->getSideVec();
        pos2.orthogonalize(up1);
    }
    pos2.setLength(cNum_calcGravityCenter_f1);
    pos2.add(CameraLocalUtil::getTarget(this)->getPosition());

    Vec3f up2;
    MR::calcGravityVector(this, pos2, &up2, nullptr, 0);
    up2.negate();

    f32 sq1 = up1.squared();
    f32 sq2 = up2.squared();
    f32 dot = up1.dot(up2);
    f32 dotSq = dot * dot;
    Vec3f front1 = pos1 - pos2;
    Vec3f front2 = pos2 - pos1;
    f32 proj1 = front1.dot(up2);
    f32 proj2 = front2.dot(up1);
    f32 cosDiff = sq1 * sq2 - dotSq;
    if (cosDiff < cNum_calcGravityCenter_f2) {
        mGravityCenter = CameraLocalUtil::getTarget(this)->getPosition();
    } else {
        f32 dist = 1.0f / cosDiff * (sq2 * proj2 + dot * proj1);
        mGravityCenter = pos1 + up1 * dist;
    }
}
