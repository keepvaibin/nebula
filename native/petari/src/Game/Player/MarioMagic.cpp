#include "Game/Player/MarioMagic.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioModule.hpp"

void Mario::stopPunch() {
    if (isStatusActive(MarioStatus_Magic)) {
        closeStatus(mMagic);
    }

    MarioActor* actor = mActor;

    if (!actor->mPunchHitTimer) {
        actor->mPunchFrame = 0;
        actor->mAirPunchHitCount = 0;
    }

    actor->mPunchHitTimer = 0;
}

void Mario::startMagic() {
    if (!mMovementStates.jumping) {
        if (!mActor->mTakeCount) {
            if (!mMovementStates.slopeSlide) {
                if (!isStatusActive(MarioStatus_Slider)) {
                    if (isSkatableFloor()) {
                        doSkate();
                    } else {
                        clearSlope();
                        changeStatus(mMagic);
                        stopAnimationUpper(nullptr);
                        mActionStates.swingEffectPending = 1;
                    }
                }
            }
        }
    }
}

MarioMagic::MarioMagic(MarioActor* pActor) : MarioState(pActor, MarioStatus_Magic) {
    mTimer = 0;
}

bool MarioMagic::start() {
    changeAnimation("地上ひねり");
    stopEffect("パンチブラー左");
    stopEffect("パンチブラー右");
    playEffect("共通地上スピン");
    playSound("声スピン");
    playSound("スピンジャンプ");
    startPadVib(2);
    mTimer = 0;
    return true;
}

bool MarioMagic::close() {
    stopEffect("スピンライト");

    if (mTimer < 0x1A) {
        playEffect("スピンライト消去");
    }

    return true;
}

bool MarioMagic::update() {
    if (mActor->isRequestJump()) {
        getPlayer()->tryJump();
        return false;
    } else if (!isAnimationRun("地上ひねり")) {
        return false;
    }

    mTimer++;

    if (mTimer == 25) {
        stopEffect("スピンライト");
        playEffect("スピンライト消去");
    }

    if (getPlayer()->mMovementStates.jumping) {
        getPlayer()->procJump(false);
    } else {
        getPlayer()->mainMove();
    }

    getPlayer()->updateWalkSpeed();
    return true;
}
