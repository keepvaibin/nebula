#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Camera/CameraTargetObj.hpp"

namespace {
    static const f32 sMovingThreshold = 1.0f;
};  // namespace

CameraTargetHolder::CameraTargetHolder() {
    mTarget = nullptr;
    mTargetActor = new CameraTargetActor("\203\101\203\116\203\136\201\133\222\215\226\332" /* アクター注目 */);
    mTargetPlayer = new CameraTargetPlayer("\203\175\203\212\203\111\222\215\226\332" /* マリオ注目 */);
}

void CameraTargetHolder::movement() {
    mTarget->movement();
}

CameraTargetObj* CameraTargetHolder::get() {
    return mTarget;
}

void CameraTargetHolder::set(CameraTargetObj* pTarget) {
    mTarget = pTarget;
}

void CameraTargetHolder::set(const LiveActor* pActor) {
    mTargetActor->mActor = pActor;
    mTarget = mTargetActor;
}

void CameraTargetHolder::set(const MarioActor* pActor) {
    mTargetPlayer->mActor = pActor;
    mTarget = mTargetPlayer;
}

bool CameraTargetHolder::isOnGround() const {
    return !mTarget->isJumping() || mTarget->isWaterMode() || mTarget->isOnWaterSurface();
}

bool CameraTargetHolder::isMoving() const {
    return mTarget->getLastMove().length() > ::sMovingThreshold;
}
