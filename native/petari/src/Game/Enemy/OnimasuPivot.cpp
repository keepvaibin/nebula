#include "Game/Enemy/OnimasuPivot.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_startMoveInner_f1, "startMoveInner.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoseInner_f1, "updatePoseInner.f1", 565.6854f);

OnimasuPivot::OnimasuPivot(const char* pName) : Onimasu(pName), mCurNormal(), mNormals() {
    lumacore::setClass(this, lumacore::kClassId< OnimasuPivot >);
    mLastPointQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
    mNextPointQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void OnimasuPivot::initFromRailPoint() {
    mNormals.init(MR::getRailPointNum(this) / 2);
}

void OnimasuPivot::startMoveInner() {
    Vec3f pivot = getPivotPointPos();
    Vec3f toLastPoint = getLastPointPos() - pivot;
    Vec3f toNextPoint = getNextPointPos() - pivot;

    if (MR::isSameDirection(toLastPoint, toNextPoint)) {
        Pos3f mtx;
        mtx.identity();
        mtx.setRotate(mRotateAxis, PI * cNum_startMoveInner_f1);
        mtx.mult(toLastPoint, toLastPoint);
    }

    MR::makeQuatFromVec(&mLastPointQuat, toLastPoint, mRotateAxis);
    MR::makeQuatFromVec(&mNextPointQuat, toNextPoint, mRotateAxis);
}

const Vec3f OnimasuPivot::getLastPointPos() const {
    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, getLastPointNo() * 2);

    return railPointPos;
}

const Vec3f OnimasuPivot::getPivotPointPos() const {
    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, (getLastPointNo() * 2) + 1);

    return railPointPos;
}

const Vec3f OnimasuPivot::getNextPointPos() const {
    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, mCurNormal * 2);

    return railPointPos;
}

void OnimasuPivot::updatePoseInner() {
    f32 slerpFactor = static_cast< f32 >(getNerveStep()) / static_cast< f32 >(getTimeToNextPoint());

    if (1.0f < slerpFactor) {
        slerpFactor = 1.0f;
    }

    Quat4f quat;
    quat.set(mLastPointQuat);
    quat.slerp(mNextPointQuat, slerpFactor);

    Vec3f fromPivotPoint(gZeroVec);

    quat.getZDir(fromPivotPoint);

    // This constant is 400.0f * sqrt(2.0f), which is hardcoded
    // for the distance between points in toy time galaxy.
    fromPivotPoint.setLength(cNum_updatePoseInner_f1);

    fromPivotPoint.add(getPivotPointPos());

    mPosition.set(fromPivotPoint);
}

s32 OnimasuPivot::getNextPointNo() const {
    return mCurNormal * 2;
}

s32 OnimasuPivot::getLastPointNo() const {
    s32 n = mCurNormal - 1;

    if (n < 0) {
        return (MR::getRailPointNum(this) / 2) - 1;
    }

    return n;
}

const Vec3f& OnimasuPivot::getLastPointNormal() const {
    return mNormals[getLastPointNo()];
}

const Vec3f& OnimasuPivot::getNextPointNormal() const {
    return mNormals[mCurNormal];
}

void OnimasuPivot::incrementNextPoint() {
    mCurNormal++;

    if (mCurNormal == MR::getRailPointNum(this) / 2) {
        mCurNormal = 0;
    }
}

void OnimasuPivot::collectRailPointInfo() {
    for (s32 i = 0; i < mNormals.size(); i++) {
        Vec3f normal(gZeroVec);
        Vec3f pos(gZeroVec);
        OnimasuFunction::getPolygonOnRailPoint(&pos, &normal, this, i * 2);
        mNormals[i].set(normal);
    }
}
