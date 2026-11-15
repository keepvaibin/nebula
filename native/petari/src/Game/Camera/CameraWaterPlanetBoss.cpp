#include "Game/Camera/CameraWaterPlanetBoss.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sAngleRate = 0.02f;
    const s32 sRoundingFrame = 60;
};  // namespace

CameraWaterPlanetBoss::CameraWaterPlanetBoss(const char* pName)
    : Camera(pName), mDistMin(300.0f), mDistMax(1200.0f), mSideBlendRateMin(0.01f), mSideBlendRate(0.01f), mCollideCount(),
      mLastMoveDir(0.0f, 0.0f, 0.0f), mIsBlendUpEnabled(), mWPoint(0.0f, 0.0f, 0.0f), mDotFrontMin(), mAngleXMin(), mDotFrontMax(), mAngleXMax(),
      mIsRounding(), mRoundingFrame(), mSide(1.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraWaterPlanetBoss >);
}

void CameraWaterPlanetBoss::reset() {
    mIsRounding = false;
    mSideBlendRate = mSideBlendRateMin;
    mCollideCount = 0;
    mLastMoveDir.zero();

    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraWaterPlanetBoss::calc() {
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Vec3f gravUp = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f front, up, side;
    f32 dist = CameraLocalUtil::calcFollowFrame(this, watchPos, gravUp, mDistMin, mDistMax, false, &front, &up, &side);

    Vec3f gravSide = CameraLocalUtil::getTarget(this)->getSideVec();
    gravSide -= gravUp * gravUp.dot(gravSide);
    MR::normalize(&gravSide);

    if (mIsRounding) {
        gravSide = mSide;
        mRoundingFrame++;

        f32 rate = static_cast< f32 >(mRoundingFrame) / static_cast< f32 >(::sRoundingFrame);
        if (rate > 1.0f) {
            rate = 1.0f;
        }
        rate *= rate;

        f32 angle = gravSide.angle(side) * 180.0f / MR::pi();
        f32 angle2 = angle - angle * rate;
        if (MR::isNearZero(angle)) {
            mSideBlendRate = 1.0f;
        } else {
            mSideBlendRate = 1.0f - angle2 / angle;
        }

        if (mRoundingFrame >= ::sRoundingFrame) {
            mIsRounding = false;
        }
    } else {
        if (CameraLocalUtil::tryCameraResetTrigger()) {
            mIsRounding = true;
            mRoundingFrame = 0;
            mSide.set(gravSide);
        }

        f32 rate = mSideBlendRateMin * MR::abs(gravUp.dot(front));
        mSideBlendRate *= 0.005f;
        if (mSideBlendRate < rate) {
            mSideBlendRate = rate;
        }
    }

    Quat4f rot;
    rot.setRotate(side, gravSide, mSideBlendRate);
    rot.transform(front);
    rot.transform(up);
    rot.transform(side);

    Vec3f pos = watchPos - front * dist;
    f32 collideRate = CameraLocalUtil::updateMoveCollision(&mCollideCount, &mLastMoveDir, CameraLocalUtil::getTarget(this), pos, front * dist);

    if (mCollideCount > 0) {
        rot.setRotate(front, mLastMoveDir, collideRate);
        rot.transform(front);
        rot.transform(up);
        rot.transform(side);
    }

    if (mIsBlendUpEnabled && mCollideCount == 0) {
        Vec3f wPoint = mWPoint;
        mZoneMatrix.mult(wPoint, wPoint);

        Vec3f dir = CameraLocalUtil::getTarget(this)->getPosition() - wPoint;
        f32 gravDot = gravUp.dot(dir);
        f32 rate = (gravDot - mDotFrontMin) / (mDotFrontMax - mDotFrontMin);
        if (rate > 1.0f) {
            rate = 1.0f;
        } else if (rate < 0.0f) {
            rate = 0.0f;
        }

        Vec3f rotMin = gravUp;
        rot.setRotate(side, mAngleXMin + MR::pi() / 2.0f);
        rot.transform(rotMin);

        Vec3f rotMax = gravUp;
        rot.setRotate(side, mAngleXMax + MR::pi() / 2.0f);
        rot.transform(rotMax);

        Vec3f upBlend = rotMin;
        rot.setRotate(rotMin, rotMax, rate);
        rot.transform(upBlend);

        rot.setRotate(front, upBlend, ::sAngleRate);
        rot.transform(front);
        rot.transform(side);
        rot.transform(up);
    }

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setPos(this, watchPos - front * dist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);

    return CameraLocalUtil::getTarget(this);
}

void CameraWaterPlanetBoss::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.y, general->mAxis.x, general->mDist, general->mNum1 != 0, general->mWPoint, general->mAxis.z, general->mUp.x, general->mUp.y, general->mUp.z);
}
