#include "Game/MapObj/CrystalSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryOff_f1, "tryOff.f1", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOn_f1, "exeOn.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f1, "calcRotSpeed.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f2, "calcRotSpeed.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f3, "calcRotSpeed.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f4, "calcRotSpeed.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f5, "calcRotSpeed.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f6, "calcRotSpeed.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotSpeed_f7, "calcRotSpeed.f7", 0.99f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeOn_setBckRate_1, "exeOn.setBckRate.1", 10.0f);

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
    sensor_offs.y = cNum_init_f1;
    sensor_offs.z = 0.0f;
    MR::addHitSensorMapObj(this, "body", 16, cNum_init_f2, sensor_offs);
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
    if (mRotateSpeed < cNum_tryOff_f1) {
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
        mRotateSpeed = cNum_exeOn_f1;
        MR::setBckRate(this, cNum_exeOn_setBckRate_1);
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
        if (mRotateSpeed < cNum_calcRotSpeed_f1) {
            mRotateSpeed += cNum_calcRotSpeed_f2;
        }

        if (mRotateSpeed > cNum_calcRotSpeed_f3) {
            mRotateSpeed = cNum_calcRotSpeed_f4;
        }
    } else {
        if (mRotateSpeed > cNum_calcRotSpeed_f5) {
            mRotateSpeed = cNum_calcRotSpeed_f6;
        }

        MR::setBckRate(this, mRotateSpeed);
        mRotateSpeed *= cNum_calcRotSpeed_f7;

        if (mRotateSpeed < 0.0f) {
            mRotateSpeed = 0.0f;
        }
    }
}
