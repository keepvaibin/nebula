#include "Game/MapObj/FountainBig.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpout_f1, "exeSpout.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpoutEnd_f1, "exeSpoutEnd.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 600.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeSign_1, "exeSign.1", 80);
NEBULA_TUNABLE_NAMED(s32, cStep_exeSpout_1, "exeSpout.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeSpoutEnd_1, "exeSpoutEnd.1", 10);

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
    MR::addHitSensorCallbackEye(this, "body", 4, cNum_init_f1);
    MR::invalidateHitSensors(this);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    Vec3f vec;
    MR::calcUpVec(&vec, this);
    mClippingRadius.scaleAdd(cNum_init_f2, vec, mPosition);
    MR::setClippingTypeSphere(this, cNum_init_f3, &mClippingRadius);
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
    if (MR::isStep(this, cStep_exeSign_1)) {
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
    if (MR::isLessEqualStep(this, cStep_exeSpout_1)) {
        f32 calcNerve = MR::calcNerveRate(this, 20);
        mScale.y = calcNerve;
        mScale.x = 1.0f;
        mScale.z = 1.0f;
        MR::clamp(calcNerve, cNum_exeSpout_f1, 1.0f);
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
    MR::clamp(calcNerve, cNum_exeSpoutEnd_f1, 1.0f);
    if (MR::isStep(this, cStep_exeSpoutEnd_1)) {
        MR::hideModel(this);
        setNerve(GET_NERVE(FountainBig, FountainBigNrvWait));
    }
}

void FountainBig::updateHitSensor(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    Vec3f up, bottom, top;
    MR::calcUpVec(&up, this);
    bottom.scaleAdd(radius, up, mPosition);
    top.scaleAdd(cNum_updateHitSensor_f1 - radius, up, bottom);
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
