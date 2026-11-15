#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioFreeze.hpp"

bool Mario::doFreeze() {
    if (mMovementStates.invincible) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_Freeze) {
        return false;
    }

    if (mFreeze->mAfterTimer != 0) {
        return false;
    }

    mActor->resetPlayerModeOnDamage();

    stopJump();
    stopWalk();

    mActor->damageDropThrowMemoSensor();
    cancelSquatMode();

    stopAnimationUpper(nullptr);
    changeStatus(mFreeze);
    return true;
}

MarioFreeze::MarioFreeze(MarioActor* pActor) : MarioState(pActor, MarioStatus_Freeze), mIsFrozen(), mVerticalSpeed(), mThawing(), mFreezeTimer(), mAfterTimer() {
}

bool MarioFreeze::notice() {
    if (mActor->mHealth == 0) {
        if (getNoticedStatus() == MarioStatus_Swim) {
            mActor->mAnimationFrozen = true;
            mActor->forceGameOver();
        }

        return true;
    }

    return false;
}

bool MarioFreeze::start() {
    changeAnimationNonStop("氷結");

    playSound("声氷ダメージ");
    playSound("氷ダメージ");
    playSound("ダメージ");

    startPadVib(3);
    mActor->decLife(0);

    mFreezeTimer = 180;
    mThawing = 0;
    mVerticalSpeed = 0.0f;

    getPlayer()->resetInline();

    mActor->showFreezeModel();

    mIsFrozen = true;

    return true;
}

bool MarioFreeze::update() {
    if (mFreezeTimer != 0) {
        mFreezeTimer--;

        if (mThawing != 0) {
            if (getPlayer()->getMovementStates().onGround) {
                addVelocity(getFrontVec(), -1.0f);
            }
        } else if (mFreezeTimer < 120 && mActor->mHealth != 0 && mActor->isRequestSpin()) {
            addVelocity(getFrontVec(), -10.0f);
            changeAnimation("地上ひねり");

            playSound("声スピン");
            playSound("スピンジャンプ");
            playSound("氷ダメージ終了");

            mActor->hideFreezeModel();
            mIsFrozen = false;
            return false;
        }
    }

    if (mFreezeTimer == 0) {
        if (mThawing != 0) {
            if (mActor->mHealth != 0) {
                playSound("声氷ダメージ終了");
                return false;
            }
        } else {
            mThawing = 1;
            if (!getPlayer()->getMovementStates().onGround) {
                mFreezeTimer = 10;
            } else {
                mFreezeTimer = 20;
            }

            if (mActor->mHealth == 0) {
                if (!getPlayer()->getMovementStates().onGround) {
                    mActor->forceGameOverNonStop();
                } else {
                    mActor->forceGameOver();
                }
            } else if (getPlayer()->getMovementStates().onGround) {
                changeAnimation("氷結解除");
                playSound("氷ダメージ終了");
                mActor->hideFreezeModel();
                mIsFrozen = false;
            }
        }
    }

    if (mFreezeTimer < 150) {
        if (!getPlayer()->getMovementStates().onGround || mVerticalSpeed < 0.0f) {
            getPlayer()->setJumpVec(getGravityVec() * mVerticalSpeed);
            addVelocity(getGravityVec() * mVerticalSpeed);
            mVerticalSpeed += 1.5f;
            if (mVerticalSpeed < 0.0f) {
                getPlayer()->mMovementStates.onGround = false;
            }
        } else if (mVerticalSpeed > 0.0f) {
            if (mVerticalSpeed < 8.0f) {
                mVerticalSpeed = 0.0f;
            } else {
                mVerticalSpeed = 0.4f * -mVerticalSpeed;
                getPlayer()->mMovementStates.onGround = false;
                getPlayer()->setJumpVec(getGravityVec() * mVerticalSpeed);
            }
        }
    }

    return true;
}

bool MarioFreeze::close() {
    mAfterTimer = 120;

    if (mIsFrozen) {
        playSound("氷ダメージ終了");
        mActor->hideFreezeModel();
    }

    return true;
}
