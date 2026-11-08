#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioStun.hpp"

MarioStun::MarioStun(MarioActor* pActor) : MarioState(pActor, MarioStatus_Stun), mRecovering(), mRecoverTimer() {
}

bool MarioStun::close() {
    stopAnimation("しびれ");  // "hesitation"
    return true;
}

bool MarioStun::start() {
    changeAnimationNonStop("しびれ");
    startPadVib("マリオ[しびれ]");
    playSound("声しびれ");
    mRecoverTimer = 0x3c;
    mRecovering = 0;
    return true;
}

bool MarioStun::update() {
    if (mRecoverTimer != 0) {
        mRecoverTimer--;
    }

    if (mRecoverTimer == 0) {
        if (mRecovering != 0) {
            return false;
        }

        mRecovering = 1;
        if (!getPlayer()->mMovementStates.onGround) {
            mRecoverTimer = 0xa;
        } else {
            mRecoverTimer = 0x1e;
        }

        if (getPlayer()->mMovementStates.onGround) {
            changeAnimation("しびれ回復");
        }
    }

    if (mRecovering != 0 && (mActor->isRequestRush() || checkTrgA())) {
        stopAnimation(nullptr);
        if (checkTrgA()) {
            getPlayer()->tryJump();
        }

        return false;
    }

    return true;
}
