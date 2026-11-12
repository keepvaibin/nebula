#include "Game/MapObj/CrystalSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

/* Note -- this file was compiled super early on in the game's life, so it was compiled under -O2 where the release build is -O4,p. Keep this in mind!
 */

namespace NrvCrystalSwitch {
    NEW_NERVE(CrystalSwitchNrvOff, CrystalSwitch, Off);
    NEW_NERVE(CrystalSwitchNrvSwitchDown, CrystalSwitch, SwitchDown);
    NEW_NERVE(CrystalSwitchNrvOn, CrystalSwitch, On);
    NEW_NERVE(CrystalSwitchNrvSwitchUp, CrystalSwitch, SwitchUp);
};  // namespace NrvCrystalSwitch

CrystalSwitch::CrystalSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< CrystalSwitch >);
    mRotateSpeed = 0.0f;
    mIsHit = 0;
}

void CrystalSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("CrystalSwitch", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    Vec3f sensor_offs;
    sensor_offs.x = 0.0f;
    sensor_offs.y = 100.0f;
    sensor_offs.z = 0.0f;
    MR::addHitSensorMapObj(this, "body", 16, 100.0f, sensor_offs);
    initSound(4, false);
    MR::needStageSwitchWriteA(this, rIter);
    initNerve(GET_NERVE(CrystalSwitch, CrystalSwitchNrvOff));
    initEffectKeeper(0, nullptr);
    MR::startBck(this, "Wait");
    MR::startBpk(this, "Off");
    appear();
}

void CrystalSwitch::control() {
    calcRotSpeed();
    mIsHit = 0;
}

void CrystalSwitch::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        pReceiver->receiveMessage(ACTMES_PUSH, pSender);
    }
}

bool CrystalSwitch::receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    mIsHit = 1;
    return true;
}

bool CrystalSwitch::trySwitchDown() {
    if (mIsHit) {
        setNerve(GET_NERVE(CrystalSwitch, CrystalSwitchNrvSwitchDown));
        return true;
    }

    return false;
}

bool CrystalSwitch::tryOn() {
    if (getNerveStep() > 10) {
        setNerve(GET_NERVE(CrystalSwitch, CrystalSwitchNrvOn));
        return true;
    }

    return false;
}

bool CrystalSwitch::tryOff() {
    if (mRotateSpeed < 8.0f) {
        setNerve(GET_NERVE(CrystalSwitch, CrystalSwitchNrvOff));
        return true;
    }

    return false;
}

void CrystalSwitch::exeOff() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::offSwitchA(this);
    }

    if (trySwitchDown()) {
        return;
    }
}

void CrystalSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this)) {
        MR::startBpk(this, "On");
        MR::invalidateClipping(this);
    }

    if (tryOn()) {
        return;
    }
}

void CrystalSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::onSwitchA(this);
        MR::shakeCameraNormal();
        MR::startSound(this, "SE_OJ_CRYSTAL_SWITCH_ON");
        mRotateSpeed = 10.0f;
        MR::setBckRate(this, 10.0f);
    }
}

void CrystalSwitch::exeSwitchUp() {
    if (MR::isFirstStep(this)) {
        MR::startBpk(this, "Off");
    }

    tryOff();
}

void CrystalSwitch::calcRotSpeed() {
    if (isNerve(GET_NERVE(CrystalSwitch, CrystalSwitchNrvOn))) {
        if (mRotateSpeed < 10.0f) {
            mRotateSpeed += 1.5f;
        }

        if (mRotateSpeed > 10.0f) {
            mRotateSpeed = 10.0f;
        }
    } else {
        if (mRotateSpeed > 10.0f) {
            mRotateSpeed = 10.0f;
        }

        MR::setBckRate(this, mRotateSpeed);
        mRotateSpeed *= 0.99f;

        if (mRotateSpeed < 0.0f) {
            mRotateSpeed = 0.0f;
        }
    }
}
