#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioFlip.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioStun.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

bool MarioActor::isEnableSpinPunch() {
    if (getMovementStates()._23) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Slider) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Swim) {
        return false;
    }

    if (mMario->getCurrentStatus() == MarioStatus_Flip) {
        return mMario->mFlip->_14 >= 2;  // FIXME
    }

    if (mMario->getCurrentStatus() == MarioStatus_Stun) {
        bool out = false;
        if (mMario->mStun->_12 || mMario->mStun->_14 < 15) {
            out = true;
        }

        return out;
    }

    if (getMovementStates()._A && mMario->calcDistToCeil(false) < 160.0f) {
        return false;
    }

    return true;
}

bool MarioActor::trySpinPunch() {
    bool out = true;
    if (getMovementStates().jumping) {
        changeAnimation("\213\363\203\160\203\223\203\140" /* 空パンチ */);
        setPunchHitTimer(15);
        mMario->mWalkSpeed = 0.0f;
    } else if (isEnableSpinPunch()) {
        if (mMario->isStatusActive(MarioStatus_Bury)) {
            playSound("\203\130\203\120\203\114\203\210\217\111\227\271\203\130\203\163\203\223" /* スケキヨ終了スピン */, -1);
        }

        if (getMovementStates()._A) {
            if (mMario->calcDistToCeil(false) > 160.0f) {
                changeAnimation("\203\124\203\175\201\133\203\134\203\213\203\147" /* サマーソルト */);
                playSound("\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* スピンジャンプ */, -1);
                playSound("\220\272\203\130\203\163\203\223" /* 声スピン */, -1);
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
    _950 = false;

    return out;
}

void MarioActor::printHitMark(HitSensor* pSensor) {
    TVec3f vec(pSensor->mPosition - _2A0);
    vec.setLength(200.0f);
    playEffectTrans("\203\160\203\223\203\140\203\161\203\142\203\147" /* パンチヒット */, _2A0 + vec);
}

void MarioActor::reactionPunch(HitSensor* pSensor) {
    if (mPlayerMode == 1) {
        return;
    }

    if (isJumping()) {
        _38C = 5;
    } else {
        _38C = 5;
        if (mMario->mWalkSpeed > 0.1f) {
            mMario->mWalkSpeed = 0.1f;
            mMario->mTargetWalkSpeedIndex = 1;
        }
    }

    if (!getMovementStates()._2B) {
        jumpHop();
    }

    if (!MR::sendArbitraryMsg(ACTMES_HITMARK_EMIT, pSensor, getSensor("body"))) {
        printHitMark(pSensor);
    }

    mMario->startPadVib(2);

    if (pSensor->isType(ATYPE_BEGOMAN)) {
        mMario->stopWalk();

        TVec3f reactDir(_2A0 - pSensor->mPosition);
        MR::vecKillElement(reactDir, getGravityVector(), &reactDir);
        reactDir.setLength(20.0f);
        mMario->addReaction(reactDir);
    }

    if (mMario->isSwimming()) {
        TVec3f punchDir(_2A0 - pSensor->mPosition);
        punchDir.setLength(5.0f);
        mMario->mSwim->hitPunch(punchDir);
    }
}

bool MarioActor::sendPunch(HitSensor* pSensor, bool myBool) {
    if (isDamaging()) {
        _944 = false;
        return false;
    }

    if (myBool && mPlayerMode == 3) {
        bool out = doFreezeAttack(pSensor);

        if (out && !selectCustomEffectSpinHitSound(pSensor->mHost->mName)) {
            MR::startSpinHitSound(this);
        }

        return out;
    }

    _FCC = true;
    bool out = MR::sendMsgPlayerPunch(pSensor, getSensor("body"));
    _FCC = false;

    if (myBool && out && !selectCustomEffectSpinHitSound(pSensor->mHost->mName)) {
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

    for (u32 idx = 0; idx < _974; idx++) {
        if (_954[idx] == pSensor) {
            return false;
        }
    }

    if (_974 >= 8) {
        return false;
    }

    if (!_3E5 && mPlayerMode != 1) {
        TVec3f vec(pSensor->mPosition - getSensor("body")->mPosition);
        MR::vecKillElement(vec, getGravityVector(), &vec);

        if (MR::diffAngleAbs(vec, mMario->mFrontVec) >= MR::pi() / 4.0f) {
            return false;
        }
    }

    if (sendPunch(pSensor, true)) {
        _954[_974] = pSensor;
        _924 = pSensor;
        _974++;

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

    if (isStatus && !getMovementStates()._8 && !isDamaging() && !getMovementStates()._B) {
        TVec3f newPos(pSensor->mPosition - getSensor("body")->mPosition);
        TVec3f* frontVec = &mMario->mFrontVec;
        if (MR::diffAngleAbsHorizontal(newPos, *frontVec, getGravityVector()) >= HALF_PI) {
            return false;
        }

        if (MR::sendArbitraryMsg(ACTMES_KICK, pSensor, getSensor("body"))) {
            playSound("\220\272\203\211\203\223\203\152\203\223\203\117\203\114\203\142\203\116" /* 声ランニングキック */, -1);
            changeAnimation("\203\211\203\223\203\152\203\223\203\117\203\114\203\142\203\116" /* ランニングキック */);
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
    if (isAnimationRun("\203\101\203\103\203\130\202\320\202\313\202\350\213\363\222\206" /* アイスひねり空中 */)) {
        return;
    }

    if (isJumping()) {
        if (mMario->_430 != 13) {
            changeAnimation("\203\101\203\103\203\130\202\320\202\313\202\350\213\363\222\206" /* アイスひねり空中 */);
            playSound("\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* スピンジャンプ */, -1);
            playSound("\220\272\203\130\203\163\203\223" /* 声スピン */, -1);

            if (mMario->_42C == 0) {
                jumpHop();
                mMario->_42C++;
            }
        }

        return;
    }

    if (mMario->isSkatableFloor()) {
        mMario->doSkate();
        return;
    }

    if (!isAnimationRun("\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */)) {
        playSound("\203\130\203\120\201\133\203\147\203\130\203\163\203\223" /* スケートスピン */, -1);
        playSound("\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* スピンジャンプ */, -1);
        playSound("\220\272\203\160\203\223\203\140" /* 声パンチ */, -1);
    }

    changeAnimation("\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */);
}
