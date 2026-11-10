#include "Game/Camera/CameraWaterFollow.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f4, "calc.f4", 0.005f);

namespace {
    NEBULA_TUNABLE(s32, sRoundingFrame, 60);
};  // namespace

CameraWaterFollow::CameraWaterFollow(const char* pName)
    : Camera(pName), mDistMin(300.0f), mDistMax(1200.0f), mSideBlendRateMin(0.01f), mSideBlendRate(0.01f), mCollideCount(),
      mLastMoveDir(0.0f, 0.0f, 0.0f), mIsRounding(), mRoundingFrame(), mSide(1.0f, 0.0f, 0.0f) {
}

void CameraWaterFollow::reset() {
    mIsRounding = false;
    mSideBlendRate = mSideBlendRateMin;
    mCollideCount = 0;
    mLastMoveDir.zero();

    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraWaterFollow::calc() {
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);

    Vec3f gravUp = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f front, up, side;
    f32 dist = CameraLocalUtil::calcFollowFrame(this, watchPos, gravUp, mDistMin, mDistMax, true, &front, &up, &side);

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

        f32 angle = gravSide.angle(side) * cNum_calc_f3;
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
        mSideBlendRate *= cNum_calc_f4;
        if (mSideBlendRate < rate) {
            mSideBlendRate = rate;
        }
    }

    Quat4f rot;
    rot.setRotate(side, gravSide, mSideBlendRate);
    rot.transform(front);
    rot.transform(up);

    Vec3f pos = watchPos - front * dist;
    f32 collideRate = CameraLocalUtil::updateMoveCollision(&mCollideCount, &mLastMoveDir, CameraLocalUtil::getTarget(this), pos, front * dist);

    if (mCollideCount > 0) {
        rot.setRotate(front, mLastMoveDir, collideRate);
        rot.transform(front);
        rot.transform(up);
    }

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setPos(this, watchPos - front * dist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);

    return CameraLocalUtil::getTarget(this);
}

void CameraWaterFollow::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.y, general->mAxis.x, general->mDist);
}
