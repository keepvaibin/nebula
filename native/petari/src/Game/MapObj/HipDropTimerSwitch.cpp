#include "Game/MapObj/HipDropTimerSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const sTimerSeSet[2][4] = {{"SE_SY_TIMER_A_2", "SE_SY_TIMER_A_1", "SE_SY_TIMER_A_QUASI_0", "SE_SY_TIMER_A_0"},
                                           {"SE_SY_TIMER_B_2", "SE_SY_TIMER_B_1", "SE_SY_TIMER_B_QUASI_0", "SE_SY_TIMER_B_0"}};
};  // namespace

namespace NrvHipDropSwitch {
    NEW_NERVE(HipDropTimerSwitchNrvOff, HipDropTimerSwitch, Off);
    NEW_NERVE(HipDropTimerSwitchNrvSwitchDown, HipDropTimerSwitch, SwitchDown);
    NEW_NERVE(HipDropTimerSwitchNrvOn, HipDropTimerSwitch, On);
    NEW_NERVE(HipDropTimerSwitchNrvSwitchUp, HipDropTimerSwitch, SwitchUp);
};  // namespace NrvHipDropSwitch

HipDropTimerSwitch::HipDropTimerSwitch(const char* pName)
    : LiveActor(pName), mMapObjConnector(nullptr), mCollisionParts(nullptr), mTimerMax(300), mTimerSFXType(0), mWasLightPressed(false),
      mIsLightPressed(false) {
    lumacore::setClass(this, lumacore::kClassId< HipDropTimerSwitch >);
    mSpring = new SpringValue;
    mMapObjConnector = new MapObjConnector(this);
    mSpring->setParam(0.0f, 0.0f, 0.2f, 0.9f, 0.0f);
}

void HipDropTimerSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("HipDropTimerSwitch", nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    MR::initLightCtrl(this);
    initSound(4, false);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorMapObj(this, "hit", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTarget(this, 150.0f, Vec3f(0.0f));
    MR::initCollisionParts(this, "HipDropTimerSwitch", getSensor("body"), nullptr);
    mCollisionParts = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("hit"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mCollisionParts);
    initNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvOff));
    MR::needStageSwitchWriteA(this, rIter);
    MR::tryRegisterDemoCast(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mTimerMax);
    MR::getPlacementArg1NoInit(rIter, &mTimerSFXType);

    if (mTimerSFXType <= 0 || mTimerSFXType >= ARRAY_SIZE(::sTimerSeSet)) {
        mTimerSFXType = 0;
    }

    initEffectKeeper(0, nullptr);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
        return;
    }

    makeActorAppeared();
}

void HipDropTimerSwitch::initAfterPlacement() {
    mMapObjConnector->attachToUnder();
}

void HipDropTimerSwitch::control() {
    mWasLightPressed = mIsLightPressed;
    mIsLightPressed = false;
}

void HipDropTimerSwitch::calcAnim() {
    LiveActor::calcAnim();
    MtxPtr jointMtx = MR::getJointMtx(this, "Move");
    Pos3f moveMtx;
    moveMtx.setInline(jointMtx);
    f32 springOffset = mSpring->mSpringValue;

    if (!MR::isNearZero(springOffset)) {
        Vec3f pos, up;
        moveMtx.getTrans(pos);
        MR::calcUpVec(&up, this);
        pos += up * springOffset;
        moveMtx.setTrans(pos);
        NebulaMtxCopy(moveMtx, jointMtx);
    }

    mCollisionParts->setMtx(moveMtx);
}

void HipDropTimerSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mMapObjConnector->connect();
}

void HipDropTimerSwitch::updateTimerSE() {
    if (MR::isPlayerDead()) {
        return;
    }

    if (MR::isPowerStarGetDemoActive()) {
        return;
    }

    s32 subSecond = mTimerMax % NebulaFrameClock::stepsOfSeconds(1);
    s32 timeElapsed = getNerveStep() - subSecond;

    if (subSecond != 0) {
        timeElapsed += NebulaFrameClock::stepsOfSeconds(1);
    }

    if (timeElapsed < NebulaFrameClock::stepsOfSeconds(1)) {
        return;
    }

    if (mTimerMax == getNerveStep()) {
        // Time's up
        MR::startSystemSE(::sTimerSeSet[mTimerSFXType][3]);
        return;
    }

    if (timeElapsed % NebulaFrameClock::stepsOfSeconds(1) != 0) {
        return;
    }

    if (timeElapsed < mTimerMax - NebulaFrameClock::stepsOfSeconds(10)) {
        // > 10s remaining
        MR::startSystemSE(::sTimerSeSet[mTimerSFXType][0]);
    } else if (timeElapsed < mTimerMax - NebulaFrameClock::stepsOfSeconds(2)) {
        // 8 to 2s remaining
        MR::startSystemSE(::sTimerSeSet[mTimerSFXType][1]);
    } else if (timeElapsed < mTimerMax) {
        // < 2 seconds remaining
        MR::startSystemSE(::sTimerSeSet[mTimerSFXType][2]);
    }
}

bool HipDropTimerSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        mIsLightPressed = true;
        return true;
    }

    if (MR::isMsgPlayerHipDropFloor(msg) && mCollisionParts->mHitSensor == pReceiver && isNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvOff))) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvSwitchDown));
        mSpring->reset();
        return true;
    }

    return false;
}

bool HipDropTimerSwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgFloorTouch(msg) && mCollisionParts->mHitSensor == pReceiver) {
        mIsLightPressed = true;
        return !mWasLightPressed;
    }

    return false;
}

void HipDropTimerSwitch::exeOff() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::offSwitchA(this);
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Off");
        mSpring->reset();
    }

    // "Weak"
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mIsLightPressed = true;
    }

    if (!mWasLightPressed && mIsLightPressed) {
        mSpring->mVelocity += -10.0f;
        MR::startSound(this, "SE_OJ_PNC_KINOKO_BOUND");
    }

    mSpring->update();
}

void HipDropTimerSwitch::exeSwitchDown() {
    if (MR::isStep(this, 0)) {
        MR::startBck(this, "On");
        MR::startBrk(this, "On");
    }

    if (MR::isGreaterStep(this, 0) && MR::isBckStopped(this)) {
        setNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvOn));
    }
}

void HipDropTimerSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::onSwitchA(this);
        MR::startSound(this, "SE_OJ_HIPDROP_SWITCH_ON");
        MR::shakeCameraNormal();
    }

    updateTimerSE();

    if (MR::isGreaterStep(this, mTimerMax)) {
        setNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvSwitchUp));
    }
}

void HipDropTimerSwitch::exeSwitchUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Off");
        MR::startBrk(this, "Off");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(HipDropSwitch, HipDropTimerSwitchNrvOff));
    }
}
