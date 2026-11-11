#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioFlip.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioStun.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_isEnableSpinPunch_f1, "isEnableSpinPunch.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_trySpinPunch_f1, "trySpinPunch.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_printHitMark_f1, "printHitMark.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactionPunch_f1, "reactionPunch.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactionPunch_f2, "reactionPunch.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactionPunch_f3, "reactionPunch.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reactionPunch_f4, "reactionPunch.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPunchAirAfter_f1, "tryPunchAirAfter.f1", 4.0f);

bool MarioActor::isEnableSpinPunch() {
    if (getMovementStates().slopeSlide) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Slider) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Swim) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Flip) {
        return mMario->mFlip->mStep >= 2;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Stun) {
        bool out = false;
        if (mMario->mStun->mRecovering || mMario->mStun->mRecoverTimer < 15) {
            out = true;
        }

        return out;
    }

    if (getMovementStates().squat && mMario->calcDistToCeil(false) < cNum_isEnableSpinPunch_f1) {
        return false;
    }

    return true;
}

bool MarioActor::trySpinPunch() {
    bool out = true;
    if (getMovementStates().jumping) {
        changeAnimation("空パンチ");
        setPunchHitTimer(15);
        mMario->mWalkSpeed = 0.0f;
    } else if (isEnableSpinPunch()) {
        if (mMario->isStatusActive(MarioStatus_Bury)) {
            playSound("スケキヨ終了スピン", -1);
        }

        if (getMovementStates().squat) {
            if (mMario->calcDistToCeil(false) > cNum_trySpinPunch_f1) {
                changeAnimation("サマーソルト");
                playSound("スピンジャンプ", -1);
                playSound("声スピン", -1);
            } else {
                return false;
            }
        } else {
            mMario->startMagic();
        }

        setPunchHitTimer(25);
    } else {
        out = false;
    }

    tryReleaseBombTeresa();

    return out;
}

void MarioActor::printHitMark(HitSensor* pSensor) {
    Vec3f vec(pSensor->mPosition - mCenterPos);
    vec.setLength(cNum_printHitMark_f1);
    playEffectTrans("パンチヒット", mCenterPos + vec);
}

void MarioActor::reactionPunch(HitSensor* pSensor) {
    if (mPlayerMode == 1) {
        return;
    }

    if (isJumping()) {
        mStunTimer = 5;
    } else {
        mStunTimer = 5;
        if (mMario->mWalkSpeed > cNum_reactionPunch_f1) {
            mMario->mWalkSpeed = cNum_reactionPunch_f2;
            mMario->mTargetWalkSpeedIndex = 1;
        }
    }

    if (!getMovementStates().spinBlocked) {
        jumpHop();
    }

    if (!MR::sendArbitraryMsg(ACTMES_HITMARK_EMIT, pSensor, getSensor("body"))) {
        printHitMark(pSensor);
    }

    mMario->startPadVib(2);

    if (pSensor->isType(ATYPE_BEGOMAN)) {
        mMario->stopWalk();

        Vec3f reactDir(mCenterPos - pSensor->mPosition);
        MR::vecKillElement(reactDir, getGravityVector(), &reactDir);
        reactDir.setLength(cNum_reactionPunch_f3);
        mMario->addReaction(reactDir);
    }

    if (mMario->isSwimming()) {
        Vec3f punchDir(mCenterPos - pSensor->mPosition);
        punchDir.setLength(cNum_reactionPunch_f4);
        mMario->mSwim->hitPunch(punchDir);
    }
}

bool MarioActor::sendPunch(HitSensor* pSensor, bool isSpinAttack) {
    if (isDamaging()) {
        mPunchHitTimer = false;
        return false;
    }

    if (isSpinAttack && mPlayerMode == PlayerMode_Ice) {
        bool out = doFreezeAttack(pSensor);

        if (out && !selectCustomEffectSpinHitSound(pSensor->mHost->mName)) {
            MR::startSpinHitSound(this);
        }

        return out;
    }

    mAttacking = true;
    bool out = MR::sendMsgPlayerPunch(pSensor, getSensor("body"));
    mAttacking = false;

    if (isSpinAttack && out && !selectCustomEffectSpinHitSound(pSensor->mHost->mName)) {
        MR::startSpinHitSound(this);
    }

    return out;
}

bool MarioActor::doFreezeAttack(HitSensor* pSensor) {
    return MR::sendArbitraryMsg(ACTMES_FREEZE_ATTACK, pSensor, getSensor("body"));
}

bool MarioActor::tryPunchAirAfter(HitSensor* pSensor) {
    if (pSensor->isType(ACTMES_FORCE_KILL)) {
        return false;
    }

    for (u32 idx = 0; idx < mAirPunchHitCount; idx++) {
        if (mAirPunchHits[idx] == pSensor) {
            return false;
        }
    }

    if (!mPunchSensorOn && mPlayerMode != 1) {
        Vec3f vec(pSensor->mPosition - getSensor("body")->mPosition);
        MR::vecKillElement(vec, getGravityVector(), &vec);

        if (MR::diffAngleAbs(vec, mMario->mFrontVec) >= MR::pi() / cNum_tryPunchAirAfter_f1) {
            return false;
        }
    }

    if (sendPunch(pSensor, true)) {
        mAirPunchHits.at(mAirPunchHitCount) = pSensor;
        mRushSensor = pSensor;
        mAirPunchHitCount++;

        reactionPunch(pSensor);

        return true;
    }

    return false;
}

bool MarioActor::sendBodyAttack(HitSensor* pSensor) {
    bool isStatus = true;
    if (mMario->getCurrentStatus() != MarioStatus_None && (s32)mMario->getCurrentStatus() != (s32)MarioStatus_Skate) {
        isStatus = false;
    }

    if (isStatus && !getMovementStates().frontWall && !isDamaging() && !getMovementStates().hipDrop) {
        Vec3f newPos(pSensor->mPosition - getSensor("body")->mPosition);
        Vec3f* frontVec = &mMario->mFrontVec;
        if (MR::diffAngleAbsHorizontal(newPos, *frontVec, getGravityVector()) >= HALF_PI) {
            return false;
        }

        if (MR::sendArbitraryMsg(ACTMES_KICK, pSensor, getSensor("body"))) {
            playSound("声ランニングキック", -1);
            changeAnimation("ランニングキック");
            mMario->startPadVib(2);

            if (!MR::sendArbitraryMsg(ACTMES_HITMARK_EMIT, pSensor, getSensor("body"))) {
                printHitMark(pSensor);
            }

            return true;
        }
    }

    if (mPlayerMode == 1 && MR::sendArbitraryMsg(ACTMES_INVINCIBLE_ATTACK, pSensor, getSensor("body"))) {
        mMario->startPadVib(2);
        printHitMark(pSensor);

        return true;
    }

    return false;
}

void MarioActor::doFreezeAttack() {
    if (isAnimationRun("アイスひねり空中")) {
        return;
    }

    if (isJumping()) {
        if (mMario->mJumpKind != 13) {
            changeAnimation("アイスひねり空中");
            playSound("スピンジャンプ", -1);
            playSound("声スピン", -1);

            if (mMario->mFireShotCount == 0) {
                jumpHop();
                mMario->mFireShotCount++;
            }
        }

        return;
    }

    if (mMario->isSkatableFloor()) {
        mMario->doSkate();
        return;
    }

    if (!isAnimationRun("アイスひねり")) {
        playSound("スケートスピン", -1);
        playSound("スピンジャンプ", -1);
        playSound("声パンチ", -1);
    }

    changeAnimation("アイスひねり");
}
