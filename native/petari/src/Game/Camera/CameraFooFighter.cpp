#include "Game/Camera/CameraFooFighter.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

namespace {
    const f32 sRateWhenBraking = 0.05f;
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
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Vec3f gravUp = -CameraLocalUtil::getTarget(this)->getGravityVector();
    Vec3f newFront, newUp, newSide;
    f32 dist = CameraLocalUtil::calcFollowFrame(this, watchPoint, gravUp, mDistMin, mDistMax, true, &newFront, &newUp, &newSide);

    if (MR::isPlayerNeedBrakingCamera()) {
        Pos3f mtx1;
        mtx1.identity();
        mtx1.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
        mtx1.setYDir(-CameraLocalUtil::getTarget(this)->getFrontVec());
        mtx1.setZDir(CameraLocalUtil::getTarget(this)->getUpVec());

        Quat4f rot1;
        mtx1.getQuat(rot1);

        Pos3f mtx2;
        mtx2.identity();
        mtx2.setXDir(newSide);
        mtx2.setYDir(newUp);
        mtx2.setZDir(newFront);

        Quat4f rot2;
        mtx2.getQuat(rot2);

        Quat4f rotQ;
        rotQ.slerp(rot2, rot1, ::sRateWhenBraking);

        mtx2.makeQuat(rotQ);
        mtx2.getXDir(newSide);
        mtx2.getYDir(newUp);
        mtx2.getZDir(newFront);
    } else {
        Vec3f targetSide = CameraLocalUtil::getTarget(this)->getSideVec();
        targetSide -= gravUp * gravUp.dot(targetSide);
        MR::normalize(&targetSide);

        f32 rate = mAngleXRateMin * MR::abs(gravUp.dot(newFront));
        mAngleXRate *= 0.995f;
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
