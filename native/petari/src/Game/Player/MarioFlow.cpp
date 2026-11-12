#include "Game/Player/MarioFlow.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::doFlow() {
    if (isStatusActive(MarioStatus_Flow)) {
        return false;
    }

    if (isAnimationRun("空中ひねり")) {
        return false;
    }

    changeStatus(mFlow);

    return true;
}

bool MarioFlow::start() {
    mTimer = 0;
    mStep = 0;

    changeAnimationNonStop("中ダメージ");
    startPadVib(2);

    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates.hipDrop = false;
    getPlayer()->mMovementStates.spinBlocked = true;

    mVelocity = getFrontVec() * mActor->mConst->getTable()->mJumpDistFlow;
    mVelocity += -mActor->mGravityVec * mActor->mConst->getTable()->mJumpHeightFlow;
    getPlayer()->mJumpVec = mVelocity;

    addVelocity(mVelocity);

    getPlayer()->stopWalk();

    return true;
}

bool MarioFlow::update() {
    mTimer++;

    switch (mStep) {
    case 0:
        addVelocity(mVelocity);

        mVelocity += mActor->mGravityVec * mActor->getConst().getTable()->mGravityFlow;

        if (mTimer == 20) {
            changeAnimation("中ダメージ空中");
        }

        if (mTimer > 30 && mActor->isRequestRush()) {
            getPlayer()->trySpinJump(0);

            return false;
        }

        if (getPlayer()->getMovementStates().onGround) {
            getPlayer()->mMovementStates.jumping = false;

            changeAnimation("中ダメージ着地");

            playEffect("共通ダメージ着地");
            playSound("吹っ飛び倒れ");

            MR::vecKillElement(mVelocity, mActor->mGravityVec, &mVelocity);

            mTimer = 0;
            mStep++;
        }
        break;
    case 1:
        if (getPlayer()->getMovementStates().onGround == false) {
            getPlayer()->mMovementStates.jumping = true;
            mStep = 0;
        } else {
            addVelocity(mVelocity);

            mVelocity.x *= 0.95f;
            mVelocity.y *= 0.95f;
            mVelocity.z *= 0.95f;

            if (!isAnimationRun("中ダメージ着地")) {
                return false;
            }

            if (mTimer > 15 && checkTrgA()) {
                getPlayer()->tryJump();

                return false;
            }
        }
        break;
    }

    getPlayer()->mJumpVec = mVelocity;

    return true;
}

bool MarioFlow::close() {
    stopAnimation("ダメージ");
    stopAnimation("ダメージ着地", "基本");

    return true;
}

MarioFlow::MarioFlow(MarioActor* pActor) : MarioState(pActor, MarioStatus_Flow), mTimer(), mStep() {
    mVelocity.zero();
}
