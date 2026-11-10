#include "Game/Boss/TombSpiderActionBase.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderFunction.hpp"
#include "Game/Boss/TombSpiderGland.hpp"
#include "Game/Boss/TombSpiderVitalSpot.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateAttackLoop_f1, "updateAttackLoop.f1", 360.0f);

namespace {
    NEBULA_TUNABLE(s32, cEnergyMax, 3);
    NEBULA_TUNABLE(s32, cStepAttackLoopSensorValidate, 20);
    // After a hit the acid keeps going this long.
    const s32 cStepAttackLoopMin = NebulaFrameClock::stepsOfMilliseconds(500);
    // Steps before the chance ends at which the end-sign animation starts, then doubles in speed.
    NEBULA_TUNABLE(s32, cStepChanceEndSign1, -480);
    NEBULA_TUNABLE(s32, cStepChanceEndSign2, -240);
    NEBULA_TUNABLE(f32, cChanceEndSignAnimRate1, 1.0f);
    NEBULA_TUNABLE(f32, cChanceEndSignAnimRate2, 2.0f);
    const s32 cStepChanceEndDemo = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cStepChanceEndAnim = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, cGlandNum, 4);
    NEBULA_TUNABLE(s32, cVitalSpotNum, 3);

    TombSpiderGland* getGland(const TombSpider* pParent, s32 index) {
        switch (index) {
        case 0:
            return TombSpiderFunction::getGlandFrontL(pParent);
        case 1:
            return TombSpiderFunction::getGlandFrontR(pParent);
        case 2:
            return TombSpiderFunction::getGlandRearL(pParent);
        default:
            return TombSpiderFunction::getGlandRearR(pParent);
        }
    }

    TombSpiderVitalSpot* getVitalSpot(const TombSpider* pParent, s32 index) {
        switch (index) {
        case 0:
            return TombSpiderFunction::getVitalSpotC(pParent);
        case 1:
            return TombSpiderFunction::getVitalSpotL(pParent);
        default:
            return TombSpiderFunction::getVitalSpotR(pParent);
        }
    }

    void startUndamagedGlandAction(TombSpider* pParent, const char* pActionName) {
        for (s32 i = 0; i < cGlandNum; i++) {
            if (!getGland(pParent, i)->isDamage()) {
                MR::startAction(getGland(pParent, i), pActionName);
            }
        }
    }

    void startAttackGlandAction(TombSpider* pParent, const char* pActionName) {
        for (s32 i = 0; i < cGlandNum; i++) {
            if (TombSpiderFunction::isAttackEnable(getGland(pParent, i))) {
                MR::startAction(getGland(pParent, i), pActionName);
            }
        }
    }

    void startChanceVitalSpots(const TombSpider* pParent) {
        for (s32 i = 0; i < cVitalSpotNum; i++) {
            getVitalSpot(pParent, i)->startChance();
        }
    }

    void endChanceVitalSpots(const TombSpider* pParent) {
        for (s32 i = 0; i < cVitalSpotNum; i++) {
            getVitalSpot(pParent, i)->endChance();
        }
    }
}  // namespace

TombSpiderActionBase::TombSpiderActionBase(TombSpider* pParent, const char* pName)
    : ActorStateBase< TombSpider >(pName, pParent), mParent(pParent), mRotateAccel(0.0f), mRotateSpeed(0.0f), mHitStep(-1), mWaitTime(0),
      mAttackTime(0), mEnergy(::cEnergyMax), mBattlePosZ(0.0f), mIsDemoChance(true) {
}

void TombSpiderActionBase::init() {
    kill();
}

void TombSpiderActionBase::appear() {
    mBattlePosZ = mParent->mPosition.z;
    mEnergy = ::cEnergyMax;
    mIsDead = false;
}

bool TombSpiderActionBase::updateWait(s32 minWaitTime, s32 maxWaitTime) {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "Wait");
        ::startUndamagedGlandAction(mParent, "Wait");
        mWaitTime = MR::getRandom(minWaitTime, maxWaitTime);
    }

    return MR::isStep(this, mWaitTime);
}

bool TombSpiderActionBase::updateDamageEye() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "EyeDamage");
        TombSpiderFunction::endAcid(mParent);
        MR::startSound(mParent, "SE_BM_TSPIDER_DAMAGE_EYE");
        MR::shakeCameraNormal();
    }

    return MR::isBckStopped(mParent);
}

bool TombSpiderActionBase::updateDamageHip() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "HipDamage");
        TombSpiderFunction::endAcid(mParent);
        MR::startSound(mParent, "SE_BM_TSPIDER_DAMAGE_HIP");
        MR::shakeCameraNormal();
    }

    return MR::isBckStopped(mParent);
}

bool TombSpiderActionBase::updateDamageGland() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "GlandDamage");
        MR::shakeCameraNormal();
        TombSpiderFunction::endAcid(mParent);
        TombSpiderFunction::invalidateAttackSensor(mParent);
        MR::startSound(mParent, "SE_BM_TSPIDER_DAMAGE_GLAND");
    }

    return MR::isBckStopped(mParent);
}

bool TombSpiderActionBase::updateAttackStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "AttackStart");
        ::startAttackGlandAction(mParent, "AttackStart");
        MR::startSound(mParent, "SE_BM_TSPIDER_PRE_ACID");
    }

    if (MR::isBckStopped(mParent)) {
        return true;
    }

    MR::startLevelSound(mParent, "SE_BM_LV_TSPIDER_PRE_ACID");
    return false;
}

// The body turns toward the player while acid sprays; a hit (mHitStep) cuts the spray short.
bool TombSpiderActionBase::updateAttackLoop(s32 minAttackTime, s32 maxAttackTime, f32 rotateAccel, f32 maxRotateSpeed) {
    if (MR::isFirstStep(this)) {
        mRotateSpeed = 0.0f;

        if (TombSpiderFunction::isRotateLeftToPlayer(mParent)) {
            MR::startAction(mParent, "AttackLoopL");
            mRotateAccel = rotateAccel;
        } else {
            MR::startAction(mParent, "AttackLoopR");
            mRotateAccel = -rotateAccel;
        }

        ::startAttackGlandAction(mParent, "AttackLoop");

        TombSpiderFunction::startAcid(mParent);
        mHitStep = -1;
        mAttackTime = MR::getRandom(minAttackTime, maxAttackTime);
    }

    if (MR::isStep(this, ::cStepAttackLoopSensorValidate)) {
        TombSpiderFunction::validateAttackSensor(mParent);
    }

    mRotateSpeed += mRotateAccel;
    mRotateSpeed = MR::clamp(mRotateSpeed, -maxRotateSpeed, maxRotateSpeed);
    mParent->mRotation.z += mRotateSpeed;
    mParent->mRotation.z = MR::repeat(mParent->mRotation.z, 0.0f, cNum_updateAttackLoop_f1);

    MR::startLevelSound(mParent, "SE_BM_LV_TSPIDER_ACID");

    if ((mHitStep >= 0 && MR::isStep(this, mHitStep + ::cStepAttackLoopMin)) || MR::isGreaterStep(this, mAttackTime)) {
        TombSpiderFunction::endAcid(mParent);
        return true;
    }

    return false;
}

bool TombSpiderActionBase::updateAttackEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "AttackEnd");
        MR::startSound(mParent, "SE_BM_TSPIDER_ACID_END");
    }

    return MR::isBckStopped(mParent);
}

// The first chance plays as a demo; later ones play in battle.
bool TombSpiderActionBase::updateChanceStart() {
    return mIsDemoChance ? updateChanceStartDemo() : updateChanceStartNoDemo();
}

bool TombSpiderActionBase::updateChanceStartNoDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "ChanceStart");
        ::startChanceVitalSpots(mParent);

        TombSpiderFunction::invalidateMainPartsSensor(mParent);
        TombSpiderFunction::invalidateGlandSensor(mParent);

        mParent->getSensor("body")->validate();
    }

    if (MR::isBckStopped(mParent)) {
        mParent->getSensor("body")->invalidate();
        TombSpiderFunction::validateVitalSpotSensor(mParent);
        MR::onSwitchA(mParent);
        return true;
    }

    return false;
}

bool TombSpiderActionBase::updateChanceStartDemo() {
    if (MR::isFirstStep(this)) {
        TombSpiderFunction::invalidateMainPartsSensor(mParent);
        TombSpiderFunction::invalidateGlandSensor(mParent);
        TombSpiderFunction::validateVitalSpotSensor(mParent);
        TombSpiderFunction::startTombSpiderDemo(mParent, "チャンス開始", "チャンス開始[トゥームスパイダー]");

        MR::sendMsgToAllLiveActor(ACTMES_TOMB_SPIDER_DEMO_START, nullptr);
        MR::startAction(mParent, "ChanceStart");
        ::startChanceVitalSpots(mParent);
    }

    if (MR::isBckStopped(mParent)) {
        TombSpiderFunction::endTombSpiderDemo(mParent, "チャンス開始", "チャンス開始[トゥームスパイダー]");
        MR::onSwitchA(mParent);
        return true;
    }

    return false;
}

// The end sign plays once at the first sign step, then loops, at double speed from the second.
bool TombSpiderActionBase::updateChanceWait(s32 waitEndTime) {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "ChanceWait");
        TombSpiderFunction::killThreadAttacherAll(mParent);
        MR::startSpiderThreadChance();
    }

    if (MR::isStep(this, waitEndTime + ::cStepChanceEndSign1)) {
        MR::startAction(mParent, "ChanceWaitEndSign");
        MR::setBckRate(mParent, ::cChanceEndSignAnimRate1);
    }

    if (MR::isBckOneTimeAndStopped(mParent)) {
        MR::startAction(mParent, "ChanceWaitEndSign");
        if (MR::isGreaterStep(this, waitEndTime + ::cStepChanceEndSign2)) {
            MR::setBckRate(mParent, ::cChanceEndSignAnimRate2);
        } else if (MR::isGreaterStep(this, waitEndTime + ::cStepChanceEndSign1)) {
            MR::setBckRate(mParent, ::cChanceEndSignAnimRate1);
        }
    }

    return MR::isStep(this, waitEndTime);
}

bool TombSpiderActionBase::updateChanceDamage() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "ChanceDamage");
    }

    if (MR::isBckStopped(mParent)) {
        TombSpiderFunction::killThreadAttacherAll(mParent);
        mParent->mPosition.z = mBattlePosZ;
        return true;
    }

    return false;
}

bool TombSpiderActionBase::updateChanceEnd() {
    return mIsDemoChance ? updateChanceEndDemo() : updateChanceEndNoDemo();
}

bool TombSpiderActionBase::updateChanceEndNoDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mParent, "ChanceEnd");
        ::endChanceVitalSpots(mParent);

        TombSpiderFunction::invalidateVitalSpotSensor(mParent);
        MR::offSwitchA(mParent);
        TombSpiderFunction::appearThreadAttacherAll(mParent);
        MR::startSpiderThreadBattle();
    }

    if (MR::isBckStopped(mParent)) {
        TombSpiderFunction::validateMainPartsSensor(mParent);
        TombSpiderFunction::validateGlandSensor(mParent);
        return true;
    }

    return false;
}

bool TombSpiderActionBase::updateChanceEndDemo() {
    if (MR::isFirstStep(this)) {
        TombSpiderFunction::validateMainPartsSensor(mParent);
        TombSpiderFunction::validateGlandSensor(mParent);
        TombSpiderFunction::invalidateVitalSpotSensor(mParent);
        TombSpiderFunction::startTombSpiderDemo(mParent, "チャンス終了", "チャンス開始[トゥームスパイダー]");
        MR::offSwitchA(mParent);
        TombSpiderFunction::appearThreadAttacherAll(mParent);
        MR::startSpiderThreadBattle();
        MR::sendMsgToAllLiveActor(ACTMES_TOMB_SPIDER_DEMO_START, nullptr);
    }

    if (MR::isStep(this, ::cStepChanceEndAnim)) {
        MR::startAction(mParent, "ChanceEnd");
        ::endChanceVitalSpots(mParent);
    }

    if (MR::isStep(this, ::cStepChanceEndDemo)) {
        mIsDemoChance = false;
        TombSpiderFunction::endTombSpiderDemo(mParent, "チャンス終了", "チャンス開始[トゥームスパイダー]");
        return true;
    }

    return false;
}

bool TombSpiderActionBase::tryWaitChanceStartDemo(const Nerve* pNerve) {
    if (!mIsDemoChance) {
        return false;
    }

    MR::requestStartDemoMarioPuppetable(this, mParent, "チャンス開始", pNerve, nullptr);
    return true;
}

bool TombSpiderActionBase::tryWaitChanceEndDemo(const Nerve* pNerve) {
    if (!mIsDemoChance) {
        return false;
    }

    MR::requestStartDemoMarioPuppetable(this, mParent, "チャンス終了", pNerve, nullptr);
    return true;
}

void TombSpiderActionBase::startChanceDamage() {
    mEnergy--;
    MR::shakeCameraNormal();
    TombSpiderFunction::appearThreadAttacherAll(mParent);
    if (mEnergy <= 0) {
        MR::sendMsgToAllLiveActor(ACTMES_TOMB_SPIDER_DEMO_START, nullptr);
        MR::offSwitchA(mParent);
        TombSpiderFunction::appearThreadAttacherAll(mParent);
        MR::startSpiderThreadBattle();
    }
}
