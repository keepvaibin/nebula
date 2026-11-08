#include "Game/MapObj/DesertLandMoveSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>

namespace NrvDesertLandMoveSwitch {
    NEW_NERVE(HostTypeWait, DesertLandMoveSwitch, Wait);
    NEW_NERVE(HostTypeSwitchDown, DesertLandMoveSwitch, SwitchDown);
    NEW_NERVE(HostTypeOn, DesertLandMoveSwitch, On);
    NEW_NERVE(HostTypeReturn, DesertLandMoveSwitch, Return);
};  // namespace NrvDesertLandMoveSwitch

DesertLandMoveSwitch::DesertLandMoveSwitch(const char* pName) : LiveActor(pName) {
    mCollisionParts = 0;
    mSpringValue = new SpringValue();
    mMapObjConnector = new MapObjConnector(this);
    mIsHipDropped = 0;
    mWasOnPlayer = 0;
    mIsOnPlayer = 0;
    mTimer = 0;
    mObjName = 0;
    mSpringValue->setParam(0.0f, 0.0f, 0.35f, 0.75f, 0.0f);
}

void DesertLandMoveSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mTimer);
    s32 stack_C = -1;
    MR::getPlacementArg1NoInit(rIter, &stack_C);
    initModelAndCollision(rIter);
    MR::connectToSceneMapObj(this);
    if (stack_C == -1) {
        initNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeWait));
    } else {
        MR::startBck(this, "On");
        MR::setAllAnimFrameAtEnd(this, "On");
        initNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeOn));
    }
    f32 stack_8 = -1.0f;
    MR::calcModelBoundingRadius(&stack_8, this);
    MR::setClippingTypeSphere(this, stack_8);
    MR::setGroupClipping(this, rIter, 0x10);
    initSound(4, 0);
    MR::needStageSwitchWriteA(this, rIter);
    MR::tryRegisterDemoCast(this, rIter);
    makeActorAppeared();
}

void DesertLandMoveSwitch::initAfterPlacement() {
    mMapObjConnector->attachToUnder();
}

void DesertLandMoveSwitch::calcAnim() {
    LiveActor::calcAnim();
    MtxPtr mtx = MR::getJointMtx(this, "Move");
    Pos3f mtx2;
    mtx2.setInline(mtx);
    f32 val = mSpringValue->mSpringValue;
    if (!MR::isNearZero(val)) {
        Vec3f pos, up;
        mtx2.getTrans(pos);
        MR::calcUpVec(&up, this);
        pos += up * val;
        mtx2.setTrans(pos);
        NebulaMtxCopy(mtx2, mtx);
    }
    mCollisionParts->setMtx(mtx2);
}

void DesertLandMoveSwitch::control() {
    mIsHipDropped = 0;
    mWasOnPlayer = mIsOnPlayer;
    mIsOnPlayer = 0;
}

void DesertLandMoveSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mMapObjConnector->connect();
}

bool DesertLandMoveSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerHipDropFloor(msg) && !mIsHipDropped) {
        mIsHipDropped = 1;
        return true;
    }
    return false;
}

bool DesertLandMoveSwitch::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == 0xB4) {
        mIsOnPlayer = 1;
        return mWasOnPlayer == 0;
    }
    return false;
}

void DesertLandMoveSwitch::initModelAndCollision(const TableIter& rIter) {
    const char* pObjName = "";
    MR::getObjectName(&pObjName, rIter);
    mObjName = pObjName;
    initModelManagerWithAnm(pObjName, 0, 0);
    initHitSensor(2);
    HitSensor* sensorBody = MR::addHitSensorMapObj(this, "body", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    HitSensor* sensorMove = MR::addHitSensorMapObj(this, "move", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, pObjName, sensorBody, 0);
    mCollisionParts = MR::createCollisionPartsFromLiveActor(this, "Move", sensorMove, MR::CollisionScaleType_Unk2);
    MR::validateCollisionParts(mCollisionParts);
}

bool DesertLandMoveSwitch::tryOn() {
    if (MR::isGreaterStep(this, 6) && MR::isBckStopped(this)) {
        MR::startSound(this, "SE_OJ_HIPDROP_SWITCH_ON");
        MR::shakeCameraNormal();
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeOn));
        return true;
    }
    return false;
}

bool DesertLandMoveSwitch::trySwitchDown() {
    if (!mIsHipDropped)
        return false;

    setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeSwitchDown));
    return true;
}

bool DesertLandMoveSwitch::tryConnect() {
    if (mMapObjConnector->mParts != 0)
        return true;

    return mMapObjConnector->attachToUnder();
}

void DesertLandMoveSwitch::updateTimerSE() {
    if (!MR::isPlayerDead() && !MR::isPowerStarGetDemoActive()) {
        s32 l = mTimer % 60;
        s32 step = getNerveStep();
        s32 lf = step - l;
        if (l != 0) {
            lf += 60;
        }
        s32 step2 = getNerveStep();
        if (MR::isEqualString(mObjName, "DesertLandDownSwitch") && MR::isEqualStageName("SandClockGalaxy")) {
            lf += 180;
            step2 += 180;
        }
        if (lf >= 60) {
            s32 s = mTimer;
            if (lf <= s) {
                if (step2 == s) {
                    MR::startSystemSE("SE_SY_TIMER_A_0");
                } else {
                    if (!(lf % 60)) {
                        if (lf < s - 600)
                            MR::startSystemSE("SE_SY_TIMER_A_2");
                        else if (lf < s - 120)
                            MR::startSystemSE("SE_SY_TIMER_A_1");
                        else if (lf < s)
                            MR::startSystemSE("SE_SY_TIMER_A_QUASI_0");
                    }
                }
            }
        }
    }
}

void DesertLandMoveSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mIsHipDropped = 0;
        mSpringValue->reset();
    }

    tryConnect();
    if (!mWasOnPlayer && mIsOnPlayer) {
        mSpringValue->mVelocity += -10.0f;
    }

    mSpringValue->update();
    if (trySwitchDown()) {
        mSpringValue->reset();
    }
}

void DesertLandMoveSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this))
        MR::invalidateClipping(this);

    if (MR::isStep(this, 6))
        MR::startBck(this, "On");

    if (tryOn())
        return;
}

void DesertLandMoveSwitch::exeOn() {
    if (MR::isFirstStep(this))
        MR::onSwitchA(this);
    tryConnect();
    if (mTimer > 0) {
        updateTimerSE();
        if (MR::isStep(this, mTimer))
            setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeReturn));
    } else if (!MR::isOnSwitchA(this))
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeReturn));
}

void DesertLandMoveSwitch::exeReturn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Return");
        MR::offSwitchA(this);
        mIsHipDropped = 0;
        mSpringValue->reset();
    }

    if (!mWasOnPlayer && mIsOnPlayer)
        mSpringValue->mVelocity += -10.0f;

    mSpringValue->update();

    if (trySwitchDown())
        mSpringValue->reset();
    else if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeWait));
    }
}

void SpringValue::reset() {
    mSpringValue = mRestValue;
    mVelocity = 0.0f;
}
