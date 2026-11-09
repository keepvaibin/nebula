#include "Game/Player/MarioSukekiyo.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MtxUtil.hpp"

MarioSukekiyo::MarioSukekiyo(MarioActor* pActor) : MarioState(pActor, MarioStatus_Sukekiyo) {
    mPoseUp.zero();
    mPoseSide.zero();
    mEscaping = 0;
}

MarioBury::MarioBury(MarioActor* pActor) : MarioSukekiyo(pActor) {
    mStatusId = MarioStatus_Bury;
}

bool MarioSukekiyo::notice() {
    return false;
}

bool MarioSukekiyo::postureCtrl(MtxPtr pMtx) {
    MR::makeMtxUpSide(pMtx, mPoseUp, mPoseSide);
    return true;
}

bool MarioSukekiyo::start() {
    playSound("スケキヨ開始");
    Mario* player = getPlayer();
    playEffectRT("属性尻ドロップ", player->mGroundNorm, getTrans());
    startPadVib("最強");
    startCamVib(3);
    mActor->mBindOn = 0;
    mPoseUp = getPlayer()->mGroundNorm;
    mPoseSide = getPlayer()->mSideVec;
    getPlayer()->forceSetHeadVecKeepSide(mPoseUp);
    mEscaping = 0;
    getPlayer()->stopJump();
    getPlayer()->stopWalk();

    if (mStatusId == MarioStatus_Sukekiyo) {
        changeAnimation("スケキヨ");
    } else {
        playSound("声足埋まり開始");
        changeAnimation("埋まり");
    }

    return true;
}

bool MarioSukekiyo::update() {
    if (!getPlayer()->isCurrentFloorSand()) {
        return false;
    }

    if (mEscaping) {
        return isAnimationRun(nullptr) != false;
    }

    if (mActor->isRequestRush()) {
        mEscaping = 1;
    }

    if (checkTrgA()) {
        mEscaping = 1;
    }

    if (mEscaping) {
        if (mStatusId == MarioStatus_Sukekiyo) {
            changeAnimation("スケキヨ脱出", "基本");
            playSound("声スケキヨ終了");
        } else {
            changeAnimation("埋まり脱出", "基本");
            playSound("声足埋まり終了");
        }

        playSound("スケキヨ終了");
    }

    return true;
}

bool MarioSukekiyo::close() {
    getPlayer()->stopWalk();
    mActor->mBindOn = 1;
    stopAnimation(nullptr, "基本");
    return true;
}
