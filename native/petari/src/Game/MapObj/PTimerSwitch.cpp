#include "Game/MapObj/PTimerSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeOn_1, "exeOn.1", 25);

namespace NrvPTimerSwitch {
    NEW_NERVE(PTimerSwitchNrvOff, PTimerSwitch, Off);
    NEW_NERVE(PTimerSwitchNrvSwitchDown, PTimerSwitch, SwitchDown);
    NEW_NERVE(PTimerSwitchNrvOn, PTimerSwitch, On);
};  // namespace NrvPTimerSwitch

PTimerSwitch::PTimerSwitch(const char* pName) : LiveActor(pName) {
    mSwitchCollision = nullptr;
    mConnector = nullptr;
    mPressStep = 0;
    mIsPressed = false;
}

void PTimerSwitch::reset() {
    LiveActor::appear();
    setNerve(GET_NERVE(PTimerSwitch, PTimerSwitchNrvOff));
    MR::validateCollisionParts(mSwitchCollision);
    MR::startBck(this, "Wait");
    mPressStep = 0;
    mIsPressed = false;
}

void PTimerSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PTimerSwitch", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "hit", ATYPE_SWITCH, 16, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    MR::initCollisionParts(this, "PTimerSwitch", getSensor("body"), 0);
    mSwitchCollision = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("hit"), MR::CollisionScaleType_Default);
    initSound(4, false);
    MR::needStageSwitchWriteA(this, rIter);
    initNerve(GET_NERVE(PTimerSwitch, PTimerSwitchNrvOff));
    initEffectKeeper(0, nullptr);
    MR::startBck(this, "Wait");
    mConnector = new MapObjConnector(this);
    appear();
}

void PTimerSwitch::initAfterPlacement() {
    mConnector->attachToUnder();
}

void PTimerSwitch::appear() {
    reset();
}

void PTimerSwitch::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Delete");
    MR::invalidateCollisionParts(mSwitchCollision);
}

void PTimerSwitch::calcAnim() {
    LiveActor::calcAnim();
    Pos3f mtx;
    mtx.setInline(MR::getJointMtx(this, "Move"));
    mSwitchCollision->setMtx(mtx);
}

void PTimerSwitch::control() {
    if (mIsPressed) {
        ++mPressStep;
    } else if (mPressStep > 0) {
        --mPressStep;
    }

    mIsPressed = false;
}

void PTimerSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mConnector->connect();
}

bool PTimerSwitch::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool PTimerSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!MR::isMsgPlayerTrample(msg)) {
        return false;
    }

    if (pReceiver->mType != 0x4f) {
        return false;
    }

    if (!isNerve(GET_NERVE(PTimerSwitch, PTimerSwitchNrvOff))) {
        return false;
    }

    mIsPressed = true;
    return true;
}

bool PTimerSwitch::trySwitchDown() {
    if (mPressStep > 0) {
        setNerve(GET_NERVE(PTimerSwitch, PTimerSwitchNrvSwitchDown));
        return true;
    }

    return false;
}

bool PTimerSwitch::tryOn() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PTimerSwitch, PTimerSwitchNrvOn));
        return true;
    }

    return false;
}

void PTimerSwitch::exeOff() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::offSwitchA(this);
    }

    if (!trySwitchDown()) {
        return;
    }
}

void PTimerSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "On");
        MR::invalidateClipping(this);
    }

    if (!tryOn()) {
        return;
    }
}

void PTimerSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::onSwitchA(this);
        MR::shakeCameraNormal();
        MR::startSound(this, "SE_OJ_SWITCH_1");
    }

    if (MR::isGreaterStep(this, cStep_exeOn_1)) {
        kill();
    }
}
