#include "Game/Player/MarioFaint.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::doFlipWeak(const Vec3f& rVec) {
    if (mMovementStates.damaged) {
        return false;
    }

    mFaint->mNoDamage = true;

    if (faint(rVec)) {
        if (isStatusActive(MarioStatus_Swim)) {
            mSwim->mDamageType = 1;
            mFaint->mNoDamage = false;
        }

        return true;
    }

    mFaint->mNoDamage = false;
    forceStopTornado();
    return false;
}

bool Mario::faint(const Vec3f& rVec) {
    mDamageDir = rVec;
    if (!isEnableAddDamage()) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_Damage) {
        return false;
    }

    mFaint->setVec(rVec);
    stopWalk();
    forceStopTornado();
    mActor->damageDropThrowMemoSensor();

    if (getCurrentStatus() != MarioStatus_Faint) {
        mMovementStates.fainted = true;
        return true;
    }

    return mMovementStates.fainted;
}

MarioFaint::MarioFaint(MarioActor* pActor) : MarioState(pActor, MarioStatus_Faint), mTimer(), mAfterTimer(), mStep() {
    mVelocity.zero();
    mNoDamage = false;
    mTookDamage = false;
}

void MarioFaint::setVec(const Vec3f& rVec) {
    MR::vecKillElement(rVec, mActor->mGravityVec, &mVelocity);
    mVelocity.setLength(mActor->getConst().getTable()->mSlideDistFaint);
    mTimer = 0;
    mStep = 0;

    if (getPlayer()->isStatusActive(MarioStatus_Hang)) {
        mVelocity.zero();
    }
}

bool MarioFaint::update() {
    mTimer++;

    switch (mStep) {
    case 0:
        addVelocity(mVelocity);
        mVelocity.scale(mActor->getConst().getTable()->mFaintFriction1);
        if (mTimer == mActor->getConst().getTable()->mFaintTimer1) {
            mStep++;
        }

        break;
    case 1:
        if (!getPlayer()->getMovementStates().onGround) {
            return false;
        }

        addVelocity(mVelocity);
        mVelocity.scale(mActor->getConst().getTable()->mFaintFriction2);

        if (checkTrgA()) {
            getPlayer()->tryJump();
            return false;
        }

        if (mTimer == mActor->mConst->getTable()->mFaintTimer1 + mActor->getConst().getTable()->mFaintTimer2) {
            return false;
        }

        break;
    }

    return true;
}

bool MarioFaint::start() {
    mTimer = 0;
    mStep = 0;
    getPlayer()->mMovementStates.hipDrop = false;
    getPlayer()->mMovementStates.jumping = false;

    if (mVelocity.dot(getPlayer()->mFrontVec) > 0.0f) {
        getPlayer()->setFrontVecKeepUp(mVelocity);
        changeAnimation("後方小ダメージ");
    } else {
        getPlayer()->setFrontVecKeepUp(-mVelocity);
        changeAnimation("前方小ダメージ");
    }

    if (mNoDamage) {
        changeAnimation("ノーダメージ");
    }

    playSound("声小ダメージ");
    playSound("ダメージ");
    playEffect("ダメージ");
    startPadVib(2);

    addVelocity(mVelocity);

    mTookDamage = !mNoDamage;

    if (!mNoDamage) {
        mActor->decLife(0);
        mActor->resetPlayerModeOnDamage();

        if (mActor->mHealth == 0) {
            if (!getPlayer()->getMovementStates().onGround) {
                mActor->forceGameOverNonStop();
            } else {
                mActor->forceGameOver();
            }
        }

        return true;
    }

    mNoDamage = false;
    mActor->resetPlayerModeOnNoDamage();
    return true;
}

bool MarioFaint::close() {
    if (getPlayer()->getMovementStates().onGround) {
        stopAnimation("後方小ダメージ");
        stopAnimation("前方小ダメージ", "基本");
    }

    if (mTookDamage) {
        mAfterTimer = 120;
    }

    return true;
}
