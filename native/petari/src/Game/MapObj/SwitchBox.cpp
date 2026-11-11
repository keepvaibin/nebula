#include "Game/MapObj/SwitchBox.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 150.0f);

namespace NrvSwitchBox {
    NEW_NERVE(SwitchBoxNrvWait, SwitchBox, Wait);
    NEW_NERVE(SwitchBoxNrvHit, SwitchBox, Hit);
};  // namespace NrvSwitchBox

SwitchBox::SwitchBox(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SwitchBox >);
}

void SwitchBox::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SwitchBox", nullptr, false);
    MR::connectToSceneMapObjNoCalcAnim(this);
    initSound(2, false);
    initNerve(GET_NERVE(SwitchBox, SwitchBoxNrvWait));
    initEffectKeeper(3, nullptr);
    f32 newScale = mScale.x * cNum_init_f1;
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 8, newScale, Vec3f(0.0f, newScale, 0.0f));

    s32 hasShadow = 1;

    if (MR::checkTableEntries(rIter)) {
        MR::getPlacementArg1NoInit(rIter, &hasShadow);
    }

    setNerve(GET_NERVE(SwitchBox, SwitchBoxNrvWait));
    MR::initCollisionParts(this, "SwitchBox", getSensor("body"), nullptr);
    MR::validateClipping(this);

    if (hasShadow) {
        MR::initShadowVolumeBox(this, Vec3f(cNum_init_f2, cNum_init_f3, cNum_init_f4));
        MR::setShadowVolumeStartDropOffset(this, nullptr, cNum_init_f5);
        MR::setShadowVolumeEndDropOffset(this, nullptr, cNum_init_f6);
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::excludeCalcShadowToMyCollision(this, nullptr);
        MR::onCalcGravity(this);
        MR::onCalcShadowOneTime(this, nullptr);
    }

    MR::addToAttributeGroupSearchTurtle(this);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }

    MR::needStageSwitchWriteDead(this, rIter);
    appear();
}

void SwitchBox::initAfterPlacement() {
}

void SwitchBox::exeWait() {
    if (mFloorTouchTimer != 0) {
        --mFloorTouchTimer;
    }

    if (mHitCoolDown != 0) {
        --mHitCoolDown;
        if (mHitCoolDown == 0) {
            mHitPoint = 1;
        }
    }

    if (mHitPoint == 0) {
        setNerve(GET_NERVE(SwitchBox, SwitchBoxNrvHit));
    }
}

void SwitchBox::exeHit() {
    if (getNerveStep() == 0) {
        getSensor("body")->invalidate();
    }

    if (getNerveStep() == 5) {
        MR::hideModel(this);
    }

    if (getNerveStep() == 15) {
        kill();
    }
}

void SwitchBox::appear() {
    MR::offSwitchDead(this);

    mHitPoint = 1;
    mHitCoolDown = 0;
    mFloorTouchTimer = 0;

    LiveActor::appear();
    MR::showModel(this);
    setNerve(GET_NERVE(SwitchBox, SwitchBoxNrvWait));
}

void SwitchBox::kill() {
    MR::onSwitchDead(this);
    LiveActor::kill();
}

void SwitchBox::doHit(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (mHitPoint != 0) {
        --mHitPoint;
    }

    if (mHitPoint == 0) {
        MR::invalidateCollisionParts(this);
    }

    mHitCoolDown = 30;

    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::startSound(this, "SE_OJ_S_BLOCK_BREAK_W");
        MR::emitEffect(this, "BreakWater");
    } else {
        MR::startSound(this, "SE_OJ_S_BLOCK_BREAK");
        MR::emitEffect(this, "Break");
    }
}

bool SwitchBox::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerUpperPunch(msg) && mHitPoint != 0) {
        doHit(pSender, pReceiver);
        pSender->receiveMessage(ACTMES_REFLECT_V, pReceiver);
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg) && mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerHipDropFloor(msg)) {
        if (mFloorTouchTimer != 0) {
            doHit(pSender, pReceiver);
            return true;
        }

        return true;
    }

    return false;
}

bool SwitchBox::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    return false;
}

bool SwitchBox::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_IS_PUNCH_ENABLE) {
        return mHitPoint;
    }

    if (msg == ACTMES_IS_BROKEN) {
        return !mHitPoint;
    }

    if (MR::isMsgFloorTouch(msg)) {
        mFloorTouchTimer = 2;
    }

    return false;
}
