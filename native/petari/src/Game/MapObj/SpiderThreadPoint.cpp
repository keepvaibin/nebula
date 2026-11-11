#include "Game/MapObj/SpiderThreadPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/MapObj/SpiderThreadWindCtrl.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_startThreadLevelSound_f1, "startThreadLevelSound.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startThreadLevelSound_f2, "startThreadLevelSound.f2", 100.0f);

namespace {
    SpiderThreadWindCtrl* getWindCtrl() {
        return MR::getSceneObj< SpiderThread >(SceneObj_SpiderThread)->mWindCtrl;
    }

    NEBULA_TUNABLE(f32, sAccelRate, 0.02f);
    NEBULA_TUNABLE(f32, sSpeedMinToStop, 0.1f);
    NEBULA_TUNABLE(f32, sDistanceToStop, 1.0f);
};  // namespace

SpiderThreadPoint::SpiderThreadPoint(const Vec3f& rPos, f32 friction)
    : mBasePos(rPos), mPosition(rPos), mVelocity(0.0f, 0.0f, 0.0f), mFriction(friction), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f) {
    mWindStartTime = ::getWindCtrl()->getTimeToStartWind();
    mWindTime = ::getWindCtrl()->getWindTime();
    mPrevStretchDist = 0.0f;
}

bool SpiderThreadPoint::updateSpring() {
    mVelocity.scale(mFriction);
    Vec3f toBase(mBasePos - mPosition);
    Vec3f accel(toBase * ::sAccelRate);
    mVelocity.add(accel);
    mPosition.add(mVelocity);

    Vec3f offsetOnPlane(mBasePos - mPosition);
    offsetOnPlane.z = 0.0f;

    f32 distance = offsetOnPlane.length();
    f32 speed = mVelocity.length();

    if (speed < ::sSpeedMinToStop && distance < ::sDistanceToStop) {
        mVelocity.zero();
        return true;
    }

    startThreadLevelSound();
    return false;
}

void SpiderThreadPoint::updateDrawVec(const Vec3f* pPosA, const Vec3f* pPosB) {
    Vec3f diff(*pPosB);
    diff.sub(*pPosA);
    diff.z = 0.0f;

    if (!MR::isNearZero(diff)) {
        MR::normalize(&diff);
        mUp.set< f32 >(diff.y, -diff.x, 0.0f);
    }
}

void SpiderThreadPoint::updateWind(f32 dampener) {
    if (mWindStartTime > 0) {
        mWindStartTime--;
    } else {
        if (--mWindTime > 0) {
            mVelocity.add(::getWindCtrl()->mWind);
        } else {
            mWindStartTime = ::getWindCtrl()->getTimeToStartWind();
            mWindTime = ::getWindCtrl()->getWindTime();
        }
    }
    mVelocity.scale(dampener);
}

void SpiderThreadPoint::updatePos() {
    mPosition.add(mVelocity);
}

void SpiderThreadPoint::restrict(const Vec3f* pAnchor, f32 length) {
    Vec3f nextPosDiff(mPosition);
    nextPosDiff.add(mVelocity);
    nextPosDiff.sub(*pAnchor);

    Vec3f dir(nextPosDiff);
    if (!MR::isNearZero(dir)) {
        MR::normalize(&dir);

        if (nextPosDiff.squared() >= length * length) {
            Vec3f restriction = dir * length;
            mVelocity.x -= nextPosDiff.x - restriction.x;
            mVelocity.y -= nextPosDiff.y - restriction.y;
            mVelocity.z -= nextPosDiff.z - restriction.z;
        }
    }
}

void SpiderThreadPoint::updateHang(const Vec3f& rPos) {
    mPosition = rPos;
    mVelocity.zero();
}

bool SpiderThreadPoint::tryTouch(f32 radius, const Vec3f& rVec, s32 channel) {
    if (!MR::isStarPointerPointing(mPosition, radius, channel, false)) {
        return false;
    }

    mVelocity.add(rVec);
    return true;
}

bool SpiderThreadPoint::tryPush(const Vec3f& rPos, f32 radius) {
    f32 dist = MR::calcDistanceXY(mPosition, rPos);
    if (dist > radius || mPosition.z < rPos.z) {
        return false;
    }

    mVelocity.z += getPosZDiff(rPos);

    return true;
}

void SpiderThreadPoint::startThreadLevelSound() {
    f32 stretchDist = mBasePos.distance(mPosition);
    if (mPrevStretchDist > 0.0f && stretchDist > cNum_startThreadLevelSound_f1) {
        f32 diff = MR::abs(mPrevStretchDist - stretchDist);
        if (diff > 1.0f) {
            MR::startSystemLevelSE("SE_OJ_LV_SPIDER_THREAD_PULL", systemLevelParam(stretchDist, diff * cNum_startThreadLevelSound_f2));
        }
    }
    mPrevStretchDist = stretchDist;
}
