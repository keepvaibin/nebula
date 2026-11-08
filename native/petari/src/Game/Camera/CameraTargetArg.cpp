#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Player/MarioActor.hpp"

CameraTargetArg::CameraTargetArg(const LiveActor* pLiveActor)
    : mTargetObj(nullptr), mTargetMtx(nullptr), mLiveActor(pLiveActor), mMarioActor(nullptr) {
}

// The first set of: target object, target matrix, actor, player.
void CameraTargetArg::setTarget() const {
    if (mTargetObj != nullptr) {
        MR::getCameraDirector()->setTarget(mTargetObj);
    } else if (mTargetMtx != nullptr) {
        mTargetMtx->invalidateLastMove();
        MR::getCameraDirector()->setTarget(mTargetMtx);
    } else if (mLiveActor != nullptr) {
        MR::getCameraDirector()->setTargetActor(mLiveActor);
    } else if (mMarioActor != nullptr) {
        MR::getCameraDirector()->setTargetPlayer(mMarioActor);
    }
}
