#include "Game/MapObj/DesertLandMoveSwitch.hpp"
#include "Game/System/NebulaFrameClock.hpp"
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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_DesertLandMoveSwitch_f1, "DesertLandMoveSwitch.f1", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_DesertLandMoveSwitch_f2, "DesertLandMoveSwitch.f2", 0.75f);

namespace {
    // The press animation starts after this many steps, and the switch turns on when it ends.
    NEBULA_TUNABLE(s32, cSwitchDownSteps, 6);
    // Stepping on gives the top this push down.
    NEBULA_TUNABLE(f32, cStepOnVelocity, -10.0f);
    const s32 cTimerTickSteps = NebulaFrameClock::stepsOfSeconds(1);
    // SandClockGalaxy's down switch ticks as if 180 steps further on.
    const s32 cSandClockTimerLead = NebulaFrameClock::stepsOfSeconds(3);
};  // namespace

namespace NrvDesertLandMoveSwitch {
    NEW_NERVE(HostTypeWait, DesertLandMoveSwitch, Wait);
    NEW_NERVE(HostTypeSwitchDown, DesertLandMoveSwitch, SwitchDown);
    NEW_NERVE(HostTypeOn, DesertLandMoveSwitch, On);
    NEW_NERVE(HostTypeReturn, DesertLandMoveSwitch, Return);
};  // namespace NrvDesertLandMoveSwitch

// A sand-land hip-drop switch whose top ("Move") springs when stepped on; it turns switch A on, for arg 0 steps when
// arg 0 is set (with a countdown), else until something turns A off. Arg 1 set starts it on.
DesertLandMoveSwitch::DesertLandMoveSwitch(const char* pName)
    : LiveActor(pName), mCollisionParts(nullptr), mSpringValue(new SpringValue()), mMapObjConnector(new MapObjConnector(this)), mIsHipDropped(false),
      mWasOnPlayer(false), mIsOnPlayer(false), mTimer(0), mObjName(nullptr), mTimerLead(0) {
    mSpringValue->setParam(0.0f, 0.0f, cNum_DesertLandMoveSwitch_f1, cNum_DesertLandMoveSwitch_f2, 0.0f);
}

void DesertLandMoveSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mTimer);
    s32 startOn = -1;
    MR::getPlacementArg1NoInit(rIter, &startOn);
    initModelAndCollision(rIter);
    MR::connectToSceneMapObj(this);

    if (startOn == -1) {
        initNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeWait));
    } else {
        MR::startBck(this, "On");
        MR::setAllAnimFrameAtEnd(this, "On");
        initNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeOn));
    }

    f32 radius = -1.0f;
    MR::calcModelBoundingRadius(&radius, this);
    MR::setClippingTypeSphere(this, radius);
    MR::setGroupClipping(this, rIter, 16);
    initSound(4, false);
    MR::needStageSwitchWriteA(this, rIter);
    MR::tryRegisterDemoCast(this, rIter);

    if (MR::isEqualString(mObjName, "DesertLandDownSwitch") && MR::isEqualStageName("SandClockGalaxy")) {
        mTimerLead = cSandClockTimerLead;
    }

    makeActorAppeared();
}

void DesertLandMoveSwitch::initAfterPlacement() {
    mMapObjConnector->attachToUnder();
}

// The top's collision follows the Move joint, raised along up by the spring.
void DesertLandMoveSwitch::calcAnim() {
    LiveActor::calcAnim();
    MtxPtr pMoveJoint = MR::getJointMtx(this, "Move");
    Pos3f moveMtx;
    moveMtx.setInline(pMoveJoint);
    f32 springValue = mSpringValue->mSpringValue;

    if (!MR::isNearZero(springValue)) {
        Vec3f pos, up;
        moveMtx.getTrans(pos);
        MR::calcUpVec(&up, this);
        pos += up * springValue;
        moveMtx.setTrans(pos);
        NebulaMtxCopy(moveMtx, pMoveJoint);
    }

    mCollisionParts->setMtx(moveMtx);
}

// The flags hold for the frame they were received in.
void DesertLandMoveSwitch::control() {
    mIsHipDropped = false;
    mWasOnPlayer = mIsOnPlayer;
    mIsOnPlayer = false;
}

void DesertLandMoveSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mMapObjConnector->connect();
}

bool DesertLandMoveSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerHipDropFloor(msg) && !mIsHipDropped) {
        mIsHipDropped = true;
        return true;
    }

    return false;
}

// Taken the frame the player steps on.
bool DesertLandMoveSwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_FLOOR_TOUCH) {
        mIsOnPlayer = true;
        return !mWasOnPlayer;
    }

    return false;
}

void DesertLandMoveSwitch::initModelAndCollision(const TableIter& rIter) {
    const char* pObjName = "";
    MR::getObjectName(&pObjName, rIter);
    mObjName = pObjName;
    initModelManagerWithAnm(pObjName, nullptr, false);
    initHitSensor(2);
    HitSensor* pBodySensor = MR::addHitSensorMapObj(this, "body", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    HitSensor* pMoveSensor = MR::addHitSensorMapObj(this, "move", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, pObjName, pBodySensor, nullptr);
    mCollisionParts = MR::createCollisionPartsFromLiveActor(this, "Move", pMoveSensor, MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mCollisionParts);
}

bool DesertLandMoveSwitch::tryOn() {
    if (MR::isGreaterStep(this, cSwitchDownSteps) && MR::isBckStopped(this)) {
        MR::startSound(this, "SE_OJ_HIPDROP_SWITCH_ON");
        MR::shakeCameraNormal();
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeOn));
        return true;
    }

    return false;
}

bool DesertLandMoveSwitch::trySwitchDown() {
    if (!mIsHipDropped) {
        return false;
    }

    setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeSwitchDown));
    return true;
}

bool DesertLandMoveSwitch::tryConnect() {
    if (mMapObjConnector->mParts != nullptr) {
        return true;
    }

    return mMapObjConnector->attachToUnder();
}

// Ticks on whole seconds before the end (tickStep counts from the first whole second), faster in the last ten seconds
// and the last two, and rings at the end; quiet while the player is dead or a star is being collected.
void DesertLandMoveSwitch::updateTimerSE() {
    if (MR::isPlayerDead() || MR::isPowerStarGetDemoActive()) {
        return;
    }

    s32 secondPhase = mTimer % cTimerTickSteps;
    s32 tickStep = getNerveStep() - secondPhase;

    if (secondPhase != 0) {
        tickStep += cTimerTickSteps;
    }

    s32 step = getNerveStep();
    tickStep += mTimerLead;
    step += mTimerLead;

    if (tickStep < cTimerTickSteps || tickStep > mTimer) {
        return;
    }

    if (step == mTimer) {
        MR::startSystemSE("SE_SY_TIMER_A_0");
    } else if (tickStep % cTimerTickSteps == 0) {
        if (tickStep < mTimer - 600) {
            MR::startSystemSE("SE_SY_TIMER_A_2");
        } else if (tickStep < mTimer - 120) {
            MR::startSystemSE("SE_SY_TIMER_A_1");
        } else if (tickStep < mTimer) {
            MR::startSystemSE("SE_SY_TIMER_A_QUASI_0");
        }
    }
}

// Returns true when a hip drop starts the press (the spring then stops).
bool DesertLandMoveSwitch::updateSpring() {
    if (!mWasOnPlayer && mIsOnPlayer) {
        mSpringValue->mVelocity += cStepOnVelocity;
    }

    mSpringValue->update();

    if (trySwitchDown()) {
        mSpringValue->reset();
        return true;
    }

    return false;
}

void DesertLandMoveSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mIsHipDropped = false;
        mSpringValue->reset();
    }

    tryConnect();
    updateSpring();
}

void DesertLandMoveSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    if (MR::isStep(this, cSwitchDownSteps)) {
        MR::startBck(this, "On");
    }

    tryOn();
}

void DesertLandMoveSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::onSwitchA(this);
    }

    tryConnect();

    if (mTimer > 0) {
        updateTimerSE();

        if (MR::isStep(this, mTimer)) {
            setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeReturn));
        }
    } else if (!MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeReturn));
    }
}

void DesertLandMoveSwitch::exeReturn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Return");
        MR::offSwitchA(this);
        mIsHipDropped = false;
        mSpringValue->reset();
    }

    if (!updateSpring() && MR::isBckStopped(this)) {
        setNerve(GET_NERVE(DesertLandMoveSwitch, HostTypeWait));
    }
}
