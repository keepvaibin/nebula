#include "Game/MapObj/FountainBig.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvFountainBig {
    NEW_NERVE(FountainBigNrvWait, FountainBig, Wait);
    NEW_NERVE(FountainBigNrvSign, FountainBig, Sign);
    NEW_NERVE(FountainBigNrvSignStop, FountainBig, SignStop);
    NEW_NERVE(FountainBigNrvSpout, FountainBig, Spout);
    NEW_NERVE(FountainBigNrvSpoutEnd, FountainBig, SpoutEnd);
};  // namespace NrvFountainBig

FountainBig::FountainBig(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FountainBig >);
    mClippingRadius.x = 0.0f;
    mClippingRadius.y = 0.0f;
    mClippingRadius.z = 0.0f;
    mSpoutTimer = -1;
}

void FountainBig::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("FountainBig", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorCallbackEye(this, "body", 4, 60.0f);
    MR::invalidateHitSensors(this);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    Vec3f vec;
    MR::calcUpVec(&vec, this);
    mClippingRadius.scaleAdd(300.0f, vec, mPosition);
    MR::setClippingTypeSphere(this, 600.0f, &mClippingRadius);
    MR::hideModel(this);
    MR::startBtk(this, "FountainBig");
    initNerve(GET_NERVE(FountainBig, FountainBigNrvWait));
    makeActorAppeared();
}

void FountainBig::exeWait() {
    if (MR::isFirstStep(this)) {
        mSpoutTimer = -1;
        MR::invalidateHitSensors(this);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(FountainBig, FountainBigNrvSign));
    }
}

void FountainBig::exeSign() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "FountainBigSign");
    }
    MR::startLevelSound(this, "SE_OJ_LV_FOUNTAIN_BIG_SIGN");
    if (MR::isStep(this, 80)) {
        setNerve(GET_NERVE(FountainBig, FountainBigNrvSignStop));
    }
}

void FountainBig::exeSignStop() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, "FountainBigSign");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(FountainBig, FountainBigNrvSpout));
    }
}

void FountainBig::exeSpout() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::emitEffect(this, "FountainBig");
        MR::startSound(this, "SE_OJ_FOUNTAIN_BIG_START");
        MR::validateHitSensors(this);
    }
    s32 spoutTimer = mSpoutTimer;
    if (spoutTimer != -1) {
        if (spoutTimer >= 60 && MR::isOnGroundPlayer()) {
            mSpoutTimer = -1;
            MR::validateHitSensors(this);
        } else {
            mSpoutTimer++;
        }
    }
    if (MR::isLessEqualStep(this, 20)) {
        f32 calcNerve = MR::calcNerveRate(this, 20);
        mScale.y = calcNerve;
        mScale.x = 1.0f;
        mScale.z = 1.0f;
        MR::clamp(calcNerve, 0.01f, 1.0f);
    }
    MR::startLevelSound(this, "SE_OJ_LV_FOUNTAIN_BIG_SPOUT");
    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        MR::invalidateHitSensors(this);
        MR::deleteEffect(this, "FountainBig");
        setNerve(GET_NERVE(FountainBig, FountainBigNrvSpoutEnd));
    }
}

void FountainBig::exeSpoutEnd() {
    f32 calcNerve = 1.0f - MR::calcNerveRate(this, 10);
    mScale.x = 1.0f;
    mScale.z = 1.0f;
    mScale.y = calcNerve;
    MR::clamp(calcNerve, 0.01f, 1.0f);
    if (MR::isStep(this, 10)) {
        MR::hideModel(this);
        setNerve(GET_NERVE(FountainBig, FountainBigNrvWait));
    }
}

void FountainBig::updateHitSensor(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    Vec3f up, bottom, top;
    MR::calcUpVec(&up, this);
    bottom.scaleAdd(radius, up, mPosition);
    top.scaleAdd(600.0f - radius, up, bottom);
    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), bottom, top);
}

void FountainBig::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerElementModeBee()) {
            bool check = false;
            if (MR::isOnGroundPlayer()) {
                check = MR::sendMsgEnemyAttackFlipWeak(pReceiver, pSender);
            } else {
                check = MR::sendMsgEnemyAttackFlip(pReceiver, pSender);
            }
            if (check) {
                mSpoutTimer = 0;
                MR::invalidateHitSensors(this);
            }
        } else {
            MR::tryRumblePadWeak(this, PLAYER_1);
            MR::sendArbitraryMsg(ACTMES_FOUNTAINJUMP, pReceiver, pSender);
        }
    }
}
