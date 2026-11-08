#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioCrush.hpp"
#include "Game/Player/MarioState.hpp"

bool Mario::requestCrush() {
    mActionStates.crushRequested = true;
    return true;
}

bool Mario::tryCrush() {
    if (mMovementStates.invincible) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    if (mMovementStates.jumping || !mMovementStates.onGround) {
        setTrans(mShadowPos);
    }

    if (getCurrentStatus() == MarioStatus_Crush) {
        return false;
    }

    mActor->damageDropThrowMemoSensor();
    mActor->resetPlayerModeOnDamage();

    stopJump();
    stopWalk();

    changeStatus(mCrush);
    return true;
}

MarioCrush::MarioCrush(MarioActor* pActor) : MarioState(pActor, MarioStatus_Crush), mRecovering(), mTimer() {
}

bool MarioCrush::close() {
    getPlayer()->mMovementStates.scaleBody = true;
    mActor->mScale.set(1.0f);
    stopAnimation("しびれ");
    getPlayer()->setDamageAfterTimer(120);
    return true;
}

bool MarioCrush::start() {
    changeAnimationNonStop("しびれ");
    startPadVib("マリオ[しびれ]");
    playSound("声しびれ");

    getPlayer()->mMovementStates.scaleBody = true;
    mActor->mScale.set(1.0f, 0.2f, 1.0f);
    startPadVib(3);

    mActor->decLifeLarge();

    if (mActor->mHealth == 0) {
        mActor->forceGameOver();
    }

    mTimer = 180;
    mRecovering = 0;

    return true;
}

bool MarioCrush::update() {
    if (mTimer != 0) {
        mTimer--;
    }

    if (mTimer == 0) {
        if (mRecovering != 0) {
            return false;
        }

        mRecovering = 1;

        if (!getPlayer()->getMovementStates().onGround) {
            mTimer = 10;
        } else {
            mTimer = 30;
        }

        if (getPlayer()->getMovementStates().onGround) {
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
