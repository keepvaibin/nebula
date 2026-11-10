#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioParalyze.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_close_f1, "close.f1", 10.0f);

bool Mario::doParalyze() {
    if (mMovementStates.invincible) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_Paralyze) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (mMovementStates.damaged) {
        return false;
    }

    if (getDamageAfterTimer() != 0) {
        return false;
    }

    if (isDamaging()) {
        return false;
    }

    mActor->damageDropThrowMemoSensor();
    mActor->resetPlayerModeOnDamage();

    stopJump();
    stopWalk();

    changeStatus(mParalyze);
    return true;
}

MarioParalyze::MarioParalyze(MarioActor* pActor) : MarioState(pActor, MarioStatus_Paralyze), mRecovering(), mTimer(), mAfterTimer(), mNotDecLife() {
}

bool MarioParalyze::start() {
    changeAnimationNonStop("電気ダメージ");
    playSound("ダメージ");
    playSound("電気ダメージ");
    playSound("声電気ダメージ");
    playEffect("ビリビリ");

    startPadVib(3);

    if (!mNotDecLife) {
        mActor->decLifeLarge();
    }

    mNotDecLife = false;
    mTimer = 60;
    mRecovering = 0;

    return true;
}

bool MarioParalyze::update() {
    if (mTimer != 0) {
        mTimer--;
        if (mRecovering != 0 && getPlayer()->getMovementStates().onGround) {
            addVelocity(getFrontVec(), -cNum_update_f1);
        }
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
            changeAnimation("電気ダメージ終了");
            playSound("声電気ダメージ終了");
        }

        if (mActor->mHealth == 0) {
            if (!getPlayer()->mMovementStates.onGround) {
                mActor->forceGameOverNonStop();
            } else {
                mActor->forceGameOver();
            }

            mActor->changeGameOverAnimation();
        }
    }

    return true;
}

bool MarioParalyze::close() {
    if (mActor->mHealth == 0) {
        if (!getPlayer()->mMovementStates.onGround) {
            mActor->forceGameOverNonStop();
        } else {
            mActor->forceGameOver();
        }

        mActor->changeGameOverAnimation();
    }

    stopAnimation("電気ダメージ");
    stopEffect("ビリビリ");

    mAfterTimer = 120;

    if (!getPlayer()->isStatusActive(MarioStatus_Swim) && !getPlayer()->getMovementStates().onGround) {
        getPlayer()->tryFreeJump(getFrontVec() * -cNum_close_f1, true);
    }

    return true;
}
