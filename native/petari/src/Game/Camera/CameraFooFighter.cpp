#include "Game/Camera/CameraFooFighter.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 0.995f);

namespace {
    NEBULA_TUNABLE(f32, sRateWhenBraking, 0.05f);
};  // namespace

CameraFooFighter::CameraFooFighter(const char* pName)
    : Camera(pName), mDistMin(300.0f), mDistMax(1200.0f), mAngleXRateMin(0.03f), mAngleXRate(0.03f), mCollideCount(),
      mTargetLastMoveDir(0.0f, 0.0f, 0.0f) {
}

void CameraFooFighter::reset() {
    mCollideCount = 0;
    mAngleXRate = mAngleXRateMin;
    mTargetLastMoveDir.zero();

    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraFooFighter::calc() {
    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);

    Vec3f gravUp = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f newFront, newUp, newSide;
    f32 dist = CameraLocalUtil::calcFollowFrame(this, watchPoint, gravUp, mDistMin, mDistMax, true, &newFront, &newUp, &newSide);

    if (MR::isPlayerNeedBrakingCamera()) {
        Pos3f targetFrame;
        targetFrame.identity();
        targetFrame.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
        targetFrame.setYDir(-CameraLocalUtil::getTarget(this)->getFrontVec());
        targetFrame.setZDir(CameraLocalUtil::getTarget(this)->getUpVec());

        Quat4f targetRot;
        targetFrame.getQuat(targetRot);

        Pos3f followFrame;
        followFrame.identity();
        followFrame.setXDir(newSide);
        followFrame.setYDir(newUp);
        followFrame.setZDir(newFront);

        Quat4f followRot;
        followFrame.getQuat(followRot);

        Quat4f blendRot;
        blendRot.slerp(followRot, targetRot, ::sRateWhenBraking);

        followFrame.makeQuat(blendRot);
        followFrame.getXDir(newSide);
        followFrame.getYDir(newUp);
        followFrame.getZDir(newFront);
    } else {
        Vec3f targetSide = CameraLocalUtil::getTarget(this)->getSideVec();
        targetSide -= gravUp * gravUp.dot(targetSide);
        MR::normalize(&targetSide);

        f32 rate = mAngleXRateMin * MR::abs(gravUp.dot(newFront));
        mAngleXRate *= cNum_calc_f3;
        if (mAngleXRate < rate) {
            mAngleXRate = rate;
        }

        Quat4f rot;
        rot.setRotate(newSide, targetSide, rate);
        rot.transform(newFront);
        rot.transform(newUp);
        Vec3f pos = watchPoint - newFront * dist;
        f32 collideRate =
            CameraLocalUtil::updateMoveCollision(&mCollideCount, &mTargetLastMoveDir, CameraLocalUtil::getTarget(this), pos, newFront * dist);

        if (mCollideCount > 0) {
            rot.setRotate(newFront, mTargetLastMoveDir, collideRate);
            rot.transform(newFront);
            rot.transform(newUp);
        }
    }

    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setPos(this, watchPoint - newFront * dist);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, newUp);
    return CameraLocalUtil::getTarget(this);
}

void CameraFooFighter::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.y, general->mAxis.x, general->mDist);
}
